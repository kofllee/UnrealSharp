# Precompiled UnrealSharp for Blueprint-only projects (Win64)

## Validate or resume without repeating successful stages

`BuildPrecompiledForBP.ps1 -PreflightOnly` performs no native or managed builds.
Pass the same EngineRoot, HostProject, PluginDirectory and OutputDirectory as
the normal invocation. It verifies the Game SDK target, configuration, framework,
engine fingerprint and every recorded SHA256 hash.

After a failed BuildPlugin, preserve its `HostProject` directory. Use
`-ResumeHostProject '<failed package>/HostProject/HostProject.uproject'` with a
new OutputDirectory to continue. This mode skips the producer Editor/Game SDK
stages, updates staged native sources and calls UE's own CompilePlugin and
PackagePlugin functions against the existing host. UBT keeps successful objects
and rebuilds only invalidated inputs. It still verifies Editor, Development
and Shipping; these checks do not imply rebuilding unchanged targets.

The source producer must contain the matching already generated managed SDKs.
Use only a disposable host created by BuildPlugin as ResumeHostProject.
Do not use a normal game project as the resume host.

Fresh builds stage sources under `<OutputDirectory>.work/HostProject`, or under
`-WorkDirectory <path>`. Keep that directory and a failed package's HostProject
while diagnosing; remove them after a successful release. The final plugin's
`Intermediate/Build` directory contains required native precompiled manifests
and objects: **do not delete it** from a distributable BP plugin.

The native Core and Utilities modules use explicit private PCHs, avoiding a
dependency on which shared PCH the host happens to select. This is not a
StrictIncludes / no-PCH certification.

The distributor uses UE's CompilePlugin and PackagePlugin functions directly
so that an existing synthetic host can be resumed. Game Core and Utilities are
precompiled without native debug type records: MSVC's embedded PCH type records
contain producer absolute paths and otherwise cause LNK2011 after relocation.
Editor debugging is unchanged; managed debugging remains available.

The output descriptor sets EnabledByDefault=false. Explicitly enable UnrealSharp
in the consumer's Plugins window or .uproject. This makes UE detect the native
plugin and generate its normal temporary target under Intermediate/Source.
The consumer remains BP-only; an installed MSVC/Windows SDK toolchain is still
needed to link the game executable. Distributed Game Build.cs rules use
bUsePrecompiled rather than compiling the plugin's native source again.

Installed pure-BP consumers reuse Editor/Game SDKs during native UHT. Game SDK
identity and hashes are checked first. Projects that add native project modules
continue through normal glue generation. Reflected APIs from additional native
plugins must be included in the producer SDK; this fast path does not generate
new bindings for them.

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
