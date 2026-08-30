#define _GNU_SOURCE
#include <cart/canvas.h>
#include <cart/input.h>
#include <cart/runtime.h>
#include <cart/scene.h>
#include <cart/transition.h>
#include <cart/presentation.h>
#include <cart/worker_pool.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define LW 320
#define LH 180
#define SCALE 4
#define FW (LW * SCALE)
#define FH (LH * SCALE)
#define RENDER_THREADS 4
#define SCENES 6
#define FPS 30

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t next_scene_requested = 0;
static uint32_t low[LW * LH];
static struct cart_canvas low_canvas = {
    .pixels = low,
    .width = LW,
    .height = LH,
    .stride = LW,
};

/* One render job slot per worker; the pool writes bands in lockstep and
 * these live for the process lifetime. The selected scene is published here
 * before each dispatch (contexts have no scene member by ABI contract). */
static struct cart_scene_render_context row_contexts[CART_WORKER_POOL_MAX];
static void *row_slots[CART_WORKER_POOL_MAX];
static const struct cart_scene *row_scene;
static struct cart_worker_pool render_pool;

/* Crossfade staging. Scene changes are never hard cuts: the outgoing and
 * incoming scenes are rendered into these buffers and blended into `low`
 * over CART_TRANSITION_FRAMES. See
 * docs/plans/2026-08-27-pstv-transition-flash-repair.md. */
static uint32_t low_from[LW * LH];
static uint32_t low_to[LW * LH];
static struct cart_transition transition;
static size_t displayed_scene;
static uint32_t display_row[FW];

/* The simplefb driver has no page flip or vsync ioctl. Presentation therefore
 * uses one linear write-combining sweep per completed logical frame. A second
 * full-frame DRAM staging copy measured at ~15 FPS on the PSTV, so it is not
 * a viable mitigation; the true atomic fix remains a real display driver. */

static void row_job(void *slot, int row_start, int row_end, uint64_t frame)
{
    struct cart_scene_render_context *context = slot;

    context->row_start = row_start;
    context->row_end = row_end;
    context->frame = frame;
    context->phase = CART_SCENE_RENDER_ROWS;
    row_scene->render(context);
}

static void render_frame(int which, uint64_t frame)
{
    const struct cart_scene *selected;
    struct cart_scene_render_context overlay_context;

    selected = cart_scene_at((size_t)which);
    if (selected == NULL || selected->render == NULL)
        return;
    row_scene = selected;
    {
        /* Bind the canvas for every active context — including slot 0 in
         * inline mode, which the pool never touches on its own. */
        int to_bind = render_pool.worker_count > 0 ? render_pool.worker_count : 1;

        for (int index = 0; index < to_bind && index < CART_WORKER_POOL_MAX; index++)
            row_contexts[index].canvas = &low_canvas;
    }
    if (render_pool.worker_count > 0) {
        if (cart_worker_pool_dispatch(&render_pool, row_job, row_slots,
                                      frame) != 0)
            return;
    } else {
        row_job(&row_contexts[0], 0, LH, frame);
    }
    overlay_context = (struct cart_scene_render_context) {
        .canvas = &low_canvas,
        .row_start = 0,
        .row_end = LH,
        .frame = frame,
        .phase = CART_SCENE_RENDER_OVERLAY,
    };
    selected->render(&overlay_context);
}

/* Render a specific scene into a specific buffer. render_frame() binds every
 * active context to &low_canvas at dispatch time, so retargeting the canvas
 * pixel pointer around the call redirects the whole pipeline, workers
 * included. */
static void render_scene_into(uint32_t *destination, int which, uint64_t frame)
{
    uint32_t *saved = low_canvas.pixels;

    low_canvas.pixels = destination;
    render_frame(which, frame);
    low_canvas.pixels = saved;
}

static int dump_ppm(const char *path)
{
    FILE *f=fopen(path,"wb"); if(!f){perror(path);return 1;}
    fprintf(f,"P6\n%d %d\n255\n",FW,FH);
    for(int y=0;y<LH;y++) for(int ky=0;ky<SCALE;ky++)
        for(int x=0;x<LW;x++) for(int kx=0;kx<SCALE;kx++){
            uint32_t c=low[y*LW+x]; uint8_t p[3]={c&255,(c>>8)&255,(c>>16)&255}; fwrite(p,1,3,f);
        }
    fclose(f); return 0;
}

static void on_signal(int sig)
{
    if(sig==SIGUSR1) next_scene_requested=1;
    else running=0;
}

static int monotonic_now_ns(uint64_t *now_ns)
{
    struct timespec now;

    if (now_ns == NULL || clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    *now_ns = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    return 0;
}

static struct timespec timespec_from_ns(uint64_t value_ns)
{
    return (struct timespec) {
        .tv_sec = (time_t)(value_ns / UINT64_C(1000000000)),
        .tv_nsec = (long)(value_ns % UINT64_C(1000000000)),
    };
}

int main(int argc,char **argv)
{
    const char *dump=NULL; int dump_scene=0, dump_frame=120;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--dump")&&i+1<argc)dump=argv[++i];
        else if(!strcmp(argv[i],"--scene")&&i+1<argc)dump_scene=atoi(argv[++i])%SCENES;
        else if(!strcmp(argv[i],"--frame")&&i+1<argc)dump_frame=atoi(argv[++i]);
    }
    if (cart_scenes_init() != 0) {
        fprintf(stderr, "scene initialization failed\n");
        return 1;
    }
    /* Dump mode runs inline (thread-free, deterministic); live rendering
     * uses the persistent pool created after fb setup. */
    if (dump) {
        if (cart_worker_pool_init(&render_pool, 0, LH) != 0) {
            fprintf(stderr, "worker pool initialization failed\n");
            return 1;
        }
        render_frame(dump_scene, dump_frame);
        cart_worker_pool_shutdown(&render_pool);
        return dump_ppm(dump);
    }

    signal(SIGINT,on_signal); signal(SIGTERM,on_signal); signal(SIGHUP,on_signal); signal(SIGUSR1,on_signal);
    int fd=open("/dev/fb0",O_RDWR); if(fd<0){perror("/dev/fb0");return 1;}
    struct fb_var_screeninfo v; struct fb_fix_screeninfo fix;
    if(ioctl(fd,FBIOGET_VSCREENINFO,&v)||ioctl(fd,FBIOGET_FSCREENINFO,&fix)){perror("fb ioctl");return 1;}
    if(v.xres!=FW||v.yres!=FH||v.bits_per_pixel!=32||fix.line_length!=FW*4){fprintf(stderr,"unexpected fb %ux%u %ubpp stride %u\n",v.xres,v.yres,v.bits_per_pixel,fix.line_length);return 1;}
    uint32_t *fb=mmap(NULL,FW*FH*4,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); if(fb==MAP_FAILED){perror("mmap");return 1;}
    struct cart_input input;
    if (cart_input_init(&input, "/sys/class/input", "/dev/input") < 0)
        perror("input discovery");
    else if (input.source[0].fd >= 0 || input.source[1].fd >= 0 ||
             input.source[2].fd >= 0 || input.source[3].fd >= 0)
        fprintf(stderr, "input sources connected\n");
    uint64_t now_ns;
    if (monotonic_now_ns(&now_ns) != 0) {
        perror("clock_gettime");
        cart_input_shutdown(&input);
        munmap(fb, FW * FH * 4);
        close(fd);
        return 1;
    }
    struct cart_runtime runtime;
    for (int index = 0; index < CART_WORKER_POOL_MAX; index++)
        row_slots[index] = &row_contexts[index];
    if (cart_worker_pool_init(&render_pool, RENDER_THREADS, LH) != 0) {
        fprintf(stderr, "worker pool initialization failed\n");
        cart_input_shutdown(&input);
        munmap(fb, FW * FH * 4);
        close(fd);
        return 1;
    }
    if (cart_runtime_init(&runtime, cart_scene_count(), now_ns,
                          UINT64_C(1000000000) / FPS,
                          UINT64_C(8) * UINT64_C(1000000000),
                          CART_RUNTIME_MIN_CHANGE_NS) != 0) {
        fprintf(stderr, "runtime initialization failed\n");
        cart_input_shutdown(&input);
        munmap(fb, FW * FH * 4);
        close(fd);
        return 1;
    }
    displayed_scene = runtime.scene_index;
    memset(&transition, 0, sizeof(transition));
    uint64_t report_start_ns=now_ns;
    uint64_t report_frame=0;
    uint64_t rendered_frames=0;
    while(running){
        if (monotonic_now_ns(&now_ns) != 0) {
            perror("clock_gettime");
            break;
        }
        if (cart_runtime_tick(&runtime, now_ns) != 0) {
            fprintf(stderr, "runtime deadline exhausted\n");
            break;
        }
        if(next_scene_requested){
            cart_runtime_request_next(&runtime, now_ns,
                                      UINT64_C(3600) * UINT64_C(1000000000));
            next_scene_requested=0;
        }
        {
            enum cart_input_action polled = CART_INPUT_NONE;
            struct cart_input_frame frame;

            if (cart_input_poll(&input, &polled, &frame) != 0) {
                perror("input poll");
                cart_input_shutdown(&input);
            } else if (polled == CART_INPUT_QUIT) {
                running = 0;
            } else if (polled == CART_INPUT_NEXT) {
                cart_runtime_request_next(&runtime, now_ns,
                                           UINT64_C(8) * UINT64_C(1000000000));
            } else if (polled == CART_INPUT_PREVIOUS) {
                cart_runtime_request_previous(&runtime, now_ns,
                                               UINT64_C(8) * UINT64_C(1000000000));
            }
        }
        /* Begin a fade whenever the runtime selects a different scene.
         * A fade already in flight is never interrupted — the rate limiter
         * in cart_runtime guarantees the next change cannot arrive before
         * this one completes. Sample both endpoints once: rendering both
         * scenes on every fade frame cuts the target's frame rate in half. */
        if (runtime.scene_index != displayed_scene &&
            !cart_transition_active(&transition)) {
            cart_transition_begin(&transition, displayed_scene,
                                  runtime.scene_index);
            if (cart_transition_sources_need_render(&transition)) {
                render_scene_into(low_from, (int)transition.from_scene,
                                  runtime.frame);
                render_scene_into(low_to, (int)transition.to_scene,
                                  runtime.frame);
                cart_transition_mark_sources_rendered(&transition);
            }
        }
        if (cart_transition_active(&transition)) {
            cart_transition_blend(&transition, low, low_from, low_to,
                                  (size_t)(LW * LH),
                                  transition.elapsed_frames);
            cart_transition_advance(&transition);
            if (!cart_transition_active(&transition))
                displayed_scene = transition.to_scene;
        } else {
            render_frame((int)displayed_scene, runtime.frame);
        }
        cart_presentation_upscale(fb, low, display_row);
        rendered_frames++;
        if(rendered_frames-report_frame>=300){
            uint64_t elapsed_ns=now_ns-report_start_ns;
            double elapsed=(double)elapsed_ns/1e9;
            if (elapsed > 0.0)
                fprintf(stderr,"frames=%llu scene=%zu fps=%.2f dropped=%llu\n",
                        (unsigned long long)rendered_frames, runtime.scene_index,
                        (rendered_frames-report_frame)/elapsed,
                        (unsigned long long)runtime.dropped_deadlines);
            report_start_ns=now_ns; report_frame=rendered_frames;
        }
        if (monotonic_now_ns(&now_ns) != 0) {
            perror("clock_gettime");
            break;
        }
        if (cart_runtime_tick(&runtime, now_ns) != 0) {
            fprintf(stderr, "runtime deadline exhausted\n");
            break;
        }
        struct timespec delay=timespec_from_ns(runtime.sleep_ns);
        int sleep_status=clock_nanosleep(CLOCK_MONOTONIC,0,&delay,NULL);
        if (sleep_status != 0 && sleep_status != EINTR) {
            errno=sleep_status;
            perror("clock_nanosleep");
            break;
        }
    }
    cart_worker_pool_shutdown(&render_pool);
    cart_input_shutdown(&input);
    munmap(fb,FW*FH*4);
    close(fd);
    return 0;
}
