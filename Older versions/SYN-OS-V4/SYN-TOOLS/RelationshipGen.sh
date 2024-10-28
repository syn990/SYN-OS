#!/bin/bash

# Declare a variable for the directory to be scanned
DIR_TO_SCAN=~/SYN-OS

# The script output is directed into SYN-OS.dot file
exec > SYN-OS.dot

# Start the 'digraph' structure for Graphviz
echo "digraph SYNOS {"

# For each directory under the specified directory, create an edge in the graph
# from that directory to its parent directory
find $DIR_TO_SCAN -type d | awk -F/ -v OFS='/' '{
    if (NF>1) {
        print "    \""$0"\" -> \""$1"/"$2"\"";
    }
}'

# For each file under the specified directory, create an edge in the graph
# from that file to its parent directory
find $DIR_TO_SCAN -type f | awk -F/ -v OFS='/' '{
    print "    \""$0"\" -> \""$1"/"$2"\"";
}'

# End the 'digraph' structure
echo "}"

# Inform the user about the output file
echo "Dot file has been generated. Use the Graphviz 'dot' command to render it, e.g."
echo "dot -Tpng -O SYN-OS.dot"
