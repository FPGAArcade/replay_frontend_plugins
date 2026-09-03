// libco - Cooperative threading library (public domain)
// Minimal header for ScummVM integration

#ifndef LIBCO_H
#define LIBCO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* cothread_t;

// Get the currently active context
cothread_t co_active(void);

// Create a new coroutine with given stack size and entry function
cothread_t co_create(unsigned int stack_size, void (*entry)(void));

// Delete/free a coroutine
void co_delete(cothread_t cothread);

// Switch to a different coroutine
void co_switch(cothread_t cothread);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_H
