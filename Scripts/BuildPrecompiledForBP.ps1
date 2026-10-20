#requires -Version 5.1
# Build UnrealSharp for a pure Blueprint project on Windows / UE 5.8.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$EngineRoot,
    [Parameter(Mandatory=$true)][string]$HostProject,
    [Parameter(Mandatory=$true)][string]$PluginDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateSet("Development", "Shipping")][string[]]$Configurations = @("Development", "Shipping"),
    [switch]$BuildPluginOnly,
    [switch]$PreflightOnly,
    [string]$WorkDirectory,
    [string]$ResumeHostProject
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
Require-File $UBT
Require-File $UAT
if (-not $PreflightOnly -and (Test-Path -LiteralPath $OutputDirectory)) {
    throw "OutputDirectory must be new: $OutputDirectory"
}
if (-not $WorkDirectory) { $WorkDirectory = $OutputDirectory + ".work" }
$WorkDirectory = [IO.Path]::GetFullPath($WorkDirectory)
$pluginRoot = $PluginDirectory.TrimEnd('\') + '\'
foreach ($destination in @($OutputDirectory, $WorkDirectory)) {
    if ($destination -eq $PluginDirectory -or $destination.StartsWith($pluginRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Build output/work directories must be outside the source plugin: $destination"
    }
}
if (-not $BuildPluginOnly -and -not $PreflightOnly -and -not $ResumeHostProject) {
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
    foreach ($file in @("UnrealSharp.Plugins.dll","UnrealSharp.dll","UnrealSharp.Binds.dll","UnrealSharp.Core.dll","UnrealSharp.Plugins.deps.json","UnrealSharp.Plugins.runtimeconfig.json","hostfxr.dll","hostpolicy.dll","coreclr.dll")) {
        Require-File (Join-Path $sdk $file)
    }
    $manifest = Get-Content -LiteralPath (Join-Path $sdk "UnrealSharp.GameSdk.json") -Raw | ConvertFrom-Json
    $engineIdentity = (Get-FileHash -LiteralPath (Join-Path $EngineRoot "Engine\Build\Build.version") -Algorithm SHA256).Hash
    if ($manifest.Platform -ne "Win64" -or $manifest.Target -ne "Game" -or
        $manifest.Configuration -ne $Config -or $manifest.Framework -ne "net10.0" -or
        $manifest.EngineIdentity -ne $engineIdentity) { throw "Incompatible Game SDK: $sdk" }
    $entries = @($manifest.Files.PSObject.Properties)
    if ($entries.Count -eq 0) { throw "Empty SDK manifest: $sdk" }
    $sdkRoot = [IO.Path]::GetFullPath($sdk).TrimEnd('\') + '\'
    foreach ($entry in $entries) {
        $asset = [IO.Path]::GetFullPath((Join-Path $sdk $entry.Name))
        if (-not $asset.StartsWith($sdkRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "Invalid SDK path: $($entry.Name)" }
        Require-File $asset
        if ((Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash -ne $entry.Value) { throw "Corrupted SDK asset: $asset" }
    }
}
Write-Host "Managed preflight passed: Editor + $($Configurations -join ', ') Game SDKs exist."
if ($PreflightOnly) { return }

if (Test-Path -LiteralPath $OutputDirectory) {
    throw "OutputDirectory must be new, refusing to mix old binaries: $OutputDirectory"
}
if ($ResumeHostProject) {
    $ResumeHostProject = (Resolve-Path -LiteralPath $ResumeHostProject).Path
    $resumePlugin = Join-Path (Split-Path -Parent $ResumeHostProject) "Plugins\UnrealSharp"
    Require-File (Join-Path $resumePlugin "UnrealSharp.uplugin")
    if ($resumePlugin -eq $PluginDirectory) { throw "ResumeHostProject must be a disposable BuildPlugin host, not the producer project." }
    foreach ($folder in @("Source", "Build", "Scripts", "Docs", "Config")) {
        & robocopy.exe (Join-Path $PluginDirectory $folder) (Join-Path $resumePlugin $folder) /E /XD obj bin /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Cannot update staged $folder." }
    }
    Run $UAT @("ResumePrecompiledPlugin", "-ScriptsForProject=$HostProject", "-ScriptDir=$HostDirectory",
        "-HostProject=$ResumeHostProject", "-Plugin=$(Join-Path $resumePlugin 'UnrealSharp.uplugin')", "-Package=$OutputDirectory")
} else {
# BuildPlugin's temporary HostProject must NEVER regenerate its own Editor C#
# bindings: the source host already produced compatible, tested managed SDKs.
# Copy only clean sources; avoid 40k+ files from obj, Intermediate and .git.
$buildInput = Join-Path $WorkDirectory "HostProject\Plugins\UnrealSharp"
if (Test-Path -LiteralPath $buildInput) { throw "Staging input already exists; retain it for diagnosis or choose a new WorkDirectory: $buildInput" }
New-Item -Path $buildInput -ItemType Directory -Force | Out-Null
& robocopy.exe $PluginDirectory $buildInput /E /XD .git .vs Intermediate Saved obj bin /XF *.bak /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) {
    throw "Failed to stage clean BuildPlugin input (robocopy code $LASTEXITCODE): $buildInput"
}
Require-File (Join-Path $buildInput "UnrealSharp.uplugin")
Write-Host "BuildPlugin source staging: $buildInput"
$stagedHost = Join-Path $WorkDirectory "HostProject\HostProject.uproject"
'{ "FileVersion": 3, "Plugins": [ { "Name": "UnrealSharp", "Enabled": true } ] }' | Set-Content -LiteralPath $stagedHost -Encoding UTF8

$previousSkip = [Environment]::GetEnvironmentVariable("UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN", "Process")
try {
    $env:UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN = "1"
    Run $UAT @(
        "ResumePrecompiledPlugin",
        "-ScriptsForProject=$HostProject",
        "-ScriptDir=$HostDirectory",
        "-HostProject=$stagedHost",
        "-Plugin=$(Join-Path $buildInput 'UnrealSharp.uplugin')",
        "-Package=$OutputDirectory"
    )
} finally {
    [Environment]::SetEnvironmentVariable("UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN", $previousSkip, "Process")
}
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
$Descriptor | Add-Member -NotePropertyName EnabledByDefault -NotePropertyValue $false -Force
$Descriptor | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath (Join-Path $OutputDirectory "UnrealSharp.uplugin") -Encoding UTF8
Get-ChildItem -LiteralPath (Join-Path $OutputDirectory "Source") -Recurse -Filter "*.Build.cs" | ForEach-Object {
    $rules = [IO.File]::ReadAllText($_.FullName)
    $rules = $rules -replace '(PCHUsage = (?:ModuleRules\.)?PCHUsageMode.UseExplicitOrSharedPCHs;)', ('$1' + "`r`n        if (Target.Type != TargetType.Editor) bUsePrecompiled = true;")
    [IO.File]::WriteAllText($_.FullName, $rules)
}
Require-File (Join-Path $OutputDirectory "Managed\global.json")
Require-File (Join-Path $OutputDirectory "Config\UnrealSharp.Settings.json")
Require-File (Join-Path $OutputDirectory "Config\Default.UnrealSharpTypes.json")
if (-not (Test-Path -LiteralPath (Join-Path $OutputDirectory "Templates") -PathType Container)) { throw "Missing plugin templates." }
Require-File (Join-Path $OutputDirectory "Build\Scripts\UnrealSharp.Automation.csproj")
foreach ($Config in $Configurations) {
    Require-File (Join-Path $ManagedDest "Precompiled\Win64\Game\$Config\net10.0\UnrealSharp.GameSdk.json")
    $NativeManifest = @(Get-ChildItem (Join-Path $OutputDirectory "Intermediate\Build") -Recurse -Filter "*.precompiled" -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match "UnrealGame" -and $_.FullName -match $Config })
    foreach ($module in @("UnrealSharpCore", "UnrealSharpUtilities", "UnrealSharpBinds", "UnrealSharpAsync")) {
        if (-not ($NativeManifest | Where-Object { $_.Name -eq "$module.precompiled" })) {
            throw "Missing UnrealGame $Config $module precompiled manifest."
        }
    }
}
foreach ($module in @("UnrealSharpCore", "UnrealSharpUtilities", "UnrealSharpBinds", "UnrealSharpAsync", "UnrealSharpEditor", "UnrealSharpCompiler", "UnrealSharpAsyncBlueprint", "UnrealSharpRuntimeGlue")) {
    Require-File (Join-Path $OutputDirectory "Binaries\Win64\UnrealEditor-$module.dll")
}
Write-Host "Built installed plugin: $OutputDirectory"
Write-Host "Copy the entire folder to <BPProject>\Plugins\UnrealSharp."
if ($ResumeHostProject) { Write-Host "Disposable build host retained for incremental rebuilds: $ResumeHostProject" }
else { Write-Host "Temporary source staging can now be removed: $WorkDirectory" }
Write-Host "Package game in Unreal, then use UnrealSharp Package Project to package C#."
