borders(1)

# NAME

JankyBorders - A window border system for macOS

# SYNOPSIS

*borders [options]*

*borders --screen-capture-status*

*borders --request-screen-capture*

# DESCRIPTION

*JankyBorders* is a lightweight tool designed to add colored borders to
user windows on macOS 14.0+. It enhances the user experience by visually
highlighting the currently focused window without relying on the accessibility
API, thereby being faster than comparable tools.

The upstream project targets macOS 14.0 and newer. Changes in this fork are
validated on macOS 26.5.2; earlier releases are outside this fork's current
verification boundary.

# OPTIONS

*style=<style>*
	Specifies the style of the border (either _round_, _square_, _uniform_, or
	_none_)

*position=<position>*
	Controls where the border is drawn relative to the window edge. _inside_
	draws entirely over the window, _center_ straddles the edge, and _outside_
	draws entirely beyond it. The default _auto_ preserves the traditional
	appearance, but moves individual edges inside when they would be clipped by
	the display boundary.

*active_color=<color>*
	Sets the border color for the focused window (format: _0xAARRGGBB_). For
	instance, active_color=0xff00ff00 creates a green border.

*adaptive_color=<mode>*
	Controls automatic contrast colors for the focused window. The default
	_off_ preserves _active_color_. _active_ samples pixels just inside each
	window edge and independently selects pure black or pure white for each
	edge. _focus_ instead combines luminance from all usable edges and applies
	one color to the entire focused ring: _0xffe5484d_ (#E5484D) when the window
	edge is bright overall, or _0xffe1e3e4_ (#E1E3E4) when it is dark overall.
	The four edges never mix red and gray-white in _focus_ mode. Inactive windows
	continue to use _inactive_color_. A recommended local example is: borders
	adaptive_color=focus active_color=0xffe5484d width=5.0.

	Both sampling modes require macOS screen-recording permission and are
	event-driven after focus, window creation or unhide, resize, and the final
	Space-recovery attempt. Scrolling and video playback alone do not trigger a
	new sample. In _focus_ mode, invalid edges are excluded and the remaining
	usable edges still produce one color for the whole ring. Once a consistent
	color exists, a partial-edge sample cannot replace it; a different color must
	be confirmed by two complete successful samples. If no edge is usable or a
	capture or analysis temporarily fails, the ring keeps its previous consistent
	color until a later successful sample. Before the first successful sample, the
	entire ring falls back uniformly to _active_color_.
	Losing screen-recording permission still disables sampling and returns to that
	fallback. It uses the first color from a multi-edge
	_active_color_, or the midpoint of a gradient. This process-wide setting cannot
	be combined with
	_apply-to=<window-id>_.

*inactive_color=<color>*
	Sets the border color for all windows not in focus.

*background_color=<color>*
	Sets the background fill color for all windows (only _0xAARRGGBB_ arguments supported).

*width=<float>*
	Determines the width of the border. For example, width=5.0 creates a border
	5.0 points wide.

*hidpi=<boolean>*
	If set to _on_, the border will be drawn with retina resolution.

*active_only=<boolean>*
	If set to _on_, only the focused window receives a border. This reduces
	the number of backing surfaces kept alive and can lower memory usage.
	Since inactive borders are not kept alive in this mode, _inactive_color_
	has no visible effect. The setting can be changed at runtime.

*ax_focus=<boolean>*
	If set to _on_, the (slower) accessibility API is used to resolve the
	focused window. Enabled automatically if the (parent) process has
	accessibility permissions. Improves compatibility with other tools
	modifying window properties on a system level (e.g. yabai).

*blacklist=<application_list>*
	The applications specified here are excluded from being bordered. For
	example, blacklist="Safari,kitty" excludes Safari and kitty from being
	bordered.

*whitelist=<application_list>*
	Once this list is populated, only applications listed here are considered
	for receiving a border. If the whitelist is empty (default) it is inactive.

*--screen-capture-status*
	Prints whether screen-recording permission is granted. This utility exits
	without starting a border instance or requesting Accessibility permission.

*--request-screen-capture*
	Explicitly asks macOS for the screen-recording permission used by
	_adaptive_color=active_ and _adaptive_color=focus_. This utility exits
	without starting a border instance or requesting Accessibility permission.
	After granting access in System Settings, restart a running local service with
	_make service-restart_. The LaunchAgent never invokes this option at login.
	When using the Make target, install the signed app first; the target launches
	the app through LaunchServices so permission is attached to the service
	identity.

If an instance of *borders* is already running, subsequent invocations will
update the existing process with the new arguments.

If no instance of *borders* is running and no arguments are supplied, we try
to execute the first configuration file found at
$XDG_CONFIG_HOME/borders/bordersrc (when XDG_CONFIG_HOME is absolute),
$HOME/.config/borders/bordersrc, or the legacy $HOME/.bordersrc path.

# NOMENCLATURE

*<float>*
	A floating point number.

*<boolean>*
	Either _on_ or _off_.

*<mode>*
	For _adaptive_color_, either _off_ (the default), _active_, or
	_focus_.

*<style>*
	Either _round_, _square_, _uniform_, or _none_. The _none_ style disables
	borders without stopping the running process, so a later style update can
	enable them again.

*<position>*
	Either _auto_, _inside_, _center_, or _outside_.

*<color>*
	A color formatted as _0xAARRGGBB_, where _A_ is the alpha channel, _R_ the
	red channel, _G_ the green channel and _B_ the blue channel. All
	constituents take a base 16 number, i.e. _0-F_.

	For active and inactive borders, a comma-separated list can color each
	edge independently: one color applies to every edge, two colors apply to
	the top and bottom edges (the other edges are hidden), and four colors
	apply to the top, right, bottom, and left edges. Adjacent colors meet at
	45-degree corners. Per-edge colors can also be wrapped in _glow(...)_.
	Background colors remain single-color only.

	The color argument can take the special values: ++
	_gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB)_ ++
	_gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB)_ ++
	_glow(0xAARRGGBB[,0xAARRGGBB...])_ ++
	_glow(gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB))_ ++
	_glow(gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB))_ ++
(You might need to quote these arguments depending on your shell)

	The glow color for a gradient is the alpha-weighted blend of its two
	endpoint colors.

*<application_list>*
	A comma separated list of application names. This argument should be
	quoted.

# LOCAL SERVICE

When building this fork from its source checkout, _make install-service_
performs the complete first-time setup. It automatically creates a stable,
local-only code-signing identity in the login keychain. macOS may prompt you to
unlock the keychain and approve a user-level trust setting limited to code
signing; the Keychain Access certificate wizard is not required.

Use _make setup-signing_ to prepare the identity without installing the
service. _make signing-help_ documents the optional manual path, and
_make signing-status_ validates the result. Automatic setup never changes
system/admin trust, never grants every application private-key access, never
writes the generated private key to a regular file, and refuses to overwrite
an existing item with the same name.

The installer packages a versioned _JankyBorders.app_. The stable application
identity is intended to preserve macOS screen-recording permission across
compatible updates and rollbacks. It is not a Developer ID and is not suitable
for distribution to other Macs.

Manage the per-user LaunchAgent with:

	_make install-service_ ++
	_make update-service_ ++
	_make rollback-service_ ++
	_make service-status_ ++
	_make service-start_ ++
	_make service-stop_ ++
	_make service-restart_ ++
	_make uninstall-service_

# BUGS

Report them at https://github.com/FelixKratz/JankyBorders/issues.

# MAINTAINERS

Felix Kratz <felix.kratz@tu-dortmund.de>
