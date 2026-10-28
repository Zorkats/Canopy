using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.VisualTree;
using CanopyStudio.ViewModels.Game;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.Views.Game;

public partial class EditsModBar : UserControl
{
    private readonly KeyBinding save;
    private InputElement? host;

    public EditsModBar()
    {
        InitializeComponent();
        // Ctrl+S saves while focus is anywhere in the Game area, not only in
        // the bar, and only while the bar shows.
        save = new KeyBinding
        {
            Gesture = new KeyGesture(Key.S, KeyModifiers.Control),
            Command = new RelayCommand(() =>
            {
                if (IsEffectivelyVisible && DataContext is EditsModBarViewModel bar && bar.SaveCommand.CanExecute(null))
                {
                    bar.SaveCommand.Execute(null);
                }
            }),
        };
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        host = this.FindAncestorOfType<GameAreaView>() ?? (InputElement)this;
        host.KeyBindings.Add(save);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        host?.KeyBindings.Remove(save);
        host = null;
    }
}
