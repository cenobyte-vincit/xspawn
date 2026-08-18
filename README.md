# xspawn

xspawn starts a program on macOS through launchd and never execs that program itself. The aim is that an EDR records launchd as the parent, not this tool or the calling shell.

by cenobyte <vincitamorpatriae@gmail.com> 2026

https://github.com/cenobyte-vincit/xspawn

## Summary

xspawn opens `xpc_pipe_create_from_port(bootstrap_port)` and bootstraps the execution of a program through `_xpc_pipe_interface_routine`, the same private XPC pipe launchctl uses, and never execs the program itself.

- Root is refused. There is no other-UID mode.
- One invocation submits or removes one job.
- The client never execs `/bin/launchctl`.

## Requirements

### Runtime host

- macOS (Darwin) with a `gui/<uid>` session

### Build host

- macOS (Darwin) with Xcode Command Line Tools or Xcode
- C17 compiler (`cc`)
- `make`
- **cppcheck** for development (`brew install cppcheck`)

## Build

```bash
make
```

## Usage

```bash
xspawn oneshot -l <label> [-o <stdout>] [-e <stderr>] [--] <program> [args...]
xspawn submit  -l <label> [-o <stdout>] [-e <stderr>] [--] <program> [args...]
xspawn remove  -l <label>
xspawn load    -p <plist>
```

| Subcommand | Lifecycle |
|------------|-----------|
| `oneshot` | One-shot (`RunAtLoad` + `LaunchOnlyOnce`) |
| `submit` | KeepAlive |
| `load` | Caller-owned plist as written |
| `remove` | Unload by label |

One-shot (`RunAtLoad` + `LaunchOnlyOnce`; `0` means no sleep):

```bash
./xspawn oneshot -l com.example.once -- /tmp/helloworld 0
```

Arguments after `--` are `ProgramArguments`. That includes inline code (`python3 -c`, `perl -e`). CrowdStrike Falcon for macOS records the full `CommandLine`, so use inline code with interpreters sparingly.

```bash
./xspawn oneshot -l com.example.py -o /tmp/py.out -- \
	/usr/bin/python3 -c "print('hello world')"
```

KeepAlive job, the same lifecycle as `launchctl submit`. Sleep `60` so CrowdStrike Falcon for macOS and `launchctl print` still see the process:

```bash
./xspawn submit -l com.example.svc \
	-o /tmp/out.log -e /tmp/err.log -- /tmp/helloworld 60
```

Inspect with `launchctl print` (oracle only; this client does not call it):

```bash
launchctl print gui/$(id -u)/com.example.svc
```

Success shows `type = LaunchAgent` (not `Submitted`), `program` as the absolute path, and `state = running` or briefly `xpcproxy`. `Submitted` means the job did not take the bootstrap path.

Clean up a test job:

```bash
./xspawn remove -l com.example.svc
```

Load a caller-owned plist (not deleted after the reply):

```bash
./xspawn load -p /tmp/job.plist
```

`<program>` must be an absolute path. launchd does not search `$PATH`.

`load -p` requires an absolute path ending in `.plist`.

`-o` / `-e` may be relative. They are resolved against the current working directory before they are written into the plist. Omitted `-o` and `-e` are `/dev/null`.

`oneshot` and `submit` probe the label in `gui` and `user` (descriptor 708) before writing the temp plist. A taken label exits with `label already loaded` and no stdout. That check exists so a doomed 800 does not write `$TMPDIR/XXXXXX/XXXXXX.plist` (a DFIR artefact; CrowdStrike Falcon keeps the path in `ASEPFilePath`) or print the XML copy of the job dictionary. A free label prints the temp path, then that XML, then sends 800. `load -p` runs the same occupancy check on the file's `Label`, then prints the caller path and XML. The temp directory is removed on every exit. `remove` is by label.

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | Bootstrap or bootout XPC succeeded |
| 1 | Usage error, invalid label, root, or launchd/XPC rejection |

## Verify

Build host (`make` and the test tree; often colocated with a `gui` session). These checks are not a clean-runtime proof:

```bash
make
make test
make test-unit
make test-functional
```

```bash
./xspawn oneshot -l com.example.once -- /tmp/helloworld 0
```

## Limitations

- Same-user `gui/<uid>` only. Root is refused. No other-UID targeting.
- Ephemeral: not persistent across reboot or logout.
- Private XPC load and bootout constants are pinned to macOS 26.6.1 build 25G76. Re-pin if `sw_vers -buildVersion` changes (see ARCHITECTURE.md).
- Temp plist is `$TMPDIR/XXXXXX/XXXXXX.plist` (`$TMPDIR` must be absolute, else `/tmp`). The directory is removed on every exit. A taken label never creates that file.
- CrowdStrike Falcon for macOS records the temp plist path in the Auto-Start Extensibility Point field (`ASEPFilePath`) in the `ProcessRollup2` event. Process parent remains `launchd`.
- A standalone `xspawn` run is visible as this client: shell history, and an EDR process event for this binary. CrowdStrike Falcon for macOS records the full `CommandLine`, which includes the program path and its arguments. Compile the client into other tooling when that image and argv would be distinctive. Embedding does not remove `ASEPFilePath` or the launchd.log bootstrap line (see ARCHITECTURE.md, Parentage).
- Launchd also logs the spawn under `/private/var/log/com.apple.xpc.launchd/launchd.log` (see ARCHITECTURE.md, Parentage).
- Do not put an `XPCService` key in a hand-written plist: xpcproxy then forks and CrowdStrike Falcon for macOS records parent `xpcproxy`.

## Private XPC protocol

launchd is a Mach bootstrap server. This client does not use public XPC (`xpc_connection_create`). It opens a private libxpc pipe on the inherited `bootstrap_port` with `xpc_pipe_create_from_port(bootstrap_port, 4)`, then sends `_xpc_pipe_interface_routine`. Those symbols are in libxpc and are not in the SDK headers.

The routine ID is the descriptor argument, not a key in the request dictionary. On macOS 26.6.1 build 25G76, load is descriptor 800 and bootout is 801. Interface flags are 6. A `gui/<uid>` session is required: the inherited port is the gui launchd domain only inside an Aqua login session, and this client only sends `type` 8 with `handle` = uid.

Load (800) is an XPC dictionary. The job definition is not in the message body.

```text
handle   uid (uint64)
type     8 (gui)
paths    [absolute .plist]
by-cli   true
```

launchd `stat`s the path, parses the plist, then `posix_spawn`s xpcproxy. xpcproxy `exec`s the program in the same PID. Success is pipe return 0, no `xpc-fault`, `error` 0, `bootstrap-error` 0.

Bootout (801) is by label: `handle`, `type` 8, `name`, `no-einprogress`, `wait`. No plist.

The channel is prior art. Jonathan Levin (*launjctl*, 2015; *Mac OS X and iOS Internals* Vol. 1) showed that `launchctl` talks to launchd over a private XPC pipe, and documented `xpc_pipe_create_from_port` / `xpc_pipe_routine` with dict keys `type`, `handle`, `subsystem`, `routine`, and `name`. Patrick Wardle (*The Art of Mac Malware* Vol. 2) documented `_xpc_pipe_interface_routine` as the later send entry. Csaba Fitzl and Brandon Dalton (OBTS) mapped the same dict family and the domain type codes (`gui` is 8). Public snippets already used `xpc_pipe_create_from_port(bootstrap_port, 4)`.

Those write-ups describe the class of protocol. They do not ship the live 25G76 load constants. Levin's 2015 capture put `subsystem` and `routine` inside the dictionary and used `xpc_pipe_routine`. On 25G76 those keys are absent. `launchctl bootstrap` hits `_xpc_pipe_interface_routine` with the routine ID as the descriptor argument. arm64e static analysis of `launchctl` still looks like the old dict path and suggests 703 as a load ID. Live x86_64 lldb and an arm64 client run both use 800 / 801 with those keys absent. This client ships that one shape on both slices.

Register dumps, the lldb re-pin recipe, and parentage field notes are in ARCHITECTURE.md.

## See also

- ARCHITECTURE.md: spawn path, parentage, live wire, re-pin
- AGENTS.md: build host versus target
