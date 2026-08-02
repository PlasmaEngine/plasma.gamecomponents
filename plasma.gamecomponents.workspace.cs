// #pl-version 1

using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class PlasmaGameComponentsWorkspace : WorkspaceRules
{
    public PlasmaGameComponentsWorkspace(BuildContext context) : base(context)
    {
        TargetNames.Add("GameComponentsPlugin");
    }
}
