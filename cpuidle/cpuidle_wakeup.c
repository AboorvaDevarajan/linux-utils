#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

typedef enum { MODE_PIPE, MODE_TIMER } wakeup_mode_t;

static wakeup_mode_t wakeup_mode = MODE_TIMER; // Default mode

// Global configuration variables
int wakee_cpu = 0;
int waker_cpu = 0;
unsigned long wakeup_interval_ns = 100000ULL; // Default wakeup interval in ns
unsigned long test_duration_sec = 5; // Default test duration in seconds

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

    // Argument parsing
    while ((opt = getopt(argc, argv, "w:e:s:d:pth")) != -1) {
        switch (opt) {
        case 'w':
            wakee_cpu = atoi(optarg);
            break;
        case 'e':
            waker_cpu = atoi(optarg);
            break;
        case 's':
            wakeup_interval_ns = strtoul(optarg, NULL, 10) * 1000;
            break;
        case 'd':
            test_duration_sec = strtoul(optarg, NULL, 10);
            break;
        case 'p':
            wakeup_mode = MODE_PIPE;
            break;
        case 't':
            wakeup_mode = MODE_TIMER;
            break;
        case 'h':
        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    // Implement the rest of the program logic here

    return 0;
}

