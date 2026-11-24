to run, first place the cubiomes library in /cubiomes (if different, change path to cubiomes in header files), compile with libcubiomes.a (or libcubiomes_static if new version), then run:

> gcc main.c sb_noise.c crunch.c cubiomes/libcubiomes.a -O3 -pthread -lm -fwrapv -o test && ./test
