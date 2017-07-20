/*
 * Copyright (c) 2016, Technische Universität Dresden, Germany
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions
 *    and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions
 *    and the following disclaimer in the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>

#include <linux/unistd.h>
#include <sys/syscall.h>

#include <papi.h>

#define NUM_EVENTS 16

#if !defined(BACKEND_SCOREP) && !defined(BACKEND_VTRACE)
#define BACKEND_VTRACE
#endif

#if defined(BACKEND_SCOREP) && defined(BACKEND_VTRACE)
#error "Cannot compile for both VT and Score-P at the same time!\n"
#endif


#ifdef BACKEND_SCOREP
#include <scorep/SCOREP_MetricPlugins.h>
#endif
#ifdef BACKEND_VTRACE
#include <vampirtrace/vt_plugin_cntr.h>
#endif



#ifdef BACKEND_SCOREP
    typedef SCOREP_Metric_Plugin_MetricProperties metric_properties_t;
    typedef SCOREP_MetricTimeValuePair timevalue_t;
    typedef SCOREP_Metric_Plugin_Info plugin_info_type;
#endif

#ifdef BACKEND_VTRACE
    typedef vt_plugin_cntr_metric_info metric_properties_t;
    typedef vt_plugin_cntr_timevalue timevalue_t;
    typedef vt_plugin_cntr_info plugin_info_type;
#endif


struct vt_stuff {
    int32_t data_count;
    timevalue_t *result_vector;
};

static int EventCodes[NUM_EVENTS];

struct event {
    int enabled;
    unsigned long cpu;
    int EventSet;
    int num_cntrs;
    pthread_t thread;
    //struct vt_stuff res[NUM_EVENTS];
    long long *timevalues;
    int32_t  sample_count;
}__attribute__((aligned(64)));

struct event event_list[512];
static int event_list_size=0;

static int counter_enabled = 1;
static pthread_mutex_t add_counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static int global_num_cntrs = 0;

static uint64_t (*wtime)(void) = NULL;

#define DEFAULT_BUF_SIZE (size_t)(4*1024*1024)
static size_t buf_size = DEFAULT_BUF_SIZE; // 4MB per Event per Thread
//static int max_data_count;
static int interval_us = 100000; //100ms


static
size_t parse_buffer_size(const char *s)
{
    char *tmp = NULL;
    size_t size;

    // parse number part
    size = strtoll(s, &tmp, 10);

    if (size == 0)
    {
        fprintf(stderr, "Failed to parse buffer size ('%s'), using default %zu\n", s, DEFAULT_BUF_SIZE);
        return DEFAULT_BUF_SIZE;
    }

    // skip whitespace characters
    while(*tmp == ' ') tmp++;

    switch(*tmp)
    {
        case 'G':
            size *= 1024;
            // fall through
        case 'M':
            size *= 1024;
            // fall through
        case 'K':
            size *= 1024;
            // fall through
        default:
            break;
    }

    return size;

}

void set_pform_wtime_function(uint64_t(*pform_wtime)(void))
{
    wtime = pform_wtime;
}

// returns a timestamp in Nanoseconds
static inline
uint64_t gettime()
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);

    return (ts.tv_sec * 1E9 + ts.tv_nsec);
}

int32_t init(void)
{
    char * env_string;
    int ret;

#if defined(HAVE_DEBUG)
     fprintf(stderr, "APAPI: init called\n");
#endif

#if defined(BACKEND_SCOREP)
    env_string = getenv("SCOREP_METRIC_APAPI_INTERVAL_US");
#elif defined(BACKEND_VTRACE)
    env_string = getenv("VT_APAPI_INTERVAL_US");
#endif
    if (env_string == NULL)
        interval_us = 100000;
    else {
        interval_us = atoi(env_string);
        if (interval_us == 0) {
            fprintf(stderr,
                    "Could not parse VT_APAPI_INTERVAL_US, using 100 ms\n");
            interval_us = 100000;
        }
    }
#if defined(BACKEND_SCOREP)
    env_string = getenv("SCOREP_METRIC_APAPI_BUF_SIZE");
#elif defined(BACKEND_VTRACE)
    env_string = getenv("VT_APAPI_BUF_SIZE");
#endif
    if (env_string != NULL) {
        buf_size = parse_buffer_size(env_string);
          if (buf_size < 1024)
          {
                fprintf(stderr, "Given buffer size (%zu) too small, falling back to default (%zu)\n", buf_size, DEFAULT_BUF_SIZE);
                buf_size = DEFAULT_BUF_SIZE;
          }
    }
    //max_data_count = (buf_size * 1024 * 1024) /  sizeof(vt_plugin_cntr_timevalue);
    //fprintf(stderr, "VT_APAPI_BUF_SIZE: using %dMB (%d Elements) per Event per Thread\n", buf_size, max_data_count);

    ret = PAPI_library_init(PAPI_VER_CURRENT);
    if (ret != PAPI_VER_CURRENT) {
        if (ret > 0)
            fprintf(stderr, "cannot initialize library: PAPI library version does not match the version this plugin was compiled with!\n");
        else
            fprintf(stderr, "cannot initialize library: %s\n", PAPI_strerror(ret));
        return -1;
    }

    ret = PAPI_thread_init( pthread_self );
    if (ret != PAPI_OK) {
        fprintf(stderr, "cannot initialize thread support: %s\n", PAPI_strerror(ret));
        return -1;
    }

    return 0;
}

metric_properties_t * get_event_info(char * event_name)
{
    /* Prepend APAPI_ to create a meaningful counter name */
    int ret;

    #define STR_SIZE 128

    char apapi_name[STR_SIZE];
    memset(apapi_name, 0, STR_SIZE);
    strcpy(apapi_name, "A");
    strncat(apapi_name, event_name, STR_SIZE - 1 - strlen(apapi_name));

    /* parse the event name and put the event code into a global variable */
    if ((ret = PAPI_event_name_to_code(event_name, &EventCodes[global_num_cntrs])) != PAPI_OK)
    {
        fprintf(stderr, "APAPI: Failed to encode event %s: %s\n", event_name, PAPI_strerror(ret));
        return NULL;
    }

    /* check if the counter is available on this architecture */
    if ((ret = PAPI_query_event(EventCodes[global_num_cntrs])) != PAPI_OK) {
        fprintf(stderr, "APAPI: event %s is not avaible on this architecture\n", event_name);
        return NULL;
    }

    global_num_cntrs++;

    metric_properties_t *return_values = malloc(2 * sizeof(metric_properties_t));

    if (return_values == NULL)
    {
      fprintf(stderr, "APAPI: Failed to allocate memory for information data structure\n");
      return NULL;
    }

    /* if the description is null it should be considered the end */
    return_values[0].name = strdup(apapi_name);
    return_values[0].unit = NULL;
#ifdef BACKEND_SCOREP
    return_values[ 0 ].description = NULL;
    return_values[ 0 ].mode        = SCOREP_METRIC_MODE_ACCUMULATED_LAST;
    return_values[ 0 ].mode        = SCOREP_METRIC_MODE_ACCUMULATED_START;
    return_values[ 0 ].value_type  = SCOREP_METRIC_VALUE_UINT64;
    return_values[ 0 ].base        = SCOREP_METRIC_BASE_DECIMAL;
    return_values[ 0 ].exponent    = 0;
#endif
#ifdef BACKEND_VTRACE
    return_values[0].cntr_property = VT_PLUGIN_CNTR_ACC
        | VT_PLUGIN_CNTR_UNSIGNED | VT_PLUGIN_CNTR_LAST;
#endif
    /* Last element empty */
    return_values[1].name = NULL;

    return return_values;
}

void fini(void)
{
#if defined(HAVE_DEBUG)
    fprintf(stderr, "finishing done\n");
#endif
}


void * thread_report(void * _id)
{
    int id = (intptr_t) _id;
    //long long int values[NUM_EVENTS];
    //memset(values, 0, sizeof(long long int));

#if defined(HAVE_DEBUG)
     uint64_t start_time, end_time;
     uint64_t timesum = 0;
#endif

     struct event *ev = &event_list[id];

     size_t num_buf_elems = buf_size/sizeof(long long);

     ev->timevalues = calloc(num_buf_elems, sizeof(long long));
     size_t tv_pos = 0;

     ev->sample_count = 0;

    while (counter_enabled) {
        if (wtime == NULL)
            return NULL;
        if (ev->enabled) {
                //int i;
                int ret;
                uint64_t timestamp;

                if ((tv_pos + ev->num_cntrs + 1) > num_buf_elems)
                {
                    static int once = 0;
                    if (!once)
                    {
                        fprintf(stderr, "Buffer reached maximum %zuB. Loosing events.\n", (buf_size));
                        fprintf(stderr, "Set VT_APAPI_BUF_SIZE environment variable to increase buffer size\n");
                        once = 1;
                    }
                    break; // continue would lead to a busy wait
                }

            /* measure time and read value */
            timestamp = wtime();
                ev->timevalues[tv_pos] = timestamp;
                tv_pos++;
#if defined(HAVE_DEBUG)
                start_time = gettime();
#endif
            /* read papi counters */
            //ret = PAPI_accum(ev->EventSet, values);
            ret = PAPI_read(ev->EventSet, &ev->timevalues[tv_pos]);
            if (ret != PAPI_OK) {
                fprintf(stderr, "failed to accum counters for id %d\n", id);
                     return NULL;
            }
            //ret = PAPI_reset(ev->EventSet);
                //memcpy(&ev->timevalues[tv_pos], values, ev->num_cntrs * sizeof(long long));
                //tv_cur += ev->num_cntrs;
                tv_pos += ev->num_cntrs;
                ev->sample_count++;
#if 0
            /* write the counter value with timestamp to vampirtrace result vector */
            for (i=0;i<ev->num_cntrs;i++) {
                     struct vt_stuff *res = &(ev->res[i]);
                int dcount = res->data_count;
                     if (dcount == max_data_count) {
                        static int once = 0;
                        if (!once) {
                        fprintf(stderr, "Buffer reached maximum %d. Loosing events.\n", max_data_count);
                        fprintf(stderr, "Set VT_APAPI_BUF_SIZE environment variable to increase buffer size\n");
                            once = 1;
                        }
                  continue;
                }

                res->result_vector[dcount].timestamp = timestamp;
                res->result_vector[dcount].value = values[i];
                res->data_count++;
            }
#endif

#if defined(HAVE_DEBUG)
                end_time = gettime();
                timesum += end_time - start_time;
#endif

        }

        usleep(interval_us);
    }

    int ret = PAPI_stop(event_list[id].EventSet, NULL);
    if (ret != PAPI_OK) {
        fprintf(stderr, "failed to stop counters for id %d\n", id);
    }
#if 0
     uint64_t num_samples = 0;
     int i;
     for (i=0;i<event_list[id].num_cntrs;i++) {
        num_samples += event_list[id].res[i].data_count;
     }
#endif
#if defined(HAVE_DEBUG)
     printf("INFO: Average time per sample query: %llu\n", timesum / ev->sample_count++);
#endif
    return NULL;
}

int32_t add_counter(char * event_name)
{
    int i, id = -1;
    int is_thread_sampled = 0;
    int counter_id;
    int ret;
    unsigned long cpu;
    pthread_mutex_lock(&add_counter_mutex);

    /* use the thread id to identify the thread */
    cpu = syscall(__NR_gettid);
    for (i=0;i<event_list_size;i++) {
        if (event_list[i].cpu == cpu) {
            is_thread_sampled = 1;
            id = i;
                break;
        }
    }

    /* thread is not sampled yet. intialize stuff */
    if (!is_thread_sampled || id == -1) {
        id = event_list_size;
        memset(&event_list[i], 0, sizeof(struct event));
        event_list[id].EventSet = PAPI_NULL;
        event_list_size++;
        event_list[id].cpu = cpu;
        event_list[id].enabled = 1;
        event_list[id].num_cntrs = 0;
    }

    counter_id = (id << 8) + event_list[id].num_cntrs;

     //event_list[id].res[counter_id].result_vector = malloc(max_data_count * sizeof(vt_plugin_cntr_timevalue));
     //event_list[id].res[counter_id].data_count    = 0;

    event_list[id].num_cntrs++;

     /* Unlock the mutex before any return can happen */
     pthread_mutex_unlock(&add_counter_mutex);

    /* register PAPI event set when all counters have been added
     * and start the corresponding sampling thread */
    if (event_list[id].num_cntrs == global_num_cntrs) {
        if ((ret = PAPI_create_eventset(&event_list[id].EventSet)) != PAPI_OK) {
            fprintf(stderr, "failed to create EventSet for id %d: %s\n", id, PAPI_strerror(ret));
            return -1;
        }

        if ((ret = PAPI_add_events(event_list[id].EventSet, EventCodes, event_list[id].num_cntrs)) != PAPI_OK) {
            fprintf(stderr, "failed to add %i events for id %d: %s\n", global_num_cntrs, id, PAPI_strerror(ret));
            return -1;
        }

        if ((ret = PAPI_start(event_list[id].EventSet)) != PAPI_OK) {
            fprintf(stderr, "failed to start counters for id %d: %s\n", id, PAPI_strerror(ret));
            return -1;
        }

        if ((ret = pthread_create(&event_list[id].thread, NULL, &thread_report, (void *)(intptr_t) id) != 0))
        {
            fprintf(stderr, "failed to create sampling thread: %s\n", strerror(ret));
            return -1;
        }
    }

    return counter_id;
}

uint64_t get_all_values(int32_t id, timevalue_t **result)
{
        int i;
    int evt_id = id >> 8;
    int cntr_id = id & 0xff;

     if (counter_enabled)
     {
        counter_enabled = 0;

        for (i=0;i<event_list_size;i++) {
                pthread_join(event_list[i].thread, NULL);
            }
     }

    timevalue_t *res = malloc(event_list[evt_id].sample_count * sizeof(timevalue_t));
  if (res == NULL)
  {
    fprintf(stderr, "APAPI: Failed to allocate memory for results\n");
    return 0;
  }
    long long *timevalues = event_list[evt_id].timevalues;
    size_t tv_pos = 0;
    for (i = 0; i < event_list[evt_id].sample_count; i++)
    {
        res[i].timestamp = timevalues[tv_pos];
        tv_pos++;
        res[i].value = timevalues[tv_pos + cntr_id];
        tv_pos += event_list[evt_id].num_cntrs;
    }
    *result = res;

    return event_list[evt_id].sample_count;
}

int enable_counter(int ID)
{
    event_list[ID].enabled = 1;
    return 0;
}

int disable_counter(int ID)
{
    event_list[ID].enabled = 0;
    return 0;
}

#ifdef BACKEND_SCOREP
SCOREP_METRIC_PLUGIN_ENTRY( apapi_plugin )
#endif
#ifdef BACKEND_VTRACE
vt_plugin_cntr_info get_info()
#endif
{
    plugin_info_type info;
    memset(&info,0,sizeof(plugin_info_type));
#ifdef BACKEND_SCOREP
    info.plugin_version               = SCOREP_METRIC_PLUGIN_VERSION;
    info.run_per                      = SCOREP_METRIC_PER_THREAD;
    info.sync                         = SCOREP_METRIC_ASYNC;
    info.delta_t                      = UINT64_MAX;
    info.initialize                   = init;
    info.set_clock_function           = set_pform_wtime_function;
#endif

#ifdef BACKEND_VTRACE
    info.init                     = init;
    info.vt_plugin_cntr_version   = VT_PLUGIN_CNTR_VERSION;
    info.run_per                  = VT_PLUGIN_CNTR_PER_THREAD;
    info.synch                    = VT_PLUGIN_CNTR_ASYNCH_POST_MORTEM;
    info.set_pform_wtime_function = set_pform_wtime_function;
#endif
    info.add_counter              = add_counter;
    info.get_event_info           = get_event_info;
    info.get_all_values           = get_all_values;
    info.finalize                 = fini;
    return info;
}
