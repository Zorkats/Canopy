using System.Collections.ObjectModel;
using System.ComponentModel;
using CanopyStudio.Core.Lua;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create;

// One Lua file open in the Lua view. A new file is not on disk until the
// project is saved, so it starts unsaved. A file that could not be read is
// shown read only, so saving cannot write over it.
public sealed partial class LuaFileViewModel : ObservableObject
{
    // The text as last read or saved; null for a file not on disk yet.
    private string? savedText;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsDirty))]
    [NotifyPropertyChangedFor(nameof(SizeProblem))]
    private string text;

    public LuaFileViewModel(LuaFileName name, string text, bool onDisk, string? error = null)
    {
        Name = name;
        this.text = text;
        savedText = onDisk ? text : null;
        Error = error;
    }

    public LuaFileName Name { get; }
    public string Relative => Name.Relative;
    public string Module => Name.Module;
    public bool IsDirty => Text != savedText;
    public string? Error { get; }
    public bool IsReadOnly => Error is not null;
    public string? SizeProblem => LuaFiles.SizeProblem(Text);

    // How the game runs it.
    public string RunsHow => Module == LuaFiles.MainModule
        ? "Runs first, as the game starts."
        : $"Loaded by require(\"{Module}\").";

    internal void MarkSaved()
    {
        savedText = Text;
        OnPropertyChanged(nameof(IsDirty));
    }
}

// The Lua view of a project: the mod's Lua files, the one being edited, and
// a box that adds a file by its require name. Files are saved with the
// project, never on their own.
public sealed partial class LuaViewModel : ObservableObject
{
    private readonly string folder;
    private readonly Func<bool> unsafeMod;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasFile))]
    private LuaFileViewModel? selectedFile;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(NewNameProblem))]
    [NotifyPropertyChangedFor(nameof(HasNewNameProblem))]
    [NotifyCanExecuteChangedFor(nameof(AddFileCommand))]
    private string newName = "";

    // What the last save could not do, null when it worked.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasMessage))]
    private string? message;

    public LuaViewModel(string folder, Func<bool> unsafeMod)
    {
        this.folder = folder;
        this.unsafeMod = unsafeMod;
        foreach (LuaFileName name in LuaFiles.List(folder))
        {
            string? read = LuaFiles.Read(folder, name, out string? error);
            Add(new LuaFileViewModel(name, read ?? "", onDisk: true, error));
        }
        SelectedFile = Files.FirstOrDefault();
        if (Files.Count == 0)
        {
            NewName = LuaFiles.MainModule;
        }
    }

    public ObservableCollection<LuaFileViewModel> Files { get; } = [];
    public LuaCatalog Catalog => LuaCatalog.Default;
    // An unsafe mod is offered the commands only it may call.
    public bool UnsafeMod => unsafeMod();

    public bool HasFiles => Files.Count > 0;
    public bool HasFile => SelectedFile is not null;
    public bool HasMessage => Message is not null;
    public bool IsDirty => Files.Any(file => file.IsDirty);

    public string? NewNameProblem =>
        NewName.Trim().Length == 0 ? null : LuaFiles.ModuleProblem(LuaFiles.ModuleOf(NewName));

    public bool HasNewNameProblem => NewNameProblem is not null;

    // A file's text changed between saved and not.
    public event EventHandler? DirtyChanged;

    // Writes every unsaved file. Null when all were written, else why the
    // first that failed was not; the ones after it stay unsaved.
    public string? Save()
    {
        foreach (LuaFileViewModel file in Files.Where(file => file.IsDirty).ToList())
        {
            if (LuaFiles.Write(folder, file.Name, file.Text) is { } error)
            {
                return Message = error;
            }
            file.MarkSaved();
        }
        return Message = null;
    }

    private bool CanAddFile() => NewName.Trim().Length > 0 && NewNameProblem is null;

    // Opens the file NewName names, adding it when the mod has none by that
    // name. A new main.lua starts from a template.
    [RelayCommand(CanExecute = nameof(CanAddFile))]
    private void AddFile()
    {
        string module = LuaFiles.ModuleOf(NewName);
        LuaFileViewModel? file = Files.FirstOrDefault(known =>
            string.Equals(known.Module, module, StringComparison.OrdinalIgnoreCase));
        if (file is null)
        {
            file = new LuaFileViewModel(LuaFiles.NameOf(module),
                module == LuaFiles.MainModule ? LuaFiles.MainTemplate : "", onDisk: false);
            Add(file);
            OnFileDirtyChanged();
        }
        SelectedFile = file;
        NewName = "";
    }

    // Puts file in its place: main.lua first, then by path.
    private void Add(LuaFileViewModel file)
    {
        int index = 0;
        while (index < Files.Count && Before(Files[index], file))
        {
            ++index;
        }
        Files.Insert(index, file);
        file.PropertyChanged += OnFilePropertyChanged;
        OnPropertyChanged(nameof(HasFiles));
    }

    private static bool Before(LuaFileViewModel left, LuaFileViewModel right)
    {
        bool leftMain = left.Module == LuaFiles.MainModule;
        bool rightMain = right.Module == LuaFiles.MainModule;
        return leftMain != rightMain
            ? leftMain
            : string.Compare(left.Relative, right.Relative, StringComparison.OrdinalIgnoreCase) < 0;
    }

    private void OnFilePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(LuaFileViewModel.IsDirty))
        {
            OnFileDirtyChanged();
        }
    }

    private void OnFileDirtyChanged()
    {
        OnPropertyChanged(nameof(IsDirty));
        DirtyChanged?.Invoke(this, EventArgs.Empty);
    }
}
