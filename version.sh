#!/bin/sh

grep -m1 VBL_VERSION vbl.cpp |cut -d\" -f2

