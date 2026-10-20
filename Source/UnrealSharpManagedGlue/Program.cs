using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using UnrealSharpManagedGlue;
using UnrealSharpManagedGlue.Utilities;

[UnrealHeaderTool]
public static class Program
{
    static Program()
    {
        AssemblyLoadContext.Default.Resolving += (context, assemblyName) =>
        {
            string? baseDirectory = Path.GetDirectoryName(typeof(Program).Assembly.Location);
            
            if (baseDirectory == null)
            {
                return null;
            }
            
            string assemblyPath = Path.Combine(baseDirectory, $"{assemblyName.Name}.dll");

            if (!File.Exists(assemblyPath)) return null;
            Assembly newAssembly;
            using FileStream assemblyFile = File.Open(assemblyPath, FileMode.Open, FileAccess.Read, FileShare.Read);
            string pdbPath = Path.ChangeExtension(assemblyPath, ".pdb");
        
            if (File.Exists(pdbPath))
            {
                using FileStream pdbFile = File.Open(pdbPath, FileMode.Open, FileAccess.Read, FileShare.Read);
                newAssembly = AssemblyLoadContext.Default.LoadFromStream(assemblyFile, pdbFile);
            }
            else
            {
                newAssembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(assemblyPath);
            }
            
            return newAssembly;
        };
    }
    
    [UhtExporter(Name = "UnrealSharpCore", Description = "Exports C++ to C# code", Options = UhtExporterOptions.Default, ModuleName = "UnrealSharpCore")]
    private static void Main(IUhtExportFactory factory)
    {
        // BuildPlugin creates a temporary HostProject to precompile *native* modules.
        // That synthetic Editor target exposes a different set of engine plugins,
        // whose C# glue need not (and sometimes cannot) be compiled. The distributable
        // already contains independently verified Editor and Game managed assemblies.
        if (string.Equals(Environment.GetEnvironmentVariable("UNREALSHARP_SKIP_MANAGED_UHT_FOR_BUILDPLUGIN"),
                "1", StringComparison.Ordinal))
        {
            ConsoleUtilities.Log("BuildPlugin native-only pass: reusing prebuilt managed Editor/Game SDK; skipping C# glue export.");
            return;
        }

        ConsoleUtilities.Log("Initializing UnrealSharp C# bindings generator...");
        
        GeneratorStatics.Initialize(factory);
        if (PrecompiledBlueprintConsumer.TryUseSdk())
        {
            return;
        }
        ExportBindings();
        
        FileExporter.CleanOldExportedFiles();
        BuildUtilities.BuildBindings();
        
        ModuleFactory.SyncModuleProjects();
        BuildUtilities.GenerateUserSolution();
    }

    private static void ExportBindings()
    {
        ConsoleUtilities.Log("Starting C# bindings export...");

        try
        {
            Stopwatch stopwatch = Stopwatch.StartNew();
            
            GlueGenerator.GenerateBindings();
            TaskManager.WaitForTasks();

            stopwatch.Stop();
            
            string timeString = stopwatch.Elapsed.TotalSeconds < 1 ? $"{stopwatch.Elapsed.TotalMilliseconds:F0}ms" : $"{stopwatch.Elapsed.TotalSeconds:F2}s";
            ConsoleUtilities.Log($"Finished exporting C# bindings in {timeString}");
        }
        catch (Exception ex)
        {
            ConsoleUtilities.Log("Critical failure during export process:");
            ConsoleUtilities.Log(ex.ToString());
            throw;
        }
    }
}
