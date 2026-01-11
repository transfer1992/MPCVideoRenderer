# Fork Info

This is a forked version MPC Video Renderer that adds support for page flipped 3D video.
It also supports the Open3DOLED on screen trigger boxes and PC serial sync protocol.
Releases will be built and released on the main open3doled repository releases page https://github.com/open3doled/open-3d-oled/releases.

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
