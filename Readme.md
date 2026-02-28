# Fork Info

This is a forked version MPC Video Renderer that adds support for page flipped 3D video.
It also supports the Open3DOLED on screen trigger boxes and PC serial sync protocol.
Prebuilt filters are available from the main open3doled repository releases page https://github.com/open3doled/open-3d-oled/releases.
Although this renderer path is still experimental, it is currently the preferred playback method on Windows because it has better performance plus built-in calibration and the most important features from the Python-based 3DPlayer.

## Open3DOLED Install Notes

* Prebuilt `MpcVideoRenderer64.ax` / `MpcVideoRenderer.ax` binaries are published on the open3doled release page.
* The helper scripts in `distrib` (`Install_MPCVR_64.cmd` / `Uninstall_MPCVR_64.cmd` and 32-bit variants) register or unregister the DirectShow filter.
* On some players/systems this is enough. On others, the player may continue loading a private copy from its own install folder.
* If registration alone does not switch the active renderer, replace the player-side `MpcVideoRenderer64.ax`/`MpcVideoRenderer.ax` with this forked build (keep a backup so you can roll back quickly).
* After replacement, run the matching install script again to make sure COM registration points at the expected file.

## Open3DOLED 3D Settings Menu Features

The Open3DOLED build adds a dedicated 3D settings page with display + emitter controls in one place.

### Playback and Display Controls

* `Enable pageflipping` toggle for software pageflipped output (runtime on/off).
* `Target framerate` with support for fractional rates; `0` uses display refresh rate automatically.
* Automatic layout handling for full-SBS/full-TAB and half-SBS/half-TAB content, including half-format default selection.
* `Flip eyes` support in renderer output.
* `Zoom` control (with black borders) applied to final rendered output.
* `Parallax` control to shift perceived depth by applying opposite per-eye horizontal offsets.
* OSD status output including current eye/layout/rate/drive mode/COM and geometry diagnostics (`In`, `Vid`, `Ren`, `SrcRect`, AR values).

### Optical Trigger Box Controls (Drive Mode 0)

* Full Open3DOLED trigger-box controls (size, spacing, border, corner, position, brightness, display size).
* Trigger boxes render whenever optical drive mode is active.
* Calibration mode adds expanded black background + reticle overlay.
* Calibration OSD/help supports live tuning hotkeys with immediate in-render updates.

### Emitter Controls (Drive Mode 1 and Optical)

* `Drive mode` control (`0` optical / `1` PC serial, with `Other` shown for unsupported runtime values).
* COM port selection (`AUTO` + detected ports), connect/disconnect, and port list refresh.
* Auto-connect with reconnect handling after USB unplug/replug, plus user override (`Disable auto-connect`).
* Read/apply/save workflow for emitter values (`Read`, `Apply`, `Save EEPROM`).
* Firmware version readback from emitter.
* Firmware update dialog support integrated in renderer properties.

### Config and Persistence

* Display settings are stored in JSON (`last_display_settings.json`).
* Local emitter connection preferences are stored separately (`local_emitter_settings.json`).
* Default location for these files is `%APPDATA%\\MPCVR`.
* Emitter profile import/export via JSON is supported from the settings page.

### Runtime Warnings and Diagnostics

* OSD + UI warnings for disconnected serial link and reconnect status.
* Dirty-state warnings when emitter settings are changed but not yet applied to emitter.
* Dirty-state warnings when emitter settings are applied but not yet saved to EEPROM.
* Timing guidance warnings (for example average timing mode / target frametime mismatch conditions).
* Global hotkeys for quick toggles (pageflip, OSD, calibration, properties, flip eyes) plus calibration-specific hotkeys shown in the calibration help overlay.

# MPC Video Renderer

MPC Video Renderer is a free and open-source video renderer for DirectShow. The renderer can potentially work with any DirectShow player, but full support is available only in the MPC-BE. Recommended MPC-BE 1.8.2.136 or newer.

## Key features

* Can work with DXVA2 and Direct3D 11 hardware decoder.
* DVXA2 and Direct3D11 Video Processor with hardware de-interlacing for NV12, YUY2, P010 formats.
* Shader video processor for various YUV, RGB and grayscale formats.
* Various frame resizing algorithms, including Super Resolution.
* Subtitle and OSD display.
* Rotation and flip of the video frame.
* Dithering when the final color depth is reduced from 10/16 bits to 8 bits.
* HDR video support (HDR10, HLG and partially Dolby Vision).
* Automatic HDR to SDR conversion.
* Transferring HDR10 data to the display.

## Minimum system requirements

* An SSE2-capable CPU
* Windows 7¹ or newer
* DirectX 9.0c video card

¹For Windows 7, you must have D3DCompiler_47.dll file. It can be installed via update KB4019990.

## Recommended system requirements

* An SSE2-capable CPU
* Windows 10 or newer
* DirectX 10/11 video card

## License

MPC Video Renderer's code is licensed under [GPL v3].

## Links

[Nightly builds](https://github.com/Aleksoid1978/VideoRenderer/wiki/Nightly-builds)

[Topic in MPC-BE forum (Russian)](https://mpc-be.org/forum/index.php?topic=381)

[MPC-BE](https://github.com/Aleksoid1978/MPC-BE)

## Donate

<https://mpc-be.org/forum/index.php?topic=240>
