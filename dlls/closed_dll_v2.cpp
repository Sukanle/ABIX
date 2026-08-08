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

#include "abix/abix.hpp"    // IWYU pragma: keep
#include "plugin_types.h"   // IWYU pragma: keep

using namespace skl::abix;

struct Player {
    void *vtable;         ///< private, not exposed
    uint64_t id;          ///< private, not exposed
    uint64_t timestamp;   // v2 new private field
    int health;           ///< public, offset 24
    float x, y;           ///< public, offset 28, 32
};

static_assert(offsetof(Player, health) == 24, "Player::health offset must be 24 in v2");
static_assert(offsetof(Player, x) == 28, "Player::x offset must be 28 in v2");
static_assert(offsetof(Player, y) == 32, "Player::y offset must be 32 in v2");
static_assert(sizeof(Player) == 40, "Player size must be 40 in v2");


// v2 new type hash: 0x5678EF90 (diff from v1)
static constexpr uint64_t PLAYER_TYPE_HASH_V2 = 0X5678EF90ULL;

static int s_player_alive = 0;

extern "C" Player *create_player(uint64_t id) {
    ++s_player_alive;
    auto *p = new Player{};
    p->vtable = nullptr;
    p->id = id;
    p->timestamp = 0XDEADBEEFCAFEULL;
    p->health = 200;
    p->x = 0.0F;
    p->y = 0.0F;
    return p;
}

extern "C" void destroy_player(Player *p) {
    if (p) {
        --s_player_alive;
        delete p;
    }
}

extern "C" int player_alive() { return s_player_alive; }

extern "C" int player_get_health(Player *p) { return p ? p->health : -1; }

extern "C" void player_set_health(Player *p, int h) {
    if (p) p->health = h;
}

extern "C" float player_get_x(Player *p) { return p ? p->x : 0.0F; }

extern "C" float player_get_y(Player *p) { return p ? p->y : 0.0F; }

extern "C" void player_set_position(Player *p, float nx, float ny) {
    if (p) {
        p->x = nx;
        p->y = ny;
    }
}

extern "C" uint64_t player_get_id(Player *p) { return p ? p->id : 0; }

extern "C" uint64_t player_type_hash() { return PLAYER_TYPE_HASH_V2; }

extern "C" int player_offset_health() { return (int)offsetof(Player, health); }
extern "C" int player_offset_x() { return (int)offsetof(Player, x); }
extern "C" int player_offset_y() { return (int)offsetof(Player, y); }
extern "C" int player_sizeof() { return (int)sizeof(Player); }

// clang-format off
SKL_ABIX_DEFINE_TABLE(
    SKL_ABIX_ENTRY("create_player", create_player),
    SKL_ABIX_ENTRY("destroy_player", destroy_player),
    SKL_ABIX_ENTRY("player_alive", player_alive),
    SKL_ABIX_ENTRY("player_get_health", player_get_health),
    SKL_ABIX_ENTRY("player_set_health", player_set_health),
    SKL_ABIX_ENTRY("player_get_x", player_get_x),
    SKL_ABIX_ENTRY("player_get_y", player_get_y),
    SKL_ABIX_ENTRY("player_set_position", player_set_position),
    SKL_ABIX_ENTRY("player_get_id", player_get_id),
    SKL_ABIX_ENTRY("player_type_hash", player_type_hash),
    SKL_ABIX_ENTRY("player_offset_health", player_offset_health),
    SKL_ABIX_ENTRY("player_offset_x", player_offset_x),
    SKL_ABIX_ENTRY("player_offset_y", player_offset_y),
    SKL_ABIX_ENTRY("player_sizeof", player_sizeof),
)
// clang-format on