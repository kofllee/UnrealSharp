using System;
using System.Collections.Generic;
using System.IO;

namespace UnrealSharp.Automation.Utilities;

public struct DotnetVersionInfo
{
    public string VersionName;
    public Version LatestVersion;
}

public static class DotNetUtilities
{
    private const int DotnetMajorVersion = 10;
    private static readonly Version MinimumSdkVersion = new(10, 0, 201);

    public static string Version => $"net{DotnetMajorVersion}.0";
    public static string DotnetFolder => ResolveDotNetRoot();

    public static string HostFxrFilename =>
        OperatingSystem.IsWindows() ? "hostfxr.dll" :
        OperatingSystem.IsMacOS() ? "libhostfxr.dylib" :
        OperatingSystem.IsLinux() ? "libhostfxr.so" :
        throw new PlatformNotSupportedException("Unsupported platform");

    private static string CoreClrFilename =>
        OperatingSystem.IsWindows() ? "coreclr.dll" :
        OperatingSystem.IsMacOS() ? "libcoreclr.dylib" :
        OperatingSystem.IsLinux() ? "libcoreclr.so" :
        throw new PlatformNotSupportedException("Unsupported platform");

    private static string DotNetExeName => OperatingSystem.IsWindows() ? "dotnet.exe" : "dotnet";

    public static string DotNetExecutable => Path.Combine(DotnetFolder, DotNetExeName);
    public static string DotNetSdkDirectory => Path.Combine(DotnetFolder, "sdk");
    public static string LatestDotNetSdkPath =>
        Path.Combine(DotNetSdkDirectory, LatestDotNetSdkVersionInfo.VersionName);
    public static DotnetVersionInfo LatestDotNetSdkVersionInfo =>
        ParseLatestDotnetVersionsInDirectory(DotNetSdkDirectory, MinimumSdkVersion, "MSBuild.dll", "dotnet.dll");

    // Keep this ordering identical to native CSDotnetUtilties.cpp. A pinned root
    // from the Editor has priority and is never silently replaced.
    private static string ResolveDotNetRoot()
    {
        string? pinned = Environment.GetEnvironmentVariable("UNREALSHARP_DOTNET_ROOT");
        if (!string.IsNullOrWhiteSpace(pinned))
        {
            if (IsCompatibleDotNetRoot(pinned.Trim()))
            {
                return pinned.Trim();
            }

            throw new InvalidOperationException(
                $"UnrealSharp's selected .NET SDK root is no longer valid: '{pinned}'. " +
                "A .NET 10 SDK >= 10.0.201 with Runtime and hostfxr is required.");
        }

        var candidates = new List<string>();
        candidates.Add(Environment.GetEnvironmentVariable("DOTNET_ROOT") ?? string.Empty);
        if (OperatingSystem.IsWindows())
        {
            candidates.Add(Environment.GetEnvironmentVariable("DOTNET_ROOT_X64") ?? string.Empty);
        }
        string? path = Environment.GetEnvironmentVariable("PATH");
        if (!string.IsNullOrEmpty(path))
        {
            candidates.AddRange(path.Split(Path.PathSeparator));
        }
        if (OperatingSystem.IsWindows())
        {
            candidates.Add(Path.Combine(Environment.GetEnvironmentVariable("ProgramFiles") ?? string.Empty, "dotnet"));
            candidates.Add(Path.Combine(Environment.GetEnvironmentVariable("LOCALAPPDATA") ?? string.Empty, "Microsoft", "dotnet"));
        }
        else if (OperatingSystem.IsMacOS())
        {
            candidates.Add("/usr/local/share/dotnet");
            candidates.Add("/opt/homebrew/opt/dotnet/libexec");
            candidates.Add(Path.Combine(Environment.GetEnvironmentVariable("HOME") ?? string.Empty, ".dotnet"));
        }
        else if (OperatingSystem.IsLinux())
        {
            candidates.Add("/usr/share/dotnet");
            candidates.Add("/usr/local/share/dotnet");
            candidates.Add(Path.Combine(Environment.GetEnvironmentVariable("HOME") ?? string.Empty, ".dotnet"));
        }

        foreach (string candidate in candidates)
        {
            string root = candidate.Trim();
            if (IsCompatibleDotNetRoot(root))
            {
                return root;
            }
        }

        throw new InvalidOperationException(
            "Could not find a compatible .NET 10 SDK (>= 10.0.201) with Runtime and hostfxr. " +
            "Install .NET 10 SDK or set UNREALSHARP_DOTNET_ROOT to its directory.");
    }

    private static bool IsCompatibleDotNetRoot(string root)
    {
        if (string.IsNullOrWhiteSpace(root) ||
            IsUnrealBundledDotNet(Path.Combine(root, DotNetExeName)) ||
            !File.Exists(Path.Combine(root, DotNetExeName)))
        {
            return false;
        }

        return HasVersionedLibrary(Path.Combine(root, "sdk"), "MSBuild.dll", MinimumSdkVersion, "dotnet.dll") &&
               HasVersionedLibrary(Path.Combine(root, "shared", "Microsoft.NETCore.App"), CoreClrFilename) &&
               HasVersionedLibrary(Path.Combine(root, "host", "fxr"), HostFxrFilename);
    }

    private static bool HasVersionedLibrary(string directory, string library, Version? minimum = null,
        string? additionalLibrary = null)
    {
        if (!Directory.Exists(directory)) return false;
        foreach (string folder in Directory.EnumerateDirectories(directory, "10.*"))
        {
            if (!TryStableDotNet10Version(Path.GetFileName(folder), out Version? version) ||
                (minimum != null && version < minimum))
            {
                continue;
            }

            if (File.Exists(Path.Combine(folder, library)) &&
                (additionalLibrary == null || File.Exists(Path.Combine(folder, additionalLibrary))))
                return true;
        }
        return false;
    }

    public static DotnetVersionInfo ParseLatestDotnetVersionsInDirectory(string directory) =>
        ParseLatestDotnetVersionsInDirectory(directory, null);

    private static DotnetVersionInfo ParseLatestDotnetVersionsInDirectory(string directory, Version? minimum,
        string? requiredLibrary = null, string? additionalLibrary = null)
    {
        if (!Directory.Exists(directory))
            throw new DirectoryNotFoundException($"Missing .NET directory: {directory}");

        string? name = null;
        Version latest = new(0, 0);
        foreach (string folder in Directory.EnumerateDirectories(directory, "10.*"))
        {
            string folderName = Path.GetFileName(folder);
            if (!TryStableDotNet10Version(folderName, out Version? version) ||
                (minimum != null && version < minimum) ||
                (requiredLibrary != null && !File.Exists(Path.Combine(folder, requiredLibrary))) ||
                (additionalLibrary != null && !File.Exists(Path.Combine(folder, additionalLibrary))) ||
                (name != null && version <= latest))
            {
                continue;
            }

            name = folderName;
            latest = version;
        }

        if (name == null)
            throw new InvalidOperationException($"No compatible .NET 10 version in {directory}");

        return new DotnetVersionInfo { VersionName = name, LatestVersion = latest };
    }

    private static bool TryStableDotNet10Version(string name, out Version version)
    {
        if (System.Version.TryParse(name, out Version? parsed) &&
            parsed.Major == DotnetMajorVersion && parsed.Minor == 0 &&
            parsed.Build >= 0 && parsed.Revision < 0)
        {
            version = parsed;
            return true;
        }
        version = new Version(0, 0);
        return false;
    }

    public static string LatestHostFxrPath
    {
        get
        {
            string root = Path.Combine(DotnetFolder, "host", "fxr");
            string name = ParseLatestDotnetVersionsInDirectory(root, null, HostFxrFilename).VersionName;
            return Path.Combine(root, name, HostFxrFilename);
        }
    }

    private static bool IsUnrealBundledDotNet(string path) =>
        path.Replace('\\', '/').Contains("/Binaries/ThirdParty/DotNet/",
            StringComparison.OrdinalIgnoreCase);
}
