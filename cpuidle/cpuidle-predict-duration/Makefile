CC = cc
CFLAGS =  -O2
TARGET = predict_cpuidle_interval
SRC = predict_cpuidle_interval.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
