# Precompiled UnrealSharp for Blueprint-only projects (Win64)

The native precompiled plugin must contain **both Editor and Game** binaries,
and its managed payload must include a **target-specific Game SDK**. Editor DLLs
are not a substitute for Game DLLs or precompiled native receipts.

## Create the installed plugin

Use a C++ host project with this source fork under Plugins/UnrealSharp,
installed UE 5.8, MSVC Unreal toolchain, and the .NET 10 SDK.

PowerShell:

    .\Scripts\BuildPrecompiledForBP.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -HostProject 'C:\Dev\Host\Host.uproject' -PluginDirectory 'C:\Dev\Host\Plugins\UnrealSharp' -OutputDirectory 'C:\Dev\UnrealSharpDistribution'

The script builds Editor, native Game Development and native Game Shipping;
then it runs PrecompileGameSdk for each configuration, calls Unreal BuildPlugin,
copies target-specific managed artifacts, and verifies the native UnrealGame
.precompiled files for each requested configuration.

The managed SDK is stored separately from Editor assemblies:

    Binaries/Managed/net10.0/                       Editor
    Binaries/Managed/Precompiled/Win64/Game/Development/net10.0/
    Binaries/Managed/Precompiled/Win64/Game/Shipping/net10.0/

PrecompileGameSdk requires UHT-generated Game source from the C++ host target.
It publishes a self-contained .NET runtime, checks required DLLs and writes
UnrealSharp.GameSdk.json with UE Build.version fingerprint and SHA256 file
hashes. A BP-only project never needs to run a native Game UHT pass to recreate
these base bindings.

Copy the complete output of BuildPlugin to BPProject/Plugins/UnrealSharp.
Producer and consumer should use the exact same installed UE build.

## Package the BP-only consumer

1. Open the BP project in UnrealEditor. Editor C# builds normally, using
   Binaries/Managed/net10.0 files shipped with the plugin.
2. Package the game in Unreal as usual for Win64 Development or Shipping.
3. In the UnrealSharp Editor toolbar select Package Project and choose the
   archived folder containing the executable. This invokes the *separate*
   UnrealSharp PackageProject UAT command for the Game target.
4. The managed packager copies the matching verified Game SDK, publishes user
   code against Game references, builds project-specific Game glue if present,
   checks LoadOrder DLLs and creates UnrealSharpBuild.flag after validation.

Native BuildCookRun does NOT currently invoke UnrealSharp PackageProject
automatically. A native EXE alone is not yet a complete C# packaged game.

## Constraints and verification

- Do not copy an UnrealEditor-*.dll in place of a native UnrealGame artifact.
- If BuildPlugin does not produce native Game manifests for both configurations,
  the builder stops rather than silently delivering an Editor-only plugin.
- If Game SDK is missing, mismatched or corrupted, packaging aborts. There is
  no fallback to Editor DLLs. Missing glue DLLs are fatal.
- Native third-party plugins with reflected APIs that were not enabled during
  Game UHT compilation may require additional Game-specific generated glue.
- This script targets Windows Win64. It does not claim validated Mac/Linux
  Blueprint-only Game releases.
- Test in a clean BP project and on a Windows machine without .NET SDK:
  Editor C# class compilation, Game Development/Shipping packaging, managed
  BeginPlay, and a deliberate missing Game DLL that must cause failure.
- The source patch alone does not prove that the UE 5.8/Game smoke tests passed.


## Fast retry without building Editor / Game again

After a successful initial native build and successful Development/Shipping
PrecompileGameSdk steps, use the following switch for the next attempt:

    .\Scripts\BuildPrecompiledForBP.ps1 -EngineRoot "C:\Program Files\Epic Games\UE_5.8" -HostProject "C:\Dev\Host\Host.uproject" -PluginDirectory "C:\Dev\Host\Plugins\UnrealSharp" -OutputDirectory "C:\Dev\UnrealSharpDistribution_2" -BuildPluginOnly

This validates existing managed SDKs and calls only native BuildPlugin. Use a
**new** OutputDirectory on retry: a failed BuildPlugin leaves temporary HostProject
files there, which must not be mixed into a new release.

BuildPlugin uses a clean source staging copy excluding .git, Intermediate, obj,
bin and backups. The builder sets
UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN=1 only for that UAT subprocess;
the UnrealSharp UHT exporter skips the unnecessary managed C# glue export
while native Unreal UHT still runs and precompiles C++ modules.

The skip is intentionally scoped to BuildPlugin only. Normal Editor and Game
host UHT generation and C# SDK compilation are unaffected. Do not set this
environment variable globally. Pre-existing Engine Editor and Game SDKs
must already be present before using -BuildPluginOnly.
