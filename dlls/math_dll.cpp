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

extern "C" int add(int a, int b) { return a + b; }
extern "C" double multiply(double x, double y) { return x * y; }

struct CalcState {
    int value;
    int op_count;
    explicit CalcState(int init = 0)
        : value(init)
        , op_count(0) {}
};

static int s_calc_state_alive = 0;

extern "C" CalcState *create_calc_state(int init) {
    ++s_calc_state_alive;
    return new CalcState(init);
}
extern "C" void destroy_calc_state(CalcState *cs) {
    if (cs) {
        --s_calc_state_alive;
        delete cs;
    }
}
extern "C" int calc_state_alive() { return s_calc_state_alive; }
extern "C" int calc_state_value(CalcState *cs) { return cs ? cs->value : -1; }
extern "C" int calc_state_add(CalcState *cs, int delta) {
    if (!cs) return -1;
    cs->value += delta;
    ++cs->op_count;
    return cs->value;
}
extern "C" int calc_state_op_count(CalcState *cs) { return cs ? cs->op_count : -1; }

struct CalcConfig {
    double factor;
    const char *mode;
    int *history;
    int history_len;
    CalcConfig(double f, const char *m, int hlen)
        : factor(f)
        , mode(m)
        , history_len(hlen) {
        history = new int[hlen];
        for (int i = 0; i < hlen; ++i)
            history[i] = 0;
    }
    ~CalcConfig() { delete[] history; }
};

static int s_calc_config_alive = 0;

extern "C" CalcConfig *create_calc_config(double factor, const char *mode) {
    ++s_calc_config_alive;
    return new CalcConfig(factor, mode, 10);
}
extern "C" void destroy_calc_config(CalcConfig *cfg) {
    if (cfg) {
        --s_calc_config_alive;
        delete cfg;
    }
}
extern "C" int calc_config_alive() { return s_calc_config_alive; }
extern "C" double calc_config_factor(CalcConfig *cfg) { return cfg ? cfg->factor : -1.0; }
extern "C" const char *calc_config_mode(CalcConfig *cfg) { return cfg ? cfg->mode : nullptr; }
extern "C" void calc_config_record(CalcConfig *cfg, int idx, int val) {
    if (cfg && idx >= 0 && idx < cfg->history_len) cfg->history[idx] = val;
}
extern "C" int calc_config_history(CalcConfig *cfg, int idx) {
    return (cfg && idx >= 0 && idx < cfg->history_len) ? cfg->history[idx] : -1;
}

ref_dll_ptr<CalcConfig> create_shared_calc_config(double factor, const char *mode) {
    return ref_dll_ptr<CalcConfig>(create_calc_config(factor, mode), destroy_calc_config);
}

struct SharedCounter {
    int count;
    explicit SharedCounter(int init = 0)
        : count(init) {}
};

static int s_shared_counter_alive = 0;

extern "C" SharedCounter *create_shared_counter(int init) {
    ++s_shared_counter_alive;
    return new SharedCounter(init);
}
extern "C" void destroy_shared_counter(SharedCounter *sc) {
    if (sc) {
        --s_shared_counter_alive;
        delete sc;
    }
}
extern "C" int shared_counter_alive() { return s_shared_counter_alive; }
extern "C" int shared_counter_value(SharedCounter *sc) { return sc ? sc->count : -1; }
extern "C" int shared_counter_increment(SharedCounter *sc) {
    if (!sc) return -1;
    return ++sc->count;
}
extern "C" int shared_counter_decrement(SharedCounter *sc) {
    if (!sc) return -1;
    return --sc->count;
}

shared_dll_ptr<SharedCounter> create_shared_shared_counter(int init) {
    return shared_dll_ptr<SharedCounter>(create_shared_counter(init), destroy_shared_counter);
}

// clang-format off
SKL_ABIX_DEFINE_TABLE(
    // Basic arithmetic
    SKL_ABIX_ENTRY("add", add), SKL_ABIX_ENTRY("multiply", multiply),

    // unique_dll_ptr: CalcState
    SKL_ABIX_ENTRY("create_calc_state", create_calc_state), SKL_ABIX_ENTRY("destroy_calc_state", destroy_calc_state),
    SKL_ABIX_ENTRY("calc_state_alive", calc_state_alive), SKL_ABIX_ENTRY("calc_state_value", calc_state_value),
    SKL_ABIX_ENTRY("calc_state_add", calc_state_add), SKL_ABIX_ENTRY("calc_state_op_count", calc_state_op_count),

    // ref_dll_ptr / view_dll_ptr: CalcConfig
    SKL_ABIX_ENTRY("create_calc_config", create_calc_config),
    SKL_ABIX_ENTRY("destroy_calc_config", destroy_calc_config), SKL_ABIX_ENTRY("calc_config_alive", calc_config_alive),
    SKL_ABIX_ENTRY("calc_config_factor", calc_config_factor), SKL_ABIX_ENTRY("calc_config_mode", calc_config_mode),
    SKL_ABIX_ENTRY("calc_config_record", calc_config_record),
    SKL_ABIX_ENTRY("calc_config_history", calc_config_history),
    SKL_ABIX_ENTRY("create_shared_calc_config", create_shared_calc_config),

    // shared_dll_ptr / weak_dll_ptr: SharedCounter
    SKL_ABIX_ENTRY("create_shared_counter", create_shared_counter),
    SKL_ABIX_ENTRY("destroy_shared_counter", destroy_shared_counter),
    SKL_ABIX_ENTRY("shared_counter_alive", shared_counter_alive),
    SKL_ABIX_ENTRY("shared_counter_value", shared_counter_value),
    SKL_ABIX_ENTRY("shared_counter_increment", shared_counter_increment),
    SKL_ABIX_ENTRY("shared_counter_decrement", shared_counter_decrement),
    SKL_ABIX_ENTRY("create_shared_shared_counter", create_shared_shared_counter), 
)
// clang-format on