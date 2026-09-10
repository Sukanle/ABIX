// ============================================================
// Shared helpers for EBR benchmarks
// ============================================================
#pragma once

#include <benchmark/benchmark.h>

#include "abix/abix.hpp"

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#endif

#ifndef SET_THREAD_AFFINITY
#define SET_THREAD_AFFINITY
inline bool set_thread_affinity(int cpu_id) {
#if defined(__APPLE__)
    thread_affinity_policy_data_t policy = {cpu_id};
    thread_port_t thread = pthread_mach_thread_np(pthread_self());
    kern_return_t kr = thread_policy_set(thread, THREAD_AFFINITY_POLICY,
                                         (thread_policy_t)&policy,
                                         THREAD_AFFINITY_POLICY_COUNT);
    return kr == KERN_SUCCESS;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#else
    (void)cpu_id;
    return false;
#endif
}
#endif

// ============================================================
// Shared domain
// ============================================================
inline skl::abix::rcu_domain &domain = skl::abix::rcu_domain::instance();

// ============================================================
// Layout diagnostics — print rcu_domain cache-line layout
// ============================================================
inline void print_rcu_domain_layout() {
    printf("rcu_domain layout: sizeof=%zu, cache_lines=%zu\n",
           sizeof(skl::abix::rcu_domain),
           (sizeof(skl::abix::rcu_domain) + 63) / 64);
    printf("  Layout: [global_epoch+completed_epoch | threads+retired | writer_lock]\n");
    printf("  3 cache lines isolated: %s\n",
           sizeof(skl::abix::rcu_domain) >= 192 ? "yes" : "no");
}