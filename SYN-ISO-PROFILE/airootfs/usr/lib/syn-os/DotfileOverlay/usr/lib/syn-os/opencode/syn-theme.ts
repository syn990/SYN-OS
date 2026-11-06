// syn-theme: opencode and the SYN-OS desktop on one theme, both ways.
//
//   desktop -> opencode   ~/.config/syn-os/current-theme changes (syn-theme-apply, the waybar
//                         menu, a linked phone) and this opencode switches to that theme
//   opencode -> desktop   a SYN-OS-* theme picked in /themes is applied to the desktop with
//                         syn-theme-apply, which also tells every linked phone (syn-link)
//
// The themes themselves are written by syn-theme-opencode (run by syn-theme-apply) into
// ~/.config/opencode/themes; ~/.config/opencode/tui.json loads this file. A theme picked here
// that is not a SYN-OS one stays in opencode only, until the desktop next changes.
//
// SYN-OS : The Syntax Operating System
// Author : William Hayward-Holland (Syntax990)
// License: MIT License
import type { TuiPlugin, TuiPluginModule } from "@opencode-ai/plugin/tui"
import { spawn } from "node:child_process"
import { existsSync, readFileSync } from "node:fs"
import { homedir } from "node:os"
import { join } from "node:path"

const CURRENT = join(homedir(), ".config", "syn-os", "current-theme")
const PALETTES = join(homedir(), ".config", "syn-os", "themes")
const EVERY_MS = 700

function desktop(): string {
  try {
    return readFileSync(CURRENT, "utf8").trim()
  } catch {
    return ""
  }
}

const tui: TuiPlugin = async (api) => {
  let seen = "" // the desktop theme this opencode last acted on
  let mine = "" // the theme opencode wore after our last look
  let retry: ReturnType<typeof setTimeout> | undefined

  const wear = (name: string) => {
    if (api.theme.selected === name) return
    if (api.theme.has(name)) {
      api.theme.set(name)
      return
    }
    // a palette newer than this opencode: its theme list is read again on SIGUSR2
    // (opencode's own refresh, 250 ms and 1 s later), then the switch is tried once more
    process.kill(process.pid, "SIGUSR2")
    clearTimeout(retry)
    retry = setTimeout(() => {
      if (desktop() === name && api.theme.has(name)) {
        api.theme.set(name)
        mine = api.theme.selected
      }
    }, 1500)
  }

  const look = () => {
    if (!api.theme.ready) return
    const sel = api.theme.selected
    // /themes previews every row the cursor passes (and puts the old one back on escape):
    // while a dialog is open and the theme has moved, someone is browsing; only what the
    // picker leaves behind once it closes is a pick
    if (api.ui.dialog.open && sel !== mine) return
    if (sel !== mine) {
      // picked in opencode since our last look
      mine = sel
      if (sel.startsWith("SYN-OS-") && sel !== desktop() && existsSync(join(PALETTES, sel + ".theme"))) {
        seen = sel // the desktop is about to say this name: nothing to follow then
        spawn("syn-theme-apply", [sel], { detached: true, stdio: "ignore" }).on("error", () => {}).unref()
        return
      }
    }
    const now = desktop()
    if (now && now !== seen) {
      seen = now
      wear(now)
      mine = api.theme.selected
    }
  }

  const start = setInterval(() => {
    if (!api.theme.ready) return
    clearInterval(start)
    mine = api.theme.selected // what opencode opened with is not a pick
    look()
    const tick = setInterval(look, EVERY_MS)
    api.lifecycle.onDispose(() => {
      clearInterval(tick)
      clearTimeout(retry)
    })
  }, 100)
  api.lifecycle.onDispose(() => clearInterval(start))
}

export default { id: "syn-theme", tui } satisfies TuiPluginModule
