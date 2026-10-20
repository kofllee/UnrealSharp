using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

namespace UnrealSharpManagedGlue.Utilities;

public static class BuildUtilities
{
    public static void BuildBindings()
    {
        if (GeneratorStatics.TargetType != TargetRules.TargetType.Editor || !FileExporter.HasModifiedEngineGlue)
        {
            return;
        }
        
        ConsoleUtilities.Log("Engine glue has been modified since the last build. Rebuilding UnrealSharp bindings...");

        List<KeyValuePair<string, string>> actionArgs =
        [
            new("Folders", Path.Combine(GeneratorStatics.ManagedPath, "UnrealSharp")),
            new("TargetConfiguration", GeneratorStatics.TargetConfiguration.ToString())
        ];
        
        if (!UnrealSharpAutomationUtilities.InvokeUnrealSharpAutomation("BuildSolution", actionArgs))
            throw new IOException("Editor engine bindings compilation failed.");
    }
    
    public static void GenerateUserSolution()
    {
        if (GeneratorStatics.TargetType != TargetRules.TargetType.Editor)
        {
            return;
        }
        
        List<KeyValuePair<string, string>> ActionArgs =
        [
            new("TargetType", GeneratorStatics.TargetType.ToString()),
            new("TargetConfiguration", GeneratorStatics.TargetConfiguration.ToString())
        ];
        if (!UnrealSharpAutomationUtilities.InvokeUnrealSharpAutomation("GenerateUserSolution", ActionArgs))
            throw new IOException("Editor user solution generation failed.");
    }
}
