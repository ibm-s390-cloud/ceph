#!/usr/bin/env bash

set -ex

echo 1 > file1
echo 2 > file2
echo 3 > file3
[ -e file4 ] && rm file4
mkdir -p .snap/snap1
echo 4 > file4
now=`ls`
then=`ls .snap/snap1`
rm -rf .snap/snap1
if [ "$now" = "$then" ]; then
    echo live and snap contents are identical?
    false
fi

# do it again
echo 1 > file1
echo 2 > file2
echo 3 > file3
mkdir -p .snap/snap1
echo 4 > file4
rm -rf .snap/snap1

rm file?

echo OK
