using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using AutomationTool;
using UnrealBuildBase;
using UnrealBuildTool;

namespace UnrealSharp.Automation.Utilities;

// Target-specific SDK identity: never use Editor assemblies for packaged games.
public static class PrecompiledGameSdkUtilities
{
    public const string ManifestName = "UnrealSharp.GameSdk.json";
    public sealed class Manifest
    {
        public string Platform { get; set; } = "";
        public string Target { get; set; } = "";
        public string Configuration { get; set; } = "";
        public string Framework { get; set; } = "";
        public string EngineIdentity { get; set; } = "";
        public Dictionary<string, string> Files { get; set; } = new(StringComparer.OrdinalIgnoreCase);
    }

    public static bool HasSdk(string root) => File.Exists(Path.Combine(root, ManifestName));

    private static string EngineIdentity()
    {
        string file = Path.Combine(Unreal.EngineDirectory.FullName, "Build", "Build.version");
        if (!File.Exists(file))
            throw new AutomationException($"Cannot identify UE build: {file}");
        return Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file)));
    }

    public static void VerifyRuntimeFiles(string root, UnrealTargetPlatform platform)
    {
        string[] native = platform == UnrealTargetPlatform.Win64
            ? ["hostfxr.dll", "hostpolicy.dll", "coreclr.dll"]
            : platform == UnrealTargetPlatform.Mac
                ? ["libhostfxr.dylib", "libhostpolicy.dylib", "libcoreclr.dylib"]
                : throw new NotSupportedException($"Unsupported Game runtime platform {platform}");
        string[] required = [
            "UnrealSharp.Plugins.dll", "UnrealSharp.dll", "UnrealSharp.Binds.dll",
            "UnrealSharp.Core.dll", "UnrealSharp.Plugins.deps.json",
            "UnrealSharp.Plugins.runtimeconfig.json", .. native
        ];
        foreach (string name in required)
            if (!File.Exists(Path.Combine(root, name)))
                throw new AutomationException($"Missing managed Game runtime file: {Path.Combine(root, name)}");
    }

    public static void WriteManifest(string root, UnrealTargetPlatform platform,
        TargetType target, UnrealTargetConfiguration config)
    {
        VerifyRuntimeFiles(root, platform);
        Dictionary<string, string> files = Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .Where(file => Path.GetFileName(file) != ManifestName)
            .ToDictionary(file => Path.GetRelativePath(root, file),
                file => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))),
                StringComparer.OrdinalIgnoreCase);
        if (files.Count == 0) throw new AutomationException("Empty Game SDK.");
        Manifest manifest = new()
        {
            Platform = platform.ToString(), Target = target.ToString(),
            Configuration = config.ToString(), Framework = DotNetUtilities.Version,
            EngineIdentity = EngineIdentity(), Files = files
        };
        File.WriteAllText(Path.Combine(root, ManifestName),
            JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }));
    }

    public static void CopyVerifiedSdk(string source, string output, UnrealTargetPlatform platform,
        TargetType target, UnrealTargetConfiguration config)
    {
        string path = Path.Combine(source, ManifestName);
        if (!File.Exists(path)) throw new AutomationException($"Missing Game SDK manifest: {path}");
        Manifest? manifest = JsonSerializer.Deserialize<Manifest>(File.ReadAllText(path));
        if (manifest == null || manifest.Platform != platform.ToString() ||
            manifest.Target != target.ToString() || manifest.Configuration != config.ToString() ||
            manifest.Framework != DotNetUtilities.Version || manifest.EngineIdentity != EngineIdentity() ||
            manifest.Files == null || manifest.Files.Count == 0)
            throw new AutomationException($"Wrong Unreal build/target/configuration for precompiled SDK {source}");
        VerifyRuntimeFiles(source, platform);
        string basePath = Path.GetFullPath(source);
        foreach ((string relative, string hash) in manifest.Files)
        {
            string full = Path.GetFullPath(Path.Combine(basePath, relative));
            string resolvedRelative = Path.GetRelativePath(basePath, full);
            if (Path.IsPathRooted(relative) || resolvedRelative.StartsWith("..", StringComparison.Ordinal) ||
                !resolvedRelative.Equals(relative, StringComparison.OrdinalIgnoreCase))
                throw new AutomationException($"Unsafe Game SDK file path: {relative}");
            if (!File.Exists(full) || !Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(full)))
                .Equals(hash, StringComparison.OrdinalIgnoreCase))
                throw new AutomationException($"Missing or corrupted Game SDK asset: {relative}");
        }
        Directory.CreateDirectory(output);
        foreach (string relative in manifest.Files.Keys)
        {
            string to = Path.Combine(output, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(to)!);
            File.Copy(Path.Combine(source, relative), to, true);
        }
    }
}
