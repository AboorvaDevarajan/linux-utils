The CPU Idle State Wakeup Test is a simple program that helps CPU behaves when it wakes up from CPU Idle states.

This test spawns two threads, waker thread and the wakee thread:

Waker thread keeps waking up the wakee thread either through a timer or by sending messages through a pipe after
a specified interval.

The test measures how often the CPU transitions in and out of its idle states and how long it spends in these
states and more cpuidle stats.

### Modes

The test operates in two modes:

- Pipe-based wakeup: The waker thread triggers the wakee thread by writing to a pipe.
- Timer-based wakeup: The wakee thread is periodically woken up based on a configurable timer.

### Usage
```
Usage: [options]
-w <wakee_cpu>          CPU to run wakee thread on
-e <waker_cpu>          CPU to run waker thread on
-s <sleep_interval_us>  Sleep interval between wakeups in us (default: 100 us)
-d <test_duration>      Duration in seconds to run the test (default: 5)
-p                      Pipe-based wakeup
-t                      Timer-based wakeup (default)
-h                      Show this help message



[cpuidle]# ./cpuidle_wakeup  -w 110 -e 20 -s 50 -p
Total CPU Idle states: 2
CPU Wakee: 110, CPU Waker: 20
Test complete. Wakee wakeups: 96775, Total wake time: 478422400 ns
Wakee thread average wakeup latency  = 4.944 us
Wakee thread sleep interval  = 51.502 us
Idle state summary for CPU 110:
State 0 (snooze):
  Usage diff: 96767
  Time diff: 4474899 ns
  Above diff: 0 (0.00%)
  Below diff: 0 (0.00%)
State 1 (CEDE):
  Usage diff: 5
  Time diff: 203 ns
  Above diff: 5 (100.00%)
  Below diff: 0 (0.00%)

----- Overall Above/Below Summary -----
Total Above: 5 (0.01% of total usage)
Total Below: 0 (0.00% of total usage)

```
