#!/bin/bash

echo "digraph G {"; echo "rankdir=\"LR\";"; root="$1"; path="${root##*/}"; echo "\"$path\""; for dir in $(/usr/bin/find $1 -type d); do parent_dir=$(dirname "$dir"); dir_name="${dir##*/}"; parent_name="${parent_dir##*/}"; echo "\"$parent_name\" -> \"$dir_name\";"; done; echo "}"
