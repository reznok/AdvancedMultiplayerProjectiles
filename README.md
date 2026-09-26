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
