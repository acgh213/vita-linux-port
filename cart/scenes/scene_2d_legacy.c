#include <cart/scene.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LW 320
#define LH 180
#define SCENES 6

static _Thread_local struct cart_canvas *current_canvas;
static float sins[2048];

/* Keep the v0.1 drawing body literal while binding it to the active scene. */
#define low (current_canvas->pixels)
#define low_canvas (*current_canvas)

static const uint32_t PAL[] = {
    0xffb8a9f5, /* trans pink */
    0xffe8d6ff, /* pale pink */
    0xffffffff, /* white */
    0xffface5b, /* trans blue */
    0xffe8bc49, /* deeper blue */
    0xffff8cc5, /* lavender */
    0xffff6c9d, /* purple */
    0xffc8f7ff, /* cream */
    0xffa82a6b, /* dark violet */
    0xffa147f4  /* hot pink */
};

static inline uint32_t rgba(int r, int g, int b)
{
    return cart_rgba(r, g, b);
}

static inline int si(int x)
{
    return (int)(sins[x & 2047] * 1024.0f);
}

static inline uint32_t mix(uint32_t a, uint32_t b, int t)
{
    return cart_mix(a, b, t < 0 ? 0U : (unsigned int)t);
}

static inline void px(int x, int y, uint32_t c)
{
    cart_canvas_px(&low_canvas, x, y, c);
}

static void rect(int x, int y, int w, int h, uint32_t c)
{
    cart_canvas_rect(&low_canvas, x, y, w, h, c);
}

static void circle(int cx, int cy, int r, uint32_t c)
{
    cart_canvas_circle(&low_canvas, cx, cy, r, c);
}

static void heart(int cx, int cy, int s, uint32_t c, uint32_t edge)
{
    static const char *shape[] = {
        "01100110", "11111111", "11111111", "11111111",
        "01111110", "00111100", "00011000", "00000000"
    };
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        if (shape[y][x] == '1') {
            int border = x == 0 || x == 7 || y == 0 || y == 6 ||
                         (x > 0 && shape[y][x-1] == '0') ||
                         (x < 7 && shape[y][x+1] == '0') ||
                         (y > 0 && shape[y-1][x] == '0');
            rect(cx + (x-4)*s, cy + (y-3)*s, s, s, border ? edge : c);
        }
    }
    if (s > 1) rect(cx-2*s, cy-2*s, s, s, 0xffffffffu);
}

static void star(int cx, int cy, int r, uint32_t c)
{
    for (int i = -r; i <= r; i++) {
        px(cx+i, cy, c); px(cx, cy+i, c);
        if (abs(i) < r/2+1) { px(cx+i, cy+i, c); px(cx+i, cy-i, c); }
    }
}

static void flower(int cx, int cy, int s, uint32_t petal, uint32_t center)
{
    circle(cx-s, cy, s, petal); circle(cx+s, cy, s, petal);
    circle(cx, cy-s, s, petal); circle(cx, cy+s, s, petal);
    circle(cx, cy, s, center);
}

static const uint8_t FONT[37][7] = {
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},{0,0,0,0,0,0,0}
};

static int font_index(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    return 36;
}

static void text(int x, int y, const char *s, int scale, uint32_t c, uint32_t shadow)
{
    if (shadow) text(x+scale, y+scale, s, scale, shadow, 0);
    for (; *s; s++, x += 6*scale) {
        if (*s == ' ') continue;
        int idx = font_index(*s);
        for (int yy = 0; yy < 7; yy++)
            for (int xx = 0; xx < 5; xx++)
                if (FONT[idx][yy] & (1 << (4-xx)))
                    rect(x+xx*scale, y+yy*scale, scale, scale, c);
    }
}

static void text_center(int y, const char *s, int scale, uint32_t c, uint32_t shadow)
{
    int width = ((int)strlen(s) * 6 - 1) * scale;
    text((LW - width) / 2, y, s, scale, c, shadow);
}

static void scene_candy(int y0, int y1, int t)
{
    for (int y=y0; y<y1; y++) for (int x=0; x<LW; x++) {
        int dx=x-160, dy=y-90;
        int d=(dx*dx+dy*dy)>>4;
        int warp=si((x*9 + t*11)&2047)+si((y*13-t*7)&2047);
        int band=((d + (warp>>7) - t*8)>>4)&7;
        uint32_t a=PAL[band%5], b=PAL[(band+1)%5];
        low[y*LW+x]=mix(a,b,(si(d*5+t*17)+1024)>>3);
    }
}

static void scene_checker(int y0, int y1, int t)
{
    for (int y=y0; y<y1; y++) for (int x=0; x<LW; x++) {
        int yy=y+(si(x*10+t*14)>>8);
        int xx=x+(si(y*13-t*9)>>8);
        int q=((xx>>3)^(yy>>3))&1;
        int glow=(si(x*6+y*8+t*12)+1024)>>4;
        low[y*LW+x]=mix(q?PAL[0]:PAL[4], q?PAL[2]:PAL[5], glow);
    }
}

static void scene_bubbles(int y0, int y1, int t)
{
    int bx[7],by[7];
    for(int i=0;i<7;i++){ bx[i]=160+(si(t*(7+i)+i*273)*(45+i*5)>>10); by[i]=90+(si(t*(9+i)+i*411)*(28+i*4)>>10); }
    for(int y=y0;y<y1;y++) for(int x=0;x<LW;x++){
        int field=0;
        for(int i=0;i<7;i++){int dx=x-bx[i],dy=y-by[i]; field += (9000+i*700)/(18+dx*dx+dy*dy);}
        int wave=(si(x*8-y*6+t*18)+1024)>>5;
        if(field>22) low[y*LW+x]=mix(PAL[1],PAL[5],(field*9+wave)&255);
        else low[y*LW+x]=mix(PAL[8],PAL[6],wave);
    }
}

static void scene_ribbons(int y0, int y1, int t)
{
    for(int y=y0;y<y1;y++) for(int x=0;x<LW;x++){
        int wave=90+(si(x*12+t*13)>>5);
        int wave2=90+(si(x*9-t*17+500)>>4);
        int d1=abs(y-wave), d2=abs(y-wave2);
        uint32_t base=mix(PAL[3],PAL[1],(y*255)/LH);
        if(d1<8) base=mix(PAL[0],PAL[2],d1*30);
        if(d2<5) base=mix(PAL[6],PAL[2],d2*45);
        low[y*LW+x]=base;
    }
}

static void scene_chrome(int y0, int y1, int t)
{
    (void)t;
    for(int y=y0;y<y1;y++) for(int x=0;x<LW;x++){
        if(y<95){ int glow=(si(x*5+y*4)+1024)>>4; low[y*LW+x]=mix(PAL[8],PAL[0],glow); }
        else {
            int z=y-94;
            int horizon=((z*13)&31)<2;
            int van=(x-160)*90/(z+8);
            int vertical=(abs(van)%42)<2;
            low[y*LW+x]=(horizon||vertical)?PAL[2]:mix(PAL[6],PAL[8],z);
        }
    }
}

static void scene_sugar(int y0, int y1, int t)
{
    for(int y=y0;y<y1;y++) for(int x=0;x<LW;x++){
        int a=si(x*10+t*13), b=si(y*14-t*9), c=si((x+y)*7+t*5);
        int v=(a+b+c+3072)/12;
        int stripe=((x+y+(si(t*17+x*3)>>7))>>3)&3;
        low[y*LW+x]=mix(PAL[(stripe+0)%5],PAL[(stripe+2)%5],v&255);
    }
}

static uint32_t rng32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; return x ^ (x >> 16);
}

static void overlay(int which, int t)
{
    if(which==0){
        for(int i=0;i<18;i++){uint32_t r=rng32(i*991u); int x=(r+t*(1+i%3))%LW, y=((r>>11)+i*17)%LH; star(x,y,2+i%3,PAL[2]);}
        heart(160,82,5,PAL[0],PAL[8]);
        text_center(139,"CANDY VORTEX",3,PAL[2],PAL[8]);
    } else if(which==1){
        for(int i=0;i<24;i++){uint32_t r=rng32(i*1777u); int x=(r%340)-10, y=((r>>10)+t*(2+i%4))%210-15; heart(x,y,1+(i%3==0),PAL[i%5],PAL[8]);}
        text_center(72,"GIRL MODE",5,PAL[2],PAL[8]);
        text_center(119,"MAXIMUM",4,PAL[0],PAL[8]);
    } else if(which==2){
        for(int i=0;i<20;i++){uint32_t r=rng32(i*31337u); star(r%LW,(r>>10)%LH,1+i%3,PAL[2]);}
        text_center(14,"BUBBLEGUM",5,PAL[2],PAL[8]);
        text_center(145,"OVERDRIVE",4,PAL[0],PAL[8]);
    } else if(which==3){
        for(int i=0;i<24;i++){
            int x=(i*29+t*(i%3+1))%350-15, y=70+(si(t*(8+i)+i*193)*55>>10);
            uint32_t left=PAL[(i+0)%5], right=PAL[(i+3)%5];
            circle(x-4,y-2,3,left); circle(x+4,y-2,3,right);
            circle(x-3,y+3,2,left); circle(x+3,y+3,2,right);
            rect(x-1,y-4,2,9,PAL[8]);
            if((i+t/3)%4==0) star(x-10,y+5,2,PAL[2]);
        }
        for(int i=0;i<12;i++) flower((i*41+t)%340-10,20+(i*23)%140,2,PAL[(i+1)%5],PAL[2]);
        text_center(143,"BUTTERFLY RIOT",3,PAL[2],PAL[8]);
    } else if(which==4){
        int cx=160+(si(t*9)*28>>10), cy=67+(si(t*13)*9>>10), r=39;
        for(int y=-r;y<=r;y++) for(int x=-r;x<=r;x++) if(x*x+y*y<=r*r){int band=((y+r)*10)/(2*r+1); px(cx+x,cy+y,PAL[band%8]);}
        circle(cx-13,cy-15,7,0xffffffffu); star(cx-17,cy-18,6,0xffffffffu);
        text_center(11,"SHE HER",6,PAL[2],PAL[8]);
        text_center(143,"HYPERDRIVE",4,PAL[0],PAL[8]);
    } else {
        for(int i=0;i<35;i++){uint32_t r=rng32(i*7331u); int x=r%LW,y=(r>>10)%LH,s=1+(i%3); if(i%3==0)heart(x,y,s,PAL[i%5],PAL[8]); else if(i%3==1)flower(x,y,2+s,PAL[i%5],PAL[2]); else star(x,y,2+s,PAL[2]);}
        text_center(54,"TOO MUCH",6,PAL[2],PAL[8]);
        text_center(104,"IS ENOUGH",5,PAL[9],PAL[8]);
    }

    /* candy scene dots, not an interface */
    for(int i=0;i<SCENES;i++) circle(145+i*6,171, i==which?2:1, i==which?PAL[2]:PAL[8]);
}

#undef low_canvas
#undef low

int cart_legacy_scenes_init(void)
{
    for (int index = 0; index < 2048; index++)
        sins[index] = sinf((float)index * 6.28318530718f / 2048.0f);
    return 0;
}

static void render_legacy_scene(const struct cart_scene_render_context *context, int which)
{
    if (context == NULL || context->canvas == NULL || which < 0 || which >= SCENES)
        return;
    current_canvas = context->canvas;
    if (context->phase == CART_SCENE_RENDER_ROWS) {
        switch (which) {
        case 0: scene_candy(context->row_start, context->row_end, context->frame); break;
        case 1: scene_checker(context->row_start, context->row_end, context->frame); break;
        case 2: scene_bubbles(context->row_start, context->row_end, context->frame); break;
        case 3: scene_ribbons(context->row_start, context->row_end, context->frame); break;
        case 4: scene_chrome(context->row_start, context->row_end, context->frame); break;
        default: scene_sugar(context->row_start, context->row_end, context->frame); break;
        }
    } else {
        overlay(which, context->frame);
    }
    current_canvas = NULL;
}

void cart_legacy_scene_candy(const struct cart_scene_render_context *context) { render_legacy_scene(context, 0); }
void cart_legacy_scene_girl_mode(const struct cart_scene_render_context *context) { render_legacy_scene(context, 1); }
void cart_legacy_scene_bubblegum(const struct cart_scene_render_context *context) { render_legacy_scene(context, 2); }
void cart_legacy_scene_butterfly_riot(const struct cart_scene_render_context *context) { render_legacy_scene(context, 3); }
void cart_legacy_scene_hyperdrive(const struct cart_scene_render_context *context) { render_legacy_scene(context, 4); }
void cart_legacy_scene_too_much(const struct cart_scene_render_context *context) { render_legacy_scene(context, 5); }
