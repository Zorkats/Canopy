namespace CanopyStudio.Core.Sound;

// A part of a sound played over and over, in frames: once playback reaches
// End it goes on from Start. Music plays its intro, then repeats its loop.
public readonly record struct SoundLoop(long Start, long End)
{
    public bool Fits(DecodedSound sound) => Start >= 0 && End > Start && End <= sound.Frames;
}

// Plays one decoded sound at a time. Nothing plays until Play; a player that
// cannot start says why in Problem instead of throwing. Positions are in
// frames of the sound playing. Use it from one thread.
public interface ISoundPlayer : IDisposable
{
    // Why sound cannot be played, null when it can or has not been tried.
    string? Problem { get; }

    // The sound last given to Play, null before the first.
    DecodedSound? Sound { get; }

    // True from Play until the end, Stop or Pause.
    bool IsPlaying { get; }

    bool IsPaused { get; }

    long Position { get; }

    // From 0, silent, to 1, as decoded.
    float Volume { get; set; }

    // The part that repeats, null to play once to the end. A loop that does
    // not fit the sound is ignored.
    SoundLoop? Loop { get; set; }

    // Plays sound from its start. Null when it plays, else why not.
    string? Play(DecodedSound sound, SoundLoop? loop = null);

    void Pause();

    void Resume();

    // Stops and goes back to the start.
    void Stop();

    void Seek(long frame);
}
