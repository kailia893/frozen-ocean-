Compile and run from the repository root with Cubiomes available at `cubiomes/`:

```sh
gcc main.c lb_noise.c crunch.c cubiomes/libcubiomes.a -O3 -pthread -lm -fwrapv -o test && ./test
```
