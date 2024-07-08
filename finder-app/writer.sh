#!/bin/bash

# Define our function
writer() {
    # Check if correct number of arguments provided
    if [ $# -ne 2 ]; then
        echo "Error: Two arguments are required: a filename with full path and a text string"
        return 1
    fi

    # Assign arguments to variables for better readability
    writefile=$1
    writestr=$2

    # Create the path if it does not exist
    mkdir -p "$(dirname "$writefile")"

    # Try to write the file
    echo "$writestr" > "$writefile"

    # Check if the file was written correctly
    if [ $? -ne 0 ]; then
        echo "Error: The file $writefile could not be created"
        return 1
    fi
}

writer "$1" "$2"
