#!/bin/sh

filesdir=$1
searchstr=$2

if [ "$#" -ne 2 ]; then
    echo "Error: Two arguments must be provided to the script."
    exit 1
fi

if [ ! -d "$filesdir" ]; then
    echo "Error: First argument must be the directry but ${filesdir} does not exists."
    exit 1
fi

file_count=$(find "$filesdir" -type f | wc -l)
matching_lines_count=$(grep -r "$searchstr" "$filesdir" | wc -l)


echo "The number of files are $file_count and the number of matching lines are $matching_lines_count"

exit 0

