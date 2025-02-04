# FAT32 Disk Image Tool

## Overview

This is a Linux-based program for manipulating a FAT32 disk image, allowing file listing, reading, creation, deletion, and writing to an existing disk image. It is built specifically for Linux environments and does not support other operating systems.

## Compilation

Makefile

## Usage

./fat32_tool DISKIMAGE OPTION [PARAMETERS]

## Commands:

-l : List files

-r -a <FILENAME> : Read file as ASCII

-r -b <FILENAME> : Read file as binary

-c <FILENAME> : Create file

-d <FILENAME> : Delete file

-w <FILENAME> <OFFSET> <N> <DATA> : Write N bytes at OFFSET

-h : Help menu

## Notes

Supports only root directory

Filenames are case insensitive

No subdirectory support

