/* 与真实旧EPD绘图函数逐字节比较，坏输入不得修改目标。/ Byte-compare real legacy EPD drawing and reject corrupt inputs without writes. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_image.h"

typedef struct {int x,y,width,height;} EpdRect;
typedef struct {uint16_t x,y;} Coord_xy;
enum EpdRotation {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
static enum EpdRotation display_rotation;
static int epd_width(void){return 1216;}
static int epd_height(void){return 684;}
static enum EpdRotation epd_get_rotation(void){return display_rotation;}
#define _swap_int(a,b) do {uint16_t swap_value=(a);(a)=(b);(b)=swap_value;} while(0)
/* 仅屏蔽未改动的旧参考函数告警，新解码器仍按Werror校验。/ Suppress warnings only in unchanged reference functions. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "legacy_image_functions.inc"
#pragma GCC diagnostic pop

static uint8_t *read_file(const char *name,size_t *bytes){FILE *f=fopen(name,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);uint8_t *out=malloc((size_t)n);assert(out);assert(fread(out,1,(size_t)n,f)==(size_t)n);fclose(f);*bytes=(size_t)n;return out;}
static void unchanged(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)assert(p[i]==0xa5);}
int main(int argc,char **argv){
    assert(argc==3);size_t raw_n,packed_n;uint8_t *raw=read_file(argv[1],&raw_n),*packed=read_file(argv[2],&packed_n);assert(raw_n==415872);
    uint8_t *guard=malloc(raw_n+32),*legacy=malloc(raw_n),*damaged=malloc(packed_n);assert(guard&&legacy&&damaged);uint8_t *fb=guard+16;
    assert(ui_image_validate(packed,packed_n,1216,684));
    for(int rot=0;rot<4;rot++){
        memset(guard,0xa5,raw_n+32);memset(legacy,0x5a,raw_n);display_rotation=(enum EpdRotation)rot;
        EpdRect area={0,0,epd_rotated_display_width(),epd_rotated_display_height()};
        epd_draw_rotated_image(area,raw,legacy);
        assert(ui_image_decode(fb,raw_n,packed,packed_n,1216,684,(ui_image_rotation_t)rot));
        assert(!memcmp(fb,legacy,raw_n));unchanged(guard,16);unchanged(fb+raw_n,16);
    }
    memset(guard,0xa5,raw_n+32);
    size_t offsets[]={0,7,8,9,10,11,12,15,16,19,20,23,24};
    for(size_t i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){
        memcpy(damaged,packed,packed_n);damaged[offsets[i]]^=0x80;
        assert(!ui_image_decode(fb,raw_n,damaged,packed_n,1216,684,UI_IMAGE_PORTRAIT));unchanged(guard,raw_n+32);
    }
    memcpy(damaged,packed,packed_n);damaged[packed_n-1]^=1;
    assert(!ui_image_decode(fb,raw_n,damaged,packed_n,1216,684,UI_IMAGE_LANDSCAPE));unchanged(guard,raw_n+32);
    for(size_t n=0;n<32;n++){assert(!ui_image_decode(fb,raw_n,packed,n,1216,684,UI_IMAGE_LANDSCAPE));unchanged(guard,raw_n+32);}
    assert(!ui_image_decode(fb,raw_n,packed,packed_n-1,1216,684,UI_IMAGE_LANDSCAPE));
    assert(!ui_image_decode(fb,raw_n-1,packed,packed_n,1216,684,UI_IMAGE_LANDSCAPE));
    assert(!ui_image_decode(fb,raw_n,packed,packed_n,1214,684,UI_IMAGE_LANDSCAPE));
    assert(!ui_image_decode(fb,raw_n,packed,packed_n,1216,684,(ui_image_rotation_t)99));
    assert(!ui_image_decode(fb,raw_n,NULL,0,1216,684,UI_IMAGE_LANDSCAPE));
    assert(!ui_image_decode(NULL,raw_n,packed,packed_n,1216,684,UI_IMAGE_LANDSCAPE));
    assert(!ui_image_decode(packed,raw_n,packed,packed_n,1216,684,UI_IMAGE_LANDSCAPE));
    unchanged(guard,raw_n+32);
    printf("%s: exact legacy EPD output in all four rotations; CRC/header/truncation/capacity/overlap rejection leaves framebuffer untouched\n",argv[2]);
    free(raw);free(packed);free(guard);free(legacy);free(damaged);return 0;
}
