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
    while (!stop) {
        assert(write(pipe_fd_wakee[WRITE], &pipec, 1) == 1);
    }
}

static void *wakee_fn(void *arg) {
    while (!stop) {
        assert(read(pipe_fd_wakee[READ], &pipec, 1) == 1);
    }
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
    return 0;
}

