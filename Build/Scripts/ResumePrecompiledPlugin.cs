using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using AutomationTool;
using EpicGames.Core;
using UnrealBuildBase;
using UnrealBuildTool;

namespace UnrealSharp.Automation;

public sealed class ResumePrecompiledPlugin : BuildCommand
{
    public override void ExecuteBuild()
    {
        FileReference host = new(ParseParamValue("HostProject") ?? throw new AutomationException("Missing -HostProject"));
        FileReference descriptor = new(ParseParamValue("Plugin") ?? throw new AutomationException("Missing -Plugin"));
        DirectoryReference output = new(ParseParamValue("Package") ?? throw new AutomationException("Missing -Package"));
        if (!FileReference.Exists(host) || !FileReference.Exists(descriptor))
            throw new AutomationException("The existing BuildPlugin host and plugin must exist.");
        if (DirectoryReference.Exists(output))
            throw new AutomationException("Package output must be new.");
        string source = descriptor.Directory.FullName + Path.DirectorySeparatorChar;
        if (output.FullName.StartsWith(source, StringComparison.OrdinalIgnoreCase))
            throw new AutomationException("Package output cannot be inside the source plugin.");
        string? previous = Environment.GetEnvironmentVariable("UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN");
        try
        {
            Environment.SetEnvironmentVariable("UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN", "1");
            PluginDescriptor plugin = PluginDescriptor.FromFile(descriptor);
            List<UnrealTargetPlatform> platforms = [UnrealTargetPlatform.Win64];
            FileReference[] editor = BuildPlugin.CompilePlugin(UnrealBuild.UnrealBuildToolDll,
                host, descriptor, plugin, platforms, []);
            FileReference[] game = BuildPlugin.CompilePlugin(UnrealBuild.UnrealBuildToolDll,
                host, descriptor, plugin, [], platforms,
                " -DisableDebugInfoModules=UnrealSharpCore+UnrealSharpUtilities");
            FileReference[] products = editor.Concat(game).Distinct().ToArray();
            BuildPlugin.PackagePlugin(descriptor, products, output, false, platforms);
        }
        finally
        {
            Environment.SetEnvironmentVariable("UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN", previous);
        }
    }
}
