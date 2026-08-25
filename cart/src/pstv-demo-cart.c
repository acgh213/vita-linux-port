#define _GNU_SOURCE
#include <cart/canvas.h>
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
static int scene = 0;
static int frame_no = 0;

struct job {
    int row_start;
    int row_end;
    int frame;
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

static void render_frame(int which, int frame)
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
    struct timespec next, report_start;
    clock_gettime(CLOCK_MONOTONIC,&next);
    report_start=next;
    int report_frame=0;
    int manual_hold=0;
    while(running){
        if(next_scene_requested){scene=(scene+1)%SCENES;manual_hold=FPS*3600;next_scene_requested=0;}
        if(!manual_hold) scene=(frame_no/(FPS*8))%SCENES;
        if(infd>=0){struct input_event ev; while(read(infd,&ev,sizeof(ev))==(ssize_t)sizeof(ev)) if(ev.type==EV_KEY&&ev.value==1){
            if(ev.code==KEY_ESC||ev.code==KEY_Q)running=0;
            else {scene=(scene+1)%SCENES;manual_hold=FPS*8;}
        }}
        if(manual_hold>0)manual_hold--;
        render_frame(scene,frame_no); upscale(fb); frame_no++;
        if(frame_no-report_frame>=300){
            struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
            double elapsed=(double)(now.tv_sec-report_start.tv_sec)+(double)(now.tv_nsec-report_start.tv_nsec)/1e9;
            fprintf(stderr,"frames=%d scene=%d fps=%.2f\n",frame_no,scene,(frame_no-report_frame)/elapsed);
            report_start=now; report_frame=frame_no;
        }
        next.tv_nsec+=1000000000/FPS; if(next.tv_nsec>=1000000000){next.tv_sec++;next.tv_nsec-=1000000000;}
        clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&next,NULL);
    }
    if(infd>=0)close(infd);
    munmap(fb,FW*FH*4);
    close(fd);
    return 0;
}
