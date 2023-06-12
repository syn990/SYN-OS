#!/bin/bash

generate_dot_file() {
    local root=$(pwd)
    local path=$(basename "$root")
    local output_file=$1
    local include_files=$2
    local include_dirs=$3

    echo "Generating Graphviz dot file ($output_file)..." >&2
    echo "digraph G {"
    echo "  rankdir=\"TB\";"
    echo "  nodesep=1;"
    echo "  ranksep=2;"

    generate_structure "$root" "$include_files" "$include_dirs"

    echo "}"
    echo "Graphviz dot file ($output_file) generated." >&2
}

generate_structure() {
    local dir=$1
    local include_files=$2
    local include_dirs=$3
    local parent_name=$4

    for entry in "$dir"/* "$dir"/.[^.]*; do
        if [ -f "$entry" ] && [ "$include_files" = "true" ]; then
            if [ "$(basename "$entry")" = "." ] || [ "$(basename "$entry")" = ".." ]; then
                continue  # Skip "." and ".."
            fi
            local file_name=$(realpath --relative-to="$root" "$entry")
            echo "  \"$file_name\" [shape=box, label=\"$(basename "$entry")\"];"
            if [ "$parent_name" != "" ]; then
                echo "  \"$parent_name\" -> \"$file_name\" [style=dotted];"
            fi
            echo "Processing file $entry..." >&2
        elif [ -d "$entry" ] && [ "$include_dirs" = "true" ]; then
            local subdir_name=$(realpath --relative-to="$root" "$entry")
            echo "  \"$subdir_name\" [label=\"$(basename "$entry")\"];"
            if [ "$parent_name" != "" ]; then
                echo "  \"$parent_name\" -> \"$subdir_name\";"
            fi
            echo "Processing directory $entry..." >&2
            generate_structure "$entry" "$include_files" "$include_dirs" "$subdir_name"
        fi
    done
}

run_dot_command() {
    local layout=$1
    local input_file=$2
    local output_file=$3

    echo "Generating $layout output ($output_file)..."
    $layout -Tpng "$input_file" -o "$output_file"
    echo "$layout output ($output_file) generated."
}

# Prompt the user to select an option
echo "Select an option:"
echo "1. Display files only"
echo "2. Display files and directories"
echo "3. Display files, directories, and hidden files"
read choice

# Set variables for including files and directories
include_files="false"
include_dirs="false"

case $choice in
    1)
        include_files="true"
        ;;
    2)
        include_files="true"
        include_dirs="true"
        ;;
    3)
        include_files="true"
        include_dirs="true"
        ;;
    *)
        echo "Invalid choice"
        exit
        ;;
esac

# Execute the selected function
output_file="SYN-DIRDRAW.dot"
generate_dot_file "$output_file" "$include_files" "$include_dirs"
run_dot_command "dot" "$output_file" "SYN-DIRDRAW.png"
rm "$output_file"
feh "SYN-DIRDRAW.png"
rm "SYN-DIRDRAW.png"
