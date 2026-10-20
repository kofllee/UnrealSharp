using System;
using System.Collections.Generic;
using System.IO;
using AutomationTool;
using UnrealBuildTool;
using UnrealSharp.Automation.Utilities;

namespace UnrealSharp.Automation.BuildCommands;

[Help("Build precompiled, self-contained Game C# SDK from native host UHT output.")]
[Help("UEBuildConfig=<Configuration>", "Development or Shipping")]
[Help("UETargetType=<Target>", "Defaults Game")]
public sealed class PrecompileGameSdk : BuildCommand
{
    public override void ExecuteBuild()
    {
        TargetType type = Enum.Parse<TargetType>(ParseParamValue("UETargetType", "Game"), true);
        UnrealTargetConfiguration config = Enum.Parse<UnrealTargetConfiguration>(
            ParseParamValue("UEBuildConfig", "Development"), true);
        UnrealTargetPlatform platform = UnrealTargetPlatform.Parse(ParseParamValue("TargetPlatform", "Win64"));
        UnrealArch arch = UnrealArch.Parse(ParseParamValue("TargetArchitecture", "X64"));
        Build(this, type, config, platform, arch);
    }

    public static string Build(BuildCommand command, TargetType type, UnrealTargetConfiguration config,
        UnrealTargetPlatform platform, UnrealArch arch)
    {
        if (type == TargetType.Editor)
            throw new AutomationException("PrecompileGameSdk requires a non-Editor target.");
        string plugin = command.GetUnrealSharpRootFolder();
        string uht = PathUtilities.GetUhtGeneratedOutputPath(plugin, type);
        if (!Directory.Exists(uht) ||
            Directory.GetFiles(uht, "*.generated.cs", SearchOption.AllDirectories).Length == 0)
            throw new AutomationException(
                $"Missing {type} UHT generated bindings at {uht}. Build the C++ host Game target first; " +
                "Editor UHT output is incompatible with packaged Game.");
        string sdk = PathUtilities.GameSdkOutputPath(plugin, platform, type, config);
        if (Directory.Exists(sdk)) Directory.Delete(sdk, recursive: true);
        Directory.CreateDirectory(sdk);
        string project = Path.Combine(plugin, "Managed", "UnrealSharp", "UnrealSharp.Plugins");
        if (!Directory.Exists(project))
            throw new AutomationException($"Managed SDK sources missing: {project}");
        IList<string> arguments = [
            "--runtime", DotNetSdkUtilities.GetDotNetRuntimeIdentifier(platform, arch),
            "-p:UseDefaultOutputPath=true", "-p:PublishSelfContained=true",
            "-p:SelfContained=true", $"-p:UETargetType={type}",
            $"-p:UEBuildConfig={config}", $"-p:UETargetPlatform={platform}",
            $"-p:PublishDir={sdk}"
        ];
        BuildSolution.RunBuild(project, config, publish: true, arguments);
        PrecompiledGameSdkUtilities.WriteManifest(sdk, platform, type, config);
        LoggerUtilities.LogUnrealSharpInfo($"Game SDK created: {sdk}");
        return sdk;
    }
}
