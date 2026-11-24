compile and run with xpple/cubiomes at `cubiomes/`:

```sh
gcc main.c lb_noise.c crunch.c cubiomes/libcubiomes.a -O3 -pthread -lm -fwrapv -o test && ./test
```
