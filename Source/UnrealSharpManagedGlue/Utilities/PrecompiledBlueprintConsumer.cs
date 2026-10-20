using System;
using System.IO;
using System.Text.Json;
using UnrealBuildTool;
using UnrealSharp.Automation.Utilities;

namespace UnrealSharpManagedGlue.Utilities;

public static class PrecompiledBlueprintConsumer
{
    public static bool TryUseSdk()
    {
        string? projectFile = GeneratorStatics.Factory.Session.ProjectFile;
        if (!OperatingSystem.IsWindows() || projectFile == null || !File.Exists(projectFile))
            return false;
        using JsonDocument descriptor = JsonDocument.Parse(File.ReadAllText(
            Path.Combine(GeneratorStatics.PluginDirectory, "UnrealSharp.uplugin")));
        if (!descriptor.RootElement.TryGetProperty("Installed", out JsonElement installed) || !installed.GetBoolean())
            return false;
        using JsonDocument project = JsonDocument.Parse(File.ReadAllText(projectFile));
        if (project.RootElement.TryGetProperty("Modules", out JsonElement modules) && modules.GetArrayLength() > 0)
            return false;

        if (GeneratorStatics.TargetType == TargetType.Game)
        {
            string sdk = Path.Combine(GeneratorStatics.PluginDirectory, "Binaries", "Managed", "Precompiled",
                "Win64", "Game", GeneratorStatics.TargetConfiguration.ToString(), "net10.0");
            PrecompiledGameSdkUtilities.VerifySdk(sdk, UnrealTargetPlatform.Win64,
                TargetType.Game, GeneratorStatics.TargetConfiguration);
        }
        else if (GeneratorStatics.TargetType == TargetType.Editor)
        {
            string sdk = Path.Combine(GeneratorStatics.PluginDirectory, "Binaries", "Managed", "net10.0");
            foreach (string assembly in new[] { "UnrealSharp.dll", "UnrealSharp.Binds.dll", "UnrealSharp.Plugins.dll" })
                if (!File.Exists(Path.Combine(sdk, assembly)))
                    throw new IOException($"Missing precompiled Editor SDK assembly: {assembly}");
        }
        else
        {
            return false;
        }
        ConsoleUtilities.Log("Installed Blueprint consumer: reusing precompiled managed SDK; native UHT continues normally.");
        return true;
    }
}
