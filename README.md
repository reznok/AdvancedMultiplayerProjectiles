# Advanced Multiplayer Projectiles (AMP)

Unreal Engine plugin for networked projectiles: pooled projectile actors, client-predicted or server-spawned firing, lag-compensated simulated proxies, and server-authoritative hit reporting.

## Installation

1. Clone (or add as a git submodule) into your project at `Plugins/AdvancedMultiplayerProjectiles/`.
2. Install the **NetworkTimeSync** plugin as a sibling at `Plugins/NetworkTimeSync/`. AMP depends on it for lag compensation. It is a separate MIT-licensed plugin (Younes Meziane); the last copy bundled with this repo is available in history at commit `3929718` under `Source/NetworkTimeSync/`.
3. Enable both plugins in your `.uproject` and add the modules to your `*.Build.cs`:
   ```cs
   PublicDependencyModuleNames.AddRange(new string[] { "AdvancedMultiplayerProjectiles", "NetworkTimeSync" });
   ```

## Usage

1. Put exactly one `AAMP_AMPManager` in the world (place it in the map, or spawn it from the game mode on the server). Optionally set `StartingPoolSizes` before its `BeginPlay`.
2. Create projectile actor classes whose root is a collision primitive and which own a `UAMP_ProjectileMovementComponent` (or a subclass).
3. Spawn on the server:
   ```cpp
   FAMP_ProjectileData Data;
   Data.Owner = FiringActor;
   Data.ProjectileClass = AMyProjectile::StaticClass();
   Data.Location = MuzzleLocation;
   Data.Rotation = FireDirection.Rotation().Euler();
   Data.Velocity = FireDirection * Speed;
   Data.LifeTime = 2.f;
   Data.bShowOnClient = true;
   UAMP_ProjectileMovementComponent* Projectile = Manager->SpawnProjectile(Data);
   ```
   Or fire client-predicted through `AAMP_PlayerController::SpawnClientPredictedProjectile` (override the server RPC validation: the default trusts the client).

## Hit reporting

Bind `UAMP_ProjectileMovementComponent::OnProjectileHit`. On the authority, each tick the component sweeps the segment it travelled for `ECC_Pawn` objects (sphere of `HitDetectionRadius`, owner excluded) and broadcasts once per newly hit actor. Run all gameplay from that callback on the server; the projectile itself owns no gameplay state. Unbound projectiles pay nothing.

## Simulated proxies

Server-spawned projectiles are shown on clients through the manager (replicated, always relevant). With `bUseAMP_NetRelevancy`, each `AAMP_PlayerController` within `AMP_NetRelevancyRange` gets a per-client RPC. If the game has **no** `AAMP_PlayerController`s (for example, its controllers derive from another base such as GMC's), spawns and destroys fall back to the manager's NetMulticast without distance culling, so every client still sees the projectiles. Mixing AMP and non-AMP controllers in one game is not supported by the fallback.

## Client-predicted shots fired from game code

When the game's own predicted logic runs on the firing client and on the server (for example a GMAS or GMC ability), let each side spawn its own copy and link them with a key:
1. **Firing client**, on the live (non-replayed) run: `Manager->SpawnLocalPredictedProjectiles(Data)` with `Data.PredictionKey` set. No RPC is sent.
2. **Server**, running the same logic: `Manager->SpawnProjectile(Data)` with the same `PredictionKey`, `IsClientPredicted = true`, `ClientProjectileSpawnTime` = the client's fire time in NetworkTimeSync server time, and `InterpolationType = Snap`, so the authoritative copy is fast-forwarded to the client's timeline (clamped by `MaxPredictionPing`). The firing client never shows the server copy: it is skipped by its owner pawn, or by the live predicted copy with its key (the pawn may be gone, e.g. dead, while its shots still fly).
3. **Other clients** get the server copy through the normal sim-proxy path and catch up.

Keys must be unique per shot across all players.

## End and redirect messages

- End a flight on the server with `EndProjectileAt(Location, EndCode)` (or `DestroyProjectile()`, code 0). Every client copy, including the firing client's predicted copy (matched by `PredictionKey`), snaps to `Location` and ends; read `GetEndCode()`/`GetEndLocation()` in `OnProjectileDestroyed` to play the game's detonation cosmetics.
- Change a flight on the server with `Manager->RedirectProjectile(Projectile, NewVelocity)`. Client copies are re-placed, catch up from the server's time and fire `OnProjectileRedirected`; adaptive prediction is asked again.

## Message batching

The server buffers each tick's spawn, redirect and end messages and sends them from the manager's tick as one call per kind (arrays), in that order, so a projectile spawned and ended within one tick reaches clients spawn first. The manager replicates at 100 Hz and forces a net update on every tick that sends. Why: unreliable multicasts wait for the actor's next net update, and UE keeps at most `net.MaxRPCPerNetUpdate` (default 2) calls to one function per update, dropping the rest with only a `LogRep` Verbose "Too many calls" line. One call per projectile from a 10 Hz `AInfo` lost most end messages and spawn batches under fire: firing clients' predicted copies flew through their targets, and other clients missed shots.

## Adaptive forward prediction

On clients, a remote projectile that threatens the local player is shown ahead by the local one-way delay (`ForwardPredictionScale`), so a dodge on screen matches the server. The component asks `ShouldApplyAdaptivePrediction()` every `AdaptivePredictionCheckInterval` until it answers true. The default answer is a path trace for the local pawn with `AdaptivePredictionObjectTypes` (Pawn by default), `AdaptivePredictionRadius` and `AdaptivePredictionHorizon`; override it with your game's threat test.
