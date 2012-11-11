#!/bin/bash

while [ $# -gt 0 ]; do
	echo "menuentry \"$1\" {"
	echo "    multiboot (cd)/kstart.b"
	echo "    module (cd)/user_$1.mod"
	echo "    boot"
	echo "}"
	shift
done

echo "menuentry \"idle\" {"
echo "    multiboot (cd)/kstart.b"
echo "    boot"
echo "}"
