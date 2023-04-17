# kdrv - Char Device with kfifo Ring Buffer and I/O Multiplexing

This repository contains a Linux kernel module kdrv which implements a char device with read and write operations using a kfifo ring buffer. Additionally, it supports I/O multiplexing to monitor multiple I/O devices.

## Requirements
* `make` utility
* C compiler
* Header files for the linux Kernel
```shell
$ sudo apt install linux-headers-`uname -r`
```

## Installation
1. Clone the repository:
```shell
$ git clone https://github.com/xueyang0312/kdrv.git
```
2. Change into the cloned directory:
```shell
$ cd kdrv
```
3. Compile the module:
```shell
$ make
```
4. Load the module:
```
$ make load
```

## Usage
Once the module is loaded, a new char device `/dev/kdrv` will be created. You can read and write to this device like any other char device.