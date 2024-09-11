#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <dirent.h>

#define MAX_IDLE_STATES 10
#define DEFAULT_WAKEUP_INTERVAL_NS 100000ULL
#define DEFAULT_TEST_DURATION_SEC 5

typedef enum { PIPE_WAKEUP, TIMER_WAKEUP } wakeup_mode_t;

static wakeup_mode_t current_wakeup_mode = TIMER_WAKEUP;

int wakee_cpu_id = 0;
int waker_cpu_id = 0;
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

// Main function
int main(int argc, char *argv[]) {
    int opt;

    // Argument parsing
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

    return 0;
}

