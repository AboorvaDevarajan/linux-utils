#include <stdio.h>
#include <limits.h>
#include <stdint.h>

/*
* Standalone test to verify: predicted cpuidle duration using get_typical_interval: 
* https://github.com/torvalds/linux/blob/v6.14-rc4/drivers/cpuidle/governors/menu.c#L1174
* 
* This is to reveiw changes: get_typical_interval:
* https://lore.kernel.org/all/1916668.tdWV9SEqCh@rjwysocki.net/
*/

#define INTERVAL_SHIFT 3
#define INTERVALS (1UL << INTERVAL_SHIFT)

typedef uint64_t u64;
typedef unsigned int u32;
typedef long long s64;

#define U64_MAX         ((u64)~0ULL)

#define do_div(n, base) ({ \
    uint32_t __base = (base); \
    uint32_t __rem; \
    __rem = ((uint64_t)(n)) % __base; \
    (n) = ((uint64_t)(n)) / __base; \
    __rem; \
 })

struct menu_device {
    unsigned int intervals[INTERVALS];
};

/* This is copied from
 * https://github.com/torvalds/linux/blob/v6.14-rc4/drivers/cpuidle/governors/menu.c#L117
 */
static unsigned int get_typical_interval_before(struct menu_device *data) {
    int i, divisor;
    unsigned int min, max, thresh, avg;
    uint64_t sum, variance;

    thresh = INT_MAX;

again:
    min = UINT_MAX;
    max = 0;
    sum = 0;
    divisor = 0;

    for (i = 0; i < INTERVALS; i++) {
        unsigned int value = data->intervals[i];
        if (value <= thresh) {
            sum += value;
            divisor++;
            if (value > max) max = value;
            if (value < min) min = value;
        }
    }

    if (!max) return UINT_MAX;
    avg = divisor == INTERVALS ? sum >> INTERVAL_SHIFT : sum / divisor;

    variance = 0;
    for (i = 0; i < INTERVALS; i++) {
        unsigned int value = data->intervals[i];
        if (value <= thresh) {
            int64_t diff = (int64_t)value - avg;
            variance += diff * diff;
        }
    }
    variance = divisor == INTERVALS ? variance >> INTERVAL_SHIFT : variance / divisor;
    
    if (variance <= (U64_MAX / 36)) {
        if (((u64)avg * avg > variance * 36 && divisor * 4 >= INTERVALS * 3) || variance <= 400) {
            return avg;
        }
    }

    if (divisor * 4 <= INTERVALS * 3) return UINT_MAX;
    thresh = max - 1;

    goto again;
}

/*
 * This is copied from:
 * https://lore.kernel.org/all/1916668.tdWV9SEqCh@rjwysocki.net/
 */
static unsigned int get_typical_interval_after(struct menu_device *data) {
    s64 value, min_thresh = -1, max_thresh = UINT_MAX;
    unsigned int max, min, divisor;
    u64 avg, variance, avg_sq;
    int i;

again:
    max = 0;
    min = UINT_MAX;
    avg = 0;
    variance = 0;
    divisor = 0;

    for (i = 0; i < INTERVALS; i++) {
        value = data->intervals[i];
        if (value <= min_thresh || value >= max_thresh) continue;
        divisor++;
        avg += value;
        variance += value * value;
        if (value > max) max = value;
        if (value < min) min = value;
    }

    if (!max) return UINT_MAX;
    avg = divisor == INTERVALS ? avg >> INTERVAL_SHIFT : avg / divisor;
    variance = divisor == INTERVALS ? variance >> INTERVAL_SHIFT : variance / divisor;
    avg_sq = avg * avg;
    variance -= avg_sq;

    if (variance <= (U64_MAX / 36)) {
        if (avg_sq > variance * 36 && divisor * 4 >= INTERVALS * 3 || variance <= 400) {	
            return avg;
        }
    }

    if (divisor * 4 <= INTERVALS * 3) {
        if (divisor >= INTERVALS / 2) return max;
        return UINT_MAX;
    }

    if (avg - min > max - avg) min_thresh = min;
    else max_thresh = max;

    goto again;
}

int main() {
    struct menu_device test_cases[100] = {
        {{100, 105, 110, 115, 120, 125, 130, 135}}, 
        {{200, 205, 210, 215, 220, 225, 230, 235}}, 
        {{500, 505, 510, 515, 520, 525, 530, 535}}, 
        {{1000, 1005, 1010, 1015, 1020, 1025, 1030, 1035}}, 
        {{10, 15, 20, 25, 30, 35, 40, 45}}, 
        {{50, 55, 60, 65, 70, 75, 80, 85}}, 
        {{90, 95, 100, 105, 110, 115, 120, 125}}, 
        {{130, 135, 140, 145, 150, 155, 160, 165}}, 
        {{170, 175, 180, 185, 190, 195, 200, 205}}, 
        {{210, 215, 220, 225, 230, 235, 240, 245}}, 

        {{10, 500, 10000, 20000, 30000, 40000, 50000, 60000}}, 
        {{1, 100000, 500000, 1000000, 5000000, 10000000, 50000000, 100000000}}, 
        {{UINT_MAX, 1, 2, 3, 4, 5, 6, 7}}, 
        {{9999999, 8888888, 7777777, 6666666, 5555555, 4444444, 3333333, 2222222}}, 
        {{5, 10, 50, 100, 500, 1000, 5000, 10000}}, 
        {{20, 30, 40, 50, 1000, 2000, 3000, 4000}}, 
        {{15, 25, 35, 45, 55, 65, 75, 100000}}, 
        {{25000, 20000, 15000, 10000, 5000, 2000, 1000, 500}}, 
        {{12345, 23456, 34567, 45678, 56789, 67890, 78901, 89012}}, 
        {{100, 500, 1000, 5000, 10000, 50000, 100000, 500000}}, 

        {{10, 20, 30, 1000, 2000, 3000, 40000, 80000}}, 
        {{1, 1, 2, 2, 3, 3, 10000, 10000}}, 
        {{100, 100, 100, 100, 100, 100, 100, 100}}, 
        {{250, 500, 750, 1000, 1250, 1500, 1750, 2000}}, 
        {{9000, 8000, 7000, 6000, 5000, 4000, 3000, 2000}}, 
        {{10, 50, 100, 200, 500, 1000, 2000, 4000}}, 
        {{75, 150, 225, 300, 375, 450, 525, 600}}, 
        {{100, 200, 400, 800, 1600, 3200, 6400, 12800}}, 
        {{99, 198, 297, 396, 495, 594, 693, 792}}, 
        {{800, 900, 1000, 1100, 1200, 1300, 1400, 1500}}, 

        {{UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX}}, 
        {{0, 0, 0, 0, 0, 0, 0, 0}}, 
        {{1, 1, 1, 1, 1, 1, 1, UINT_MAX}}, 
        {{1, UINT_MAX, 1, UINT_MAX, 1, UINT_MAX, 1, UINT_MAX}}, 
        {{999, 1999, 2999, 3999, 4999, 5999, 6999, 7999}}, 
        {{500, 1000, 1500, 2000, 2500, 3000, 3500, 4000}}, 
        {{4000, 8000, 12000, 16000, 20000, 24000, 28000, 32000}}, 
        {{7500, 8500, 9500, 10500, 11500, 12500, 13500, 14500}}, 
        {{3, 6, 9, 12, 15, 18, 21, 24}}, 
        {{1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000}}, 

	{{100000,200,200,250,250,230,220,260}},
	{{1,200,200,250,250,230,220,260}}
    };

    //generate some more tests
    for (int i = 40; i < 100; i++) {
        struct menu_device new_case;
        for (int j = 0; j < INTERVALS; j++) {
            if (i % 2 == 0) 
                new_case.intervals[j] = j * 100 + i;
            else 
                new_case.intervals[j] = (j + 1) * 500 - i * 10;
        }
        test_cases[i] = new_case;
    }
 
    printf("Test Case, Intervals, Before Value, After Value, Difference\n");
    for (int i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        unsigned int before = get_typical_interval_before(&test_cases[i]);
        unsigned int after = get_typical_interval_after(&test_cases[i]);
        long long difference = (long long)after - (long long)before;
	printf("%d, ", i+1);
	for (int j = 0; j < INTERVALS; j++) {
	    printf("%u", test_cases[i].intervals[j]);
	    if (j < INTERVALS - 1) printf("|"); 
	}
	printf(", %u, %u, %lld\n", before, after, difference);
    }

    return 0;
}
