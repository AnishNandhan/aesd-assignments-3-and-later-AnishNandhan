#!/bin/sh
# Script to find a specific string from files
# Author: Anish Nandhan

print_usage() {
    echo "Wrong usage of script."
    echo "Correct usage: $0 <directory> <text to search for>"
}

if [ $# -ne 3 ]
then
    print_usage()
    exit 1
fi

if [ ! -d $1 ]
then
    echo "Directory $1 does not exist"
    exit 1
fi

file_count=$( find -L $1 -type f -print | wc -l )
all_files=$(find -L $1 -type f -print ) 
match_count=$( grep -r $2 $all_files | wc -l )

echo "The number of files are $file_count and the number of matching lines are $match_count"

exit 0