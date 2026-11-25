Download, unzip, then run make, then run ./run. Simple as that, blueelephant better not mess this up



#
#
#
#
#
#
#
#
#
#
#
#
#
#
#
#
#
#

# Old instructions ignore

to run, first place the cubiomes library in /cubiomes (if different, change path to cubiomes in header files), compile with libcubiomes.a (or libcubiomes_static if new version), then run:

> gcc main.c sb_noise.c crunch.c cubiomes/libcubiomes.a -O3 -pthread -lm -fwrapv -o test && ./test





Note: to compile cubiomes, you can copy and past this command 

```
cd cubiomes
find . -name "*.c" -exec gcc -c -O3 -DNDEBUG -fwrapv \
    -I. -Ifeatures -Iloot -Iloot/loot_tables -Iloot/cjson {} \;
ar rcs libcubiomes.a *.o
rm -rf *.o
cd ..
```
