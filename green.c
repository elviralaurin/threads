#include <stdlib.h>
#include <stdio.h>
#include <ucontext.h>
#include <assert.h>
#include "green.h"
#include <signal.h>
#include <sys/time.h>

#define FALSE 0
#define TRUE 1

#define STACK_SIZE 4096

#define PERIOD 100

#define TIMERS_ENABLED 1

static sigset_t block;

static ucontext_t main_cntx = {0};
static green_t main_green = {&main_cntx, NULL, NULL, NULL, NULL, NULL, FALSE}; //cntx, fun, arg, next, join, retval, zombie

static green_t *running = &main_green;

void green_thread();
void timer_handler(int);

green_t *first;
green_t *last;

void print_status() {
        if (TIMERS_ENABLED) return;

        printf("### CURRENT STATUS ###\n");
        green_t *curr = first;
        int i = 0;
        while (curr != NULL) {
                printf("--- Thread %d ---\n", i);
                if (curr->arg) printf("curr->arg: %d\n", *((int *) curr->arg));
                printf("curr->next: %p\n", curr->next);
                printf("curr->join: %p\n", curr->join);
                if (curr->retval) printf("curr->retval: %d\n", *((int *)curr->retval));
                printf("curr->zombie: %d\n", curr->zombie);

                curr = curr->next;
        }
        printf("#### END STATUS ###\n");
}

void enqueue(green_t *insert) {
        // Insert the thread last in the queue

        if (last == NULL) {
                last = insert;
                first = insert;
        } else {
                last->next = insert;
                last = insert;
        }
}

green_t *dequeue() {
        // Remove the first node in the queue

        green_t *remove = first;
        first = first->next;
        remove->next = NULL;
        if (first == NULL) {
                last = NULL;
        }

        return remove;
}

static void init() __attribute__((constructor));

void init() {
        getcontext(&main_cntx);

        sigemptyset(&block);
        sigaddset(&block, SIGVTALRM);

        struct sigaction act = {0};
        struct timeval interval;
        struct itimerval period;

        act.sa_handler = timer_handler;
        assert(sigaction(SIGVTALRM, &act, NULL) == 0);

        interval.tv_sec = 0;
        interval.tv_usec = PERIOD;
        period.it_interval = interval;
        period.it_value = interval;
        setitimer(ITIMER_VIRTUAL, &period, NULL);
}

// takes an uninitiated green_t, and sets it up
int green_create(green_t *new, void *(*fun)(void*), void *arg) {
        ucontext_t *cntx = (ucontext_t *)malloc(sizeof(ucontext_t));
        getcontext(cntx);

        // the thread's stack will be allocated on the heap
        void *stack = malloc(STACK_SIZE);

        cntx->uc_stack.ss_sp = stack;
        cntx->uc_stack.ss_size = STACK_SIZE;
        makecontext(cntx, green_thread, 0);     // set the function that should be called when the thread is run

        new->context = cntx;
        new->fun = fun;
        new->arg = arg;
        new->next = NULL;
        new->join = NULL;
        new->retval = NULL;
        new->zombie = FALSE;

        //add new to ready queue
        enqueue(new);

        return 0;
}

// run the scheduled thread's function
void green_thread() {
        green_t *this = running;

        void *result = (*this->fun)(this->arg);

        // if there is a suspended thread waiting for this thread to
        // finish execution, place that waiting (joining) thread in ready queue
        if (this->join != NULL) {
                enqueue(this->join);
                // this->join = NULL
        }

        // save result of execution
        this->retval = result;

        // we're a zombie
        this->zombie = TRUE;

        //find the next thread to run
        green_t *next = dequeue();

        running = next;
        setcontext(next->context);
}

int green_yield() {
        // prevent timer interrupt
        sigprocmask(SIG_BLOCK, &block, NULL);

        green_t *susp = running;
        // add susp to (the end of) the ready queue
        enqueue(susp);

        // select next thread for execution
        green_t *next = dequeue();

        running = next;
        swapcontext(susp->context, next->context);      // the current state is saved in susp->context
        // when susp resumes execution, it will start from here!!!

        // reallow timer interrrupt
        sigprocmask(SIG_UNBLOCK, &block, NULL);
}

int green_join(green_t *thread, void** res) {
        // suspend the current thread and wait until the input thread terminates
        // NOTE: we only allow ONE waiting thread

        // prevent timer interrupt
        sigprocmask(SIG_BLOCK, &block, NULL);

        if (!thread->zombie) {
                // it's alive!
                green_t *susp = running;
                // add as joining
                thread->join = susp;

                // select the next thread for execution
                green_t *next = dequeue();
                running = next;
                swapcontext(susp->context, next->context);      // again, current state is saved in susp->context
                // when susp resumes execution, it will start from here!!!
        }

        // collect result
        res = thread->retval;
        // free context
        free(thread->context);
        // braaiiiiiins.......
        thread->zombie = TRUE;

        // reallow timer interrupts
        sigprocmask(SIG_UNBLOCK, &block, NULL);

        return 0;
}

// initialize the condition
void green_cond_init(green_cond_t *cond) {
        cond->first = NULL;
}

// suspend the current thread on the condition
void green_cond_wait(green_cond_t *cond) {
        // prevent timer interrupt
        sigprocmask(SIG_BLOCK, &block, NULL);

        green_t *susp = running;

        if (cond->first == NULL) {
                cond->first = susp;
        } else {
                // put susp at the end of the queue from cond
                green_t *curr = cond->first;
                while (curr->next != NULL) {
                        // step it forward until we reach the end of the list
                        curr = curr->next;
                }
                // when we get here, we are at the end of the list
                curr->next = susp;
        }

        green_t *next = dequeue();
        running = next;
        swapcontext(susp->context, next->context);

        // reallow timer interrupt
        sigprocmask(SIG_UNBLOCK, &block, NULL);
}

// move the first suspended thread to the ready queue
void green_cond_signal(green_cond_t *cond) {
        if (cond->first != NULL) {
                green_t *ready = cond->first;
                cond->first = cond->first->next;
                enqueue(ready);
        }
}

void timer_handler(int sig) {
        green_t *susp = running;

        // add the running thread to the ready queue
        enqueue(susp);

        // find the next thread to run
        green_t *next = dequeue();
        running = next;
        swapcontext(susp->context, next->context);
}

int green_mutex_init(green_mutex_t *mutex) {
        mutex->taken = FALSE;
        mutex->first = NULL;
}

int green_mutex_lock(green_mutex_t *mutex) {
        // block timer interrupt
        sigprocmask(SIG_BLOCK, &block, NULL);

        green_t *susp = running;
        if (mutex->taken) {
                // suspend the running thread
                if (mutex->first == NULL) {
                        mutex->first = susp;
                } else {
                        green_t *curr = mutex->first;   // there is a queue starting from mutex->first, so
                        while (curr->next != NULL) {    // step forward until the end
                                curr = curr->next;
                        }
                        // curr is now the last in the queue
                        curr->next = susp;
                }

                // find the next thread to run
                green_t *next = dequeue();
                running = next;
                swapcontext(susp->context, next->context);
        } else {
                // take the lock
                mutex->taken = TRUE;
        }

        // unblock
        sigprocmask(SIG_UNBLOCK, &block, NULL);
        return 0;
}

int green_mutex_unlock(green_mutex_t *mutex) {
        // block timer interrupt
        sigprocmask(SIG_BLOCK, &block, NULL);

        if (mutex->first != NULL) {
                // move suspended thread to ready queue
                enqueue(mutex->first);
                mutex->first = mutex->first->next;
        } else {
                // release lock
                mutex->taken = FALSE;
        }

        // unblock
        sigprocmask(SIG_UNBLOCK, &block, NULL);
        return 0;

}
