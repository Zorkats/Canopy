using CanopyStudio.Core.Setup;

namespace CanopyStudio.Tests;

public sealed class OnboardingTests
{
    private static OnboardingFlow FirstRun() => new(wasSetupDone: false, folderInUse: false);

    [Fact]
    public void StepsRunInOrder()
    {
        Assert.Equal(
            [
                OnboardingStep.Welcome, OnboardingStep.GameFolder, OnboardingStep.GameAssets,
                OnboardingStep.Canopy, OnboardingStep.Theme, OnboardingStep.Start,
            ],
            OnboardingFlow.Steps);
    }

    [Fact]
    public void FirstRunStartsAtWelcomeWithEveryStepPending()
    {
        OnboardingFlow flow = FirstRun();

        Assert.Equal(OnboardingStep.Welcome, flow.Current);
        Assert.True(flow.IsFirst);
        Assert.False(flow.CanBack);
        Assert.False(flow.HasFolder);
        Assert.False(flow.SetupDone);
        Assert.All(OnboardingFlow.Steps, step => Assert.Equal(OnboardingState.Pending, flow.State(step)));
    }

    [Fact]
    public void NextMarksTheStepDoneAndMovesOn()
    {
        OnboardingFlow flow = FirstRun();

        Assert.True(flow.Next());

        Assert.Equal(OnboardingStep.GameFolder, flow.Current);
        Assert.Equal(OnboardingState.Done, flow.State(OnboardingStep.Welcome));
    }

    [Fact]
    public void BackMovesWithoutChangingStates()
    {
        OnboardingFlow flow = FirstRun();
        flow.Next();
        flow.Skip();

        Assert.True(flow.Back());

        Assert.Equal(OnboardingStep.GameFolder, flow.Current);
        Assert.Equal(OnboardingState.Skipped, flow.State(OnboardingStep.GameFolder));
        Assert.True(flow.Back());
        Assert.False(flow.Back());
        Assert.Equal(OnboardingStep.Welcome, flow.Current);
    }

    [Fact]
    public void SkipMarksTheStepSkippedAndMovesOn()
    {
        OnboardingFlow flow = FirstRun();
        flow.Next();

        Assert.True(flow.Skip());

        Assert.Equal(OnboardingStep.GameAssets, flow.Current);
        Assert.Equal(OnboardingState.Skipped, flow.State(OnboardingStep.GameFolder));
    }

    [Fact]
    public void SkipKeepsADoneStepDone()
    {
        OnboardingFlow flow = FirstRun();
        flow.Next();
        flow.ChooseFolder();

        flow.Skip();

        Assert.Equal(OnboardingState.Done, flow.State(OnboardingStep.GameFolder));
    }

    [Fact]
    public void StepsWithWorkNeedItDoneBeforeNext()
    {
        OnboardingFlow flow = FirstRun();
        flow.Next();
        flow.ChooseFolder();
        flow.Next();

        Assert.Equal(OnboardingStep.GameAssets, flow.Current);
        Assert.False(flow.CanNext);
        Assert.False(flow.Next());
        Assert.True(flow.Complete(OnboardingStep.GameAssets));
        Assert.True(flow.Next());
        Assert.Equal(OnboardingStep.Canopy, flow.Current);
        Assert.False(flow.CanNext);
        Assert.True(flow.Skip());
        Assert.Equal(OnboardingStep.Theme, flow.Current);
        Assert.True(flow.CanNext);
    }

    [Fact]
    public void FolderStepCannotBeDoneWithoutAFolder()
    {
        OnboardingFlow flow = FirstRun();
        flow.Next();

        Assert.False(flow.CanNext);
        Assert.False(flow.Next());
        Assert.False(flow.Complete(OnboardingStep.GameFolder));
        Assert.Equal(OnboardingState.Pending, flow.State(OnboardingStep.GameFolder));
        Assert.Equal(OnboardingStep.GameFolder, flow.Current);

        flow.ChooseFolder();

        Assert.True(flow.HasFolder);
        Assert.Equal(OnboardingState.Done, flow.State(OnboardingStep.GameFolder));
        Assert.True(flow.CanNext);
    }

    [Fact]
    public void StartHasNoNextAndChoosingFinishes()
    {
        OnboardingFlow flow = FirstRun();
        while (!flow.IsLast)
        {
            flow.Skip();
        }

        Assert.Equal(OnboardingStep.Start, flow.Current);
        Assert.False(flow.CanNext);
        Assert.True(flow.Choose(OnboardingDestination.Create));
        Assert.True(flow.IsFinished);
        Assert.Equal(OnboardingDestination.Create, flow.Destination);
        Assert.Equal(OnboardingState.Done, flow.State(OnboardingStep.Start));
    }

    [Fact]
    public void ChoosingBeforeStartDoesNothing()
    {
        OnboardingFlow flow = FirstRun();

        Assert.False(flow.Choose(OnboardingDestination.Play));
        Assert.False(flow.IsFinished);
        Assert.Equal(OnboardingDestination.Stay, flow.Destination);
    }

    [Fact]
    public void SkippingStartFinishesWithoutAnArea()
    {
        OnboardingFlow flow = FirstRun();
        while (!flow.IsFinished)
        {
            flow.Skip();
        }

        Assert.Equal(OnboardingDestination.Stay, flow.Destination);
        Assert.Equal(OnboardingState.Skipped, flow.State(OnboardingStep.Start));
    }

    [Fact]
    public void FinishingWithTheFolderStepMarksSetupDone()
    {
        OnboardingFlow flow = FirstRun();
        flow.Next();
        flow.ChooseFolder();
        flow.Next();
        while (!flow.IsLast)
        {
            flow.Skip();
        }

        flow.Choose(OnboardingDestination.Play);

        Assert.True(flow.SetupDone);
    }

    [Fact]
    public void FinishingWithoutTheFolderStepLeavesSetupNotDone()
    {
        OnboardingFlow flow = FirstRun();
        while (!flow.IsLast)
        {
            flow.Skip();
        }

        flow.Choose(OnboardingDestination.Game);

        Assert.True(flow.IsFinished);
        Assert.False(flow.SetupDone);
    }

    [Fact]
    public void LeavingFromAnyStepFinishes()
    {
        OnboardingFlow flow = FirstRun();
        flow.Next();

        flow.Leave();

        Assert.True(flow.IsFinished);
        Assert.False(flow.SetupDone);
        Assert.Equal(OnboardingState.Pending, flow.State(OnboardingStep.GameAssets));
        Assert.False(flow.Next());
        Assert.False(flow.Back());
        Assert.False(flow.Skip());
        Assert.False(flow.Complete(OnboardingStep.GameAssets));
    }

    [Fact]
    public void FolderFromOlderSettingsStillNeedsChoosing()
    {
        OnboardingFlow flow = new(wasSetupDone: false, folderInUse: true);

        Assert.False(flow.HasFolder);
        Assert.Equal(OnboardingState.Pending, flow.State(OnboardingStep.GameFolder));
    }

    [Fact]
    public void RerunStartsAtWelcomeWithTheFolderChosen()
    {
        OnboardingFlow flow = new(wasSetupDone: true, folderInUse: true);

        Assert.Equal(OnboardingStep.Welcome, flow.Current);
        Assert.True(flow.HasFolder);
        Assert.Equal(OnboardingState.Done, flow.State(OnboardingStep.GameFolder));
        flow.Next();
        Assert.True(flow.CanNext);
    }

    [Fact]
    public void RerunKeepsADoneSetupDone()
    {
        OnboardingFlow skipped = new(wasSetupDone: true, folderInUse: false);
        while (!skipped.IsFinished)
        {
            skipped.Skip();
        }
        OnboardingFlow left = new(wasSetupDone: true, folderInUse: true);
        left.Leave();

        Assert.Equal(OnboardingState.Skipped, skipped.State(OnboardingStep.GameFolder));
        Assert.True(skipped.SetupDone);
        Assert.True(left.SetupDone);
    }
}
