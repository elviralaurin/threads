#include <stdio.h>
#include "green.h"

int flag = 0;
green_cond_t cond;
green_mutex_t mut;
int global_count = 0;

void *test(void *arg) {
        int i = *(int *) arg;
        int loop = 4;
        while (loop > 0) {
                printf("Thread %d: %d\n", i, loop);
                loop--;
                green_yield();
        }
}

void *test_cond(void *arg) {
        int id = *(int *) arg;
        int loop = 4;

        while (loop > 0) {
                if (flag == id) {
                        printf("Thread %d: %d\n", id, loop);
                        loop--;
                        flag = (id + 1) % 2;
                        green_cond_signal(&cond);
                } else {
                        green_cond_wait(&cond);
                }
        }
}

void *test_timer(void *arg) {
        int id = *(int *) arg;
        int loop = 4;

        while (loop > 0) {
                if (id == flag) {
                        printf("Thread %d: %d\n", id, loop);
                        loop--;
                        flag = (id + 1) % 2;
                }
        }
}

void *test_timer_broken(void *arg) {
        for (int i = 0; i < 100000; i++) {
                int tmp = global_count; // read
                int delay = 0;
                while (delay < 1000) {
                        delay++;
                }
                tmp++;
                global_count = tmp;     //write

        }
}

void *test_mutex(void *arg) {
        for (int i = 0; i < 100000; i++) {
                green_mutex_lock(&mut);

                int tmp = global_count; // read
                int delay = 0;
                while (delay < 1000) {
                        delay++;
                }
                tmp++;
                global_count = tmp;     //write

                green_mutex_unlock(&mut);
        }
}

int main() {
        green_t g0, g1;

        int a0 = 0;
        int a1 = 1;

        green_cond_init(&cond);
        green_mutex_init(&mut);

        green_create(&g0, test_cond, &a0);
        green_create(&g1, test_cond, &a1);

        green_join(&g0, NULL);
        green_join(&g1, NULL);

        printf("Done! Count: %d\n", global_count);

        return 0;
}
