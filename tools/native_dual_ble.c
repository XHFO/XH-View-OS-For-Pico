/* SPDX-License-Identifier: Apache-2.0
 * 仅原生测试：单独回环通道代替 BLE 无线和配对；不是硬件射频验收。
 * Native-only separate loopback control adapter; no real BLE radio/security acceptance.
 */
#include "read_pico_dual_ble.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static int role,listener=-1,fd=-1;
static char code[7];
static bool radio=true,connecting,ready;
static unsigned tx_at,tx_n,rx_n,hello_at,hello_n;
static uint8_t tx[97],rx[97],hello[10];
static pico_dual_ble_status_t status={.simulated=true};
static pico_dual_ble_peer_info_t peer;
static void lost(void){if(fd>=0)close(fd);fd=-1;connecting=ready=false;tx_at=tx_n=rx_n=hello_at=hello_n=0;status.connected=false;++status.generation;}
void pico_dual_ble_stop(void){lost();if(listener>=0)close(listener);listener=-1;role=0;status.active=false;}
void pico_dual_ble_test_enabled(bool enabled){radio=enabled;lost();if(listener>=0)close(listener);listener=-1;}
void pico_dual_ble_set_pairing_info(const char* ip,const char* ap_ssid){(void)ip;(void)ap_ssid;}
esp_err_t pico_dual_ble_scan_start(void){memset(&peer,0,sizeof(peer));return ESP_OK;}
void pico_dual_ble_scan_stop(void){}
void pico_dual_ble_peer_info(pico_dual_ble_peer_info_t* out){if(out)*out=peer;}
void pico_dual_ble_test_peer(const char* ip,const char* ap_ssid){
 memset(&peer,0,sizeof(peer));
 if(ip)snprintf(peer.ip,sizeof(peer.ip),"%s",ip);
 if(ap_ssid)snprintf(peer.ap_ssid,sizeof(peer.ap_ssid),"%s",ap_ssid);
 peer.direct=ap_ssid&&ap_ssid[0];peer.found=ip&&ip[0];
}
static void pump(void){
 if(!role||!radio)return;
 if(fd<0){
  unsigned port=18772;const char* env=getenv("PICO_DUAL_TEST_BLE_PORT");if(env)port=(unsigned)strtoul(env,NULL,10);
  struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
  if(role==1){
   if(listener<0){listener=socket(AF_INET,SOCK_STREAM,0);if(listener<0)return;int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    if(bind(listener,(struct sockaddr*)&addr,sizeof(addr))||listen(listener,1)||fcntl(listener,F_SETFL,O_NONBLOCK)){close(listener);listener=-1;return;}}
   fd=accept(listener,NULL,NULL);if(fd<0)return;fcntl(fd,F_SETFL,O_NONBLOCK);
  }else{
   fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return;fcntl(fd,F_SETFL,O_NONBLOCK);
   if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))&&errno!=EINPROGRESS){lost();return;}connecting=true;
  }
  int one=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
 }
 if(connecting){fd_set w;FD_ZERO(&w);FD_SET(fd,&w);struct timeval t={0};if(select(fd+1,NULL,&w,NULL,&t)<=0)return;
  int e=0;socklen_t n=sizeof(e);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&e,&n)||e){lost();return;}connecting=false;}
 if(!ready){
  uint8_t h[10]={'B','L',2,(uint8_t)role};memcpy(h+4,code,6);
  if(hello_at<10){ssize_t n=send(fd,h+hello_at,10-hello_at,0);if(n>0)hello_at+=(unsigned)n;else if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK)){lost();return;}}
  if(hello_n<10){ssize_t n=recv(fd,hello+hello_n,10-hello_n,0);if(n>0)hello_n+=(unsigned)n;else if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK)){lost();return;}}
  if(hello_n==10&&hello_at==10){if(memcmp(hello,h,3)||hello[3]==role||memcmp(hello+4,code,6)){lost();return;}ready=true;status.connected=true;++status.generation;}
  return;
 }
 if(tx_n){ssize_t n=send(fd,tx+tx_at,tx_n-tx_at,0);if(n>0){tx_at+=(unsigned)n;if(tx_at==tx_n)tx_at=tx_n=0;}else if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK)){lost();return;}}
 unsigned want=rx_n?1u+rx[0]:1u;
 if(want>sizeof(rx)||!want){lost();return;}
 if(rx_n<want){ssize_t n=recv(fd,rx+rx_n,want-rx_n,0);if(n>0)rx_n+=(unsigned)n;else if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK)){lost();return;}}
}
esp_err_t pico_dual_ble_configure(int r,const char* c){
 if((r!=1&&r!=2)||!c||strlen(c)!=6)return ESP_ERR_INVALID_ARG;
 if(role!=r||strcmp(c,code)){pico_dual_ble_stop();role=r;memcpy(code,c,7);status.active=true;}
 pump();return ESP_OK;
}
void pico_dual_ble_request_restart(void){}
void pico_dual_ble_status(pico_dual_ble_status_t* out){pump();*out=status;}
bool pico_dual_ble_can_send(void){pump();return ready&&!tx_n;}
bool pico_dual_ble_send(const uint8_t* p,size_t n){if(!p||!n||n>96||!pico_dual_ble_can_send())return false;tx[0]=(uint8_t)n;memcpy(tx+1,p,n);tx_n=(unsigned)n+1;tx_at=0;pump();return true;}
size_t pico_dual_ble_receive(uint8_t* p,size_t cap){pump();if(!p||!ready||rx_n<2||rx_n!=1u+rx[0]||cap<rx[0])return 0;size_t n=rx[0];memcpy(p,rx+1,n);rx_n=0;return n;}
