// #pl-version 1
//
// Stages the FreeType runtime that RmlUi's font engine loads.
//
// FreeType is dual-licensed under the FreeType License and GPLv2. The FTL is a BSD-style licence
// that permits redistribution with attribution, so the DLL ships inside this package with FTL.TXT
// beside it. That keeps the package self-contained: nothing to install separately.

using System.IO;
using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public static class PlasmaPackageFreetype
{
    /// \brief Copies freetype.dll and its licence next to the plugin that loads them.
    ///
    /// The engine never had a copy step for this - freetype.dll reached Binaries/ by some other
    /// route - so the package does it explicitly rather than inheriting an arrangement that does
    /// not exist out here.
    public static void StageRuntime(BuildContext context)
    {
        if (context.Platform != TargetPlatform.Windows)
        {
            return;
        }

        // RmlUi's vendored FreeType, read out of the SDK. It stays in the engine because this is
        // not the only consumer - the engine keeps it for exactly this reason.
        var source = Path.Combine(PlasmaPackageSdk.Root, "Code", "ThirdParty", "RmlUi", "Dependencies", "freetype");
        var target = PlasmaPackageSdk.PackageBinaryDirectory(context);

        CopyIfDifferent(Path.Combine(source, "win", "x64", "freetype.dll"),
            Path.Combine(target, "freetype.dll"));

        // Listed in PackageDependencies alongside the DLL, so both land in the same component -
        // a component is what gets unpacked as a unit.
        CopyIfDifferent(Path.Combine(source, "FTL.TXT"),
            Path.Combine(target, "freetype-LICENSE.txt"));
    }

    private static void CopyIfDifferent(string source, string target)
    {
        if (!File.Exists(source))
        {
            throw new FileNotFoundException($"Missing FreeType file '{source}'.", source);
        }

        if (File.Exists(target))
        {
            var from = new FileInfo(source);
            var to = new FileInfo(target);

            if (from.Length == to.Length && from.LastWriteTimeUtc <= to.LastWriteTimeUtc)
            {
                return;
            }
        }

        Directory.CreateDirectory(Path.GetDirectoryName(target));
        File.Copy(source, target, overwrite: true);
    }
}
