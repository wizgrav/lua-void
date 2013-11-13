LIBTOOL="libtool --tag=CC --silent"
$LIBTOOL --mode=compile cc -O2 -I/usr/include/lua5.1 -lpthread -c src/void.c
$LIBTOOL --mode=link cc -O2 -I/usr/include/lua5.1 -lpthread  -rpath /usr/local/lib/lua/5.1 -o libvoid.la void.lo
mv .libs/libvoid.so.0.0.0 void.so
