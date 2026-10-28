using System.ComponentModel;
using System.Globalization;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Native;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels.Game;

public enum CellLook
{
    // The game's own value.
    Game,
    // The edits mod's value.
    Patched,
    // A value that was refused, or that the edits mod sets and Canopy
    // refuses.
    Refused,
    // No value: the row has no such place.
    Missing,
}

// What one cell of a table grid shows.
public readonly record struct CellState(string Text, CellLook Look, string? Tip);

// One row of a table grid. A cell is worked out when it is shown, from the
// grid row, the page's current patches and any value refused here, so no
// object is made per cell. Raises PropertyChanged with Cells when any cell
// may show something new.
public sealed class TableRowItem : ObservableObject
{
    public const string CellsProperty = "Cells";

    private static readonly PropertyChangedEventArgs CellsChanged = new(CellsProperty);

    private readonly TableGridViewModel owner;
    private Dictionary<int, (string Text, string Error)>? refused;

    internal TableRowItem(TableGridViewModel owner, int position, GridRow row)
    {
        this.owner = owner;
        Position = position;
        Row = row;
        IdText = row.Id.ToString(CultureInfo.InvariantCulture);
    }

    // The row's place in the grid's list.
    public int Position { get; }
    public GridRow Row { get; }
    public int Index => Row.Index;
    public string IdText { get; }
    public string EnumText => Row.Enum ?? "";
    public bool IsPatched => owner.Patches.Patches(Position);

    // The tip on the row's own columns: why a row that does not fit shows
    // nothing.
    public string? RowTip => Row.Fits ? null : NoFit;

    public CellState Cell(int column)
    {
        if (!Row.Fits)
        {
            return new CellState("", CellLook.Missing, NoFit);
        }
        if (refused is not null && refused.TryGetValue(column, out (string Text, string Error) draft))
        {
            return new CellState(draft.Text, CellLook.Refused, $"Not saved. {draft.Error}");
        }
        if (column >= Row.Values.Count || Row.Values[column] is not { } game)
        {
            return new CellState("", CellLook.Missing, "This row has no such place.");
        }
        if (owner.Patches.At(Position, column) is not { } patch)
        {
            return new CellState(game, CellLook.Game, game.Length > LongText ? game : null);
        }
        GridColumn place = owner.Columns[column];
        string where = $"Edits mod, line {patch.Line}. Game value: {Shown(game)}.";
        FieldValue check = FieldValueCheck.Check(place.Type, patch.Value);
        return check.Ok
            ? new CellState(patch.Value, CellLook.Patched, where)
            : new CellState(patch.Value, CellLook.Refused, $"{where} Canopy refuses this value: {check.Error}");
    }

    // The text an editor starts with: what the cell shows.
    public string EditText(int column) => Cell(column).Text;

    internal bool IsRefused(int column) => refused?.ContainsKey(column) ?? false;

    internal void Refuse(int column, string text, string error)
    {
        refused ??= [];
        refused[column] = (text, error);
        Refresh();
    }

    internal void Accept(int column)
    {
        if (refused?.Remove(column) ?? false)
        {
            Refresh();
        }
    }

    internal void Refresh()
    {
        OnPropertyChanged(CellsChanged);
        OnPropertyChanged(nameof(IsPatched));
    }

    private static string Shown(string value) => value.Length == 0 ? "(empty)" : value;

    // Texts longer than this get the whole text as a tip.
    private const int LongText = 24;

    private const string NoFit =
        "The table's schema does not describe this row, so its fields are not shown and cannot be edited.";
}
