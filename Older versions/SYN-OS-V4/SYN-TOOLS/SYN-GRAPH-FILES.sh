#!/bin/sh

# Check if dependencies are installed
if ! command -v dot &> /dev/null || ! command -v feh &> /dev/null || ! command -v realpath &> /dev/null; then
    printf "\n\033[31mERROR: Your system doesn't meet the necessary prerequisites to run this script.\n\n"
    printf "This script requires the following software to be installed:\n"
    printf "  • \033[1mGraphviz (dot)\033[0m: Used for generating the visual directory structure.\n"
    printf "  • \033[1mfeh\033[0m: A fast and light image viewer used to display the generated directory structure.\n"
    printf "  • \033[1mrealpath\033[0m: A command-line utility to resolve symbolic links and produce absolute pathnames.\n\n"
    printf "Please ensure these packages are installed before running the script.\n"
    printf "On Arch Linux, you can install them with the following command:\n"
    printf "\033[32msudo pacman -S graphviz feh coreutils\033[0m\n\n\033[0m"
    exit
fi

generate_dot_file() {
    local root=$(pwd)
    local path=$(basename "$root")

    echo "Generating Graphviz dot file..." >&2
    echo "digraph G {"
    echo "  graph [bgcolor=black];"  # Set black background
    echo "  node [fontcolor=white, color=darkred, style=filled, fillcolor=black];"  # Set node attributes
    echo "  edge [fontcolor=white, color=darkred, style=dotted];"  # Set edge attributes
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

    for entry in "$dir"/*; do
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
    done
}

run_dot_commands() {
    local layout
    for layout in dot neato twopi circo fdp; do
        echo "Generating $layout output... This can take some time..."
        $layout -Tpng SYN-DIRDRAW.dot -o SYN-DIRDRAW-$layout.png
        echo "$layout output generated."
    done

    rm SYN-DIRDRAW.dot
    feh SYN-DIRDRAW-*.png
    rm SYN-DIRDRAW-*.png
}

generate_dot_file > SYN-DIRDRAW.dot
run_dot_commands
