#!/bin/sh

generate_dot_file() {
    local root=$(pwd)
    local path=$(basename "$root")

    echo "digraph G {"
    echo "rankdir=\"TB\";"
    
    generate_structure "$root"

    echo "}"
}

generate_structure() {
    local dir=$1
    local parent_name=$2

    for subdir in "$dir"/*; do
        if [ -d "$subdir" ]; then
            local subdir_name=$(basename "$subdir")
            echo "\"$subdir_name\";"
            echo "\"$parent_name\" -> \"$subdir_name\";"
            generate_structure "$subdir" "$subdir_name"
        fi
    done
}

run_dot_command() {
    dot -Tpng SYN-DIRDRAW.dot -o SYN-DIRDRAW.png
    rm SYN-DIRDRAW.dot
    feh SYN-DIRDRAW.png
    rm SYN-DIRDRAW.png
}

generate_dot_file > SYN-DIRDRAW.dot
run_dot_command
