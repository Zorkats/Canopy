using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels;

// One page of the shell, found by its stable Id (see PageIds).
public abstract class PageViewModel : ObservableObject
{
    public abstract string Id { get; }
    public abstract string Title { get; }

    // Each time the page comes into view.
    public virtual void OnShown()
    {
    }

    // What screen readers announce for the page's navigation item.
    public override string ToString() => Title;
}
