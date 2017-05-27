####################################################################
#
#  tmr_mod - timer device driver (for linux kernel with HZ = 1000)
#
# Allows to measure time intervals with a discrete of one and ten milliseconds
#
#####################################################################

## Description

A simple linux kernel device driver

## Package files:

* tmr.c		- source code of kernel device driver

* Makefile	- make file (example compilation scenario)

* README.md

## Required:
```
linux kernel headers
```

## Compilation and installation
```
make
make install
```

## Load/remove driver:
```
load: modprobe tmr_mod
remove: modprobe -r tmr_mod
```

## Using:

### From the user level functions are supported:
* open - open device
* close - close device
* read - reading time stamps
* write - set time stamp to zero - reset timer


