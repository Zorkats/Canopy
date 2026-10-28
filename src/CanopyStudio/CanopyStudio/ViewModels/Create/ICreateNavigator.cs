namespace CanopyStudio.ViewModels.Create;

// Moving inside the Create area, for panels that point at a place in the
// project. The Create area implements it.
public interface ICreateNavigator
{
    // Shows the form of section with the field key in view and focused, or
    // the section's form when key is empty. False when the project has no
    // such section.
    bool ShowField(string section, string key);

    // Shows the raw ini view with line (from 1) in view and selected.
    void ShowLine(int line);
}
