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
#include <cstring>

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
    int fd;
    char rxbuffer[64];
    int rx_len;
    int state;

    explicit Socket(int f)
        : fd(f)
        , state(1)
        , rx_len(0) {
        memset(rxbuffer, 0, sizeof(rxbuffer));
    }
    ~Socket() { state = 0; }
};

static int s_socket_alive = 0;
static int s_next_fd = 1'000;

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
    ++s_socket_alive;
    return new Socket(s_next_fd++);
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
extern "C" int socket_fd(const Socket *s) { return s ? s->fd : -1; }

extern "C" int socket_send(Socket *s, const char *data) {
    if (!s || s->state != 1) return -1;
    size_t n = strlen(data);
    if (n >= sizeof(s->rxbuffer)) n = sizeof(s->rxbuffer) - 1;
    memcpy(s->rxbuffer, data, n);
    s->rxbuffer[n] = '\0';
    s->rx_len = static_cast<int>(n);
    return static_cast<int>(n);
}
extern "C" int socket_recv(Socket *s, char *out, int cap) {
    if (!s || s->state != 1 || !out || cap <= 0) return -1;
    int n = s->rx_len < cap ? s->rx_len : cap;
    memcpy(out, s->rxbuffer, static_cast<size_t>(n));
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