## Script to measue CPUIdle stats using perf events..

### 1. Record Idle Events

```
perf record -e power:cpu_idle -a sleep 1
```


### 2. Run perf script
```
perf script -s cpuidle_stats.py
```

### 3. To also dump the raw cpuidle stats
```
PERF_CPUIDLE_DUMP_RAW=1 perf script -s cpuidle_stats.py
```

### 4. Export to CSV
```
PERF_CPUIDLE_DUMP_CSV=1 perf script -s cpuidle_stats.py
```

