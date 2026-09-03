/*
  libco - Cooperative threading library
  Combined implementation for x86_64 and aarch64
  license: public domain
*/

#define LIBCO_C
#include "libco.h"
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__aarch64__)
// ============================================================================
// ARM64 / AArch64 implementation
// ============================================================================

#include <string.h>
#ifndef __APPLE__
#include <malloc.h>
#endif

static __thread uint64_t co_active_buffer[64];
static __thread cothread_t co_active_handle;

__asm__ (
    ".globl co_switch_aarch64\n"
    ".globl _co_switch_aarch64\n"
    "co_switch_aarch64:\n"
    "_co_switch_aarch64:\n"
    "  stp x8,  x9,  [x1]\n"
    "  stp x10, x11, [x1, #16]\n"
    "  stp x12, x13, [x1, #32]\n"
    "  stp x14, x15, [x1, #48]\n"
    "  str x19, [x1, #72]\n"
    "  stp x20, x21, [x1, #80]\n"
    "  stp x22, x23, [x1, #96]\n"
    "  stp x24, x25, [x1, #112]\n"
    "  stp x26, x27, [x1, #128]\n"
    "  stp x28, x29, [x1, #144]\n"
    "  mov x16, sp\n"
    "  stp x16, x30, [x1, #160]\n"
    "  ldp x8,  x9,  [x0]\n"
    "  ldp x10, x11, [x0, #16]\n"
    "  ldp x12, x13, [x0, #32]\n"
    "  ldp x14, x15, [x0, #48]\n"
    "  ldr x19, [x0, #72]\n"
    "  ldp x20, x21, [x0, #80]\n"
    "  ldp x22, x23, [x0, #96]\n"
    "  ldp x24, x25, [x0, #112]\n"
    "  ldp x26, x27, [x0, #128]\n"
    "  ldp x28, x29, [x0, #144]\n"
    "  ldp x16, x17, [x0, #160]\n"
    "  mov sp, x16\n"
    "  br x17\n"
);

void co_switch_aarch64(cothread_t handle, cothread_t current);

static void crash(void) {
    assert(0); // Called only if cothread_t entrypoint returns
}

cothread_t co_create(unsigned int size, void (*entrypoint)(void)) {
    uint64_t *ptr = NULL;
    cothread_t handle = 0;
    size = (size + 1023) & ~1023;

    handle = aligned_alloc(1024, size + 512);
    if (!handle) return handle;

    ptr = (uint64_t*)handle;
    for (int i = 0; i < 19; i++) ptr[i] = 0;
    ptr[20] = (uintptr_t)ptr + size + 512 - 16; // stack pointer
    ptr[19] = ptr[20]; // frame pointer
    ptr[21] = (uintptr_t)entrypoint; // PC
    return handle;
}

cothread_t co_active(void) {
    if (!co_active_handle)
        co_active_handle = co_active_buffer;
    return co_active_handle;
}

void co_delete(cothread_t handle) {
    free(handle);
}

void co_switch(cothread_t handle) {
    cothread_t co_previous_handle = co_active();
    co_switch_aarch64(co_active_handle = handle, co_previous_handle);
}

#elif defined(__x86_64__) || defined(__amd64__)
// ============================================================================
// x86_64 / AMD64 implementation using pthreads
// ============================================================================

#include <pthread.h>
#include <semaphore.h>
#include <string.h>

typedef struct co_context_s co_context;

struct co_context_s {
    pthread_t thread;
    sem_t sem_run;       // Signals: "you can run now"
    sem_t sem_yield;     // Signals: "I've yielded back to you"
    void (*entry)(void);
    int is_main;
    int started;
    co_context* main_ctx; // Pointer to main context (for non-main threads)
};

// Main context - only used by main thread
static co_context s_main_context;
static int s_main_initialized = 0;

// Currently active context (global, not thread-local for pthread impl)
static co_context* s_active_handle = NULL;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* co_thread_entry(void* arg) {
    co_context* ctx = (co_context*)arg;

    // Wait for first signal to start
    sem_wait(&ctx->sem_run);

    if (ctx->entry) {
        ctx->entry();
    }

    // Entry returned - signal completion and exit
    sem_post(&ctx->sem_yield);
    return NULL;
}

cothread_t co_active(void) {
    pthread_mutex_lock(&s_mutex);
    if (!s_main_initialized) {
        s_main_initialized = 1;
        s_main_context.is_main = 1;
        s_main_context.main_ctx = NULL;
        sem_init(&s_main_context.sem_run, 0, 0);
        sem_init(&s_main_context.sem_yield, 0, 0);
        s_active_handle = &s_main_context;
    }
    co_context* result = s_active_handle;
    pthread_mutex_unlock(&s_mutex);
    return (cothread_t)result;
}

cothread_t co_create(unsigned int size, void (*entrypoint)(void)) {
    (void)size;  // Stack size handled by pthread

    co_context* ctx = (co_context*)malloc(sizeof(co_context));
    if (!ctx) return NULL;

    memset(ctx, 0, sizeof(co_context));
    ctx->entry = entrypoint;
    ctx->is_main = 0;
    ctx->started = 0;

    sem_init(&ctx->sem_run, 0, 0);
    sem_init(&ctx->sem_yield, 0, 0);

    // Initialize main context if not done and store reference
    co_active();
    ctx->main_ctx = &s_main_context;

    return (cothread_t)ctx;
}

void co_delete(cothread_t handle) {
    co_context* ctx = (co_context*)handle;
    if (ctx && !ctx->is_main) {
        sem_destroy(&ctx->sem_run);
        sem_destroy(&ctx->sem_yield);
        free(ctx);
    }
}

void co_switch(cothread_t handle) {
    co_context* target = (co_context*)handle;

    pthread_mutex_lock(&s_mutex);
    co_context* current = s_active_handle;
    pthread_mutex_unlock(&s_mutex);

    if (current && current->is_main && !target->is_main) {
        // Switching from main to ScummVM thread
        if (!target->started) {
            // First switch - create the thread
            target->started = 1;
            pthread_create(&target->thread, NULL, co_thread_entry, target);
        }
        // Signal ScummVM to run
        pthread_mutex_lock(&s_mutex);
        s_active_handle = target;
        pthread_mutex_unlock(&s_mutex);

        sem_post(&target->sem_run);
        // Wait for it to yield back
        sem_wait(&target->sem_yield);

        pthread_mutex_lock(&s_mutex);
        s_active_handle = current;
        pthread_mutex_unlock(&s_mutex);
    } else if (current && !current->is_main && target->is_main) {
        // Switching from ScummVM back to main (yield)
        sem_post(&current->sem_yield);
        sem_wait(&current->sem_run);
    }
    // No-op for other cases (entry function returned, etc.)
}

#else
#error "libco: unsupported architecture"
#endif

#ifdef __cplusplus
}
#endif
