if not ls obj 2> /dev/null > /dev/null; then
    mkdir obj
fi

for i in $(find . -maxdepth 1 -name "*.c"); do
    (gcc -c -o obj/${i%.c}.o $i -fPIC -g && echo Compiled $i) &
done
wait $(jobs -p)

echo Linking libjitc.so
gcc $(find obj -name "*.o") -o libjitc.so -fPIC -lm -g -shared

echo Packaging libjitc.a
ar rcs libjitc.a $(find obj -name "*.o")

echo Compiled
