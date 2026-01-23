#!/bin/sh
# Script to write string to a file
# Author: Anish Nandhan

print_usage() {
    echo "Wrong usage of script."
    echo "Correct usage: $0 <file_path> <text to write>"
}

if [ $# -ne 3 ]
then
    print_usage()
    exit 1
fi

parent_path=$(dirname $1)

if [ ! -d $parent_path ]
then
    mkdir -p $parent_path 2> /dev/null
    if [ $? -ne 0 ]
    then
        echo "Couldn't create directory \"$parent_path\""
        exit 1
    fi
fi


echo $2 > $1 2> /dev/null

if [ $? -ne 0 ]
then
    echo "Couldn't write to $1"
    exit 1
fi

exit 0