using System.IO;
using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class GameComponentsPluginModule : ModuleRules
{
    public GameComponentsPluginModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/GameComponentsPlugin",
            "GameComponentsPCH.h", "BUILDSYSTEM_BUILDING_GAMECOMPONENTSPLUGIN_LIB");

        // FreeType (vendored by RmlUi) reads the glyph outlines that plTextMeshComponent extrudes
        // into a mesh. Read out of the SDK rather than vendored: the engine keeps this copy anyway.
        var freetype = Path.Combine(PlasmaPackageSdk.Root, "Code", "ThirdParty", "RmlUi", "Dependencies", "freetype");
        PrivateIncludePaths.Add(Path.Combine(freetype, "include"));

        ConfigureForPlatform(TargetPlatform.Windows, () =>
        {
            PrivateLibraries.Add(Path.Combine(freetype, "win", "x64", "freetype.lib"));
            PlasmaPackageFreetype.StageRuntime(context);
        });
    }
}
