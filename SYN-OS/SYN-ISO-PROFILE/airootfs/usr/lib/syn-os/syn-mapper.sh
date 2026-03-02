#!/bin/zsh

# --- Configuration & Aesthetics ---
OUT_DIR="./syn-mapper-output"
DOT_FILE="${OUT_DIR}/SYN-DIRDRAW.dot"
RED='\033[0;31m'
BOLD='\033[1;31m'
NC='\033[0m'

# --- 1. Dependency Check ---
# Ensure the Arch system has the tools to actually run this
for dep in dot fdp realpath; do
    if ! command -v "$dep" >/dev/null 2>&1; then
        echo -e "${BOLD}[!] DEPENDENCY ERROR:${NC} Missing '$dep'"
        echo -e "    Run: ${RED}sudo pacman -S graphviz coreutils${NC}"
        exit 1
    fi
done

# --- 2. Argument Check ---
TARGET_DIR=$1

if [[ -z "$TARGET_DIR" ]]; then
    echo -e "${BOLD}[!] USAGE ERROR:${NC} Please pass a directory to scan."
    echo "    Example: 'syn-mapper /home/user/projects/syn-os' or 'syn-mapper .' for current directory."
    exit 1
fi

if [[ ! -d "$TARGET_DIR" ]]; then
    echo -e "${BOLD}[!] ERROR:${NC} Target '$TARGET_DIR' is not a valid directory."
    exit 1
fi

# Get absolute path for internal logic
SCAN_ROOT=$(realpath "$TARGET_DIR")

# --- 3. Cleanup Logic ---
if [[ -d "$OUT_DIR" ]]; then
    echo -e "${RED}[?] Old Graphviz data found in $OUT_DIR${NC}"
    echo -n "    Delete and continue? (y/n): "
    read -r opt
    [[ "$opt" != "y" ]] && { echo "Aborted."; exit 0 }
    rm -rf "$OUT_DIR"/*
else
    mkdir -p "$OUT_DIR"
fi

# --- 4. The DOT Generator ---
echo -e "${RED}[+] Generating Graphviz Architecture...${NC}"

cat <<EOF > "$DOT_FILE"
digraph G {
  bgcolor="#000000";
  rankdir="TB";
  nodesep=1;
  ranksep=2;
  node [style=filled, fillcolor="#000000", color="#8B0000", fontcolor="#FF0000", shape=box, fontname="JetBrains Mono"];
  edge [color="#FF0000"];
EOF

# Pure ZSH recursion: (D) finds hidden, (on) sorts by name
for entry in ${SCAN_ROOT}/**/*(Don); do
    # Create relative paths for clean node IDs
    rel_path="${entry#$SCAN_ROOT/}"
    parent="${rel_path%/*}"
    base="${entry##*/}"

    # Style directories differently from files
    if [[ -d "$entry" ]]; then
        echo "  \"$rel_path\" [label=\"$base/\", color=\"#FF0000\", style=bold];" >> "$DOT_FILE"
    else
        echo "  \"$rel_path\" [label=\"$base\", style=dotted];" >> "$DOT_FILE"
    fi

    # Draw edges unless it's at the root level
    if [[ "$parent" != "$rel_path" ]]; then
        echo "  \"$parent\" -> \"$rel_path\";" >> "$DOT_FILE"
    fi
done

echo "}" >> "$DOT_FILE"

# --- 5. The Render Stage ---
echo -e "${RED}[+] Rendering Layouts...${NC}"
# We'll stick to 'dot' for hierarchy and 'fdp' for clusters
for layout in dot fdp; do
    echo -ne "    - Processing $layout... \r"
    $layout -Tpng "$DOT_FILE" -o "${OUT_DIR}/SYN-OS-${layout}.png"
    $layout -Tsvg "$DOT_FILE" -o "${OUT_DIR}/SYN-OS-${layout}.svg"
done

# --- 6. The Report ---
echo -e "\n${BOLD}SYSTEM MAP COMPLETE${NC}"
echo -e "------------------------------------------------"
echo -e "SOURCE SCAN    : ${RED}$SCAN_ROOT${NC}"
echo -e "DUMP LOCATION  : ${RED}$OUT_DIR${NC}"
echo -e "FILES CREATED  : .dot, .png, .svg"
echo -e "------------------------------------------------"
echo -e "${RED}Architecture Dusted.${NC}"