/* SPDX-License-Identifier: Apache-2.0
 * 同源扇形3/5/6直达位置、独立命中与镜像几何；不接设备。
 * Test exact flat slots, radial targets, safe cancellation and mirrored geometry without devices.
 */
#include "pico_system.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { int x_mg,y_mg,z_mg; } sc7a20h_sample_t;
#include "native-fan-functions.inc"
static unsigned checks,failures;
#define CHECK(e,m) do {checks++;if(!(e)&&failures++<16)fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m);}while(0)
static void setup(int land,int side){s_config.landscape=land;s_fan_side=side;s_global_app=NULL;s_fan_selected=-1;s_fan=true;s_route=PS_SHELF;s_quick_edit.active=false;quick_defaults();}
static void check_geometry(void){
 for(int rotation=0;rotation<4;rotation++)for(int side=0;side<2;side++){
  int land=rotation==EPD_ROT_LANDSCAPE||rotation==EPD_ROT_INVERTED_LANDSCAPE;setup(land,side);host_rotation=(enum EpdRotation)rotation;
  fan_item_t a[QUICK_COUNT],mirror[QUICK_COUNT];int n=fan_layout(a);ps_view_t v=ps_view();float scale=fan_scale(),halo=22*scale+360.f*5/58.5f;
  CHECK(n==14,"3 inner, 5 middle, 6 outer direct positions");
  CHECK(fan_pick(a,n,side?v.w-1:0,v.h-1,0)==-1,"corner safe area cancels previous selection");
  CHECK(fan_pick(a,n,-1,200,0)==-1&&fan_pick(a,n,v.w,200,0)==-1,"outside coordinates cancel");
  for(int i=0;i<n;i++){
   CHECK(a[i].ring==(i<3?0:i<8?1:2),"fixed ring counts");CHECK(a[i].id==s_quick_defaults[i],"position directly identifies approved action");
   CHECK(a[i].x-halo>=0&&a[i].x+halo<=v.w&&a[i].y-halo>=0&&a[i].y+halo<=v.h,"oversized visual halo fits screen");
   for(int prior=-1;prior<n;prior++)CHECK(fan_pick(a,n,a[i].x,a[i].y,prior)==i,"every center selectable after every previous icon");
   for(int dx=-15;dx<=15;dx+=5)for(int dy=-15;dy<=15;dy+=5)CHECK(fan_pick(a,n,a[i].x+dx,a[i].y+dy,-1)==i,"enlarged compact target belongs to its own icon");
   for(int j=i+1;j<n;j++)CHECK(hypotf(a[i].x-a[j].x,a[i].y-a[j].y)>a[i].r+a[j].r,"button circles never overlap");
  }
  s_fan_side=!side;int mn=fan_layout(mirror);CHECK(mn==n,"mirrored count");
  for(int i=0;i<n;i++)CHECK(abs(a[i].x+mirror[i].x-v.w)<=1&&a[i].y==mirror[i].y&&a[i].id==mirror[i].id,"mirrored content and geometry");
 }
}
static void check_continuous_selection(void){
 for(int land=0;land<2;land++)for(int side=0;side<2;side++)for(int from=0;from<FAN_SLOTS;from++)for(int to=0;to<FAN_SLOTS;to++){
  setup(land,side);fan_item_t a[QUICK_COUNT],after[QUICK_COUNT];fan_layout(a);fan_move(a[from].x,a[from].y);
  for(int step=0;step<=40;step++)fan_move(lroundf(a[from].x+(a[to].x-a[from].x)*step/40.f),lroundf(a[from].y+(a[to].y-a[from].y)*step/40.f));
  CHECK(s_fan_selected==to,"all positions selectable after continuous drag across any rings");
  CHECK(fan_layout(after)==FAN_SLOTS&&!memcmp(a,after,sizeof(fan_item_t)*FAN_SLOTS),"hover changes no icons or layout");
  s_dirty=s_fast=false;fan_move(a[to].x,a[to].y);CHECK(!s_dirty&&!s_fast,"unchanged selected point never redraws");
  fan_move(side?ps_view().w-1:0,ps_view().h-1);CHECK(s_fan_selected==-1,"return inward cancels");
 }
}
static void check_custom_empty_and_editor(void){
 static const app_desc_t child={0};
 for(int rotation=0;rotation<4;rotation++)for(int side=0;side<2;side++)for(int count=0;count<=FAN_SLOTS;count++)for(int start=0;start<38;start++){
  bool land=rotation==EPD_ROT_LANDSCAPE||rotation==EPD_ROT_INVERTED_LANDSCAPE;setup(land,side);host_rotation=(enum EpdRotation)rotation;s_global_app=&child;s_config.landscape=!land;
  memset(s_quick_slots,QUICK_EMPTY,FAN_SLOTS);for(int i=0;i<count;i++)s_quick_slots[i]=(start+i)%38;
  fan_item_t a[QUICK_COUNT];int n=fan_layout(a);CHECK(n==FAN_SLOTS,"empty slots preserve shared physical arrangement");
  for(int i=0;i<n;i++)CHECK(a[i].id==(i<count?(start+i)%38:-1)&&fan_pick(a,n,a[i].x,a[i].y,-1)==(i<count?i:-1),"runtime empty positions absent from targeting and populated slots exact");
  s_fan=false;s_route=PS_QUICK_SETTINGS;s_quick_mode=0;s_quick_edit.active=true;s_quick_edit.side=side;memcpy(s_quick_edit.slots,s_quick_slots,FAN_SLOTS);
  n=fan_layout(a);for(int i=0;i<n;i++)CHECK(fan_pick(a,n,a[i].x,a[i].y,-1)==i,"editor every actual position remains replaceable including empty inner slot");
 }
}
static void save_maps(void){
 const char *dir=getenv("PICO_FAN_TEST_OUTPUT");if(!dir)return;
 for(int land=0;land<2;land++)for(int side=0;side<2;side++){
  setup(land,side);fan_item_t a[QUICK_COUNT];int n=fan_layout(a);ps_view_t v=ps_view();char path[768];snprintf(path,sizeof(path),"%s/hit-%s-%s.pgm",dir,side?"right":"left",land?"landscape":"portrait");
  FILE *f=fopen(path,"wb");CHECK(f!=NULL,"create hit map");if(!f)continue;fprintf(f,"P5\n%d %d\n255\n",v.w,v.h);
  for(int y=0;y<v.h;y++)for(int x=0;x<v.w;x++){int selected=fan_pick(a,n,x,y,-1);unsigned char value=selected<0?255:(selected+1)*15;fwrite(&value,1,1,f);}fclose(f);
 }
}
int main(void){check_geometry();check_continuous_selection();check_custom_empty_and_editor();save_maps();printf("fan host: %u checks; %u failures\n",checks,failures);return failures?1:0;}
