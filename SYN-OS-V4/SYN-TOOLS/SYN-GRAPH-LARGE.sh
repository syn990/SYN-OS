#!/bin/sh

generate_dot_file() {
    local root=$(pwd)
    local path=$(basename "$root")

    echo "Generating Graphviz dot file..." >&2
    echo "digraph G {"
    echo "  rankdir=\"TB\";"
    echo "  nodesep=1;"
    echo "  ranksep=2;"

    generate_structure "$root"

    echo "}"
    echo "Graphviz dot file generated." >&2
}

generate_structure() {
    local dir=$1
    local parent_name=$2

    for entry in "$dir"/* "$dir"/.*; do
        if [ -f "$entry" ] || [ -d "$entry" ]; then  # Check if it's a file or directory
            if [ "$(basename "$entry")" = "." ] || [ "$(basename "$entry")" = ".." ]; then
                continue  # Skip "." and ".."
            fi
            if [ -d "$entry" ]; then
                local subdir_name=$(realpath --relative-to="$root" "$entry")
                echo "  \"$subdir_name\" [label=\"$(basename "$entry")\"];"
                if [ "$parent_name" != "" ]; then
                    echo "  \"$parent_name\" -> \"$subdir_name\";"
                fi
                echo "Processing directory $entry..." >&2
                generate_structure "$entry" "$subdir_name"
            elif [ -f "$entry" ]; then
                local file_name=$(realpath --relative-to="$root" "$entry")
                echo "  \"$file_name\" [shape=box, label=\"$(basename "$entry")\"];"
                echo "  \"$parent_name\" -> \"$file_name\" [style=dotted];"
                echo "Processing file $entry..." >&2
            fi
        fi
    done
}



run_dot_commands() {
    local layout
    for layout in dot neato twopi circo fdp; do
        echo "Generating $layout output..."
        $layout -Tpng SYN-DIRDRAW.dot -o SYN-DIRDRAW-$layout.png
        echo "$layout output generated."
    done

    rm SYN-DIRDRAW.dot
    feh SYN-DIRDRAW-*.png
    rm SYN-DIRDRAW-*.png
}

generate_dot_file > SYN-DIRDRAW.dot
run_dot_commands
