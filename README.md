# zypak-edge

## [**I'm so stupid**](https://github.com/TheEvilSkeleton/flathub/commit/e8c77c38dfcbc76f65cbabb7a6f58d1bc85501e1)

Allows you to run [Microsoft Edge](https://github.com/TheEvilSkeleton/flathub/tree/com.microsoft.Edge) in a sandboxed environment
by using LD_PRELOAD magic and a redirection system that redirects Chromium's sandbox to use
the Flatpak sandbox.

## Basic usage

This requires your Flatpak to be using:

- `org.freedesktop.Platform` / `Sdk` version `19.08` or later.
- `org.electronjs.Electron2.BaseApp` as your base app. Recent releases include Zypak
  built-in.

Now, instead of running your Electron binary directly, call it via
`zypak-wrapper PATH/TO/MY/ELECTRON/BINARY`.

## Experimental functionality

### Forcing use of the file chooser portal

For Electron apps, you can run them with `ZYPAK_FORCE_FILE_PORTAL=1` to force the use of the file
chooser portal over the custom GTK-drawn dialogs. This comes with some very notable trade-offs:

- Selecting directories does not work on any systems with xdg-desktop-portal < 1.5.2.
- The methods for overriding this are very hacky and will likely not work in all cases.
  - In particular, any apps that use a transient file chooser will *not* work.

For basic file dialog use, these may be fine.

## Debugging

- Set `ZYPAK_DEBUG=1` to enable debug logging.
- Set `ZYPAK_STRACE=all` to run strace on the host and child processes.
  - To make it host-only or child-only, set `ZYPAK_STRACE=host` or `ZYPAK_STRACE=child`, respectively.
  - If only some child processes should be searched, use `ZYPAK_STRACE=child:type1,type2,...`, e.g.
    `ZYPAK_STRACE=child:ppapi,utility` to trace all children of `--type=utility` and `--type=ppapi`.
  - Set `ZYPAK_STRACE_FILTER=expr` to pass a filter expression to `strace -e`.
  - In order to avoid arguments being ellipsized, set `ZYPAK_STRACE_NO_LINE_LIMIT=1`.
- Set `ZYPAK_DISABLE_SANDBOX=1` to disable the use of the `--sandbox` argument
  (required if the Electron binary is not installed, as the sandboxed calls will be unable to locate the Electron binary).

## How does it work?

Zypak works by using LD_PRELOAD to trick Chromium into thinking its SUID sandbox is present and still
setuid, but all calls to it get instead redirected to another binary: Zypak's own sandbox.

This sandbox has two strategies to sandbox Chromium:

### The mimic strategy

The *mimic strategy* works on the majority of Flatpak versions. It works by mimicking the zygote
and redirecting all the incoming commands to actually become `flatpak-spawn` commands, then
returning those PIDs as the results of the "fork". This *does* have the side effect of slower
startup and higher memory usage, since there is no true zygote running, and thus this is only used
where the spawn strategy (see below) does not work.

### The spawn strategy

The *spawn strategy* a far better implementation, available on all Flatpak versions 1.8.2+. (Flatpak
1.8.0 and 1.8.1 are not really supported.) It relies on two particular new features in 1.8.0:
`expose-pids` and `SpawnStarted`:

- `expose-pids` lets the process that opens a new sandbox see the PIDs of the sandboxed processes.
  This essentially means it behaves much like using user namespaces to perform sandboxing and allows
  Chromium to see the true PIDs of its child processes rather than trying to use an intermediary
  (`flatpak-spawn` in the mimic strategy).
- `SpawnStarted` is emitted when a sandboxed process fully starts, and it passes along the PID that
  can be used for the parent to reach the sandboxed children.

In this strategy, the zygote is no longer mimicked; rather, the actual zygote is run sandboxed, just
like Chromium's official sandboxes work. The only difference is, the Flatpak sandbox is used instead
of Chromium's setuid or namespace sandboxes.

This is a bit messy because Flatpak's sandboxing APIs all use D-Bus, so a new D-Bus session must be
"injected" into the main Chromium process, which then runs in a separate thread and handles all the
sandbox functionality. When the separate zypak-sandbox binary is started, it talks to this
"supervisor" thread via a local socket pair, asking it to run the sandboxed process and staying
alive until the sandboxed process completes. Meanwhile, the supervisor thread will swap out the
sandbox PID for the true sandboxed process PID.

### Rough layout of execution

- When you use `zypak-wrapper`, it sets up the paths to the Zypak binary and
  library directories and then calls `zypak-helper`.
- `zypak-helper` will set up the LD_PRELOAD environment and start the main
  process.
    - If the spawn strategy is being used, a supervisor thread is started to manage the
      sandboxed processes and communication with Flatpak.
- When Chromium attempts to launch a sandboxed process, `zypak-sandbox` is used as the
  sandbox instead of the SUID sandbox, and it then does one of the following:
  - If the mimic strategy is being used, Zypak's mimic zygote will run, replacing
    the true zygote. All the zygote messages get handled, and process forks instead
    start a new process via `flatpak-spawn`.
  - If the spawn strategy is being used, the sandbox will send a message to the supervisor
    to start a new sandboxed process, then wait for the sandboxed process to exit.
