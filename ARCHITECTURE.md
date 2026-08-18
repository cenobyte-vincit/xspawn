# Architecture

xspawn is a same-user, unprivileged macOS client that bootstraps an ephemeral `gui/<uid>` launchd job over the private XPC pipe used by `launchctl bootstrap`. launchd performs `posix_spawn`; this process is not the exec-time parent. Apple's Endpoint Security therefore logs parent `launchd`, not the submitter.

The **channel** (private pipe to launchd) is prior art. This client loads a job by opening `xpc_pipe_create_from_port(bootstrap_port, 4)` and calling `_xpc_pipe_interface_routine` with descriptor **800**, flags **6**, and a dict of `handle` (uid), `type` (8), `paths` (the plist), and `by-cli` (true). Those values are pinned to macOS 26.6.1 build 25G76 (x86_64 lldb; arm64 client). Re-pin them if `sw_vers -buildVersion` changes.

## How the client sits in the spawn path

A Security Response (SR) team investigating a macOS process reconstructs its origin from parent and grandparent fields in EDR telemetry. Those fields are filled from Apple's Endpoint Security exec events. A payload that a shell or a helper `exec`s therefore names that submitter in the walk.

This client never execs the payload, so those parent and grandparent fields cannot name it. It asks launchd to start the job. launchd `posix_spawn`s `/usr/libexec/xpcproxy`. xpcproxy `exec`s the program in the same PID (no fork). The kernel parent process ID (PPID) stays 1. Endpoint Security logs parent `launchd`. Parent and grandparent in the EDR then name launchd, not this client or the calling shell.

`launchctl` is Apple's CLI on the same pipe. This client never execs it. `launch_msg(SubmitJob)` and `SMJobSubmit` still leave the submitter as exec-time parent. They are a different plane.

```text
Direct exec from a dropper               This client

launchd (pid 1)                          launchd (pid 1)
  `-- dropper                              |-- dropper
        `-- payload                        |     `-- xspawn (stage 1)
                                           |            (XPC only; not on
                                           |             the job's parent chain)
                                           `-- xpcproxy --> payload
                                               (exec-in-place, same PID)

ES parent = dropper                      ES parent = launchd
EDR origin walk names the dropper        EDR origin walk names launchd
```

Message and spawn, left to right in time:

```text
caller                xspawn                            launchd (pid 1)           payload
  |                            |                              |                      |
  | exec                       |                              |                      |
  |--------------------------->|                              |                      |
  |                            | write $TMPDIR/.../XXXXXX.plist |                    |
  |                            |                              |                      |
  |                            | xpc_pipe_create_from_port    |                      |
  |                            |   (bootstrap_port, 4)        |                      |
  |                            |                              |                      |
  |                            | _xpc_pipe_interface_routine  |                      |
  |                            |   descriptor 800             |                      |
  |                            |   type=8 handle=uid          |                      |
  |                            |   paths=[plist] by-cli=true  |                      |
  |                            |----------------------------->|                      |
  |                            |                              | stat + parse plist   |
  |                            |                              | posix_spawn xpcproxy |
  |                            |                              |--------------------->|
  |                            |                              |                      | exec-in-place
  |                            | reply error=0                |                      | PPID = 1
  |                            | bootstrap-error=0            |                      |
  |                            |<-----------------------------|                      |
  |                            | unlink temp plist            |                      |
  |                            |                              |                      |

Endpoint Security logs the payload exec with parent launchd.
```

## Prior art

Credit for the plane, not for this build's load constants:

| Source | What it already established |
|--------|-----------------------------|
| Jonathan Levin, *launjctl* (2015) and *Mac OS X and iOS Internals* (MOXiI) Vol. 1 | `launchctl` talks to launchd over a private XPC pipe; `xpc_pipe_create_from_port` / `xpc_pipe_routine`; dict keys `type`, `handle`, `subsystem`, `routine`, `name` |
| Patrick Wardle, *The Art of Mac Malware* Vol. 2 | `_xpc_pipe_interface_routine` as the later send entry; routine IDs for other ops (e.g. process dump `0x2c4` / 708) |
| Csaba Fitzl and Brandon Dalton, *Mac, Where's My Bootstrap?* (OBTS) | Same dict family (`type` / `subsystem` / `handle` / `name`); domain types system/user/login/pid/gui |

This client speaks the reverse-engineered private XPC protocol from `launchctl`. It implements three operations on that pipe: occupancy of a label (708), load of a plist path (800), and bootout by label (801).

Levin's 2015 capture put `subsystem`/`routine` **in the dict** and broke on `xpc_pipe_routine`. On 26.6.1 / 25G76 the load ID is the **descriptor argument** to `_xpc_pipe_interface_routine`, and those dict keys are **absent**. Treat older tables as the class of protocol, not as drop-in `#define`s.

## Components

```
xspawn.c                  CLI: oneshot | submit | remove | load
label.c                   reverse-DNS-ish label check
write-plist.c             mkdtemp dir + exclusive XXXXXX.plist
bootstrap-job.c           private pipe send (708 occupancy, 800 load, 801 bootout)
xpc-pipe.h                undeclared libxpc prototypes
```

Public SDK: `<xpc/xpc.h>`, `<servers/bootstrap.h>`, `<mach/mach.h>`. Private (declared in `xpc-pipe.h`): `xpc_pipe_create_from_port`, `_xpc_pipe_interface_routine`.

Flow:

1. Refuse root (`geteuid() == 0`). Root would target the system domain; that is a different investigation.
2. Parse subcommand and `-l` / `-o` / `-e`. Omitted `-o` / `-e` are `/dev/null`.
3. Validate the label; `oneshot` / `submit` require an absolute program path (launchd does not search `$PATH`).
4. `job_is_loaded()` sends descriptor 708 in `gui` then `user` (no `shmem`). A taken label exits here. The check exists so a doomed 800 does not write `$TMPDIR/XXXXXX/XXXXXX.plist` or print the payload XML. That on-disk file is a DFIR artefact: CrowdStrike Falcon copies the path into `ASEPFilePath` even after unlink.
5. `write_plist()` creates `$TMPDIR/XXXXXX/` (`mkdtemp`, 0700; `$TMPDIR` must be absolute, else `/tmp`) and writes `XXXXXX.plist` at mode `0600` (`mkstemps`, suffix `.plist`) as a binary plist (`bplist00`, CoreFoundation), no `XPCService` key. Before that binary write it prints the absolute path and an XML serialisation of the in-memory job dictionary to standard output (no extra open). Caller UID, not world-writable: launchd `stat`s the file and rejects bad ownership/permissions. The name must end in `.plist` (launchd `bootstrap-error` 5 otherwise).
6. `bootstrap_job(bootstrap_port, getuid(), path)` opens `xpc_pipe_create_from_port(port, 4)` and sends `_xpc_pipe_interface_routine` with the load dict below.
7. `atexit` unlinks the temp `XXXXXX.plist` and `rmdir`s the temp directory on every `exit` (0 or non-zero). `write_plist()` also removes a partial directory on its own failure. launchd has already parsed the definition; `remove` is by label.
8. `remove -l` calls `bootout_job(bootstrap_port, getuid(), label)` with descriptor 801. No plist file. No stdout dump.
9. `load -p` reads `Label`, runs the same 708 occupancy check, then prints the caller path and an XML copy (`print_plist_file`) and sends the 800 wire. The file is not unlinked.

`launchctl` is used only as a test oracle (`launchctl print` / `launchctl bootout`). The product never execs it.

## The private XPC protocol

A `gui/<uid>` session is required. This wire is the gui-domain bootstrap (`type` 8, `handle` = uid) on the inherited `bootstrap_port`. That Mach send right is the gui launchd domain only inside an Aqua login session. Outside that session the inherited port belongs to a different domain (user, pid, or system), and this client does not look another one up. Public XPC (`xpc_connection_create` and friends) is not how `launchctl bootstrap` loads a LaunchAgent.

`launchctl` opens a private libxpc pipe over `bootstrap_port` and sends a typed routine. This client does the same. The symbols live in libxpc and are not in the SDK headers. `xpc-pipe.h` declares them. The constants below are the live 25G76 capture; register-level dumps and the re-pin recipe sit under **Live wire**.

### Open a pipe

`xpc_pipe_create_from_port(bootstrap_port, 4)` wraps the Mach port as an `xpc_pipe_t`. Flag **4** is the live value on 25G76. `launchctl` later reuses a cached pipe from `_os_alloc_once`. This client creates a pipe per send. For a `gui/<uid>` load they are equivalent. The client does not call `bootstrap_look_up`; the inherited port is already the gui domain.

### Send a routine

`_xpc_pipe_interface_routine(pipe, descriptor, request, &reply, 6)` is the send.

The **descriptor** is the routine ID. It is the second C argument, not a key in the request dictionary. On macOS 26.6.1 build 25G76:

| Descriptor | Operation |
|------------|-----------|
| 800 (`0x320`) | load (bootstrap a plist path) |
| 801 (`0x321`) | bootout (unload by label) |
| 708 (`0x2c4`) | print / occupancy (named service) |

Interface flags are **6**.

Levin's 2015 capture put `subsystem` and `routine` **inside** the request dictionary and used `xpc_pipe_routine`. On this build those keys are absent, and `launchctl bootstrap` hits `_xpc_pipe_interface_routine` instead. Do not write `subsystem` or `routine`. Treat older tables as the class of protocol, not as drop-in `#define`s.

### Load dictionary (descriptor 800)

The request is an XPC dictionary. The job definition is not in the message body.

```text
handle   uint64   caller's uid (the gui domain handle, not a Mach send right)
type     uint64   8 (gui)
paths    array    one absolute path to a .plist
by-cli   bool     true
```

`type` 8 is the gui domain. Other domain codes exist (`system` 1, `user` 2, `login` 3, `pid` 5). This client only sends `type = 8` and `handle = getuid()`.

`paths` is why a file must exist on disk. The message does not carry an in-memory job dictionary (unlike legacy `launch_msg(SubmitJob)`). launchd `stat(2)`s each path, checks ownership and mode, then opens and parses the file. A missing, relative, or non-`.plist` path fails. Live launchd often returns pipe `rc == 0` with `bootstrap-error == 5` for a bad path. The client treats a non-zero `bootstrap-error` as failure.

Launchd string table for those checks:

```text
Caller specified a plist with bad ownership/permissions: path = %s, caller = %s[%d]
Could not parse plist: path = %s, error = %d: %s
Path not allowed in target domain: type = %s, path = %s ...
```

The file must exist at the moment of the XPC call. After the reply, `atexit` removes the temp directory. launchd has already parsed the definition. `/tmp` and `$TMPDIR` (`/var/folders/...`) are accepted for `gui` on 26.6.1. `~/Library/LaunchAgents` would persist across reboot if not booted out; this client does not write there.

There is no observed "no-plist" twin of this bootstrap routine that still gets launchd as the exec-time parent.

### What launchd does next

On a successful 800, launchd installs the job as `gui/<uid>/<label>`. `oneshot` is `RunAtLoad` + `LaunchOnlyOnce`. `submit` is `KeepAlive`. launchd then `posix_spawn`s `/usr/libexec/xpcproxy` for that job. xpcproxy `exec`s `ProgramArguments[0]` in the same PID. The payload's PPID is 1.

Plain `ProgramArguments` jobs still pass through xpcproxy on macOS 26 (`launchctl print` may show `state = xpcproxy` briefly). That is exec-in-place, so CrowdStrike Falcon `ParentBaseFileName` stays `launchd`. An `XPCService` key switches to the path where xpcproxy forks; CrowdStrike Falcon would then record parent `xpcproxy`.

### Reply

Success is all of: pipe return `rc == 0`, no `"xpc-fault"` string, `"error"` int64 == 0, `"bootstrap-error"` int64 == 0.

### Bootout dictionary (descriptor 801)

Same pipe-create flags and interface flags. No plist file.

```text
handle          uid (uint64)
type            8 (gui)
name            <label>
no-einprogress  true
wait            true
```

A missing label is a launchd error (non-zero). `oneshot` does not KeepAlive; `remove` is still valid on a leftover oneshot definition. The live capture and re-pin recipe are under **Bootout**.

## Plist shape

On-disk format is a binary property list (`bplist00`), not XML. Standard output is the path line plus an XML copy of the same dictionary.

Required: `Label`, `ProgramArguments` (absolute paths), and either `RunAtLoad` + `LaunchOnlyOnce` (`oneshot`) or `KeepAlive` (`submit`). Optional: `StandardOutPath`, `StandardErrorPath`.

Do **not** write an `XPCService` key. Plain `ProgramArguments` jobs still go through `/usr/libexec/xpcproxy` on macOS 26 (`launchctl print` may show `state = xpcproxy` briefly). xpcproxy **exec()s** into the target in the same PID (no fork), so the parent process ID (PPID) stays 1 and CrowdStrike Falcon `ParentBaseFileName` is `launchd`. An `XPCService` key switches to the bundle path where xpcproxy forks and CrowdStrike Falcon would record `xpcproxy`.

## Domain port

Every process inherits `bootstrap_port` (`<servers/bootstrap.h>`). In a `gui/<uid>` session that port is already the gui domain, which is why this client requires that session. It uses the inherited port; it does not call `bootstrap_look_up`.

If a later change needs an explicit lookup:

| Domain | Endpoint |
|--------|----------|
| `user/<uid>` / `gui/<uid>` | `com.apple.xpc.launchd.domain.user.<uid>` |
| `system` | `com.apple.xpc.launchd.domain.system` |
| per-process | `com.apple.xpc.launchd.domain.pid` |

`BOOTSTRAP_SUCCESS` is 0; `BOOTSTRAP_UNKNOWN_SERVICE` is 1102; `BOOTSTRAP_NOT_PRIVILEGED` is 1100.

In the **load dict**, `handle` is the UID as uint64 (not a Mach send right). `xpc_dictionary_set_mach_send` is unused on the successful path.

## Live wire: macOS 26.6.1 build 25G76 (x86_64 and arm64)

**x86_64:** 2026-08-14, System Integrity Protection (SIP) disabled, lldb on `launchctl bootstrap gui/501 <plist>`. Oracle: exit 0; `type = LaunchAgent`, `state = running`.

**arm64:** 2026-08-14, this client. Same constants succeed. SIP off is only required to attach to `/bin/launchctl`.

### Breakpoints that resolve (x86_64)

| Intent | lldb `-n` |
|--------|-----------|
| create pipe | `xpc_pipe_create_from_port` |
| send domain op | `_xpc_pipe_interface_routine` |

The leading double-underscore form (`__xpc_pipe_interface_routine`) did **not** resolve on this slice. C linkage is the single-underscore name.

x86_64 System V: `rdi`, `rsi`, `rdx`, `rcx`, `r8`.
arm64: `x0`--`x4` in the same order.

### `xpc_pipe_create_from_port` (one hit at launchctl init)

| Arg | x86_64 | arm64 | Value |
|-----|--------|-------|--------|
| port | `rdi` | `x0` | Mach port name (example `0x1f03`; varies) |
| flags | `rsi` | `x1` | **4** |

launchctl later reuses a cached pipe from `_os_alloc_once`. A client-built pipe with `(bootstrap_port, 4)` is equivalent for `gui/<uid>` load (functional tests on both slices).

### `_xpc_pipe_interface_routine`: job bootstrap **load**

| Arg | x86_64 | arm64 | Value |
|-----|--------|-------|--------|
| pipe | `rdi` | `x0` | non-NULL `xpc_pipe_t` |
| descriptor (routine ID) | `rsi` | `x1` | **800** (`0x320`) |
| request | `rdx` | `x2` | dict below |
| reply out | `rcx` | `x3` | stack `xpc_object_t *` |
| flags | `r8` | `x4` | **6** |

Request dict (`xpc_copy_description`):

```text
{
  "handle" => <uint64>: <uid>      /* gui domain handle = uid */
  "type"   => <uint64>: 8          /* gui */
  "paths"  => [ "<absolute-plist>" ]
  "by-cli" => <bool>: true
}
```

`xpc_dictionary_get_uint64` on `"routine"` and `"subsystem"` returns 0 because the keys are **absent**. Do not write them. `legacy-load` is also absent on the successful dump.

### Filter rule (same launchctl process has other hits)

| descriptor | Role |
|------------|------|
| 802 | setup: environment for launchctl pid |
| 800 | setup: large **dylib path dictionary** (not job plists) |
| 803 | setup: `type = 5` |
| 207 | unrelated XPC (e.g. logd) |
| **800** + `paths` **array** + `type == 8` + `handle == uid` | **job load** |

Treat a hit as bootstrap load only when all four of those load conditions hold. Descriptor 800 alone is not enough.

### Domain type codes

From the launchctl domain parser (also in Levin / Fitzl+Dalton):

| First segment | `type` | `handle` |
|---------------|--------|----------|
| `system` | 1 | 0 |
| `user` | 2 | uid |
| `login` | 3 | asid / handle |
| `gui` | 8 | uid |
| `pid` | 5 | pid |

This client only sends `type = 8`, `handle = getuid()`.

### Reply

Success:

- pipe return `rc == 0`
- no `"xpc-fault"` string
- `"error"` int64 == 0
- `"bootstrap-error"` int64 == 0

A missing or rejected plist can return `rc == 0`, no `xpc-fault`, `error == 0`, and **`bootstrap-error == 5`**. The client treats a non-zero `bootstrap-error` as failure.

### Product constants (x86_64 and arm64 on 25G76)

```c
#define PIPE_CREATE_FLAGS              4ull
#define PIPE_INTERFACE_FLAGS           6ull
#define BOOTSTRAP_ROUTINE_DESCRIPTOR   800   /* 0x320; pipe arg, not dict */
#define BOOTOUT_ROUTINE_DESCRIPTOR     801   /* 0x321 */
#define PRINT_ROUTINE_DESCRIPTOR       708   /* 0x2c4; occupancy / print */
#define BOOTSTRAP_DOMAIN_TYPE_GUI      8ull
#define BOOTSTRAP_DOMAIN_TYPE_USER     2ull
/* Do not set dict keys "subsystem" or "routine". */
```

If a future build rejects this shape, try in order: `handle` as a Mach send right (`xpc_dictionary_set_mach_send`); pipe-create flags 0.

### Re-pin on a new OS build

SIP must be off (`csrutil status`). Do not attach to `/bin/launchctl` on a production or managed Mac.

```text
lldb -- /bin/launchctl bootstrap gui/$(id -u) /tmp/test.plist
(lldb) breakpoint set -n xpc_pipe_create_from_port
(lldb) breakpoint set -n _xpc_pipe_interface_routine
(lldb) run
# create: x86_64 rdi=port rsi=flags; arm64 x0=port x1=flags
# send:   x86_64 rdi=pipe rsi=descriptor rdx=request rcx=reply* r8=flags
#         arm64  x0=pipe x1=descriptor x2=request x3=reply* x4=flags
# dump:   xpc_copy_description on the request; keep only hits where
#         descriptor is 800, paths is a string array, type is 8, handle is uid
```

### Occupancy / print (descriptor 708)

**x86_64:** 2026-08-16, SIP disabled, lldb on `launchctl print gui/501/com.apple.Finder` and `user/501/com.apple.contactsd`. Wardle listed 708 (`0x2c4`) as process dump. On 25G76 it is a named-service print.

Request (no `subsystem` / `routine`):

```text
handle   uid (uint64)
type     8 (gui) or 2 (user)
name     <label>
shmem    optional; 1048576 bytes (256 pages)
```

launchctl always sends `shmem` (`xpc_shmem_create` on a 1MB `mmap` `MAP_ANON|MAP_SHARED`). The text of `launchctl print` is written into that mapping. Occupancy does not need the text.

Reply, pipe `rc == 0` in every capture:

| Condition | Reply |
|-----------|--------|
| Loaded, with `shmem` | `{ "bytes-written": uint64 }` (Finder 2178; user contactsd 5284) |
| Loaded, no `shmem` | `{ "error": 22 }` |
| Not in that domain | `{ "error": 113 }` (same as `launchctl` exit 113) |

This client sends 708 **without** `shmem` and treats 22 (or `bytes-written > 0`) as loaded and 113 as free. It probes `type` 8 then `type` 2. A label that exists only in `user/<uid>` (for example `com.apple.contactsd`) still fails a gui 800 with `bootstrap-error` 5. A system-domain label (`com.apple.syslogd`) does not block gui 800; type 1 is not probed.

The 708 check runs before `write_plist()`. A taken label exits without creating `$TMPDIR/XXXXXX/XXXXXX.plist` and without printing the payload XML. That temp file is a DFIR artefact: CrowdStrike Falcon records the path in `ASEPFilePath` after unlink. The check exists so a doomed operation does not spill that artefact.

Filter: ignore setup 802 / 800-dylib / 803 / 207. The print hit is **708** plus `name` plus `type` 8 or 2 plus `handle` == uid.

```text
lldb -- /bin/launchctl print gui/$(id -u)/com.apple.Finder
(lldb) breakpoint set -n _xpc_pipe_interface_routine
(lldb) breakpoint modify 1 -c '$rsi == 708'
(lldb) run
# request rdx; after finish, reply *rcx
# also pin user/$(id -u)/<label> (type 2)
```

## What static analysis got wrong

arm64e disassembly of launchctl's subsystem-2 thin wrapper still looks like: write `"subsystem"` / `"routine"` into the dict, return **0** as the descriptor. Call-site candidates on that wrapper include 702, 703, 705, 707, 708 (`0x2BF` = 703 is the per-path loop).

**Do not use 703 as the load ID.** Live load (x86_64 lldb and arm64 client) is descriptor 800 with those keys absent. The product ships that one shape on both slices.

launchctl's own arm64e helper may still write dict keys internally; the **client** does not need to match that path.

## Parentage

A Security Response team reconstructs origin from parent and grandparent telemetry in the EDR. Apple's Endpoint Security logs the job exec with parent `launchd`, so that walk names launchd, not this client and not the calling shell. CrowdStrike Falcon `ParentBaseFileName` is the process-event parent field on that path.

Kernel PPID of a bootstrapped job is 1 (launchd); `launchctl print` shows `type = LaunchAgent` (not `Submitted`). CrowdStrike Falcon for macOS `ProcessRollup2` on 26.6.1 arm64 (2026-08-14), plain `ProgramArguments` + `RunAtLoad` + `LaunchOnlyOnce`, no `XPCService`:

- `ParentBaseFileName = launchd` (process-event parent)
- Auto-Start Extensibility Point path (`ASEPFilePath`) records the plist path (even after unlink)
- `XPC_FLAGS = 1` in the job environment (shell is `0x0`)
- unsigned locally compiled binary is accepted (`CsValidationCategory = 10`)

Process events therefore attribute the payload to launchd. CrowdStrike Falcon still joins the job to the plist via `ASEPFilePath`. `write_plist()` names the file `$TMPDIR/XXXXXX/XXXXXX.plist`. CrowdStrike Falcon copies that string into `ASEPFilePath` on every job `ProcessRollup2`, including KeepAlive respawns, and keeps it after unlink. The random directory name no longer embeds the submitter OS PID (the previous `/tmp/<label>.<pid>.plist` shape did). Embedding the client in another program (different image and argv) drops the distinctive submitter `ProcessRollup2`; ASEP still records this path.

`launch_msg(SubmitJob)` and `SMJobSubmit` (`kSMDomainUserLaunchd` and `kSMDomainSystemLaunchd`) leave the submitter as exec-time parent in CrowdStrike Falcon. They are out of scope. launchd also emits the legacy `launch(3)` deprecation warning on `launch_msg`; the bootstrap path does not.

### launchd.log

For digital forensics and incident response (DFIR), macOS records the same spawn in `/private/var/log/com.apple.xpc.launchd/launchd.log`:

```text
2026-08-14 20:21:43.322799 (gui/501 [100003]) <Notice>: entering bootstrap mode
2026-08-14 20:21:43.323136 (gui/501/com.xspawn.test.oneshot.29998) <Notice>: internal event: WILL_SPAWN, code = 0
2026-08-14 20:21:43.323146 (gui/501/com.xspawn.test.oneshot.29998) <Notice>: service state: spawn scheduled
2026-08-14 20:21:43.323148 (gui/501/com.xspawn.test.oneshot.29998) <Notice>: service state: spawning
2026-08-14 20:21:43.323183 (gui/501/com.xspawn.test.oneshot.29998) <Notice>: launching: speculative
2026-08-14 20:21:43.323787 (gui/501/com.xspawn.test.oneshot.29998 [30002]) <Notice>: xpcproxy spawned with pid 30002
2026-08-14 20:21:43.323809 (gui/501/com.xspawn.test.oneshot.29998 [30002]) <Notice>: internal event: SPAWNED, code = 0
2026-08-14 20:21:43.323812 (gui/501/com.xspawn.test.oneshot.29998 [30002]) <Notice>: service state: xpcproxy
2026-08-14 20:21:43.323891 (gui/501 [100003]) <Notice>: Bootstrap by xspawn[30001] for <private> succeeded (0: )
2026-08-14 20:21:43.323905 (gui/501 [100003]) <Notice>: exiting bootstrap mode
2026-08-14 20:21:43.333270 (gui/501/com.xspawn.test.oneshot.29998 [30002]) <Notice>: service state: running
2026-08-14 20:21:43.333289 (gui/501/com.xspawn.test.oneshot.29998 [30002]) <Notice>: Successfully spawned helloworld[30002] because speculative
2026-08-14 20:21:43.335876 (pid/30002 [helloworld]) <Notice>: uncorking exec source upfront
2026-08-14 20:21:43.335891 (pid/30002 [helloworld]) <Notice>: created
```

The log names the domain (`gui/501`), the label, xpcproxy then `helloworld` in the same PID (30002), and the bootstrap caller. That capture used a longer previous binary name, which launchd truncated to `xspawn[30001]`. `xspawn` is six characters, so a current run prints `xspawn[pid]` in full. Exit lines for the same job record `sent by launchd[1]`. That host log remains after CrowdStrike Falcon's process parent is already `launchd`.

## Bootout

`remove -l <label>` calls `bootout_job()`. Same pipe as load (`xpc_pipe_create_from_port(..., 4)`, interface flags 6). Descriptor **801**. Dict (Phase 3 x86_64 capture; no `subsystem` / `routine` keys):

```text
handle          uid (uint64)
type            8 (gui)
name            <label>
no-einprogress  true
wait            true
```

Bootout is by label; no plist file. A missing label is a launchd error (non-zero). `oneshot` does not KeepAlive; `remove` is still valid on a leftover oneshot definition.

arm64e static notes suggested subsystem **3** on a different trampoline and an uncertain label key (`name` vs `label` vs `service`). The shipped dict is the Phase 3 live capture, not those guesses. Re-pin under lldb if `sw_vers -buildVersion` is not 25G76:

```text
lldb -- /bin/launchctl bootout gui/$(id -u) <label>
(lldb) breakpoint set -n _xpc_pipe_interface_routine
# keep the hit whose dict has name=<label> and type=8
```

Oracle: `launchctl print gui/$(id -u)/<label>` exits non-zero with "Could not find service".

## Design choices

- Temp on-disk plist is required by the `paths` plane.
- Client-built pipe from `bootstrap_port` rather than launchctl's cached connection object.
- One wire on x86_64 and arm64. The Makefile is a native `cc` (no `-arch`).
- Link is `-framework Foundation` (libxpc via libSystem). No private frameworks or entitlements.
- No data store. Backup/restore is N/A.
- No `XPCService` key (see Parentage).
- Serial CLI: one job per invocation. No in-process fan-out. Operators who want many jobs invoke the binary again.

## Tests

- Unit: `label_is_valid`, `write_plist` (binary keys, temp dir, cleanup, path-plus-XML dump), `print_plist_file`, `plist_copy_label`, `bootout_job` / `bootstrap_job` / `job_is_loaded` argument guards.
- Functional: good/bad CLI args; live bootstrap of `tests/functional/fixtures/helloworld` (optional argv[1] = sleep seconds); `launchctl print` must show `type = LaunchAgent`; temp directory gone after success; `remove -l` unloads a submitted job; omitted `-o`/`-e` is `/dev/null`; `load -p` bootstraps a caller-owned plist and leaves it on disk; `oneshot` and `load` print the plist path then XML on stdout; `oneshot` of `/usr/bin/python3 -c` writes `hello world` to `-o` (not skipped); a taken label (second oneshot, or `com.apple.contactsd` in user) fails with empty stdout.

## Limitations and caveats

- Private ABI may drift across macOS major versions. Re-pin if `sw_vers -buildVersion` is not 25G76.
- Root / system domain is out of scope.
- CrowdStrike Falcon for macOS records `$TMPDIR/XXXXXX/XXXXXX.plist` in `ASEPFilePath` (see Parentage). Process parent is still `launchd`.
- Occupancy (708) is not a lock. Another bootstrap can still take the label between the check and 800.
- Launchd writes the spawn to `/private/var/log/com.apple.xpc.launchd/launchd.log` (see Parentage).
- Other domain types (`system`, `user`, `pid`) are untested.
- A future OS may reject `/tmp` plists (`Path not allowed in target domain`); then use a caller-owned path under `$HOME`.

## See also

- README.md: operator CLI
- AGENTS.md: build host versus target
- Levin, [Launchd, I'm coming for you](https://newosxbook.com/articles/jlaunchctl.html) (2015)
- Wardle, *The Art of Mac Malware* Vol. 2
- Fitzl and Dalton, [Mac, Where's My Bootstrap?](https://objectivebythesea.org/v7/talks/OBTS_v7_fCsaba_bDalton.pdf) (OBTS)
