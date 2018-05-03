"os" (proper name pending)
==========================

An x86-64 &micro;kernel in Assembly. User-space parts in Assembly and C.

## License ##

This software is licensed under the MIT license. See the LICENSE file.

## Building the cross-toolchain ##

Scripts for building a cross compiler can be found in `toolchain/`. Running
`./build.sh` in that directory will download the tarballs (if necessary),
verify the signatures, unpack, configure and build.

## Cross-compiling grub ##

On Linux systems, using the system grub is usually fine, but to support more
esoteric operating systems (e.g. macOS), grub needs to be cross-compiled with
an ELF toolchain. Use the `./build.sh` script in `grub/` to build one after
building the toolchain.

`grub-mkrescue` also requires `xorriso`.

## Building ##

After building the cross-compiler and cross-grub, just run `make` to build the
kernel(s) and user-space components. ccache is used by default, but can be
disabled by setting `CCACHE=` on the Make command line. (Just install ccache
though. Srsly.)

## Network setup ##

### User-mode networking ###

The simplest setup and the default in run_qemu.sh. QEMU presents a virtual
network with an internal DHCP server and something similar to NAT. Port 5555 on
localhost is redirected to port 80 on the virtual machine, which is where the
lwIP web server is listening.

### Setting up tun/tap networking ###

Useful when working on low-level network stuff, since it allows easy
wiresharking of all traffic over the simulated network, and makes the VM
available to all the normal network debugging tools (arping, ping, etc).

To create a tun/tap device owned by yourself,

* Install uml-utilities (for tunctl),
* run `sudo tunctl -u $(whoami)`, this will print the name of the created
  device, which should be tap0 (which is hardcoded in various places)
* then `sudo ifconfig tap0 up 192.168.100.1` to set the IP
* modify the lwIP program for a static IP
* modify run_qemu.sh or bochsrc to use the tun/tap networking

The lwIP program needs to be modified to set up a static IP at 192.168.100.3,
but after that it should present the dumb web server on that IP.

## Running in QEMU ##

To run in qemu, there's a wrapper script in ./run_qemu.sh that automatically
runs make and starts qemu.

## Running in Bochs ##

See notes/bochs.txt for build instructions for bochs 2.6.

Build everything as above, then start bochs using `bochs` from the repo root. A
.bochsrc with most of the necessary settings is provided.

## Known issues ##

Only unknown issues left. Yay!
