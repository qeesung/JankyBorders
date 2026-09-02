# JankyBorders

<img align="right" width="50%" src="images/screenshot.png" alt="Screenshot">

*JankyBorders* is a lightweight tool designed to add colored borders to
user windows on macOS 14.0+. It enhances the user experience by visually
highlighting the currently focused window without relying on the accessibility
API, thereby being faster than comparable tools.

The upstream project targets macOS 14.0 and newer. Changes in this fork are
validated on macOS 26.5.2; earlier releases are outside this fork's current
verification boundary.

## Usage
### Install
The binary can be made available by installing it through Homebrew:
```bash
brew tap FelixKratz/formulae
brew install borders
```

#### Install this fork as a user service

The local service is packaged as a versioned `JankyBorders.app`. To install the
current checkout instead of the upstream Homebrew build, use:

```bash
make install-service
```

If the installer reports that the Homebrew service is running, stop it once
with `brew services stop borders`, then run `make install-service` again.

On the first installation, this automatically creates one stable,
local-only code-signing identity in your login keychain. macOS may prompt you
to unlock the keychain and approve a user-level trust setting limited to code
signing; no Keychain Access wizard is required. The identity lets macOS
associate screen-recording permission with the same application across updates
and rollbacks. It is not a Developer ID and is not suitable for distributing
the app to other Macs.

Run `make setup-signing` if you want to prepare the identity without installing
the service. `make signing-help` documents the optional manual path. Automatic
setup never changes system/admin trust, never grants every application private
key access, never writes the generated private key to a regular file, and
refuses to overwrite an existing item with the same name.

This builds the current source, installs the signed and versioned app below
`$HOME/.local/opt/jankyborders`, updates `$HOME/.local/bin/borders`, and starts
the user LaunchAgent `io.github.qeesung.jankyborders.local`. It does not modify
the Homebrew Cellar, so the Homebrew build remains available as a rollback.

The service starts `borders` without arguments so that the normal
`bordersrc` lookup described below still applies. Its `PATH` puts
`$HOME/.local/bin` first, which also ensures that a `borders` command inside
the configuration file talks to this fork.

Use exactly one primary startup mechanism. While this LaunchAgent is installed,
do not also start `borders` from `yabairc`, `aerospace.toml`, or
`brew services`; competing instances share the same command port and can make
the service restart repeatedly. Put appearance options in `bordersrc` instead.

After pulling or changing the source, rebuild and atomically switch the local
installation with:

```bash
make update-service
```

Service lifecycle commands are:

```bash
make service-status
make service-stop
make service-start
make service-restart
make rollback-service
make uninstall-service
```

`rollback-service` swaps the `current` and `previous` version links and restarts
the service when a compatible previous service installation exists. A first
install or migration from an older local layout may not have a rollback target
until the next successful update. `service-stop` disables the LaunchAgent until
`service-start` or a new install, including across login sessions.

`uninstall-service` removes the LaunchAgent and the managed command/man-page
links but preserves versioned files under `$HOME/.local/opt/jankyborders` for
manual inspection. Logs are written to `$HOME/Library/Logs/JankyBorders`. To
return to the Homebrew service, run:

```bash
make uninstall-service
hash -r
brew services start borders
```

For a comprehensive overview of all available options and commands, consult the
man page: `man borders`. A rendered version of the man page is available in the
[Wiki](https://github.com/FelixKratz/JankyBorders/wiki/Man-Page).

### Bootstrap with yabai
For example, if you are using `yabai`, you could add:
```bash
borders active_color=0xffe1e3e4 inactive_color=0xff494d64 width=5.0 &
```
to the very end of your `yabairc`. This will start the borders with the
specified options along with yabai.

### Bootstrap with AeroSpace
You could add:
```toml
after-startup-command = [
  'exec-and-forget borders active_color=0xffe1e3e4 inactive_color=0xff494d64 width=5.0'
]
```
to your `aerospace.toml`. This will start borders with the specified options
along with AeroSpace.

### Bootstrap with brew
If you want to run this as a separate service, you could use:
```bash
brew services start borders
```

### Configuring the appearance
You can either configure the appearance directly when starting the borders
process (as shown in "Bootstrap with yabai") or use a configuration file.
The appearance can be adapted at any point in time.

#### Using a configuration file (Optional)
If the primary `borders` process is started without any arguments (or launched
as a service by brew), it searches for and executes the first existing file in
this order:

1. `$XDG_CONFIG_HOME/borders/bordersrc` when `XDG_CONFIG_HOME` is absolute
2. `$HOME/.config/borders/bordersrc`
3. `$HOME/.bordersrc` (legacy fallback)

A relative `XDG_CONFIG_HOME` is ignored, as required by the XDG Base Directory
specification.

An example configuration file could be placed at
`$HOME/.config/borders/bordersrc`. If `XDG_CONFIG_HOME` is set, it must be an
absolute path; place the file at `$XDG_CONFIG_HOME/borders/bordersrc`.
```bash
#!/bin/bash

options=(
	style=round
	width=6.0
	hidpi=off
	active_only=off
	adaptive_color=off
	active_color=0xffe2e2e3
	inactive_color=0xff414550
)

borders "${options[@]}"
```

#### Adapting the focused border to window-edge colors

Adaptive color is disabled by default. It provides two process-wide sampling
modes. `active` keeps the high-contrast behavior and independently chooses
pure black or pure white for each edge. `focus` combines the luminance from all
usable edges and applies one color to the entire focused ring: `0xffe5484d`
(#E5484D) when the window edge is bright overall, or `0xffe1e3e4` (#E1E3E4)
when it is dark overall. The four edges never mix red and gray-white in
`focus` mode. Inactive borders continue to use `inactive_color`.

For a comfortable local focus ring, use:

```bash
borders adaptive_color=focus active_color=0xffe5484d width=5.0
```

`adaptive_color` is a process-wide setting and cannot be combined with
`apply-to=<window-id>`.

Both sampling modes are event-driven: they run after focus, window creation or
unhide, resize, and the final Space-recovery attempt. They intentionally do not
capture continuously, so scrolling and video playback alone do not update the
border. In `focus` mode, invalid edges are excluded and the remaining usable
edges still produce one color for the whole ring. If no edge is usable, the
ring keeps its previous consistent `focus` color when available; otherwise the
entire ring falls back uniformly to `active_color`. If permission is unavailable
or a capture or analysis ultimately fails, the sampled cache is cleared and
the entire ring falls back uniformly to `active_color`. A multi-edge
`active_color` uses its first color for this fallback; a gradient uses its
midpoint color.

Both sampling modes require macOS screen-recording permission. The user service
never opens the permission prompt at login. Check or request permission
explicitly after `make install-service`, then restart the service after
granting it in System Settings. These Make targets deliberately execute the
installed, stably signed app through LaunchServices rather than running the
unsigned build output, so the permission belongs to the same identity used by
the LaunchAgent:

```bash
make screen-capture-status
make request-screen-capture
make service-restart
```

#### Updating the border properties during runtime
If a `borders` process is already running, invoking a new `borders` instance
with any combination of the available options will update the properties of
the already running instance.

## Documentation
Local documentation is available as `man borders` and as a rendered version in
the [Wiki](https://github.com/FelixKratz/JankyBorders/wiki/Man-Page).
