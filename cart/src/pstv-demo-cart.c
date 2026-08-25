#define _GNU_SOURCE
#include <cart/canvas.h>
#include <cart/runtime.h>
#include <cart/scene.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <math.h>
#include <pthread.h>
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
#define THREADS 4
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

struct job {
    int row_start;
    int row_end;
    uint64_t frame;
    const struct cart_scene *scene;
};

static void *worker(void *arg)
{
    const struct job *job = arg;
    const struct cart_scene_render_context context = {
        .canvas = &low_canvas,
        .row_start = job->row_start,
        .row_end = job->row_end,
        .frame = job->frame,
        .phase = CART_SCENE_RENDER_ROWS,
    };

    job->scene->render(&context);
    return NULL;
}

static void render_frame(int which, uint64_t frame)
{
    const struct cart_scene *selected;
    pthread_t threads[THREADS];
    struct job jobs[THREADS];
    struct cart_scene_render_context overlay_context;

    selected = cart_scene_at((size_t)which);
    if (selected == NULL || selected->render == NULL)
        return;
    for (int index = 0; index < THREADS; index++) {
        jobs[index] = (struct job) {
            .row_start = index * LH / THREADS,
            .row_end = (index + 1) * LH / THREADS,
            .frame = frame,
            .scene = selected,
        };
        pthread_create(&threads[index], NULL, worker, &jobs[index]);
    }
    for (int index = 0; index < THREADS; index++)
        pthread_join(threads[index], NULL);
    overlay_context = (struct cart_scene_render_context) {
        .canvas = &low_canvas,
        .row_start = 0,
        .row_end = LH,
        .frame = frame,
        .phase = CART_SCENE_RENDER_OVERLAY,
    };
    selected->render(&overlay_context);
}

static void upscale(uint32_t *dst)
{
    uint32_t row[FW];
    for(int y=0;y<LH;y++){
        for(int x=0;x<LW;x++) for(int k=0;k<SCALE;k++) row[x*SCALE+k]=low[y*LW+x];
        for(int k=0;k<SCALE;k++) memcpy(dst+(y*SCALE+k)*FW,row,sizeof(row));
    }
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
    if(dump){render_frame(dump_scene,dump_frame);return dump_ppm(dump);}

    signal(SIGINT,on_signal); signal(SIGTERM,on_signal); signal(SIGHUP,on_signal); signal(SIGUSR1,on_signal);
    int fd=open("/dev/fb0",O_RDWR); if(fd<0){perror("/dev/fb0");return 1;}
    struct fb_var_screeninfo v; struct fb_fix_screeninfo fix;
    if(ioctl(fd,FBIOGET_VSCREENINFO,&v)||ioctl(fd,FBIOGET_FSCREENINFO,&fix)){perror("fb ioctl");return 1;}
    if(v.xres!=FW||v.yres!=FH||v.bits_per_pixel!=32||fix.line_length!=FW*4){fprintf(stderr,"unexpected fb %ux%u %ubpp stride %u\n",v.xres,v.yres,v.bits_per_pixel,fix.line_length);return 1;}
    uint32_t *fb=mmap(NULL,FW*FH*4,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); if(fb==MAP_FAILED){perror("mmap");return 1;}
    int infd=open("/dev/input/event0",O_RDONLY|O_NONBLOCK);
    uint64_t now_ns;
    if (monotonic_now_ns(&now_ns) != 0) {
        perror("clock_gettime");
        if (infd >= 0) close(infd);
        munmap(fb, FW * FH * 4);
        close(fd);
        return 1;
    }
    struct cart_runtime runtime;
    if (cart_runtime_init(&runtime, cart_scene_count(), now_ns,
                          UINT64_C(1000000000) / FPS,
                          UINT64_C(8) * UINT64_C(1000000000)) != 0) {
        fprintf(stderr, "runtime initialization failed\n");
        if (infd >= 0) close(infd);
        munmap(fb, FW * FH * 4);
        close(fd);
        return 1;
    }
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
        if(infd>=0){struct input_event ev; while(read(infd,&ev,sizeof(ev))==(ssize_t)sizeof(ev)) if(ev.type==EV_KEY&&ev.value==1){
            if(ev.code==KEY_ESC||ev.code==KEY_Q)running=0;
            else cart_runtime_request_next(&runtime, now_ns,
                                           UINT64_C(8) * UINT64_C(1000000000));
        }}
        render_frame((int)runtime.scene_index, runtime.frame); upscale(fb); rendered_frames++;
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
    if(infd>=0)close(infd);
    munmap(fb,FW*FH*4);
    close(fd);
    return 0;
}
