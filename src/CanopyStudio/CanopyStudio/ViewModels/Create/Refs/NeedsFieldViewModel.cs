using System.Collections.ObjectModel;
using CanopyStudio.Core.Create;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create.Refs;

// A recipe's needs as rows of an item and a count. Each change writes the
// whole list back; rows the user did not touch keep their text as it was.
public sealed partial class NeedsFieldViewModel : FieldViewModel
{
    private ProjectKeys keys;

    public NeedsFieldViewModel(FieldContext context) : base(context)
    {
        Cooking = context.Kind.Prefix.Equals("cooking.", StringComparison.OrdinalIgnoreCase);
        keys = ReferenceFields.KeysFor(context);
        Rebuild();
    }

    // A cooking recipe also takes group:<number>.
    public bool Cooking { get; }

    public ObservableCollection<NeedRowViewModel> Rows { get; } = [];

    public string? Warning => Rows.Count == 0
        ? "A recipe needs 1 to 5 ingredients. Add one."
        : Rows.Count > ItemList.MaxNeeds
            ? $"A recipe takes at most {ItemList.MaxNeeds} ingredients, and the loader skips this one with {Rows.Count}."
            : null;

    public bool HasWarning => Warning is not null;

    private bool CanAdd => Rows.Count < ItemList.MaxNeeds;

    [RelayCommand(CanExecute = nameof(CanAdd))]
    private void Add()
    {
        Rows.Add(new NeedRowViewModel(this, new Need("", null), keys));
        Write();
    }

    internal void Remove(NeedRowViewModel row)
    {
        Rows.Remove(row);
        Write();
    }

    // Writes the rows; a refused change shows the project's text again.
    internal void Write()
    {
        SetValue(Key, ItemList.FormatNeeds(Rows.Select(row => row.Need)));
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
        foreach (Need need in ItemList.ParseNeeds(Value))
        {
            Rows.Add(new NeedRowViewModel(this, need, keys));
        }
        Changed();
    }

    private void Changed()
    {
        OnPropertyChanged(nameof(Warning));
        OnPropertyChanged(nameof(HasWarning));
        AddCommand.NotifyCanExecuteChanged();
    }
}

// One ingredient: the item picker and the count.
public sealed partial class NeedRowViewModel : ObservableObject
{
    private readonly NeedsFieldViewModel owner;

    public NeedRowViewModel(NeedsFieldViewModel owner, Need need, ProjectKeys keys)
    {
        this.owner = owner;
        Need = need;
        Picker = new RefPickerModel(References.Need(owner.Cooking), keys, owner.Context.Assets, need.Item, WriteItem);
    }

    public Need Need { get; private set; }

    public RefPickerModel Picker { get; }

    // The count as typed; empty means one.
    public string Count
    {
        get => Need.Count ?? "";
        set
        {
            string? count = string.IsNullOrWhiteSpace(value) ? null : value.Trim();
            if (count == Need.Count)
            {
                return;
            }
            Need = Need with { Count = count, Raw = null };
            owner.Write();
            OnPropertyChanged(nameof(CountWarning));
            OnPropertyChanged(nameof(HasCountWarning));
        }
    }

    public string? CountWarning => ItemList.CountProblem(Need.Count);
    public bool HasCountWarning => CountWarning is not null;

    [RelayCommand]
    private void Remove() => owner.Remove(this);

    private string? WriteItem(string? item)
    {
        Need = Need with { Item = item ?? "", Raw = null };
        owner.Write();
        return Need.Item;
    }
}
