/*
 * 小尺寸PackBits解码器，保持原4bpp图片及EPD方向的像素结果。
 * Small PackBits decoder preserving the raw 4bpp assets and EPD rotation result.
 * 冻结：不分配整帧、不操作硬件、不修改资源；坏流绝不部分写入目标。
 * Frozen: No full-frame allocation, hardware access, or asset changes; corrupt streams never partially update output.
 */
#include "ui_image.h"
#include <limits.h>
#include <string.h>

#define HEADER_BYTES 24u
static const uint8_t magic[8]={'P','I','C','O','4','B','P','1'};

static uint16_t read16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));}
static uint32_t read32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t crc_byte(uint32_t crc,uint8_t byte){
    crc^=byte;
    for(int i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
    return crc;
}
static bool dimensions(int w,int h,size_t *bytes){
    if(w<=0||h<=0||w>UINT16_MAX||h>UINT16_MAX||(w&1)||(h&1))return false;
    *bytes=(size_t)w*(size_t)h/2;
    return *bytes>0;
}
bool ui_image_validate(const uint8_t *p,size_t n,int w,int h){
    size_t expected;
    if(!p||n<HEADER_BYTES||!dimensions(w,h,&expected)||memcmp(p,magic,sizeof(magic)))return false;
    if(read16(p+8)!=w||read16(p+10)!=h||read32(p+12)!=expected||n-HEADER_BYTES!=read32(p+16))return false;
    size_t at=HEADER_BYTES,out=0;uint32_t crc=0xffffffffu;
    while(at<n){
        uint8_t control=p[at++];size_t count;
        if(control==128)return false;
        if(control<128){
            count=(size_t)control+1;
            if(count>n-at||count>expected-out)return false;
            for(size_t i=0;i<count;i++)crc=crc_byte(crc,p[at+i]);
            at+=count;
        }else{
            count=257u-(size_t)control;
            if(at>=n||count>expected-out)return false;
            uint8_t byte=p[at++];
            for(size_t i=0;i<count;i++)crc=crc_byte(crc,byte);
        }
        out+=count;
    }
    return out==expected&&~crc==read32(p+20);
}
static bool overlaps(const uint8_t *a,size_t an,const uint8_t *b,size_t bn){
    uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
    return aa<=bb?bb-aa<an:aa-bb<bn;
}
static void pixel(uint8_t *fb,int native_w,int native_h,int x,int y,uint8_t value,ui_image_rotation_t rot){
    int nx=x,ny=y;
    if(rot==UI_IMAGE_PORTRAIT){nx=native_w-y-1;ny=x;}
    else if(rot==UI_IMAGE_INVERTED_LANDSCAPE){nx=native_w-x-1;ny=native_h-y-1;}
    else if(rot==UI_IMAGE_INVERTED_PORTRAIT){nx=y;ny=native_h-x-1;}
    size_t index=(size_t)ny*(size_t)native_w/2+(size_t)nx/2;
    if(nx&1)fb[index]=(uint8_t)((fb[index]&15u)|(value<<4));
    else fb[index]=(uint8_t)((fb[index]&240u)|value);
}
static void put(uint8_t *fb,size_t out,uint8_t value,int w,int h,ui_image_rotation_t rot){
    if(rot==UI_IMAGE_LANDSCAPE){fb[out]=value;return;}
    int logical_w=(rot&1)?h:w;
    size_t first=out*2;
    int x=(int)(first%(size_t)logical_w),y=(int)(first/(size_t)logical_w);
    pixel(fb,w,h,x,y,value&15u,rot);
    pixel(fb,w,h,x+1,y,value>>4,rot);
}
bool ui_image_decode(uint8_t *fb,size_t fb_bytes,const uint8_t *p,size_t n,int w,int h,ui_image_rotation_t rot){
    size_t expected;
    if(!fb||!dimensions(w,h,&expected)||fb_bytes<expected||(unsigned)rot>3||!p||overlaps(fb,expected,p,n))return false;
    if(!ui_image_validate(p,n,w,h))return false;
    size_t at=HEADER_BYTES,out=0;
    while(at<n){
        uint8_t control=p[at++];size_t count=control<128?(size_t)control+1:257u-(size_t)control;
        if(control<128){
            if(rot==UI_IMAGE_LANDSCAPE)memcpy(fb+out,p+at,count);
            else for(size_t i=0;i<count;i++)put(fb,out+i,p[at+i],w,h,rot);
            at+=count;
        }else{
            uint8_t byte=p[at++];
            if(rot==UI_IMAGE_LANDSCAPE)memset(fb+out,byte,count);
            else for(size_t i=0;i<count;i++)put(fb,out+i,byte,w,h,rot);
        }
        out+=count;
    }
    return true;
}
