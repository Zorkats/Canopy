namespace CanopyStudio.Core.Setup;

// The steps of first-time setup, in the order they are shown.
public enum OnboardingStep
{
    Welcome,
    GameFolder,
    GameAssets,
    Canopy,
    Theme,
    Start,
}

public enum OnboardingState
{
    Pending,
    Done,
    Skipped,
}

// Where the user asked to go when setup finished, or Stay when they left it
// without picking an area.
public enum OnboardingDestination
{
    Stay,
    Play,
    Create,
    Game,
}

// First-time setup as plain data: the current step, what each step came to,
// and whether finishing counts as setup done. The caller does the work of
// each step and tells the flow when one is done; the flow keeps nothing on
// disk. The user can leave at any time: every step can be skipped and run
// again later.
public sealed class OnboardingFlow
{
    private readonly OnboardingState[] states = new OnboardingState[Steps.Count];

    // wasSetupDone and folderInUse describe Canopy Studio as setup starts. On
    // a rerun the folder in use counts as chosen, so the folder step starts
    // done; before the first setup a folder from older settings still needs
    // the user's yes.
    public OnboardingFlow(bool wasSetupDone, bool folderInUse)
    {
        WasSetupDone = wasSetupDone;
        if (wasSetupDone && folderInUse)
        {
            HasFolder = true;
            states[(int)OnboardingStep.GameFolder] = OnboardingState.Done;
        }
    }

    public static IReadOnlyList<OnboardingStep> Steps { get; } =
    [
        OnboardingStep.Welcome,
        OnboardingStep.GameFolder,
        OnboardingStep.GameAssets,
        OnboardingStep.Canopy,
        OnboardingStep.Theme,
        OnboardingStep.Start,
    ];

    public bool WasSetupDone { get; }

    // True once a game folder whose checks pass was chosen.
    public bool HasFolder { get; private set; }

    public OnboardingStep Current { get; private set; } = OnboardingStep.Welcome;

    public int CurrentIndex => (int)Current;

    public bool IsFirst => Current == Steps[0];
    public bool IsLast => Current == Steps[^1];

    public bool IsFinished { get; private set; }

    public OnboardingDestination Destination { get; private set; } = OnboardingDestination.Stay;

    // Setup counts as done once the folder step is; a setup done before stays
    // done whatever this run came to.
    public bool SetupDone => WasSetupDone || State(OnboardingStep.GameFolder) == OnboardingState.Done;

    public OnboardingState State(OnboardingStep step) => states[(int)step];

    // Steps whose Next needs their work done first. The others are done by
    // reading them (Welcome) or already hold a valid choice (Theme).
    public static bool NeedsWork(OnboardingStep step) =>
        step is OnboardingStep.GameFolder or OnboardingStep.GameAssets or OnboardingStep.Canopy;

    public bool CanBack => !IsFinished && !IsFirst;

    // Start has no Next: picking an area finishes setup.
    public bool CanNext => !IsFinished && !IsLast &&
                           (State(Current) == OnboardingState.Done || !NeedsWork(Current));

    public bool CanSkip => !IsFinished;

    // The folder in use is now one whose checks pass. Marks the folder step done.
    public void ChooseFolder()
    {
        HasFolder = true;
        states[(int)OnboardingStep.GameFolder] = OnboardingState.Done;
    }

    // step's work is done. False, and nothing changes, for the folder step
    // without a chosen folder or once setup has finished.
    public bool Complete(OnboardingStep step)
    {
        if (IsFinished || (step == OnboardingStep.GameFolder && !HasFolder))
        {
            return false;
        }
        states[(int)step] = OnboardingState.Done;
        return true;
    }

    public bool Next()
    {
        if (!CanNext)
        {
            return false;
        }
        states[CurrentIndex] = OnboardingState.Done;
        Current = Steps[CurrentIndex + 1];
        return true;
    }

    public bool Back()
    {
        if (!CanBack)
        {
            return false;
        }
        Current = Steps[CurrentIndex - 1];
        return true;
    }

    // Moves on without the current step's work. A step already done stays
    // done. Skipping Start leaves setup without picking an area.
    public bool Skip()
    {
        if (!CanSkip)
        {
            return false;
        }
        if (State(Current) != OnboardingState.Done)
        {
            states[CurrentIndex] = OnboardingState.Skipped;
        }
        if (IsLast)
        {
            IsFinished = true;
            return true;
        }
        Current = Steps[CurrentIndex + 1];
        return true;
    }

    // Picks the area to open from Start, which finishes setup.
    public bool Choose(OnboardingDestination destination)
    {
        if (IsFinished || !IsLast)
        {
            return false;
        }
        states[CurrentIndex] = OnboardingState.Done;
        Destination = destination;
        IsFinished = true;
        return true;
    }

    // Leaves setup from any step. Steps not reached stay pending.
    public void Leave()
    {
        IsFinished = true;
    }
}
