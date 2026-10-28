using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Data;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using CanopyStudio.ViewModels.Create;
using CanopyStudio.ViewModels.Create.Simple;

namespace CanopyStudio.Views.Create;

// The views of SimpleFields' editors. Null for an editor it does not know.
// The form row around each (key, help, problems) is the form's; these are
// the editors alone, built in code like FieldViewLocator's text box.
public static class SimpleFieldViews
{
    public static Control? Build(FieldViewModel field) => field switch
    {
        PathFieldViewModel path => Path(path),
        NumberFieldViewModel number => Text(number, 260),
        TextFieldViewModel text => Text(text, double.PositiveInfinity),
        OptionFieldViewModel option => Option(option),
        ListFieldViewModel list => List(list),
        LangTextFieldViewModel lang => Lang(lang),
        _ => null,
    };

    private static StackPanel Text(TextFieldViewModel field, double maxWidth) => new()
    {
        DataContext = field,
        Spacing = 4,
        Children =
        {
            Box(field.Key, nameof(TextFieldViewModel.Text), nameof(TextFieldViewModel.Watermark), maxWidth),
            Note(),
        },
    };

    private static StackPanel Path(PathFieldViewModel field)
    {
        Button browse = new() { Content = "Browse...", Margin = new(8, 0, 0, 0) };
        AutomationProperties.SetName(browse, $"Browse for {field.Key}");
        browse.Click += async (_, _) => await BrowseAsync(browse, field);
        TextBox box = Box(field.Key, nameof(TextFieldViewModel.Text), nameof(TextFieldViewModel.Watermark),
            double.PositiveInfinity);
        box.Classes.Add("mono");
        Grid row = new() { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        row.Children.Add(box);
        Grid.SetColumn(browse, 1);
        row.Children.Add(browse);
        return new StackPanel { DataContext = field, Spacing = 4, Children = { row, Note() } };
    }

    // A file picker that starts in the mod folder; the editor refuses a
    // file outside it.
    private static async Task BrowseAsync(Control anchor, PathFieldViewModel field)
    {
        if (TopLevel.GetTopLevel(anchor) is not { } top)
        {
            return;
        }
        IStorageFolder? start = await top.StorageProvider.TryGetFolderFromPathAsync(field.Folder);
        IReadOnlyList<IStorageFile> picked = await top.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = $"Choose the file for {field.Key}",
            AllowMultiple = false,
            SuggestedStartLocation = start,
            FileTypeFilter = [new FilePickerFileType(field.FileKind) { Patterns = [.. field.Patterns] }],
        });
        if (picked.Count > 0 && picked[0].TryGetLocalPath() is { } path)
        {
            field.UseFile(path);
        }
    }

    private static StackPanel Option(OptionFieldViewModel field)
    {
        ComboBox box = new()
        {
            MinWidth = 220,
            ItemsSource = field.Options,
            [!SelectingItemsControl.SelectedItemProperty] =
                new Binding(nameof(OptionFieldViewModel.Selected)) { Mode = BindingMode.TwoWay },
        };
        AutomationProperties.SetName(box, field.Key);
        return new StackPanel { DataContext = field, Spacing = 4, Children = { box, Note() } };
    }

    private static StackPanel List(ListFieldViewModel field)
    {
        ItemsControl entries = new()
        {
            ItemsSource = field.Entries,
            ItemTemplate = new FuncDataTemplate<ListEntryViewModel>((entry, _) =>
            {
                TextBox box = new()
                {
                    [!TextBox.TextProperty] = new Binding(nameof(ListEntryViewModel.Text)) { Mode = BindingMode.TwoWay },
                };
                AutomationProperties.SetName(box, $"{field.Key} entry");
                Button remove = new()
                {
                    Content = "Remove",
                    Classes = { "small" },
                    Margin = new(8, 0, 0, 0),
                    [!Button.CommandProperty] = new Binding(nameof(ListEntryViewModel.RemoveCommand)),
                };
                Grid row = new() { ColumnDefinitions = new ColumnDefinitions("*,Auto"), Margin = new(0, 0, 0, 6) };
                row.Children.Add(box);
                Grid.SetColumn(remove, 1);
                row.Children.Add(remove);
                return row;
            }),
        };
        TextBlock empty = new()
        {
            Classes = { "faint", "small" },
            Text = field.Default is { } value ? $"Nothing listed. The loader uses {value}." : "Nothing listed yet.",
            [!Visual.IsVisibleProperty] = new Binding(nameof(ListFieldViewModel.IsEmpty)),
        };
        Button add = new()
        {
            Content = "Add an entry",
            Classes = { "small" },
            HorizontalAlignment = HorizontalAlignment.Left,
            [!Button.CommandProperty] = new Binding(nameof(ListFieldViewModel.AddCommand)),
        };
        AutomationProperties.SetName(add, $"Add to {field.Key}");
        return new StackPanel { DataContext = field, Spacing = 4, Children = { empty, entries, add } };
    }

    private static StackPanel Lang(LangTextFieldViewModel field)
    {
        Grid rows = new() { ColumnDefinitions = new ColumnDefinitions("Auto,*"), Margin = new(0, 8, 0, 0) };
        for (int index = 0; index < field.Rows.Count; ++index)
        {
            LangTextRowViewModel row = field.Rows[index];
            rows.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            StackPanel label = new()
            {
                Margin = new(0, 6, 16, 6),
                VerticalAlignment = VerticalAlignment.Center,
                Children =
                {
                    new TextBlock { Classes = { "mono" }, Text = row.Key },
                    new TextBlock { Classes = { "faint", "small" }, Text = row.Language },
                },
            };
            TextBox box = new()
            {
                DataContext = row,
                Watermark = row.FallbackText,
                VerticalAlignment = VerticalAlignment.Center,
                [!TextBox.TextProperty] = new Binding(nameof(LangTextRowViewModel.Text)) { Mode = BindingMode.TwoWay },
            };
            AutomationProperties.SetName(box, $"{row.Key}, {row.Language}");
            ToolTip.SetTip(box, $"Left empty, {row.Language} shows the text of {field.Key}.");
            Grid.SetRow(label, index);
            Grid.SetRow(box, index);
            Grid.SetColumn(box, 1);
            rows.Children.Add(label);
            rows.Children.Add(box);
        }
        Expander languages = new()
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            IsExpanded = field.Rows.Any(row => !row.FallsBack),
            [!HeaderedContentControl.HeaderProperty] = new Binding(nameof(LangTextFieldViewModel.Summary)),
            Content = rows,
        };
        return new StackPanel
        {
            DataContext = field,
            Spacing = 6,
            Children =
            {
                Box(field.Key, nameof(LangTextFieldViewModel.Text), nameof(LangTextFieldViewModel.Watermark),
                    double.PositiveInfinity),
                languages,
            },
        };
    }

    private static TextBox Box(string key, string text, string watermark, double maxWidth)
    {
        TextBox box = new()
        {
            MaxWidth = maxWidth,
            HorizontalAlignment = double.IsInfinity(maxWidth) ? HorizontalAlignment.Stretch : HorizontalAlignment.Left,
            MinWidth = double.IsInfinity(maxWidth) ? 0 : 160,
            [!TextBox.TextProperty] = new Binding(text) { Mode = BindingMode.TwoWay },
            [!TextBox.WatermarkProperty] = new Binding(watermark),
        };
        AutomationProperties.SetName(box, key);
        return box;
    }

    // An editor's advice about its value, bound to Note and HasNote.
    private static TextBlock Note() => new()
    {
        Classes = { "warn", "small" },
        TextWrapping = TextWrapping.Wrap,
        [!TextBlock.TextProperty] = new Binding("Note"),
        [!Visual.IsVisibleProperty] = new Binding("HasNote"),
    };
}
