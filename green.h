#include <ucontext.h>

typedef struct green_t {
	ucontext_t *context;
	void *(*fun)(void*);
	void *arg;
	struct green_t *next;
	struct green_t *join;
	void *retval;
	int zombie;
} green_t;

typedef struct green_cond_t {
	green_t *first;
} green_cond_t;

typedef struct green_mutex_t {
	volatile int taken;
	green_t *first;
} green_mutex_t;

int green_create(green_t *thread, void *(*fun)(void*), void *arg);
int green_yield();
int green_join(green_t *thread, void** res);

void green_cond_init(green_cond_t *cond);
void green_cond_wait(green_cond_t *cond);
void green_cond_signal(green_cond_t *cond);

int green_mutex_init(green_mutex_t *mutex);
int green_mutex_lock(green_mutex_t *mutex);
int green_mutex_unlock(green_mutex_t *mutex);
