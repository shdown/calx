#!/bin/sh

if [ "$#" -ne 2 ]; then
    echo >&2 "USAGE: $0 INPUT OUTPUT"
    exit 2
fi

sed -r 's/[\\"]/\\\0/g; s/\t/\\t/g; s/\?/""\0""/g; s/.*/"\0\\n"/' -- "$1" > "$2"
