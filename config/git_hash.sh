#!/bin/bash
cd $1; echo "#define GIT_HASH" \"`git describe --always --dirty=+ --tags --long --abbrev=16`\" > $2
