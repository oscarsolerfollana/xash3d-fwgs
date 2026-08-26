**Title:** Listenserver host bypasses `cl_updaterate` and `cl_cmdrate`, handing the game library ~3x the updates it expects (breaks Firearms' bipod)

---

### Version / environment

* Engine: `0.21`, master at 633203e
* OS/arch: Windows 11, `win32-i386`, built with MinGW-w64 (GCC 16.2)
* Mod: Firearms 3.0 (retail files, verified identical by md5 to a Steam GoldSrc install)

### Summary

On a listenserver, the local (loopback) client is deliberately exempted from the send-rate
limiter so that singleplayer feels smooth. The exemption is not restricted to singleplayer,
so the **host of a multiplayer listenserver** also gets it. The game library then receives
its per-snapshot and per-packet entry points at the host's frame rate instead of at
`cl_updaterate` / `cl_cmdrate`.

Mods whose logic in those entry points is not idempotent drift because of it. In Firearms
the visible symptom is that the machine gun bipod does not tighten the spread cone at all,
while it does on GoldSrc. Setting `host_limitlocal 1` makes it behave exactly like GoldSrc,
which is what pointed at the rate.

### The two code paths

Both need to be considered; fixing only the first one is not enough (verified).

1. Server -> client, snapshots:
   [`engine/server/sv_frame.c:835`](https://github.com/FWGS/xash3d-fwgs/blob/633203e/engine/server/sv_frame.c#L835)

   ```c
   if( !host_limitlocal.value && NET_IsLocalAddress( cl->netchan.remote_address ))
       SetBits( cl->flags, FCL_SEND_NET_MESSAGE );
   ```

   This skips `next_messagetime`, so `pfnUpdateClientData` and `pfnGetWeaponData` (called
   once per snapshot from `SV_WriteClientdataToMessage`) run at frame rate.

2. Client -> server, command packets:
   [`engine/client/cl_main.c:924`](https://github.com/FWGS/xash3d-fwgs/blob/633203e/engine/client/cl_main.c#L924)

   ```c
   if( cl.maxclients == 1 || ( NET_IsLocalAddress( cls.netchan.remote_address ) && !host_limitlocal.value ) || ( ... ))
       pcmd->heldback = false;
   ```

   The `cl.maxclients == 1` term already covers singleplayer; the loopback term makes the
   host send one packet per frame in multiplayer too.

`sv_main.c:412` and `sv_main.c:430` use the same cvar but are already gated on
`svs.maxclients == 1`, so they are not affected.

### Scope

| scenario | affected |
|---|---|
| listenserver, the host | **yes** |
| listenserver, connecting players | no |
| dedicated server, anyone | no |
| singleplayer | no (and should stay exempt) |

Severity scales with the host's frame rate, since `sv.frametime == host.frametime` on a
listenserver. At 20 fps the rate would come out right by accident.

### Measurements

M60, standing still, ~30 round burst without the bipod, pause, deploy, another ~30 round
burst. Reproduced in two independent sessions. Spread values are the mod's own dispersion
parameters, read with a locally instrumented build.

| | GoldSrc | Xash3D client | Xash3D server |
|---|---|---|---|
| spread, no bipod | 0.0406 | 0.0386 | 0.0386 |
| spread, bipod deployed | **0.0105** | 0.0348 | **0.0598** |
| recoil per shot, no bipod | 1.599° | 1.639° | |
| recoil per shot, bipod | 0.200° | 0.200° | |
| rate of fire | 7.48/s | 7.49/s | |
| **snapshots/s** | **12.8** | **60.0** | |

Without the bipod, the Xash3D client and server agree to the last decimal (the RNG is
seeded deterministically). With the bipod they diverge. The state that goes over the wire
is identical between engines in both cases, so this is not a delta/encoding problem.

*Known bias in that comparison:* the GoldSrc column is a Xash3D client connected as a
**remote** client to a Steam Firearms server, so 12.8/s is GoldSrc rate-limiting an
ordinary client, not necessarily what GoldSrc does for its own local player. It was enough
to locate the difference, but it does not by itself prove what a GoldSrc host does.

### Steps to reproduce

1. `xash3d -game firearms +maxplayers 16 +map ps_island` (any listenserver with
   `maxplayers > 1`; you have to be the host)
2. Pick up an M60, stand still facing a wall, fire a long burst - note the spread
3. Deploy the bipod (`V`), fire another burst - the cone does not tighten
4. `host_limitlocal 1` in the console, repeat step 3 - now it tightens, as on GoldSrc

### Which fix would you prefer?

I have both working locally and can send a PR for whichever you prefer:

1. **Restrict the shortcut to singleplayer** (`svs.maxclients == 1` on the server side,
   drop the redundant loopback term on the client side). Two small hunks. The cost is that
   the listenserver host loses some smoothness and immediacy - the same thing anyone
   connected to a real server already lives with, but it is a behaviour change for
   everybody to fix a mod.
2. **Keep sending at frame rate, but stop calling the game library more often than
   `cl_updaterate` warrants.** Attacks the cause without the tradeoff, but touches more
   code and I would rather agree on the shape before writing it.

Happy to attach `-dev 2 -log` output; the numbers above come from extra instrumentation,
so I did not want to paste a log from a patched engine as if it were stock.
