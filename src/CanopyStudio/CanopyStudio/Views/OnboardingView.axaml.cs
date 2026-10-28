using Avalonia.Controls;
using Avalonia.Data.Converters;
using Avalonia.Media;
using CanopyStudio.Core.Identity;

namespace CanopyStudio.Views;

public sealed partial class OnboardingView : UserControl
{
    // A theme card's swatch: the theme's own accent, so each card shows the
    // colour it puts on the window.
    public static readonly IValueConverter ThemeAccent =
        new FuncValueConverter<string?, IBrush>(id => new SolidColorBrush(Color.FromUInt32(StudioThemes.Find(id).Accent)));

    public OnboardingView() => InitializeComponent();
}
