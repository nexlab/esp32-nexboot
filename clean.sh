#!/bin/bash
echo -n " * cleaning project..."
p=$(dirname $(readlink -e $0))
find $p -name '*~' -exec rm {} \;
rm -rf build
echo " OK!"
