using System.Collections.ObjectModel;
using System.Text;
using CanopyStudio.Core.Create;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create.Refs;

// A skin's replace as rows of a model texture name and the texture drawn
// instead. The key may be repeated; the form edits the first line and keeps
// the others as they are.
public sealed partial class SwapsFieldViewModel : FieldViewModel
{
    private ProjectKeys keys;

    public SwapsFieldViewModel(FieldContext context) : base(context)
    {
        keys = ReferenceFields.KeysFor(context);
        Rebuild();
    }

    public ObservableCollection<SwapRowViewModel> Rows { get; } = [];

    // The replace lines after the first, which this form does not show.
    public int MoreLines => Math.Max(0, ItemList.ValueCount(Context.Project.Structure, Context.Section, Key) - 1);

    public string? Note => MoreLines switch
    {
        0 => null,
        1 => "This skin has 1 more replace line. It is kept as it is; edit it in the raw view.",
        _ => $"This skin has {MoreLines} more replace lines. They are kept as they are; edit them in the raw view.",
    };

    public bool HasNote => Note is not null;

    public string? Warning => Rows.Count == 0 && MoreLines == 0
        ? "A skin needs at least one swap. Add one."
        : Rows.Count > ItemList.MaxSwaps
            ? $"A skin takes at most {ItemList.MaxSwaps} swaps, and the loader skips this one."
            : null;

    public bool HasWarning => Warning is not null;

    private bool CanAdd => Rows.Count < ItemList.MaxSwaps;

    [RelayCommand(CanExecute = nameof(CanAdd))]
    private void Add()
    {
        Rows.Add(new SwapRowViewModel(this, new Swap("", ""), keys));
        Write();
    }

    internal void Remove(SwapRowViewModel row)
    {
        Rows.Remove(row);
        Write();
    }

    // Writes the rows; a refused change shows the project's text again.
    internal void Write()
    {
        SetValue(Key, ItemList.FormatSwaps(Rows.Select(row => row.Swap)));
        if (Error is not null)
        {
            Rebuild();
        }
        Changed();
    }

    protected override void OnProjectChanged()
    {
        base.OnProjectChanged();
        keys = keys.WithProject(Context.Project.Structure, Context.Project.Name);
        Rebuild();
    }

    private void Rebuild()
    {
        Rows.Clear();
        foreach (Swap swap in ItemList.ParseSwaps(Value))
        {
            Rows.Add(new SwapRowViewModel(this, swap, keys));
        }
        Changed();
    }

    private void Changed()
    {
        foreach (string name in (string[])[nameof(MoreLines), nameof(Note), nameof(HasNote), nameof(Warning), nameof(HasWarning)])
        {
            OnPropertyChanged(name);
        }
        AddCommand.NotifyCanExecuteChanged();
    }
}

// One swap: the model's texture name and the texture picker.
public sealed partial class SwapRowViewModel : ObservableObject
{
    private readonly SwapsFieldViewModel owner;

    public SwapRowViewModel(SwapsFieldViewModel owner, Swap swap, ProjectKeys keys)
    {
        this.owner = owner;
        Swap = swap;
        Picker = new RefPickerModel(References.SwapTexture, keys, owner.Context.Assets, swap.Texture, WriteTexture);
    }

    public Swap Swap { get; private set; }

    public RefPickerModel Picker { get; }

    // The model texture name, as the model file spells it.
    public string Name
    {
        get => Swap.Name;
        set
        {
            string name = (value ?? "").Trim();
            if (name == Swap.Name)
            {
                return;
            }
            Swap = Swap with { Name = name, Raw = null };
            owner.Write();
            OnPropertyChanged(nameof(NameWarning));
            OnPropertyChanged(nameof(HasNameWarning));
        }
    }

    public string? NameWarning => Swap.Name.Length == 0
        ? "Type the model's texture name this swap replaces."
        : Encoding.UTF8.GetByteCount(Swap.Name) > 64 ? "A model texture name is at most 64 bytes." : null;

    public bool HasNameWarning => NameWarning is not null;

    [RelayCommand]
    private void Remove() => owner.Remove(this);

    private string? WriteTexture(string? texture)
    {
        Swap = Swap with { Texture = texture ?? "", Raw = null };
        owner.Write();
        return Swap.Texture;
    }
}
