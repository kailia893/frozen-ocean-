CC := gcc
CFLAGS := -O3 -fwrapv -pthread -std=gnu11 -MMD -MP
LDFLAGS := -pthread
LDLIBS := -lm
TARGET := run
SRCS := main.c crunch.c sb_noise.c \
        cubiomes/biomenoise.c cubiomes/noise.c cubiomes/layers.c \
        cubiomes/biomes.c cubiomes/xradv.c
OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
.PHONY: clean
clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
