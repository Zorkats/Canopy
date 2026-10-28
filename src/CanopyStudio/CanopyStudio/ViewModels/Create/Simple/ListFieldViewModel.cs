using System.Collections.ObjectModel;
using CanopyStudio.Core.Create;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create.Simple;

// One entry of a list field.
public sealed partial class ListEntryViewModel(ListFieldViewModel owner, string text) : ObservableObject
{
    [ObservableProperty]
    private string text = text;

    [RelayCommand]
    private void Remove() => owner.Remove(this);

    partial void OnTextChanged(string value) => owner.Write();
}

// A comma-separated list, edited one entry per row. Empty rows are left out
// of the value, so a new row is written once it has text.
public sealed partial class ListFieldViewModel : FieldViewModel
{
    private bool loading;

    public ListFieldViewModel(FieldContext context) : base(context)
    {
        Load();
    }

    public ObservableCollection<ListEntryViewModel> Entries { get; } = [];

    public bool IsEmpty => Entries.Count == 0;

    [RelayCommand]
    private void Add()
    {
        Entries.Add(new ListEntryViewModel(this, ""));
        OnPropertyChanged(nameof(IsEmpty));
    }

    internal void Remove(ListEntryViewModel entry)
    {
        if (Entries.Remove(entry))
        {
            OnPropertyChanged(nameof(IsEmpty));
            Write();
        }
    }

    internal void Write()
    {
        if (!loading)
        {
            SetValue(Key, ListText.Join(Entries.Select(entry => entry.Text)));
        }
    }

    protected override void OnProjectChanged()
    {
        base.OnProjectChanged();
        if (ListText.Join(Entries.Select(entry => entry.Text)) != ListText.Join(ListText.Split(Value)))
        {
            Load();
        }
    }

    private void Load()
    {
        loading = true;
        Entries.Clear();
        foreach (string part in ListText.Split(Value))
        {
            Entries.Add(new ListEntryViewModel(this, part));
        }
        loading = false;
        OnPropertyChanged(nameof(IsEmpty));
    }
}
