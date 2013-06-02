#!/bin/bash

while [ $# -gt 0 ]; do
	echo "menuentry \"${1#user/}\" {"
	echo "    multiboot (cd)/kstart.b"
	echo "    module (cd)/$1.mod"
	echo "    boot"
	echo "}"
	shift
done

echo "menuentry \"idle\" {"
echo "    multiboot (cd)/kstart.b"
echo "    boot"
echo "}"
