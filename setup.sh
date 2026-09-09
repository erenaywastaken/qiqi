#!/bin/sh

SERVER_BIN="server.out"
TRANSPILER_BIN="transpiler.out"

if [ ! -f "$TRANSPILER_BIN" ]; then
    echo "Compiling transpiler..."
    cc src/transpiler.c -o$TRANSPILER_BIN
fi
echo "Running transpiler..."
./transpiler.out site/index.ljsx dist/index.lua

if [ ! -f "$SERVER_BIN" ]; then
    echo "Compiling server..."
    cc $(pkg-config --cflags lua5.4) src/server.c src/cgi.c $(pkg-config --libs lua5.4) -o$SERVER_BIN -O2 -DNDEBUG
fi
echo "Running server..."
./server.out