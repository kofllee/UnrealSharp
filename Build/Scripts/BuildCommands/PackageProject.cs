using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using AutomationTool;
using UnrealBuildTool;
using UnrealSharp.Automation.Utilities;
using UnrealSharp.Shared;

namespace UnrealSharp.Automation.BuildCommands;

[Help("Packages the UnrealSharp managed code into an Unreal Engine packaged build.")]
[Help("ArchiveDirectory=<Path>", "REQUIRED. The base directory containing the packaged game.")]
[Help("UETargetType=<TargetType>", "REQUIRED. The Unreal Engine target type (Editor, Game, Client, Server).")]
[Help("UEBuildConfig=<Config>", "REQUIRED. The build configuration (Debug, Development, Shipping, Test).")]
[Help("TargetPlatform=<Platform>", "Optional. Target platform. Defaults to Win64.")]
[Help("TargetArchitecture=<Arch>", "Optional. Target architecture. Defaults to X64.")]
[Help("NativeAOT", "Optional flag. Enables Native AOT compilation for the user solution.")]
[Help("UserParams", "Optional. Additional parameters to forward to the user solution build. " + "These should be specified in the format -UserParams=\"-p:Property=Value\" -UserParams=\"--argument\".")]
public class PackageProject : BuildCommand
{
    private const int CleanupMaxAttempts = 5;
    private const int CleanupRetryDelayMs = 1000;

    public sealed record PackagingOptions(
        string ArchiveDirectory,
        TargetType TargetType,
        UnrealTargetConfiguration BuildConfiguration,
        UnrealTargetPlatform TargetPlatform,
        UnrealArch TargetArchitecture,
        bool NativeAot,
        string[]? UserParams = null);

    public override void ExecuteBuild()
    {
        PackagingOptions Options = ParseOptionsFromCommandLine();
        StartPackaging(Options);
    }

    private void StartPackaging(PackagingOptions options)
    {
        ValidateOptions(options);
        LogOptions(options);

        DotNetSdkUtilities.CopyGlobalJson(this);

        string PublishFolder = PathUtilities.BuildOutputPath(options.ArchiveDirectory);
        CleanBuildArtifacts(PublishFolder);

        string RuntimeIdentifier = DotNetSdkUtilities.GetDotNetRuntimeIdentifier(options.TargetPlatform, options.TargetArchitecture);
        IList<string> Arguments = BuildBaseArguments(RuntimeIdentifier, options, PublishFolder);

        EnsureGameSdk(PublishFolder, options);
        BuildUserBindings(PublishFolder, options, Arguments);
        BuildUserSolution(PublishFolder, Arguments, options);
        VerifyPackagedAssemblies(PublishFolder, options.TargetPlatform);
        EmitInstalledFlagFile(PublishFolder);

        LoggerUtilities.LogUnrealSharpInfo($"Packaging complete. Published files: {PublishFolder}");
    }

    private PackagingOptions ParseOptionsFromCommandLine()
    {
        string ArchiveDirectory = ParseRequiredStringParam("ArchiveDirectory");
        TargetType TargetType = ParseRequiredEnumParamEnum<TargetType>("UETargetType");
        UnrealTargetConfiguration TargetConfiguration = ParseRequiredEnumParamEnum<UnrealTargetConfiguration>("UEBuildConfig");

        string? PlatformString = ParseOptionalStringParam("TargetPlatform");
        UnrealTargetPlatform TargetPlatform = string.IsNullOrEmpty(PlatformString) ? UnrealTargetPlatform.Win64 : UnrealTargetPlatform.Parse(PlatformString);

        string? ArchString = ParseOptionalStringParam("TargetArchitecture");
        UnrealArch TargetArchitecture = string.IsNullOrEmpty(ArchString) ? UnrealArch.X64 : UnrealArch.Parse(ArchString);

        bool NativeAot = ParseParam("NativeAOT");

        string[] UserParams = ParseParamValues("UserParams");

        return new PackagingOptions(ArchiveDirectory, TargetType, TargetConfiguration, TargetPlatform, TargetArchitecture, NativeAot, UserParams);
    }

    private static void LogOptions(PackagingOptions options)
    {
        LoggerUtilities.LogUnrealSharpInfo("Packaging project with parameters:");
        LoggerUtilities.LogUnrealSharpInfo($"Archive Directory: {options.ArchiveDirectory}");
        LoggerUtilities.LogUnrealSharpInfo($"Target Platform: {options.TargetPlatform}");
        LoggerUtilities.LogUnrealSharpInfo($"Target Architecture: {options.TargetArchitecture}");
        LoggerUtilities.LogUnrealSharpInfo($"UE Build Configuration: {options.BuildConfiguration}");
        LoggerUtilities.LogUnrealSharpInfo($"UE Target Type: {options.TargetType}");
        LoggerUtilities.LogUnrealSharpInfo($"Native AOT: {options.NativeAot}");

        if (options.UserParams is { Length: > 0 })
        {
            LoggerUtilities.LogUnrealSharpInfo($"User Params: {string.Join(' ', options.UserParams)}");
        }
    }

    private void ValidateOptions(PackagingOptions options)
    {
        ArgumentException.ThrowIfNullOrEmpty(options.ArchiveDirectory);

        if (!Directory.Exists(options.ArchiveDirectory))
        {
            throw new DirectoryNotFoundException($"Archive directory does not exist: {options.ArchiveDirectory}");
        }

        if (options.NativeAot)
        {
            throw new NotSupportedException("Native AOT packaging is not currently supported. This option is reserved for future use and should not be set.");
        }

        string HostFxrPath = DotNetUtilities.LatestHostFxrPath;
        if (!File.Exists(HostFxrPath))
        {
            throw new FileNotFoundException($"Could not locate hostfxr library at expected path: {HostFxrPath}. Ensure that the .NET SDK is installed and accessible.");
        }

        ValidatePlatformArchitecture(options.TargetPlatform, options.TargetArchitecture);
    }

    private static void ValidatePlatformArchitecture(UnrealTargetPlatform platform, UnrealArch architecture)
    {
        if (platform == UnrealTargetPlatform.LinuxArm64 && architecture != UnrealArch.Arm64)
        {
            throw new ArgumentException($"Platform '{platform}' requires architecture '{UnrealArch.Arm64}', " + $"but '{architecture}' was specified.");
        }
    }

    private static void CleanBuildArtifacts(string folder)
    {
        if (!Directory.Exists(folder))
        {
            return;
        }

        LoggerUtilities.LogUnrealSharpInfo($"Cleaning existing output at '{folder}'.");

        for (int Attempt = 1; Attempt <= CleanupMaxAttempts; Attempt++)
        {
            try
            {
                Directory.Delete(folder, recursive: true);
                return;
            }
            catch (Exception Ex)
            {
                if (Attempt == CleanupMaxAttempts)
                {
                    throw new IOException($"Failed to clean output directory '{folder}' after {CleanupMaxAttempts} attempts. See inner exception for details.", Ex);
                }

                LoggerUtilities.LogUnrealSharpWarning($"Attempt {Attempt} to clean output directory '{folder}' failed. Retrying... Exception: {Ex.Message}");
                System.Threading.Thread.Sleep(CleanupRetryDelayMs);
            }
        }
    }

    private static IList<string> BuildBaseArguments(string runtimeIdentifier, PackagingOptions options, string publishFolder)
    {
        return
        [
            "--runtime", runtimeIdentifier,

            "-p:UseDefaultOutputPath=true",
            
            $"-p:PublishSelfContained={(options.NativeAot ? "false" : "true")}",

            $"-p:UETargetType={options.TargetType}",
            $"-p:UEBuildConfig={options.BuildConfiguration}",
            $"-p:UETargetPlatform={options.TargetPlatform}",

            $"-p:PublishDir={publishFolder}",
        ];
    }

    private void EnsureGameSdk(string output, PackagingOptions options)
    {
        string sdk = PathUtilities.GameSdkOutputPath(this.GetUnrealSharpRootFolder(),
            options.TargetPlatform, options.TargetType, options.BuildConfiguration);
        if (!PrecompiledGameSdkUtilities.HasSdk(sdk))
        {
            // Only a source C++ host that already ran native Game UHT can generate the SDK.
            sdk = PrecompileGameSdk.Build(this, options.TargetType, options.BuildConfiguration,
                options.TargetPlatform, options.TargetArchitecture);
        }
        PrecompiledGameSdkUtilities.CopyVerifiedSdk(sdk, output, options.TargetPlatform,
            options.TargetType, options.BuildConfiguration);
    }

    private static void VerifyPackagedAssemblies(string output, UnrealTargetPlatform platform)
    {
        PrecompiledGameSdkUtilities.VerifyRuntimeFiles(output, platform);
        foreach (string loadOrder in Directory.GetFiles(output, "*.LoadOrder.json"))
        {
            foreach (string name in AssemblyUtilities.ReadLoadOrder(loadOrder))
            {
                if (!File.Exists(Path.Combine(output, name + ".dll")))
                    throw new AutomationException($"Missing Game DLL '{name}' required by {loadOrder}");
            }
        }
    }

    private void BuildUserSolution(string publishFolder, IList<string> buildArguments, PackagingOptions options)
    {
        ManagedBuildTarget Target = new(options.TargetPlatform, options.TargetType, options.BuildConfiguration);
        List<FileInfo> ProjectFiles = this.GetManagedProjectFiles(Target)
            .Where(file => !ProjectUtilities.IsEditorOnlyProject(file.FullName))
            .ToList();
        if (ProjectFiles.Count == 0)
        {
            LoggerUtilities.LogUnrealSharpInfo("No managed user projects found. Skipping user publish.");
            return;
        }

        string SolutionDirectory = GenerateUserSolution.PrepareSolution(this, Target, ProjectFiles,
            GenerateUserSolution.GetBuildDirectory(this, Target), "UnrealSharpUser", forceGenerate: true);

        IList<string> BuildUserSolutionArguments = buildArguments;
        if (options.UserParams is { Length: > 0 })
        {
            BuildUserSolutionArguments = new List<string>(buildArguments);
            foreach (string UserParam in options.UserParams)
            {
                BuildUserSolutionArguments.Add(UserParam);
            }
        }

        BuildCommands.BuildSolution.RunBuild(SolutionDirectory, options.BuildConfiguration, publish: true, BuildUserSolutionArguments);
        
        EmitUserLoadOrder(publishFolder, ProjectFiles);
    }

    private void BuildUserBindings(string publishFolder, PackagingOptions options, IList<string> buildArguments)
    {
        if (this.IsInstalledUnrealSharpBuild())
        {
            CopyInstalledGlue(publishFolder);
            return;
        }

        LoggerUtilities.LogUnrealSharpInfo("Source build detected. Building glue from generated projects and emitting glue load order...");
        BuildUserGlue.Build(this, options.TargetType, options.BuildConfiguration, publishFolder, false, buildArguments);
    }

    private static void EmitUserLoadOrder(string publishFolder, List<FileInfo> projectFiles)
    {
        LoadOrderOptions Options = new LoadOrderOptions
        {
            Collectible = false,
            Priority = LoadOrderUtilities.UserLoadOrderPriority
        };

        LoadOrderUtilities.TryEmitLoadOrder(projectFiles.Select(file => file.FullName), publishFolder,
            LoadOrderUtilities.UserLoadOrderName, Options, requireAllAssemblies: true);
    }

    private void CopyInstalledGlue(string publishFolder)
    {
        string GlueFileName = AssemblyUtilities.MakeLoadOrderFileName(LoadOrderUtilities.GlueLoadOrderName);
        string GlueSource = PathUtilities.BuildOutputPath(this.GetProjectRootFolder());
        string GlueManifest = Path.Combine(GlueSource, GlueFileName);

        if (!File.Exists(GlueManifest))
        {
            throw new AutomationException($"Required installed Game glue manifest is missing: {GlueManifest}");
        }

        File.Copy(GlueManifest, Path.Combine(publishFolder, GlueFileName), true);

        foreach (string AssemblyName in AssemblyUtilities.ReadLoadOrder(GlueManifest))
        {
            CopyRequired(Path.Combine(GlueSource, AssemblyName + ".dll"), publishFolder);
            string pdb = Path.Combine(GlueSource, AssemblyName + ".pdb");
            if (File.Exists(pdb)) File.Copy(pdb, Path.Combine(publishFolder, Path.GetFileName(pdb)), true);
        }
    }

    private static void CopyRequired(string sourceFile, string destFolder)
    {
        if (!File.Exists(sourceFile))
            throw new FileNotFoundException("Required Game glue DLL missing.", sourceFile);
        File.Copy(sourceFile, Path.Combine(destFolder, Path.GetFileName(sourceFile)), true);
    }

    private void EmitInstalledFlagFile(string publishFolder)
    {
        string InstalledFlagFilePath = Path.Combine(publishFolder, BuildUtilities.UnrealSharpBuildFlagFileName);
        File.WriteAllText(InstalledFlagFilePath, string.Empty);
    }
}
