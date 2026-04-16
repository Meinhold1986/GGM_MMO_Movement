using UnrealBuildTool;
using System.Collections.Generic;

public class GGMServerTarget : TargetRules
{
    public GGMServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;

        DefaultBuildSettings = BuildSettingsVersion.V2;

        ExtraModuleNames.Add("GGM");
    }
}