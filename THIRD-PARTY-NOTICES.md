# Third-party components

No Capcom game binary, archive, ROM, audio, image, shader or captured game
memory is distributed in this repository or its public Windows package.
The MIT license covers only the independently written host and tools.

The Windows host uses these separately licensed dependencies:

| Component | Source / version | License |
| --- | --- | --- |
| libdatachannel | [4e4f4892](https://github.com/paullouisageneau/libdatachannel/tree/4e4f4892dccb2a57fe3a490d0c9d958de4244e74) | MPL-2.0 |
| libjuice | [5948a416](https://github.com/paullouisageneau/libjuice/tree/5948a4162d37bc213d6051b67ee2876ccc5a99a6) | MPL-2.0 |
| usrsctp | [fec583d5](https://github.com/sctplab/usrsctp/tree/fec583d54493f879d2ae44a743423bf8a04371ab) | BSD-3-Clause |
| plog | [94899e0b](https://github.com/SergiusTheBest/plog/tree/94899e0b926ac1b0f4750bfbd495167b4a6ae9ef) | MIT |
| OpenSSL | build-toolchain version (local build: 3.6.4) | Apache-2.0 |
| stb_vorbis | `src/third-party/stb_vorbis.c`, license included in file | MIT or public domain |
| Python / Tcl / Tk | packaged by the launcher builder | PSF / Tcl-Tk licenses |
| PyInstaller | 6.22.3 | GPL with bootloader exception |

MPL-covered library sources, including all submodules, can be obtained using
the exact source revisions above. `tools/build.py` checks out that unchanged
source before compiling. No modifications to these libraries are required.
Full dependency licenses are included under `licenses/` in the Windows package.
Python's license: https://docs.python.org/3/license.html
PyInstaller's bootloader exception: https://pyinstaller.org/en/stable/license.html

This is an unofficial research host, not an official Capcom release, not a
full source decompilation and not an emulator. An owned supported Windows
installation is required; the import tool does not download a game or bypass
the original program's normal authentication/startup.
