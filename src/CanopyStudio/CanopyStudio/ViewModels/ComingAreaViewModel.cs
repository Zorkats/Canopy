namespace CanopyStudio.ViewModels;

// An area of the shell whose pages are not built yet: it says what the
// area will do, so the rail already has its final shape.
public sealed class ComingAreaViewModel(string id, string title, string heading, string intro) : PageViewModel
{
    public override string Id => id;
    public override string Title => title;
    public string Heading => heading;
    public string Intro => intro;
}
