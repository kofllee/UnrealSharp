#requires -Version 5.1
# Build UnrealSharp for a pure Blueprint project on Windows / UE 5.8.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$EngineRoot,
    [Parameter(Mandatory=$true)][string]$HostProject,
    [Parameter(Mandatory=$true)][string]$PluginDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string[]]$Configurations = @("Development", "Shipping"),
    [switch]$BuildPluginOnly
)
$ErrorActionPreference = "Stop"
function Require-File([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing required file: $path" }
}
function Run([string]$program, [string[]]$arguments) {
    Require-File $program
    Write-Host "> $program $($arguments -join ' ')"
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $program" }
}
$EngineRoot = (Resolve-Path $EngineRoot).Path
$HostProject = (Resolve-Path $HostProject).Path
$PluginDirectory = (Resolve-Path $PluginDirectory).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$HostDirectory = Split-Path -Parent $HostProject
$Target = [IO.Path]::GetFileNameWithoutExtension($HostProject)
$UBT = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
$UAT = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
Require-File (Join-Path $PluginDirectory "UnrealSharp.uplugin")
Require-File $HostProject
if (-not $BuildPluginOnly) {
    Run $UBT @("$($Target)Editor", "Win64", "Development", "-Project=$HostProject", "-WaitMutex")
    Require-File (Join-Path $PluginDirectory "Binaries\Managed\net10.0\UnrealSharp.Plugins.dll")
    foreach ($Config in $Configurations) {
        Run $UBT @($Target, "Win64", $Config, "-Project=$HostProject", "-WaitMutex")
        Run $UAT @(
            "PrecompileGameSdk",
            "-Project=$HostProject",
            "-ScriptsForProject=$HostProject",
            "-ScriptDir=$HostDirectory",
            "-UETargetType=Game",
            "-UEBuildConfig=$Config",
            "-TargetPlatform=Win64",
            "-TargetArchitecture=X64"
        )
        Require-File (Join-Path $PluginDirectory "Binaries\Managed\Precompiled\Win64\Game\$Config\net10.0\UnrealSharp.GameSdk.json")
    }
}

# A fast, explicit preflight. Existing SDKs are reused without recompiling UBT/UHT.
Require-File (Join-Path $PluginDirectory "Binaries\Managed\net10.0\UnrealSharp.Plugins.dll")
foreach ($Config in $Configurations) {
    $sdk = Join-Path $PluginDirectory "Binaries\Managed\Precompiled\Win64\Game\$Config\net10.0"
    Require-File (Join-Path $sdk "UnrealSharp.GameSdk.json")
    foreach ($file in @("UnrealSharp.Plugins.dll","UnrealSharp.dll","UnrealSharp.Binds.dll","hostfxr.dll","hostpolicy.dll","coreclr.dll")) {
        Require-File (Join-Path $sdk $file)
    }
}
Write-Host "Managed preflight passed: Editor + $($Configurations -join ', ') Game SDKs exist."

if (Test-Path -LiteralPath $OutputDirectory) {
    throw "OutputDirectory must be new, refusing to mix old binaries: $OutputDirectory"
}
# BuildPlugin's temporary HostProject must NEVER regenerate its own Editor C#
# bindings: the source host already produced compatible, tested managed SDKs.
# Copy only clean sources; avoid 40k+ files from obj, Intermediate and .git.
$buildInput = Join-Path $env:TEMP ("UnrealSharpBuildPluginInput_" + [guid]::NewGuid().ToString("N"))
New-Item -Path $buildInput -ItemType Directory -Force | Out-Null
& robocopy.exe $PluginDirectory $buildInput /E /XD .git .vs Intermediate Saved obj bin /XF *.bak /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) {
    throw "Failed to stage clean BuildPlugin input (robocopy code $LASTEXITCODE): $buildInput"
}
Require-File (Join-Path $buildInput "UnrealSharp.uplugin")
Write-Host "BuildPlugin source staging: $buildInput"

$previousSkip = [Environment]::GetEnvironmentVariable("UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN", "Process")
try {
    $env:UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN = "1"
    Run $UAT @(
        "BuildPlugin",
        "-Plugin=$(Join-Path $buildInput 'UnrealSharp.uplugin')",
        "-Package=$OutputDirectory",
        "-TargetPlatforms=Win64",
        "-Rocket"
    )
} finally {
    [Environment]::SetEnvironmentVariable("UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN", $previousSkip, "Process")
}
$ManagedDest = Join-Path $OutputDirectory "Binaries\Managed"
New-Item -Path $ManagedDest -ItemType Directory -Force | Out-Null
Copy-Item -Path (Join-Path $PluginDirectory "Binaries\Managed\*") -Destination $ManagedDest -Recurse -Force
Require-File (Join-Path $ManagedDest "net10.0\UnrealSharp.Plugins.dll")
Require-File (Join-Path $OutputDirectory "UnrealSharp.Shared.props")
$Descriptor = Get-Content -LiteralPath (Join-Path $OutputDirectory "UnrealSharp.uplugin") -Raw | ConvertFrom-Json
if (-not $Descriptor.Installed) {
    throw "BuildPlugin distribution is not marked as Installed. Refusing a non-precompiled BP release."
}
Require-File (Join-Path $OutputDirectory "Managed\global.json")
Require-File (Join-Path $OutputDirectory "Build\Scripts\UnrealSharp.Automation.csproj")
foreach ($Config in $Configurations) {
    Require-File (Join-Path $ManagedDest "Precompiled\Win64\Game\$Config\net10.0\UnrealSharp.GameSdk.json")
    $NativeManifest = @(Get-ChildItem (Join-Path $OutputDirectory "Intermediate\Build") -Recurse -Filter "*.precompiled" -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match "UnrealGame" -and $_.FullName -match $Config })
    if ($NativeManifest.Count -eq 0) {
        throw "No UnrealGame $Config precompiled manifests in BuildPlugin distribution. Cannot ship to BP-only consumers."
    }
}
Write-Host "Built installed plugin: $OutputDirectory"
Write-Host "Copy the entire folder to <BPProject>\Plugins\UnrealSharp."
Write-Host "Package game in Unreal, then use UnrealSharp Package Project to package C#."
