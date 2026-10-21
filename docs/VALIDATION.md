# Public 0.16.0 validation

The public host was built without reading or embedding original game data.
Separate local tests then imported an owned supported installation. Those
private fixtures and outputs are deliberately absent from the repository.

| Check | Result |
| --- | --- |
| ARC bounds, unsafe paths, truncation, altered manifests | Four automated boundary tests passed using synthetic data |
| Import from installed ARC/IBIS/AFS | Imported AFS matched the previously validated resource |
| Imported audio | All 1,063 output files matched the previously validated resources |
| First-run GUI import | Real original process read, resource extraction, native self-test, atomic cache commit passed |
| Packaged launcher | Standalone EXE verified the generated cache without a system Python dependency |
| Native executable smoke test | 3,500 frames; no missing textures, audio failures or unmapped sounds |
| Different native imports | 18,000 confirmed online frames matched across captures from different original sessions |
| Public lobby | Two native lobby windows exchanged Unicode chat, invite and accept, then played 900 matching confirmed P2P frames |
| Presentation | 600 frames across five filters and two aspect ratios; one final visible blit, no exposed black clears |
| Public payload | Allowlisted source/release audit excludes game data, historical captures and credentials |

Network automation uses two processes on the same PC. It is not a two-PC
internet certification or a physical input-to-photon latency measurement.
The earlier private 0.14 rollback was manually tested by the owner over LAN
and mobile 5G; that does not replace testing this public build on two PCs.

The Type-B-inspired CRT implementation still needs visual comparison with
the selected original filter. It is an independent approximation and does
not include the original shader.
