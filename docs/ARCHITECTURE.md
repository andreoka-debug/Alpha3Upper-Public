# Public host architecture

The repository is a clean source-only public branch of the independently
written Alpha3Upper Windows host. It is not a GitHub fork of the unrelated
3sx or CPS3 projects discussed during earlier research. The private prototype
and its locally owned game resources are kept outside this repository.

## Import boundary

`launcher/import_game.py` validates hashes of a user-selected installation,
decodes bounded ARC v7 entries, unwraps the Upper AFS, copies the necessary
sound resources, and generates `audio-map.tsv` locally. It reads the mapped
main module only from a process whose executable path matches that selected
installation. It requires the original program to have been started normally.
No process memory is written or suspended. Unknown builds fail closed.

Only a compatible x64 core at the expected image base is accepted. Code
section hashes validate the supported revision. The collection-side allocation
PRNG is initialized identically by the host so independently imported images
choose the same internal state addresses. Gameplay seed negotiation remains
separate. Network compatibility hashes cover the host executable, immutable
native code section and original AFS, rather than volatile imported memory.
Local import manifests hash all generated resource files for integrity.

The renderer, audio output, input, menus, diagnostics, snapshot journal,
prediction/replay and network transport are host code. Original game routines
are executed only from the user's private local cache; none are compiled into
the public host executable. `public_audio_map.h` parses generated metadata at
runtime so the repository does not need generated game sound tables either.

## Network boundary

WinHTTP talks to the Alpha3Upper lobby/signaling Worker. WebRTC negotiates
ICE/STUN/DTLS/SCTP, then the signaling socket closes and fixed-size game packets
go directly between peers. The lobby closes when a match starts. The public
lobby namespace is independent of the older experimental lobby. There is no
relay fallback, downloaded game code, shared asset service or shared save data.

The lobby admits bounded messages and connection counts, validates nicknames,
limits chat rate, expires invitations and sends each room capability only to
its participant. Chat text is not stored. Nicknames are not authenticated
identities. Deploy your own Worker by adapting `server/wrangler.jsonc` and the
client endpoint; do not deploy over an unrelated CPS2X service.

## Distribution boundary

`tools/audit_public.py` inventories an explicit set of source folders/files,
rejects game data and secrets, and audits the public distribution. Source
archives and Git commits use that allowlist, never the private prototype root.
Generated local caches and historical private ZIPs must never enter Git history.
