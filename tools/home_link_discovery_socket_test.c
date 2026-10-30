/* SPDX-License-Identifier: Apache-2.0
 * 本地UDP回放只改传输目的地，运行生产发现循环。/ Loopback UDP replay redirects transport only and runs the production discovery loop.
 */
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <assert.h>

static uint16_t fixture_port;
static int fixture_fd, fail_send, fail_socket;
static unsigned sent;
static ssize_t replay_sendto(int fd, const void *data, size_t n, int flags, const struct sockaddr *to, socklen_t length) {
    assert(length == sizeof(struct sockaddr_in));
    const struct sockaddr_in *original = (const struct sockaddr_in *)to;
    assert(original->sin_port == htons(5353));
    assert(original->sin_addr.s_addr == inet_addr("224.0.0.251"));
    assert(n >= 18 && ((const unsigned char *)data)[n - 2] == 0x80);
    ++sent;
    if (fail_send) { errno = ENETDOWN; return -1; }
    struct sockaddr_in loopback = *original;
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK); loopback.sin_port = fixture_port;
    return sendto(fd, data, n, flags, (struct sockaddr *)&loopback, sizeof(loopback));
}
static ssize_t replay_recvfrom(int fd, void *data, size_t n, int flags, struct sockaddr *from, socklen_t *length) {
    ssize_t result = recvfrom(fd, data, n, flags, from, length);
    if (result > 0) {
        assert(((struct sockaddr_in *)from)->sin_port == fixture_port);
        ((struct sockaddr_in *)from)->sin_port = htons(5353);
    }
    return result;
}
static int replay_socket(int domain, int type, int protocol) {
    if (fail_socket) { errno = EMFILE; return -1; }
    return socket(domain, type, protocol);
}
#define PICO_NATIVE_PREVIEW 1
#define sendto replay_sendto
#define recvfrom replay_recvfrom
#define socket replay_socket
#include "../components/read_pico_home_link/home_link_discovery.c"
#undef sendto
#undef recvfrom
#undef socket
#define main parser_test_main
#include "home_link_discovery_host_test.c"
#undef main

static packet_t fixture_packet;
static void *responder(void *unused) {
    (void)unused;
    uint8_t request[512]; struct sockaddr_in peer; socklen_t length = sizeof(peer);
    assert(recvfrom(fixture_fd, request, sizeof(request), 0, (struct sockaddr *)&peer, &length) > 0);
    assert(sendto(fixture_fd, fixture_packet.b, fixture_packet.n, 0, (struct sockaddr *)&peer, length) == (ssize_t)fixture_packet.n);
    return NULL;
}
static bool cancel_after(void *context) { return now_ms() >= *(int64_t *)context; }
int main(void) {
    fixture_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); CHECK(fixture_fd >= 0);
    struct sockaddr_in local = { .sin_family = AF_INET, .sin_port = 0, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    CHECK(!bind(fixture_fd, (struct sockaddr *)&local, sizeof(local)));
    socklen_t length = sizeof(local); CHECK(!getsockname(fixture_fd, (struct sockaddr *)&local, &length)); fixture_port = local.sin_port;
    start(&fixture_packet); srv(&fixture_packet, "UDP Home._home-assistant._tcp.local", "uuid.local", 8214); a(&fixture_packet, "uuid.local");
    pthread_t server; CHECK(!pthread_create(&server, NULL, responder, NULL));
    rp_home_discovery_t out; int64_t begin = now_ms();
    CHECK(rp_home_discover(&out, 1, NULL, NULL)); CHECK(out.count == 1); CHECK(!strcmp(out.items[0].url, "http://192.168.8.42:8214"));
    CHECK(now_ms() - begin >= 500 && now_ms() - begin < 1500); CHECK(!pthread_join(server, NULL));
    begin = now_ms(); int64_t deadline = begin + 120;
    CHECK(!rp_home_discover(&out, 4000, cancel_after, &deadline)); CHECK(out.count == 0); CHECK(now_ms() - begin < 450);
    begin = now_ms(); deadline = 0;
    CHECK(!rp_home_discover(&out, 4000, cancel_after, &deadline)); CHECK(now_ms() - begin < 50);
    fail_send = 1; CHECK(!rp_home_discover(&out, 500, NULL, NULL)); fail_send = 0;
    fail_socket = 1; CHECK(!rp_home_discover(&out, 500, NULL, NULL)); fail_socket = 0;
    unsigned before_offline = sent;
    CHECK(!setenv("RPICO_HOME_LINK_OFFLINE", "1", 1));
    CHECK(rp_home_discover(&out, 4000, NULL, NULL)); CHECK(out.count == 0); CHECK(sent == before_offline);
    CHECK(!unsetenv("RPICO_HOME_LINK_OFFLINE"));
    CHECK(sent < 10); close(fixture_fd);
    printf("home discovery UDP replay: %u checks passed\n", checks);
    return 0;
}
