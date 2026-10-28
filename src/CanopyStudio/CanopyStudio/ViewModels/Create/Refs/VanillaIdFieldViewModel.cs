using System.ComponentModel;
using System.Globalization;
using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Create;
using CanopyStudio.Services;

namespace CanopyStudio.ViewModels.Create.Refs;

// A field that holds the id of a vanilla row. Tables the game's lists cover
// get a picker with names and icons; the others a number box. Either can
// open the row in the Game area's Tables.
public sealed class VanillaIdFieldViewModel : FieldViewModel
{
    public VanillaIdFieldViewModel(FieldContext context) : base(context)
    {
        Table = TableOf(context);
        PickerKind = Table switch
        {
            "item.dat" => VanillaKind.Item,
            "character.dat" => VanillaKind.Villager,
            "whistle.dat" => VanillaKind.Whistle,
            "craft.dat" => VanillaKind.Craft,
            "cooking.dat" => VanillaKind.Cooking,
            "gimmick.dat" => VanillaKind.Gimmick,
            _ => null,
        };
        PropertyChanged += OnChanged;
    }

    // The table the id is a row of, such as "item.dat"; null when unknown.
    public string? Table { get; }
    public VanillaKind? PickerKind { get; }
    public bool HasPicker => PickerKind is not null;
    public bool HasNumberBox => PickerKind is null;
    public VanillaKind PickerKindOrItem => PickerKind ?? VanillaKind.Item;
    public IGameAssets Assets => Context.Assets;

    public string Watermark => Table is null ? "A vanilla id" : $"A row id of {Table}";

    // The id for the picker: null when the value is not a number, which the
    // field then shows as it is with a warning.
    public ulong? SelectedId
    {
        get => References.Digits(Value ?? "");
        set => SetValue(Key, value?.ToString(CultureInfo.InvariantCulture));
    }

    // The value as typed in the number box.
    public string? Text
    {
        get => Value;
        set => SetValue(Key, value?.Trim());
    }

    public string? Warning
    {
        get
        {
            string text = Value ?? "";
            if (text.Length == 0)
            {
                return null;
            }
            if (References.Digits(text) is not { } id)
            {
                return $"{text} is not a number, so it is not a row of {Table ?? "the table"}.";
            }
            if (Context.Field is { Min: { } min, Max: { } max } && min < max && ((decimal)id < min || (decimal)id > max))
            {
                return $"The loader takes {min} to {max} here.";
            }
            return id >= References.FirstModId ? "Ids from 1500000000 up are Canopy's own, not a vanilla row." : null;
        }
    }

    public bool HasWarning => Warning is not null;

    // Shows the value as it is when the picker cannot.
    public bool ShowsRawValue => HasPicker && !string.IsNullOrEmpty(Value) && SelectedId is null;

    public bool CanOpenInTables => Table is not null && SelectedId is > 0;

    public void OpenInTables(IStudioNavigator navigator)
    {
        if (CanOpenInTables)
        {
            navigator.OpenTable(Table!, SelectedId);
        }
    }

    // The schema's table, or the one a [row.<table>.<Key>] section names;
    // a villager's area is a row of map.dat.
    private static string? TableOf(FieldContext context)
    {
        if (!string.IsNullOrEmpty(context.Field.Table))
        {
            return context.Field.Table;
        }
        if (string.Equals(context.Kind.Prefix, "villager.", StringComparison.OrdinalIgnoreCase)
            && string.Equals(context.Field.Key, "area", StringComparison.OrdinalIgnoreCase))
        {
            return "map.dat";
        }
        const string raw = "row.";
        string section = context.Section;
        int dot = section.IndexOf('.', raw.Length);
        return section.StartsWith(raw, StringComparison.OrdinalIgnoreCase) && dot > raw.Length
            ? section[raw.Length..dot].ToLowerInvariant() + ".dat"
            : null;
    }

    private void OnChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(Value))
        {
            return;
        }
        foreach (string name in (string[])[nameof(SelectedId), nameof(Text), nameof(Warning), nameof(HasWarning),
                     nameof(ShowsRawValue), nameof(CanOpenInTables)])
        {
            OnPropertyChanged(name);
        }
    }
}
