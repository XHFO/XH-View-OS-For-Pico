/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 输出真实二维码光栅供独立解码器校验。/ Emit actual QR rasters for an independent decoder.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_wifi_qr.h"
#define SIDE 240
static unsigned char image[SIDE*SIDE];
void epd_fill_rect(EpdRect r,uint8_t color,uint8_t* fb) {
    assert(r.x>=0 && r.y>=0 && r.x+r.width<=SIDE && r.y+r.height<=SIDE);
    for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++)fb[y*SIDE+x]=color;
}
static void save(const char* file) {
    FILE* out=fopen(file,"wb"); assert(out);
    fprintf(out,"P5\n%d %d\n255\n",SIDE,SIDE);
    assert(fwrite(image,1,sizeof(image),out)==sizeof(image)); assert(!fclose(out));
}
int main(void) {
    assert(ui_wifi_qr_prepare("ReadPico-5945","readpico"));
    ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
    save("build/wifi-qr-normal.pgm");
    assert(ui_wifi_qr_prepare("Pico;,:\\\"","pass;,:\\\""));
    ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
    save("build/wifi-qr-escaped.pgm");
    assert(ui_wifi_qr_prepare_url("http://192.168.4.1"));
    ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
    save("build/wifi-qr-url-ap.pgm");
    assert(ui_wifi_qr_prepare_url("http://192.168.123.234"));
    ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
    save("build/wifi-qr-url-sta.pgm");
    assert(ui_wifi_qr_prepare_url("http://192.168.50.123/manage#key=0123456789abcdef0123456789abcdef&view=weread"));
    ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
    save("build/wifi-qr-url-weread.pgm");
    assert(ui_wifi_qr_prepare_url("http://192.168.50.123/h#t=0123456789abcdef0123456789abcdef"));
    ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
    save("build/wifi-qr-url-home.pgm");
    assert(ui_wifi_qr_prepare_url("http://192.168.50.123:8080/h#t=0123456789abcdef0123456789abcdef"));
    assert(ui_wifi_qr_prepare_weread("https://weread.qq.com/web/reader/demoBookId"));
    ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
    save("build/wifi-qr-weread-reader.pgm");
    assert(ui_wifi_qr_prepare_weread("https://weread.qq.com/web/bookDetail/demoBookId"));
    ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
    save("build/wifi-qr-weread-detail.pgm");
    const char* invalid_reader[]={NULL,"","http://weread.qq.com/web/reader/demo","https://weread.qq.com.evil/web/reader/demo",
        "https://weread.qq.com@evil/web/reader/demo","https://weread.qq.com/web/reader/","https://weread.qq.com/web/reader/?x=1",
        "https://weread.qq.com/web/reader/demo\n","https://weread.qq.com/web/reader/demo\\evil","https://weread.qq.com/web/reader/demo'",
        "https://weread.qq.com/web/other/demo"};
    for(size_t i=0;i<sizeof(invalid_reader)/sizeof(invalid_reader[0]);i++){
        assert(!ui_wifi_qr_prepare_weread(invalid_reader[i]));ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
        for(size_t n=0;n<sizeof(image);n++)assert(image[n]==255);
    }
    const char* invalid[] = {NULL,"","https://192.168.4.1","http://256.1.1.1","http://1.2.3","http://1.2.3.4@evil","WIFI:T:WPA;S:test;;",
        "http://1.2.3.4/manage#key=0123456789abcdef0123456789abcdef&view=other",
        "http://1.2.3.4/manage#key=0123456789abcdef0123456789abcdef&view=weread&extra=x",
        "http://1.2.3.4/manage#key=0123456789abcdef0123456789abcdef&view=weread/",
        "http://1.2.3.4/manage#key=0123456789abcdef0123456789abcdef0&view=weread",
        "http://1.2.3.4/h#t=0123456789abcdef0123456789abcdef&view=weread",
        "http://1.2.3.4/h#t=0123456789abcdef0123456789abcdef&extra=1",
        "http://1.2.3.4/h#t=0123456789abcdef0123456789abcdef0",
        "http://1.2.3.4/h#t=0123456789abcdef0123456789abcde",
        "http://1.2.3.4/h#t=0123456789abcdef0123456789abcdeg",
        "http://1.2.3.4/h#t=0123456789abcdef0123456789abcdeF",
        "http://1.2.3.4/h#t=0123456789abcdef0123456789abcdef/",
        "http://1.2.3.4/h?token=0123456789abcdef0123456789abcdef",
        "http://1.2.3.4/other#t=0123456789abcdef0123456789abcdef"};
    for(size_t j=0;j<sizeof(invalid)/sizeof(invalid[0]);++j) {
        assert(!ui_wifi_qr_prepare_url(invalid[j]));
        ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
        for(size_t i=0;i<sizeof(image);i++)assert(image[i]==255);
    }
    assert(ui_wifi_qr_prepare_url("http://10.20.30.40/"));
    ui_wifi_qr_clear();
    ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
    for(size_t i=0;i<sizeof(image);i++)assert(image[i]==255);
    assert(!ui_wifi_qr_prepare(NULL,"readpico"));
    ui_wifi_qr_draw(image,(EpdRect){0,0,SIDE,SIDE});
    for(size_t i=0;i<sizeof(image);i++)assert(image[i]==255);
    puts("QR raster and invalid-cache reset passed");
}
