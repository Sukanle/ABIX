/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "abix/abix.hpp"
#include "plugin_types.h"
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socket_t = SOCKET;
#  define INVALID_SOCK socket_t(-1)
#  define CLOSE_SOCKET(s) closesocket(s)
static void init_winsock() {
    static bool s_initialized = false;
    if (!s_initialized) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        s_initialized = true;
    }
}
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
using socket_t = int;
#  define INVALID_SOCK (-1)
#  define CLOSE_SOCKET(s) close(s)
static void init_winsock() {}
#endif

using namespace skl::abix;

struct Resource {
    int id;
    int *payload;
    explicit Resource(int i)
        : id(i)
        , payload(new int[i + 1]) {
        for (int k = 0; k <= i; ++k)
            payload[k] = k;
    }
    ~Resource() { delete[] payload; }
};

static int s_resource_alive = 0;
static int s_config_alive = 0;

extern "C" Resource *create_resource(int id) {
    ++s_resource_alive;
    return new Resource(id);
}
extern "C" void destroy_resource(Resource *r) {
    if (r) {
        --s_resource_alive;
        delete r;
    }
}
extern "C" int get_resource_alive() { return s_resource_alive; }
extern "C" int resource_id(Resource *r) { return r ? r->id : -1; }
extern "C" int resource_payload(Resource *r, int i) { return r && i >= 0 && i <= r->id ? r->payload[i] : -1; }

struct Config {
    int width;
    int height;
};

extern "C" Config *create_config(int w, int h) {
    ++s_config_alive;
    return new Config{w, h};
}
extern "C" void destroy_config(Config *c) {
    if (c) {
        --s_config_alive;
        delete c;
    }
}
extern "C" int get_config_alive() { return s_config_alive; }
extern "C" int config_area(Config *c) { return c ? c->width * c->height : -1; }

ref_dll_ptr<Config> create_shared_config(int w, int h) {
    return ref_dll_ptr<Config>(create_config(w, h), destroy_config);
}

struct Socket {
    socket_t client_fd;   // 用于 send 的客户端 socket
    socket_t server_fd;   // 用于 recv 的服务端 socket（已 accept）
    int state;

    Socket()
        : client_fd(INVALID_SOCK)
        , server_fd(INVALID_SOCK)
        , state(0) {}

    ~Socket() {
        if (client_fd != INVALID_SOCK) CLOSE_SOCKET(client_fd);
        if (server_fd != INVALID_SOCK) CLOSE_SOCKET(server_fd);
    }
};

static int s_socket_alive = 0;

extern "C" char *strdup_copy(const char *src, int *out_len) {
    if (!src) {
        if (out_len) *out_len = 0;
        return nullptr;
    }
    size_t n = strlen(src);
    char *buf = static_cast<char *>(abi_alloc(n + 1));
    if (!buf) {
        if (out_len) *out_len = 0;
        return nullptr;
    }
    memcpy(buf, src, n + 1);
    if (out_len) *out_len = static_cast<int>(n);
    return buf;
}
extern "C" void string_destroy(char *s) { abi_free(s); }

extern "C" Socket *socket_open(const char *host, int port) {
    (void)host;
    (void)port;
    init_winsock();

    socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCK) return nullptr;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;   // OS 自动分配端口

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        CLOSE_SOCKET(listener);
        return nullptr;
    }
    if (listen(listener, 1) != 0) {
        CLOSE_SOCKET(listener);
        return nullptr;
    }

    // 获取实际分配的端口号
    socklen_t addr_len = sizeof(addr);
    if (getsockname(listener, (struct sockaddr *)&addr, &addr_len) != 0) {
        CLOSE_SOCKET(listener);
        return nullptr;
    }

    socket_t client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCK) {
        CLOSE_SOCKET(listener);
        return nullptr;
    }

    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        CLOSE_SOCKET(client);
        CLOSE_SOCKET(listener);
        return nullptr;
    }

    socket_t server = accept(listener, nullptr, nullptr);
    CLOSE_SOCKET(listener);   // listener 不再需要

    if (server == INVALID_SOCK) {
        CLOSE_SOCKET(client);
        return nullptr;
    }

    auto *s = new Socket();
    s->client_fd = client;
    s->server_fd = server;
    s->state = 1;
    ++s_socket_alive;
    return s;
}

extern "C" void socket_close(Socket *s) {
    if (s && s->state) {
        s->state = 0;
        --s_socket_alive;
        delete s;
    }
}

extern "C" int socket_alive() { return s_socket_alive; }
extern "C" int socket_is_open(const Socket *s) { return s ? s->state : 0; }
extern "C" int socket_fd(const Socket *s) {
    return (s && s->client_fd != INVALID_SOCK) ? static_cast<int>(s->client_fd) : -1;
}

extern "C" int socket_send(Socket *s, const char *data) {
    if (!s || s->state != 1 || !data) return -1;
    int n = static_cast<int>(strlen(data));
    int sent = send(s->client_fd, data, n, 0);
    return sent;
}

extern "C" int socket_recv(Socket *s, char *out, int cap) {
    if (!s || s->state != 1 || !out || cap <= 0) return -1;
    int n = recv(s->server_fd, out, cap, 0);
    return n;
}

// clang-format off
SKL_ABIX_DEFINE_TABLE(
    SKL_ABIX_ENTRY("config_area", config_area),
    SKL_ABIX_ENTRY("create_config", create_config),
    SKL_ABIX_ENTRY("create_resource", create_resource),
    SKL_ABIX_ENTRY("create_shared_config", create_shared_config),
    SKL_ABIX_ENTRY("destroy_config", destroy_config),
    SKL_ABIX_ENTRY("destroy_resource", destroy_resource),
    SKL_ABIX_ENTRY("get_config_alive", get_config_alive),
    SKL_ABIX_ENTRY("get_resource_alive", get_resource_alive),
    SKL_ABIX_ENTRY("resource_id", resource_id),
    SKL_ABIX_ENTRY("resource_payload", resource_payload),
    SKL_ABIX_ENTRY("socket_alive", socket_alive),
    SKL_ABIX_ENTRY("socket_close", socket_close),
    SKL_ABIX_ENTRY("socket_fd", socket_fd),
    SKL_ABIX_ENTRY("socket_is_open", socket_is_open),
    SKL_ABIX_ENTRY("socket_open", socket_open),
    SKL_ABIX_ENTRY("socket_recv", socket_recv),
    SKL_ABIX_ENTRY("socket_send", socket_send),
    SKL_ABIX_ENTRY("strdup_copy", strdup_copy),
    SKL_ABIX_ENTRY("string_destroy", string_destroy),
)
// clang-format on