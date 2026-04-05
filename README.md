**Hyperplayer**

Hyperplayer is a Windows desktop Amiga-MOD player written in C, built around a tracker-style interface inspired by the Amiga ProTracker 2.3.
I made this mainly to use as a base for videocapping playback for demoparty competitions and YouTube videos. I would consider it to be a "Amiga MOD compo player".
It loads and plays module files, shows live playback state, displays pattern data, exposes sample information, and renders multiple synchronized visualizers in the same UI. The app initializes a default hyperplayer.ini on first start, opens a configurable default folder, and is designed around a fixed 1920×1080 interface layout. If the file hyperplayer.ini is missing, it will create it on start with all the default settings.

**What it does**

Hyperplayer is focused on .MOD playback and browsing. The built-in file browser shows folders plus MOD files, lets you move through drives and directories, and loads a selected module directly into the player. Once a file is loaded, the browser is hidden so the visualizer panel takes over that area instead, but can be opened again by clicking on the "File Browser" text.
The File Browser is listing all "*.mod" files aswell as files starting with "mod." as per the Amiga standard.
During playback, Hyperplayer keeps separate OpenMPT instances for rendering audio and for UI/state tracking, so it can show song position, pattern/order/row data, sample usage, waveform previews, and visual meters while the song is playing. Audio is rendered at 44.1 kHz through the Windows waveOut API.

**Main features**

Tracker-style playback view
Hyperplayer renders a 4-channel pattern view with live row highlighting. The first row of each pattern can use a separate color, the current play row is highlighted in white during playback, and the pattern view is designed to stay visually stable around the play position instead of jumping around aggressively.

**Sample list and sample waveform preview**

The player shows all 31 sample slots with number, name, volume, and size. Clicking a sample locks the sample display to that slot. If no sample is selected, the sample waveform view automatically cycles through non-empty samples every 2 seconds.

**Live sample activity highlighting**

The sample list includes animated background highlights that react to currently active samples detected from playback state.

**Multiple synchronized visualizers**

Hyperplayer includes several real-time visual components:
 * Radial tunnel/ring visualizer with starfield and glow effects
 * Spectrum analyzer
 * VU meter
 * 4-channel quadrascope
 * Sample waveform view
 * Live pattern display
 * Configurable look and behavior

A large part of the visual behavior is controlled through hyperplayer.ini, including stereo separation, pattern colors, quadrascope color, VU meter colors and transparency, sample waveform color, spectrum analyzer settings, sample highlight fade behavior, and many radial visualizer parameters. If the INI is deleted, the app recreates it with defaults on startup.

**Controls**

**Space:** play / pause
**Right Ctrl:** restart playback from the current pattern/order position and play
**S:** stop
**Left Arrow:** previous pattern/order
**Right Arrow:** next pattern/order
**Up Arrow:** load and play previous MOD in the current folder
**Down Arrow:** load and play next MOD in the current folder
**Escape:** quit the application

**How to use it**

On first launch, the app creates hyperplayer.ini if it does not already exist.
The browser opens in the folder defined by DEFAULTDIR. If DEFAULTDIR=. then it starts in the same directory as the executable.
Browse to a folder containing .MOD files.
Click a module to load it.
Press Space or use the on-screen Play button to start playback.
While playing, use the pattern view, sample list, waveform display, spectrum analyzer, quadrascope, VU meter, and radial visualizer to inspect the module in real time.

**What it uses**

Hyperplayer is built with plain Win32 C and uses:
 * libopenmpt for module decoding, pattern access, metadata, timing, and playback state
 * Windows waveOut for audio output
 * WIC for loading PNG image assets from memory
 * Embedded resources for the background image, ProTracker font, mouse cursor PNG, and OpenMPT runtime files
 * COM for WIC-related initialization
 * Double-buffered drawing to reduce flicker during UI updates

**Configuration**

Hyperplayer reads settings from hyperplayer.ini. Current configuration sections include:
SYSTEM
AUDIO
SAMPLELIST
PATTERN
QUADRASCOPE
VUMETER
SAMPLEVIEW
SPECTRUMANALYZER
VISUALIZER

This makes it possible to change the startup folder, stereo image, text colors, waveform colors, VU colors, analyzer layout, sample highlight behavior, and the behavior of the radial tunnel visualizer without recompiling.

If you want to compile it yourself, here is the line I use to compile, using w64devkit:
C:\winprog\C\bin\gcc.exe -B C:\winprog\C\bin\ -std=c11 -O2 -Wall -Wextra -municode -mwindows main.c app.c ui.c directory_listing_win32.c action_buttons.c player.c pattern_view.c sample_list.c sample_list_usage_trigger.c sample_display.c spectrumanalyzer.c vumeter.c quadrascope.c tunnelvisualizer.c mousecursor.c urls.c resources.o -o hyperplayer_v1.exe -lgdi32 -lmsimg32 -lole32 -luuid -lwindowscodecs -lwinmm -lshell32 -lm
You will obviously have to change the paths.

**http://www.hyperunknown.net**



