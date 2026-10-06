# syn-flow

Draws how things work. syn-graphmap draws where things are, a directory
tree. syn-flow draws what happens: which service starts what, what a
keybind runs, where traffic goes, what a command actually did. **Not
built yet:** `src/main.c` knows the commands and says so.

Every diagram goes to `~/Pictures/SYN-FLOW/` as two files, the `.dot`
source and the rendered `.svg` (or `.png`), colored from the active
theme. The `.dot` stays next to the image so a diagram can be edited by
hand and rendered again.

## Commands

```
syn-flow boot               services, the order they start in, and what each runs
syn-flow keys               every labwc keybind -> the script it runs -> what that calls
syn-flow net                interfaces, routes, iwd, VPN tunnels, SYN-RELAY peers
syn-flow install            the installer's stages, read from its scripts
syn-flow trace -- CMD ...   run CMD; graph every process it started and every file it touched
syn-flow ask "SENTENCE"     a sentence in, a diagram out

  -o FILE                   write here instead of ~/Pictures/SYN-FLOW/
  --png                     PNG instead of SVG
```

## How each one works

**Categories** read the machine, never a list kept by hand:

- `boot`: on SYN-OS on runit, `/etc/sv/*` and `/var/service/*` (each
  service's `run` script says what it starts) and `/etc/runit/1` for
  stage 1. On Arch-based SYN-OS, systemd units' `Wants=`, `Requires=` and
  `After=`.
- `keys`: the `<keybind>`s in `~/.config/labwc/rc.xml`, then each
  command's script, followed one level into what it calls.
- `net`: `/sys/class/net`, `/proc/net/route`, iwd over D-Bus (the way
  syn-connect already reads it), and relay's state files under
  `$XDG_RUNTIME_DIR`.
- `install`: the installer's stage scripts under `/usr/lib/syn-os/`, read
  for the order they call each other in.

The diagrams in `docs/diagrams/src/` are drawn by hand and drift from the
code. Once a category covers one of them, it can be generated instead.

**trace** runs the command under `ptrace` with
`PTRACE_O_TRACEFORK | TRACEVFORK | TRACECLONE | TRACEEXEC` and records
each process's `execve` and `openat` calls. Processes and files are the
nodes; "started", "read" and "wrote" are the edges.
`syn-flow trace -- synos-install` draws what the installer really did,
not what the docs say it does.

**ask** sends the sentence to a model, asking for DOT and nothing else,
runs the answer through `dot` to check it, and tries once more with the
error if it doesn't parse. It's for things that aren't on this machine.

All three build one graph (nodes, edges, labels, clusters) and hand it to
one DOT writer, which applies the theme: `SYN_BG` for the background,
`SYN_PANEL` node fill, `SYN_ACCENT` edges, `SYN_TEXT` labels, Terminus.
Rendering is `dot -Tsvg`, the same Graphviz syn-graphmap uses.

## Files it will have

```
src/main.c                commands
src/syn_flow_graph.c/.h   the graph: nodes, edges, clusters
src/syn_flow_dot.c/.h     graph -> themed DOT -> dot -Tsvg
src/syn_flow_boot.c       one file per category
src/syn_flow_keys.c
src/syn_flow_net.c
src/syn_flow_install.c
src/syn_flow_trace.c      ptrace
src/syn_flow_ask.c        the model call
```

Plus `syn_theme.c` from syn-crypter-src, reused the way syn-sysmon does.

## Build order

1. The graph and the DOT writer, with the theme.
2. `boot` on SYN-OS on runit, the simplest real category.
3. `trace`.
4. `keys`, `net`, `install`.
5. `ask`.

## Open questions

- **Which model `ask` uses.** A local model keeps it offline. A hosted one
  (the Claude API through `curl`, key in a file) is more capable but
  needs the network and a key.
- **syn-graphmap.** It could fold in as `syn-flow tree DIR`: one tool,
  one DOT writer, one theme mapping, instead of a C tool and a zsh one
  drawing in two styles.
