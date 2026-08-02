using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class GameComponentsPluginTarget : TargetRules
{
    public GameComponentsPluginTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plGameComponentsPlugin";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);
        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;
        ExtraModules.Add("GameComponentsPluginModule");
    }
}
