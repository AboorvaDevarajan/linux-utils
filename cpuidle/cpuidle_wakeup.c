#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <assert.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>

#define MAX_IDLE_STATES 10
#define DEFAULT_WAKEUP_INTERVAL_NS 100000ULL
#define DEFAULT_TEST_DURATION_SEC 5

#define READ 0
#define WRITE 1

char pipec;
static int pipe_fd_wakee[2];

unsigned int stop = 0;

int clockid = CLOCK_REALTIME;

typedef enum { PIPE_WAKEUP, TIMER_WAKEUP } wakeup_mode_t;
typedef enum { IDLE_STATE_NUMERIC, IDLE_STATE_STRING } idle_state_type_t;

static wakeup_mode_t current_wakeup_mode = TIMER_WAKEUP;

int wakee_cpu_id = 0, waker_cpu_id = 0;
static pthread_t wakee_tid, waker_tid;

int total_idle_states = 0;

unsigned long wakeup_interval_ns = DEFAULT_WAKEUP_INTERVAL_NS;
unsigned long test_duration_sec = DEFAULT_TEST_DURATION_SEC;

const char *cpuidle_path_template = "/sys/devices/system/cpu/cpu%d/cpuidle";

struct idle_state_data {
    unsigned long long usage;
    unsigned long long time;
    unsigned long long above;
    unsigned long long below;
};

struct idle_state {
    int cpu_id;
    int state_index;
    char state_name[50];

    struct idle_state_data before;
    struct idle_state_data after;
};

struct idle_state wakee_idle_states[MAX_IDLE_STATES];

struct wakeup_time {
    struct timespec begin;
    struct timespec end;
};

struct wakeup_time wakee_wakeup_time;
unsigned long long wakee_wakeup_time_total_ns;
unsigned long long wakee_sleep_time_total_ns;
unsigned long long wakee_wakeup_count;

static unsigned long long compute_timediff(struct timespec before,
					   struct timespec after)
{
	unsigned long long ret_ns;
	unsigned long long ns_per_sec = 1000UL*1000*1000;

	if (after.tv_sec == before.tv_sec) {
		ret_ns = after.tv_nsec - before.tv_nsec;

		return ret_ns;
	}

	if (after.tv_sec > before.tv_sec) {
		unsigned long long diff_ns = 0;

		diff_ns = (after.tv_sec - before.tv_sec) * ns_per_sec;
		ret_ns = diff_ns + after.tv_nsec - before.tv_nsec;
		return ret_ns;
	}

	return 0;
}

void get_cpu_idle_state(const char *field, int cpu, int state, void *value, idle_state_type_t type) {
    char filepath[256];
    FILE *file;
    char buffer[64];
    snprintf(filepath, sizeof(filepath),
             "/sys/devices/system/cpu/cpu%d/cpuidle/state%d/%s", cpu, state, field);
    file = fopen(filepath, "r");
    if (!file) {
        perror("Error opening file");
        if (type == IDLE_STATE_NUMERIC) {
            *(unsigned long long *)value = 0;
        } else {
            ((char *)value)[0] = '\0';
        }
        return;
    }
    if (fgets(buffer, sizeof(buffer), file)) {
        if (type == IDLE_STATE_NUMERIC) {
            *(unsigned long long *)value = strtoull(buffer, NULL, 10);
        } else if (type == IDLE_STATE_STRING) {
            strncpy((char *)value, buffer, 64);
            ((char *)value)[strcspn((char *)value, "\n")] = '\0';
        }
    } else {
        if (type == IDLE_STATE_NUMERIC) {
            *(unsigned long long *)value = 0;
        } else {
            ((char *)value)[0] = '\0';
        }
    }
    fclose(file);
}

unsigned int get_total_idle_states(void) {
    char path[100];
    int state_count = 0;
    DIR *dir;
    struct dirent *entry;

    snprintf(path, sizeof(path), cpuidle_path_template, 0);
    dir = opendir(path);
    if (!dir) return 0;

    while ((entry = readdir(dir))) {
        if (entry->d_name[0] != '.') {
            state_count++;
        }
    }
    closedir(dir);
    return state_count;
}

static void initialize_wakee_idle_states(void) {
    for (int i = 0; i < total_idle_states; i++) {
        wakee_idle_states[i].cpu_id = wakee_cpu_id;
        wakee_idle_states[i].state_index = i;
        get_cpu_idle_state("name", wakee_cpu_id, i, wakee_idle_states[i].state_name, IDLE_STATE_STRING);
    }
}

static void snapshot_idle_state(const char *field_name, struct idle_state *s, unsigned long long *field) {
    get_cpu_idle_state(field_name, s->cpu_id, s->state_index, field, IDLE_STATE_NUMERIC);
}

void snapshot_one_before(struct idle_state *s) {
    snapshot_idle_state("usage", s, &s->before.usage);
    snapshot_idle_state("above", s, &s->before.above);
    snapshot_idle_state("below", s, &s->before.below);
    snapshot_idle_state("time", s, &s->before.time);
}

void snapshot_one_after(struct idle_state *s) {
    snapshot_idle_state("usage", s, &s->after.usage);
    snapshot_idle_state("above", s, &s->after.above);
    snapshot_idle_state("below", s, &s->after.below);
    snapshot_idle_state("time", s, &s->after.time);
}

static void snapshot_all_before(void) {
    int i;
    for (i = 0; i < total_idle_states; i++) snapshot_one_before(&wakee_idle_states[i]);
}

static void snapshot_all_after(void) {
    int i;
    for (i = 0; i < total_idle_states; i++) snapshot_one_after(&wakee_idle_states[i]);
}


static cpu_set_t* initialize_cpuset(int num_cpus, int cpu_id) {
    cpu_set_t *cpuset = CPU_ALLOC(num_cpus);
    if (cpuset == NULL) {
        perror("Unable to allocate cpuset");
        exit(EXIT_FAILURE);
    }

    size_t size = CPU_ALLOC_SIZE(num_cpus);
    CPU_ZERO_S(size, cpuset);
    CPU_SET_S(cpu_id, size, cpuset);

    return cpuset;
}

static void set_thread_affinity(pthread_attr_t *attr, cpu_set_t *cpuset) {
    size_t size = CPU_ALLOC_SIZE(sysconf(_SC_NPROCESSORS_ONLN));
    if (pthread_attr_setaffinity_np(attr, size, cpuset)) {
        perror("Error setting thread affinity");
        exit(EXIT_FAILURE);
    }
}


static void *waker_fn(void *arg) {

    struct timespec begin, cur;
    unsigned long long time_diff_ns;


    if(current_wakeup_mode == PIPE_WAKEUP) {
        while (!stop) {

	        clock_gettime(clockid, &begin);
	        do {
		        clock_gettime(clockid, &cur);
		        time_diff_ns = compute_timediff(begin, cur);
	        } while (time_diff_ns <= wakeup_interval_ns);

            clock_gettime(clockid, &wakee_wakeup_time.begin);
            assert(write(pipe_fd_wakee[WRITE], &pipec, 1) == 1);
        }
    } else {
        // add a print
    }
}

static void *wakee_fn(void *arg) {

    snapshot_all_before();
    while (!stop) {
        unsigned long long wakeup_diff, sleep_diff;
        struct timespec sleep_begin, sleep_duration;
        sleep_duration.tv_sec = wakeup_interval_ns / 1000000000ULL;
        sleep_duration.tv_nsec = wakeup_interval_ns % 1000000000ULL;
        clock_gettime(clockid, &sleep_begin);


        if(current_wakeup_mode == PIPE_WAKEUP) {
            assert(read(pipe_fd_wakee[READ], &pipec, 1) == 1);
        } else {
            nanosleep(&sleep_duration, NULL);
        }

        clock_gettime(clockid, &wakee_wakeup_time.end);
	    wakeup_diff = compute_timediff(wakee_wakeup_time.begin,
				     wakee_wakeup_time.end);
        wakee_wakeup_time_total_ns += wakeup_diff;
	    sleep_diff = compute_timediff(sleep_begin, wakee_wakeup_time.end);
	    wakee_sleep_time_total_ns += sleep_diff;
        wakee_wakeup_count++;

    }
    snapshot_all_after();

}

static void create_thread(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *), const char *thread_name) {
    int ret = pthread_create(thread, attr, start_routine, NULL);
    if (ret) {
        fprintf(stderr, "Failed to create %s thread\n", thread_name);
        exit(EXIT_FAILURE);
    }
}

void initialize_threads(void) {
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);

    pthread_attr_t wakee_attr, waker_attr;
    pthread_attr_init(&wakee_attr);
    pthread_attr_init(&waker_attr);

    printf("CPU Wakee: %d, CPU Waker: %d\n", wakee_cpu_id, waker_cpu_id);

    cpu_set_t *cpuset_wakee = initialize_cpuset(num_cpus, wakee_cpu_id);
    cpu_set_t *cpuset_waker = initialize_cpuset(num_cpus, waker_cpu_id);

    set_thread_affinity(&wakee_attr, cpuset_wakee);
    create_thread(&wakee_tid, &wakee_attr, wakee_fn, "wakee");

    set_thread_affinity(&waker_attr, cpuset_waker);
    create_thread(&waker_tid, &waker_attr, waker_fn, "waker");

    CPU_FREE(cpuset_wakee);
    CPU_FREE(cpuset_waker);
}

void cleanup(void) {
    pthread_join(wakee_tid, NULL);
    pthread_join(waker_tid, NULL);
    close(pipe_fd_wakee[READ]);
    close(pipe_fd_wakee[WRITE]);
}

void create_pipe(void) {
    if (pipe(pipe_fd_wakee)) {
        printf("Failed to create pipe\n");
        exit(EXIT_FAILURE);
    }
}

void print_idle_state_summary(int cpu_wakee, int nr_idle_states, struct idle_state *wakee_idle_states) {
    printf("Idle state summary for CPU %d:\n", cpu_wakee);

    for (int i = 0; i < nr_idle_states; i++) {
        struct idle_state *state = &wakee_idle_states[i];
        printf("State %d (%s):\n", i, state->state_name);
        printf("  Usage diff: %llu\n",
               state->after.usage - state->before.usage);
        printf("  Time diff: %llu ns\n",
               state->after.time - state->before.time);
        printf("  Above diff: %llu\n",
               state->after.above - state->before.above);
        printf("  Below diff: %llu\n",
               state->after.below - state->before.below);
    }
}

void print_usage(void) {
    printf("Usage: [options]\n"
           "-w <wakee_cpu>          CPU to run wakee thread on\n"
           "-e <waker_cpu>          CPU to run waker thread on\n"
           "-s <sleep_interval_us>  Sleep interval between wakeups in us (default: 100 us)\n"
           "-d <test_duration>      Duration in seconds to run the test (default: 5)\n"
           "-p                      Pipe-based wakeup\n"
           "-t                      Timer-based wakeup (default)\n"
           "-h                      Show this help message\n");
}

int main(int argc, char *argv[]) {
    int opt;

    while ((opt = getopt(argc, argv, "w:e:s:d:pth")) != -1) {
        switch (opt) {
        case 'w':
            wakee_cpu_id = atoi(optarg);
            break;
        case 'e':
            waker_cpu_id = atoi(optarg);
            break;
        case 's':
            wakeup_interval_ns = strtoul(optarg, NULL, 10) * 1000;
            break;
        case 'd':
            test_duration_sec = strtoul(optarg, NULL, 10);
            break;
        case 'p':
            current_wakeup_mode = PIPE_WAKEUP;
            break;
        case 't':
            current_wakeup_mode = TIMER_WAKEUP;
            break;
        case 'h':
        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    if (wakee_cpu_id == 0 || waker_cpu_id == 0) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    total_idle_states = get_total_idle_states();
    printf("Total CPU Idle states: %d\n", total_idle_states);

    initialize_wakee_idle_states();
    create_pipe();    

    initialize_threads();
    
    sleep(test_duration_sec);
    stop = 1;

    cleanup();

    if(current_wakeup_mode == PIPE_WAKEUP) {
        printf("Test complete. Wakee wakeups: %llu, Total wake time: %llu ns\n",
               wakee_wakeup_count, wakee_wakeup_time_total_ns);
	    printf("Wakee thread average wakeup latency  = %4.3f us\n",
		        wakee_wakeup_count ? ((double)(wakee_wakeup_time_total_ns)/((wakee_wakeup_count)*1000)) : 0);
    }

	printf("Wakee thread sleep interval  = %4.3f us\n",
		    wakee_wakeup_count ? ((double)(wakee_sleep_time_total_ns)/((wakee_wakeup_count)*1000)) : 0);

    print_idle_state_summary(wakee_cpu_id, total_idle_states, wakee_idle_states);

    return 0;
}
