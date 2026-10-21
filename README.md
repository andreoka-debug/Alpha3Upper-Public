# Alpha3Upper Public

An experimental native host for Windows x64, with local resource import from a
supported original installation of **Capcom Fighting Collection 2**.

**No original game files are included in this repository or the public package.**
No ROMs, compiled Capcom code, memory captures, audio, graphics, original shaders
or game logos are bundled, and the project does not download these materials.

## First-time setup

1. Download and run the Windows installer from **Releases**, or extract the
   portable Windows ZIP if you prefer to use the program without installing it.
2. Open `Alpha3Upper.exe` and select your original collection's installation folder.
3. Launch the collection normally through Steam, enter **Alpha 3 Upper**, and
   leave a match or training session running.
4. In the setup wizard, click **Importar da instalação** (Import from installation).
   The wizard checks the supported version, reads the original program's loaded
   code, extracts the required resources, and runs a local validation test.
5. Once the import succeeds, close the original game and click **Jogar** (Play).

The Windows package does not require Python to be installed. Importing does not
use the network or modify the original installation. The original process is
opened with query and read permissions only; it is not suspended, injected into,
or written to.

Imported data is stored in `%LOCALAPPDATA%\Alpha3UpperPublic\runtime-v1\data`,
outside the program folder and repository. **Do not upload this folder to GitHub.**
Subsequent launches verify the local resources. Updates to the original collection
may require additional support; unrecognized versions are rejected.

## Host features

- Arcade, versus, and training modes; regeneration, dummy controls, hitboxes,
  and a frame meter.
- Configurable keyboard and XInput controls for two players, including button
  combination shortcuts.
- Separate audio volume controls, fullscreen, 4:3 / widescreen, and CRT filters.
- Direct IP or P2P invite netplay, with rollback of up to eight frames.
- A shared public lobby with nicknames, chat, and invitations that can be
  accepted or declined.
- Individual invites remain available without joining the lobby.

**Tela → Filtro CRT → CRT tipo B (aproximação)** (Display → CRT filter → CRT
Type B approximation) is an independently implemented CRT-style effect with
RGB triads and scanlines. It does not include the collection's shader and still
requires a side-by-side visual comparison.

**Online → Lobby e chat** (Online → Lobby and chat) opens the public version's
lobby. Use the same package on both PCs. The lobby defaults to one frame of
input delay; individual invites allow settings from zero to eight frames.
Once an invitation is accepted, the lobby disconnects and the match runs directly
over WebRTC. Return to local mode and reopen the lobby to start a new session.
Chat is public and has no server-side history; nicknames are not verified
accounts. Some networks block P2P connections and may require a VPN.

## Compatibility and limitations

This initial version supports a specific revision of the Windows executable
and ARC files, identified by SHA-256 in `launcher/import_game.py`.
It does not provide or download alternative executables or authentication files.

The program executes original x64 routines, imported locally, alongside an
independent frontend. **It is neither a complete decompilation nor an emulator.**
This is an experimental, unofficial project with no affiliation with Capcom.
Differences and bugs may occur. Rollback can cause brief visual corrections;
audio waits for frame confirmation to avoid repetition. Internal network and
performance indicators do not measure total physical input-to-display latency.

## Building

On Windows x64, install Python 3.13+ and MSYS2 UCRT64 with GCC, CMake, Ninja,
and OpenSSL. Then run the following in PowerShell from the project root:

```powershell
python -m pip install pyinstaller==6.22.3
$env:MINGW_BIN='C:\msys64\ucrt64\bin'
python tools/build.py
python tools/audit_public.py
```

The build script fetches a pinned revision of libdatachannel and its submodules.
You can build the frontend without game resources; these are only needed during
local import. `dist/` contains only the launcher, host, notices, and licenses.
To build the host alone, run `python tools/build.py --core-only`.

To create the Windows installer, install Inno Setup 6 from its official site,
then run `python tools/build_installer.py --compiler "PATH\TO\ISCC.exe"`.
The installer is written to `build/installer/`. It includes only the audited
public launcher, host, documentation, and licenses. Uninstalling removes the
program but preserves imported resources and settings in the separate local cache.

## Server and contributions

`server/` contains the lobby and signaling Worker. It does not carry match inputs
or distribute game resources. The public version's lobby endpoint is separate
from the earlier experimental lobby. See the [architecture notes](docs/ARCHITECTURE.md).

Contribute only code, bug descriptions, and diagnostics without original game
data. Do not attach dumps, ARC/AFS files, music, captured game code, or your
`data/` folder to issues, pull requests, releases, or commits. The publication
audit checks these exclusions; `.gitignore` alone is not a guarantee.

Original project code is licensed under MIT; dependencies retain their own
licenses. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). No license in
this project authorizes redistribution of Capcom materials.
