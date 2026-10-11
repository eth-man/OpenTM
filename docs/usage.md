# Using OpenTM

## The pieces

| | |
|---|---|
| `opentm_app` | the GUI |
| `opentm_cli` | one-shot headless driver for scripting |
| `opentm_server` | holds console sessions so several clients can share them |
| `opentm_tray` | tray icon supervisor; owns the server's lifetime |

Running `opentm_app` starts all three. Whichever you launch brings up the rest, so there is no wrong entry point - the window starts the tray with `--no-gui` because it is already the window.

```
opentm_tray ──┬── opentm_server     sessions, outlive the window
              └── opentm_app        the window
```

Closing the window leaves the consoles connected; reopening from the tray picks the sessions back up. A second `opentm_app` raises the existing window instead of adding another. Quitting from the tray shuts the whole set down, and killing the tray outright still takes the server with it (`--exit-with-owner`), so nothing is left holding a console.

`opentm_app --standalone` skips all of that and owns its sessions in one process; they end when the window closes. The same switch lives in Preferences, for people who would rather nothing ran in the background.

The GUI drives one session per target, so several consoles can be connected at once and the panels follow whichever is selected. Output from the others is buffered and replayed when you switch back.

## One console, one owner

The console tracks protocol registration per TCP connection, and it will not give `DBGP` or `DFMP` to a second connection while a first one holds a session. While the GUI is connected, the CLI cannot have that console, and vice versa.

OpenTM says so plainly rather than hanging, the console's refusal is otherwise silent, and every later request just comes back `NOPROTO`.

Anything needing concurrent access - or a session that outlives a window - goes through `opentm_server`, which speaks line-delimited JSON over a local socket. See [the server protocol](rpc-protocol.md). Mutating verbs are refused unless it is started with `--allow-mutating`, which the tray does because the GUI cannot load, install, reset or power a console otherwise. Both listeners are loopback-only.

To point the GUI at a server you started yourself:

```sh
opentm_server --allow-mutating &
opentm_app --server 127.0.0.1:4300
```

A crashed client is harmless: the socket closes and the console frees everything. The awkward case is a client that stays connected and stops answering, a sleeping laptop, a partitioned network, which holds the console until the CP reaps the connection after some time.

## Scripting with `opentm_cli`

`opentm_cli` opens its own connection, does what it is asked, and exits. It needs no server and leaves nothing running, which suits a build step or a CI job.

A post-build step that runs the thing you just compiled and reports whether it passed:

```sh
opentm_cli --host 192.0.2.10 --serve build \
           --reset --load build/game.self \
           --pass-on "RESULT: OK" --fail-on "ASSERT|FATAL" \
           --tty-out run.log
```

Nothing is copied to the console. `--serve` exposes a host directory as `/app_home/`, and a host-side path given to `--load` is rewritten to `/app_home/<name>`, so the console pulls the executable at load time and every run uses the current build.

`--reset` combined with `--load` reboots first and holds the load until the debug agent is back, rather than firing it at a console on its way down.

The run ends when the console's own output matches `--pass-on` or `--fail-on`, not after a fixed wait. `--wait-agent` holds the other actions until the debug agent is up - after a power on, say - and `--run-timeout` bounds the whole thing.

### The exit code reports the operation, not the connection

| code | meaning |
|---|---|
| 0 | everything asked for succeeded |
| 1 | usage error |
| 2 | could not reach the console, or the session never came up |
| 3 | timed out waiting for the agent, a reply or a pattern |
| 4 | the console refused an operation (lv2 status, transfer result) |
| 5 | `--fail-on` matched |

A load that the console rejects exits 4, not 0. That is the difference between a build step that catches a broken executable and one that does not.

### Verbs

| | |
|---|---|
| run | `--load SELF`, `--install PKG`, `--ps` |
| files | `--ls PATH`, `--upload HOST:KIT`, `--download KIT:HOST`, `--mkdir PATH`, `--rm PATH`, `--mv FROM:TO`, `--chmod MODE:PATH`, `--touch PATH` |
| power | `--power-on`, `--power-off [--force]`, `--reset`, `--wol --mac MAC` |
| settings | `--settings-refresh`, `--settings-apply FILE`, `--settings-commit` |
| output | `--tty-out FILE`, `--quiet`, `--pass-on RE`, `--fail-on RE` |
| timing | `--timeout`, `--run-timeout`, `--linger`, `--wait-agent` |

`--help` lists them all. Actions run in a fixed order whatever order you write
them in, so one invocation can create a directory, upload into it and list the
result.

Power control does not need a debug session, the communications processor (DECR-1000 only) answers with the console off - so `--power-on --wait-agent --load...` works from cold.

### Two traps

**Git Bash rewrites unix looking arguments.** MSYS turns `/dev_hdd0/` into `C:/Program Files/Git/dev_hdd0/` before the CLI ever sees it. Export `MSYS_NO_PATHCONV=1` first.

**`/dev_hdd0` itself is not writable.** A `--mkdir /dev_hdd0/thing` is answered with success and creates nothing, and a later upload into it fails with `result=0x4`. Use a subdirectory such as `/dev_hdd0/game_debug`.