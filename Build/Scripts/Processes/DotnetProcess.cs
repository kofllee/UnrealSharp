using System.IO;
using UnrealSharp.Automation.Utilities;

namespace UnrealSharp.Automation.Processes;

public class DotnetProcess : BuildToolProcess
{
    public DotnetProcess() : base(DotNetUtilities.DotNetExecutable)
    {
        string root = DotNetUtilities.DotnetFolder;
        string LatestDotNetSdkPath = DotNetUtilities.LatestDotNetSdkPath;
        StartInfo.Environment["UNREALSHARP_DOTNET_ROOT"] = root;
        StartInfo.Environment["DOTNET_ROOT"] = root;
        if (System.OperatingSystem.IsWindows())
        {
            StartInfo.Environment["DOTNET_ROOT_X64"] = root;
        }
        StartInfo.Environment["PATH"] = root + System.IO.Path.PathSeparator +
            (StartInfo.Environment.TryGetValue("PATH", out string? path) ? path : string.Empty);
        StartInfo.ArgumentList.Add(Path.Combine(LatestDotNetSdkPath, "dotnet.dll"));
        StartInfo.Environment["MSBuildExtensionsPath"] = LatestDotNetSdkPath;
        StartInfo.Environment["MSBUILD_EXE_PATH"] = Path.Combine(LatestDotNetSdkPath, "MSBuild.dll");
        StartInfo.Environment["MSBuildSDKsPath"] = Path.Combine(LatestDotNetSdkPath, "Sdks");
        StartInfo.Environment["DOTNET_ROLL_FORWARD"] = "LatestMinor";
    }
}
