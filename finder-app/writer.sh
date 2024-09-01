#!/bin/sh

writefile=$1
writestr=$2

if [ "$#" -ne 2 ]; then
    echo "Error: Two arguments are required."
    exit 1
fi

dirpath=$(dirname "$writefile")

if [ ! -d "$dirpath" ]; then
    mkdir -p "$dirpath"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to create the directory."
        exit 1
    fi
fi

echo "$writestr" > "$writefile"

if [ $? -ne 0 ]; then
    echo "Error: The file could not be created or written to."
    exit 1
fi

echo "The string has been successfully written to the file."

exit 0

