import os
import csv
import numpy as np
from collections import defaultdict

idle_entry_times = {}
idle_stats = defaultdict(lambda: defaultdict(list))

def trace_begin():
    print("\nStarting CPU Idle analysis...\n")

def power__cpu_idle(event_name, context, common_cpu, common_secs, common_nsecs,
                    common_pid, common_comm, common_callchain, state, cpu_id):
    timestamp = common_secs + common_nsecs / 1e9

    if state != 4294967295:
        # Entering idle
        idle_entry_times[cpu_id] = (timestamp, state)
    else:
        # Exiting idle
        if cpu_id in idle_entry_times:
            entry_time, state_entered = idle_entry_times.pop(cpu_id)
            duration = timestamp - entry_time
            idle_stats[cpu_id][state_entered].append(duration)

def fmt_us(val):
    return f"{val * 1e6:.2f} us"

percentile_levels = [1, 10, 50, 75, 90, 95, 99, 99.99, 100]

def trace_end():
    dump_raw = os.getenv("PERF_CPUIDLE_DUMP_RAW") == "1"
    dump_csv = os.getenv("PERF_CPUIDLE_DUMP_CSV") == "1"

    print("\nCPU Idle State Statistics (perf script)\n")

    if dump_csv:
        csv_file = open("cpuidle_residencies.csv", "w", newline="")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["cpu_id", "state", "residency_us"])

    for cpu_id in sorted(idle_stats.keys()):
        print(f"CPU {cpu_id}:")
        for state in sorted(idle_stats[cpu_id].keys()):
            durations = idle_stats[cpu_id][state]
            durations_array = np.array(durations)
            count = len(durations)
            total_time = np.sum(durations_array)
            avg_time = np.mean(durations_array)
            min_time = np.min(durations_array)
            max_time = np.max(durations_array)

            print(f"  State C{state}:")
            print(f"    Entries     : {count}")
            print(f"    Total Time  : {fmt_us(total_time)}")
            print(f"    Avg Duration: {fmt_us(avg_time)}")
            print(f"    Min Duration: {fmt_us(min_time)}")
            print(f"    Max Duration: {fmt_us(max_time)}")
            print(f"    Percentiles :")
            for p in percentile_levels:
                val = np.percentile(durations_array, p)
                print(f"      {p:>6.2f}% : {fmt_us(val)}")

            if dump_raw:
                print(f"    Raw Durations:")
                for i, d in enumerate(durations, 1):
                    print(f"      {i:>4}: {fmt_us(d)}")

            if dump_csv:
                for d in durations:
                    csv_writer.writerow([cpu_id, state, round(d * 1e6, 2)])

        print()

    if dump_csv:
        csv_file.close()
        print("CSV raw residency data saved to: cpuidle_residencies.csv")

