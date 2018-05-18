#!/bin/bash
echo -n " * creating html..."
p=$(dirname $(readlink -e $0))
rm -f main/html.h
cd web
for i in *.html ; do
   xxd -i $i >> ../main/html.h
done
echo " OK!"
