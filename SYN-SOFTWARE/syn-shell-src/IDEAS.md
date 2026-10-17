# syn-shell — parked ideas

## Natural-language host control (from SYNTERNAL)

A conversational layer over the shell: the user types a plain question or
instruction ("what's my IP?", "list partitions", "what's my CPU usage?") and it
decides which system command(s) to run, executes them with human oversight, and
returns real-time results — never pre-stored output.

- Core loop: interpret query → pick command(s) → run → summarise the result.
- Real-time: always query the live system, never cache answers.
- Low-resource: must stay usable on 2–4 GB machines.
- Interactive only, started at login, always under human oversight.

Parked from the SYNTERNAL repo (archived 2026-10). Could be a mode inside
syn-shell or its own `syn-*` tool — undecided.
