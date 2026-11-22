SYN-SHELL disk space and walk mode icons. Toolbar icons are white on transparent so the program can tint them with the theme.
Each toolbar icon comes as NAME.svg, NAME.png (24 px) and NAME@2x.png (48 px).
disks    button DISKS (B_DISKS): back to the list of disks
up       button UP (B_UP): up to the parent folder
rescan   button RESCAN (B_RESCAN): scan again
blocks   button BLOCKS (B_BLOCKS): the block map view
graph    button GRAPH (B_GRAPH): the node graph view
flat     button 2D (B_FLAT): flat view
solid    button 3D (B_SOLID): 3D view
theme    button THEME (B_THEME, label is the theme's name): cycle the theme
The path field (B_PATH) has no icon.
Built into the program: cmake/icons2c.py writes src/icons_data.c from the PNGs; run it again after changing an icon.
zoomin   view button + (VB_ZOOMIN): zoom in, top right of the view
zoomout  view button - (VB_ZOOMOUT): zoom out
reset    view button (VB_RESET): the view back to how it started
