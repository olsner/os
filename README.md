"os" (proper name pending)
==========================

An x86-64 &micro;kernel in Assembly. User-space parts in Assembly and C.

## License ##

This software is licensed under the MIT license. See the LICENSE file.

## Building ##

To build, just run `make`. ccache is used by default, but can be disabled by
setting `CCACHE=` on the Make command line. (Just install ccache though.
Srsly.)

Since I'm too lazy to set up a cross compiler for the userspace parts, the
build OS must be very similar to what I'm runing on my computers. It's been
tested in Ubuntu 12.10 and 13.10 so far. A 64-bit OS is required.

## Setting up tun/tap networking ##

This is the recommended setup, since it allows easy wiresharking of all traffic
over the simulated network, and makes the VM available to all the normal
network debugging tools (arping, ping, etc).

To create a tun/tap device owned by yourself,

* Install uml-utilities (for tunctl),
* run `sudo tunctl -u $(whoami)`, this will print the name of the created
  device, which should be tap0 (which is hardcoded in various places)
* then `sudo ifconfig tap0 up 102.168.100.1` to set the IP

When running the lwIP program, the VM will present a really stupid simple web
server on 192.168.100.3.

## Running in QEMU ##

To run in qemu, there's a wrapper script in ./run_qemu.sh that automatically
runs make and starts qemu.

It is possible to run qemu with usermode networking without setting up the tap0
device, by modifying the run_qemu.sh script. The user-mode setting in the
script will forward local port 5555 to the VM's port 80.

## Running in Bochs ##

See notes/bochs.txt for build instructions for bochs 2.6.

Build the code with make, then start bochs using `bochs -f .bochsrc-2.6`.
tun/tap networking using tap0 is required for bochs.

## Known issues ##

Only unknown issues left. Yay!
