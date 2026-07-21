# Audio mixer

SYN-OS doesn't ship the usual audio panel. Instead there's a small,
purpose-built mixer that opens straight in the terminal.

Click the volume icon on the bar to open it. You'll see two lists, your
outputs (speakers, headphones) and your inputs (microphones), each with a
volume bar. A dot marks whichever device is currently the default, an "M"
marks anything muted.

## Using it

| Key | Does |
|---|---|
| `Tab` | Switch between outputs and inputs |
| `↑` / `↓` (or `j` / `k`) | Move the selection |
| `Enter` | Make the selected device the default |
| `m` | Mute or unmute it |
| `←` / `→` (or `h` / `l`) | Turn the volume down or up |
| `Esc` / `q` | Quit |

You don't need to close and reopen it to see changes made elsewhere, if
you scroll the volume with the mouse wheel on the bar while the mixer is
open, it picks that up right away.

Middle-clicking the volume icon on the bar mutes instantly without opening
the mixer at all, and scrolling on it adjusts volume in small steps.

It's themed to match whatever look you've got active, same as the Wi-Fi
picker and the encryption tool.
