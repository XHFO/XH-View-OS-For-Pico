/* SPDX-License-Identifier: Apache-2.0 */
// HID 描述、方向映射和释放门控回归。/ HID descriptors, direction mapping and release-gating regression.
#include "pager_hid.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)

static const uint8_t keyboard[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,
    0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
    0x95,0x01,0x75,0x08,0x81,0x01,
    0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xc0
};
static const uint8_t consumer_bits[] = {
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x03,
    0x15,0x00,0x25,0x01,0x09,0xe9,0x09,0xea,0x75,0x01,0x95,0x02,0x81,0x02,
    0x75,0x06,0x95,0x01,0x81,0x01,0xc0
};
static const uint8_t consumer_array[] = {
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x02,
    0x15,0x00,0x26,0xff,0x03,0x19,0x00,0x2a,0xff,0x03,0x75,0x10,0x95,0x01,0x81,0x00,0xc0
};

static void keyboard_test(void)
{
    pager_hid_t h;
    CHECK(pager_hid_parse(&h, keyboard, sizeof(keyboard)));
    const uint8_t keys[] = {0x4b,0x50,0x52,0x4e,0x4f,0x51,0x2c};
    const int dirs[] = {-1,-1,-1,1,1,1,1};
    for (unsigned i=0; i<sizeof(keys); ++i) {
        uint8_t report[8] = {0}; report[2]=keys[i];
        CHECK(pager_hid_input(&h,0,report,8)==dirs[i]);
        for (int j=0;j<20;++j) CHECK(pager_hid_input(&h,0,report,8)==0);
        CHECK(pager_hid_input(&h,0,report,7)==0);
        CHECK(pager_hid_input(&h,1,report,8)==0);
        memset(report,0,8); CHECK(pager_hid_input(&h,0,report,8)==0);
    }
    uint8_t report[8]={0,0,0x4e};
    CHECK(pager_hid_input(&h,0,report,8)==1);
    report[2]=1; CHECK(pager_hid_input(&h,0,report,8)==0);
    report[2]=0x4e; CHECK(pager_hid_input(&h,0,report,8)==0);
    report[3]=0x4b; CHECK(pager_hid_input(&h,0,report,8)==0);
    memset(report,0,8); CHECK(pager_hid_input(&h,0,report,8)==0);
    report[2]=0x4b; CHECK(pager_hid_input(&h,0,report,8)==-1);
    uint8_t held=0; CHECK(pager_hid_boot_input(&held,report,8)==-1);
    CHECK(pager_hid_boot_input(&held,report,8)==0);
    report[2]=0; CHECK(pager_hid_boot_input(&held,report,8)==0);
    report[2]=0x4e; CHECK(pager_hid_boot_input(&held,report,8)==1);
    CHECK(pager_hid_boot_input(&held,report,7)==0);
}

static void consumer_test(void)
{
    pager_hid_t h;
    CHECK(pager_hid_parse(&h,consumer_bits,sizeof(consumer_bits)));
    CHECK(h.field_count==2);
    uint8_t report=1;
    CHECK(pager_hid_input(&h,3,&report,1)==1);
    CHECK(pager_hid_input(&h,3,&report,1)==0);
    report=0; CHECK(pager_hid_input(&h,3,&report,1)==0);
    report=2; CHECK(pager_hid_input(&h,3,&report,1)==-1);
    report=3; CHECK(pager_hid_input(&h,3,&report,1)==0);
    report=0; CHECK(pager_hid_input(&h,3,&report,1)==0);
    CHECK(pager_hid_parse(&h,consumer_array,sizeof(consumer_array)));
    const uint8_t keys[]={0xe9,0xea,0xb5,0xb6};
    const int dirs[]={1,-1,1,-1};
    for(unsigned i=0;i<sizeof(keys);++i) {
        uint8_t bytes[2]={keys[i],0};
        CHECK(pager_hid_input(&h,2,bytes,2)==dirs[i]);
        CHECK(pager_hid_input(&h,2,bytes,2)==0);
        memset(bytes,0,2); CHECK(pager_hid_input(&h,2,bytes,2)==0);
    }
}

static void malformed_test(void)
{
    pager_hid_t h;
    for (size_t n=0;n<sizeof(keyboard)-1;++n) {
        // 在完整 Input 之前的所有截断均不得产生可用报告。/ No usable report before the complete Input item.
        if (n<sizeof(keyboard)-2) CHECK(!pager_hid_parse(&h,keyboard,n));
    }
    uint8_t too_many[]={0x05,7,0x75,8,0x96,1,1,0x81,0};
    CHECK(!pager_hid_parse(&h,too_many,sizeof(too_many)));
    uint8_t invalid_id[]={0x85,0}; CHECK(!pager_hid_parse(&h,invalid_id,sizeof(invalid_id)));
    uint8_t pop[]={0xb4}; CHECK(!pager_hid_parse(&h,pop,sizeof(pop)));
    uint8_t long_item[]={0xfe,3,0,1,2,3}; CHECK(!pager_hid_parse(&h,long_item,sizeof(long_item)));
    CHECK(!pager_hid_parse(&h,NULL,0));
    CHECK(!pager_hid_parse(NULL,keyboard,sizeof(keyboard)));
    CHECK(pager_hid_input(&h,0,NULL,8)==0);
    uint8_t junk[1024]={0};
    for (unsigned seed=1;seed<3000;++seed) {
        unsigned x=seed;
        for(unsigned j=0;j<sizeof(junk);++j){x=x*1664525u+1013904223u;junk[j]=(uint8_t)(x>>24);}
        (void)pager_hid_parse(&h,junk,seed%1024);
        (void)pager_hid_input(&h,(uint8_t)seed,junk,seed%65);
        ++checks;
    }
}

int main(void)
{
    keyboard_test(); consumer_test(); malformed_test();
    printf("BLE HID parser: %u checks passed\n",checks);
    return 0;
}
