#!/bin/bash

# Define our function
finder() {
    # Check if correct number of arguments provided
    if [ $# -ne 2 ]; then
        echo "Error: Two arguments are required: a directory path and a search string"
        return 1
    fi

    filesdir=$1
    searchstr=$2

    # Check if directory exists
    if [ ! -d "$filesdir" ]; then
        echo "Error: Directory $filesdir does not exist"
        return 1
    fi

    # Count the number of files
    file_count=$(find "$filesdir" -type f | wc -l)

    # Count the number of matching lines
    match_count=$(grep -r -o "$searchstr" "$filesdir" | wc -l)

    # Print the results
    echo "The number of files are $file_count and the number of matching lines are $match_count"
}

finder "$1" "$2"
