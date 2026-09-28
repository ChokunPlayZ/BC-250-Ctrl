#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_zigbee.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "zigbee_runtime.h"

struct mock_semaphore { pthread_mutex_t mutex; };
struct mock_event_group { pthread_mutex_t mutex; pthread_cond_t changed; EventBits_t bits; };
static pthread_mutex_t radio_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t radio_changed = PTHREAD_COND_INITIALIZER;
static bool initialized, stopping, hold_setup, setup_entered, fail_queue, fail_stop, hold_callback;
static bool fail_create;
static void (*queued_cb)(void *);
static void *queued_arg;
static atomic_uint setups, teardowns, workers;
static atomic_bool pause_finished;

static struct timespec deadline(TickType_t timeout)
{
    struct timespec result;
    timespec_get(&result, TIME_UTC);
    /* Bound fault-path tests without changing the production timeout. */
    uint32_t ms = timeout > 200 ? 200 : timeout;
    result.tv_nsec += (long)ms * 1000000;
    if (result.tv_nsec >= 1000000000) { ++result.tv_sec; result.tv_nsec -= 1000000000; }
    return result;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t semaphore = calloc(1, sizeof(*semaphore));
    assert(semaphore && pthread_mutex_init(&semaphore->mutex, NULL) == 0);
    return semaphore;
}
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    (void)timeout;
    return pthread_mutex_lock(&semaphore->mutex) == 0;
}
void xSemaphoreGive(SemaphoreHandle_t semaphore) { assert(pthread_mutex_unlock(&semaphore->mutex) == 0); }
void vSemaphoreDelete(SemaphoreHandle_t semaphore) { pthread_mutex_destroy(&semaphore->mutex); free(semaphore); }

EventGroupHandle_t xEventGroupCreate(void)
{
    EventGroupHandle_t group = calloc(1, sizeof(*group));
    assert(group && pthread_mutex_init(&group->mutex, NULL) == 0);
    assert(pthread_cond_init(&group->changed, NULL) == 0);
    return group;
}
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits)
{
    pthread_mutex_lock(&group->mutex);
    group->bits |= bits;
    EventBits_t result = group->bits;
    pthread_cond_broadcast(&group->changed);
    pthread_mutex_unlock(&group->mutex);
    return result;
}
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits)
{
    pthread_mutex_lock(&group->mutex);
    EventBits_t result = group->bits;
    group->bits &= ~bits;
    pthread_mutex_unlock(&group->mutex);
    return result;
}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits, int clear, int all, TickType_t timeout)
{
    struct timespec until = deadline(timeout);
    pthread_mutex_lock(&group->mutex);
    while (all ? (group->bits & bits) != bits : !(group->bits & bits)) {
        int err = timeout == portMAX_DELAY ? pthread_cond_wait(&group->changed, &group->mutex) :
                  pthread_cond_timedwait(&group->changed, &group->mutex, &until);
        if (err == ETIMEDOUT) break;
        assert(err == 0);
    }
    EventBits_t result = group->bits;
    if (clear && (all ? (result & bits) == bits : (result & bits) != 0)) group->bits &= ~bits;
    pthread_mutex_unlock(&group->mutex);
    return result;
}
void vEventGroupDelete(EventGroupHandle_t group)
{
    pthread_cond_destroy(&group->changed); pthread_mutex_destroy(&group->mutex); free(group);
}

typedef struct { TaskFunction_t function; void *arg; } task_start_t;
static void *task_entry(void *arg)
{
    task_start_t start = *(task_start_t *)arg;
    free(arg);
    start.function(start.arg);
    return NULL;
}
int xTaskCreate(TaskFunction_t fn, const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{
    (void)name; (void)stack; (void)priority; (void)handle;
    if (fail_create) { fail_create = false; return 0; }
    task_start_t *start = malloc(sizeof(*start));
    assert(start);
    *start = (task_start_t){fn, arg};
    pthread_t thread;
    assert(pthread_create(&thread, NULL, task_entry, start) == 0);
    pthread_detach(thread);
    ++workers;
    return pdPASS;
}

static void setup(void)
{
    pthread_mutex_lock(&radio_lock);
    assert(!initialized);
    setup_entered = true;
    pthread_cond_broadcast(&radio_changed);
    while (hold_setup) pthread_cond_wait(&radio_changed, &radio_lock);
    initialized = true;
    stopping = false;
    ++setups;
    pthread_cond_broadcast(&radio_changed);
    pthread_mutex_unlock(&radio_lock);
}
static void teardown(void)
{
    pthread_mutex_lock(&radio_lock);
    assert(initialized && stopping);
    initialized = false;
    ++teardowns;
    pthread_cond_broadcast(&radio_changed);
    pthread_mutex_unlock(&radio_lock);
}
esp_err_t esp_zigbee_launch_mainloop(void)
{
    pthread_mutex_lock(&radio_lock);
    while (!stopping) {
        if (queued_cb && !hold_callback) {
            void (*callback)(void *) = queued_cb;
            void *arg = queued_arg;
            queued_cb = NULL;
            pthread_mutex_unlock(&radio_lock);
            callback(arg);
            pthread_mutex_lock(&radio_lock);
        } else pthread_cond_wait(&radio_changed, &radio_lock);
    }
    pthread_mutex_unlock(&radio_lock);
    return ESP_OK;
}
esp_err_t esp_zigbee_stop(void)
{
    pthread_mutex_lock(&radio_lock);
    assert(initialized);
    bool failure = fail_stop;
    fail_stop = false;
    if (!failure) stopping = true;
    pthread_mutex_unlock(&radio_lock);
    return failure ? ESP_ERR_INVALID_STATE : ESP_OK;
}
esp_err_t esp_zigbee_task_queue_post(void (*cb)(void *), void *arg)
{
    pthread_mutex_lock(&radio_lock);
    assert(initialized);
    bool failure = fail_queue;
    fail_queue = false;
    if (!failure) { assert(!queued_cb); queued_cb = cb; queued_arg = arg; }
    pthread_cond_broadcast(&radio_changed);
    pthread_mutex_unlock(&radio_lock);
    return failure ? ESP_ERR_INVALID_STATE : ESP_OK;
}
static void wait_setup(unsigned count)
{
    struct timespec until = deadline(200);
    pthread_mutex_lock(&radio_lock);
    while (setups < count) assert(pthread_cond_timedwait(&radio_changed, &radio_lock, &until) == 0);
    assert(initialized);
    pthread_mutex_unlock(&radio_lock);
}
static void *pause_entry(void *arg)
{
    (void)arg;
    assert(bc250_zigbee_runtime_set_paused(true) == ESP_OK);
    pause_finished = true;
    return NULL;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(bc250_zigbee_runtime_start(NULL, teardown) == ESP_ERR_INVALID_ARG);
    bool deferred = !strcmp(argv[1], "deferred");
    hold_setup = !strcmp(argv[1], "initializing");
    if (deferred) assert(bc250_zigbee_runtime_set_paused(true) == ESP_OK);
    if (!strcmp(argv[1], "task_failure")) {
        fail_create = true;
        assert(bc250_zigbee_runtime_start(setup, teardown) == ESP_ERR_NO_MEM);
    }
    assert(bc250_zigbee_runtime_start(setup, teardown) == ESP_OK);
    assert(bc250_zigbee_runtime_start(setup, teardown) == ESP_ERR_INVALID_STATE);
    if (deferred) {
        assert(bc250_zigbee_runtime_set_paused(true) == ESP_OK);
        assert(setups == 0 && teardowns == 0);
        assert(bc250_zigbee_runtime_set_paused(false) == ESP_OK);
    }
    if (hold_setup) {
        pthread_mutex_lock(&radio_lock);
        while (!setup_entered) pthread_cond_wait(&radio_changed, &radio_lock);
        pthread_mutex_unlock(&radio_lock);
        pthread_t pauser;
        assert(pthread_create(&pauser, NULL, pause_entry, NULL) == 0);
        struct timespec delay = {.tv_nsec = 20000000};
        nanosleep(&delay, NULL);
        assert(!pause_finished && teardowns == 0);
        pthread_mutex_lock(&radio_lock);
        hold_setup = false;
        pthread_cond_broadcast(&radio_changed);
        pthread_mutex_unlock(&radio_lock);
        pthread_join(pauser, NULL);
    } else {
        wait_setup(1);
        if (!strcmp(argv[1], "stop_failure")) {
            fail_queue = true;
            assert(bc250_zigbee_runtime_set_paused(true) == ESP_ERR_INVALID_STATE);
            assert(teardowns == 0);
            fail_stop = true;
            assert(bc250_zigbee_runtime_set_paused(true) == ESP_ERR_INVALID_STATE);
            assert(teardowns == 0);
        }
        if (!strcmp(argv[1], "timeout")) {
            hold_callback = true;
            assert(bc250_zigbee_runtime_set_paused(true) == ESP_ERR_TIMEOUT);
            pthread_mutex_lock(&radio_lock);
            hold_callback = false;
            pthread_cond_broadcast(&radio_changed);
            pthread_mutex_unlock(&radio_lock);
            wait_setup(2); /* A late stop must automatically recover from the failed pause. */
        }
        assert(bc250_zigbee_runtime_set_paused(true) == ESP_OK);
    }
    assert(setups == teardowns && workers == 1);
    unsigned previous = setups;
    assert(bc250_zigbee_runtime_set_paused(true) == ESP_OK);
    assert(setups == previous);
    assert(bc250_zigbee_runtime_set_paused(false) == ESP_OK);
    assert(bc250_zigbee_runtime_set_paused(false) == ESP_OK);
    wait_setup(previous + 1);
    assert(bc250_zigbee_runtime_set_paused(true) == ESP_OK);
    assert(setups == teardowns && workers == 1);
    puts("Zigbee AP suspension, startup races, resume and fault recovery passed");
    return 0;
}
