using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Silk.NET.SDL;

namespace CanopyStudio.Core.Sound;

// ISoundPlayer through SDL2 (decision D4). SDL is loaded, and one output
// device opened, on the first Play; SDL's audio thread then pulls samples
// from the sound playing through a callback, which applies the volume and
// the loop. The device takes 32-bit float stereo at the sound's rate, and
// SDL converts to whatever the hardware wants. A mono sound plays on both
// sides.
public sealed unsafe class SdlSoundPlayer : ISoundPlayer
{
    private const int OutputChannels = 2;
    private const ushort BufferFrames = 1024;

    // Shared with the audio thread: everything below is read and written
    // under it.
    private readonly object gate = new();
    private DecodedSound? sound;
    private long position;
    private bool playing;
    private bool paused;
    private float volume = 1f;
    private SoundLoop? loop;

    private readonly string? driver;
    private Sdl? sdl;
    private bool started;
    private uint device;
    private int deviceRate;
    private GCHandle self;
    private bool disposed;

    // driver names an SDL audio driver to use, such as "dummy" for tests,
    // as the SDL_AUDIODRIVER variable would; null lets SDL choose.
    public SdlSoundPlayer(string? driver = null) => this.driver = driver;

    public string? Problem { get; private set; }

    public DecodedSound? Sound
    {
        get
        {
            lock (gate)
            {
                return sound;
            }
        }
    }

    public bool IsPlaying
    {
        get
        {
            lock (gate)
            {
                return playing && !paused;
            }
        }
    }

    public bool IsPaused
    {
        get
        {
            lock (gate)
            {
                return playing && paused;
            }
        }
    }

    public long Position
    {
        get
        {
            lock (gate)
            {
                return position;
            }
        }
    }

    public float Volume
    {
        get
        {
            lock (gate)
            {
                return volume;
            }
        }
        set
        {
            lock (gate)
            {
                volume = Math.Clamp(value, 0f, 1f);
            }
        }
    }

    public SoundLoop? Loop
    {
        get
        {
            lock (gate)
            {
                return loop;
            }
        }
        set
        {
            lock (gate)
            {
                loop = value;
            }
        }
    }

    public string? Play(DecodedSound sound, SoundLoop? loop = null)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (Open(sound.SampleRate) is { } problem)
        {
            return problem;
        }
        lock (gate)
        {
            this.sound = sound;
            this.loop = loop;
            position = 0;
            playing = sound.Frames > 0;
            paused = false;
        }
        sdl!.PauseAudioDevice(device, 0);
        return null;
    }

    public void Pause()
    {
        lock (gate)
        {
            if (!playing)
            {
                return;
            }
            paused = true;
        }
        if (device != 0)
        {
            sdl!.PauseAudioDevice(device, 1);
        }
    }

    public void Resume()
    {
        lock (gate)
        {
            if (!playing || !paused)
            {
                return;
            }
            paused = false;
        }
        sdl!.PauseAudioDevice(device, 0);
    }

    public void Stop()
    {
        lock (gate)
        {
            playing = false;
            paused = false;
            position = 0;
        }
        if (device != 0)
        {
            sdl!.PauseAudioDevice(device, 1);
        }
    }

    public void Seek(long frame)
    {
        lock (gate)
        {
            position = sound is { } current ? Math.Clamp(frame, 0, current.Frames) : 0;
        }
    }

    public void Dispose()
    {
        if (disposed)
        {
            return;
        }
        disposed = true;
        if (sdl is not null)
        {
            if (device != 0)
            {
                // Waits for the callback to return, so nothing runs after.
                sdl.CloseAudioDevice(device);
                device = 0;
            }
            if (started)
            {
                sdl.QuitSubSystem(Sdl.InitAudio);
                started = false;
            }
        }
        if (self.IsAllocated)
        {
            self.Free();
        }
    }

    // Loads SDL, starts its audio and opens the device at rate, once; a
    // sound at another rate opens it again. Null when open, else why not.
    private string? Open(int rate)
    {
        if (device != 0 && deviceRate == rate)
        {
            return null;
        }
        if (Problem is not null && !started)
        {
            return Problem;
        }
        if (sdl is null)
        {
            try
            {
                sdl = Sdl.GetApi();
            }
            catch (Exception exception) when (exception is DllNotFoundException or FileNotFoundException
                or BadImageFormatException or EntryPointNotFoundException or PlatformNotSupportedException
                or InvalidOperationException or TypeInitializationException)
            {
                return Problem = $"The SDL2 library could not be loaded, so sounds cannot play ({exception.Message}).";
            }
        }
        if (!started)
        {
            if (driver is not null)
            {
                sdl.SetHint("SDL_AUDIODRIVER", driver);
            }
            if (sdl.InitSubSystem(Sdl.InitAudio) != 0)
            {
                return Problem = $"SDL could not start sound: {sdl.GetErrorS()} Check that an output device is connected and turned on.";
            }
            started = true;
            self = GCHandle.Alloc(this, GCHandleType.Normal);
        }
        if (device != 0)
        {
            sdl.CloseAudioDevice(device);
            device = 0;
        }
        AudioSpec wanted = new()
        {
            Freq = rate,
            Format = (ushort)Sdl.AudioF32,
            Channels = OutputChannels,
            Samples = BufferFrames,
            Callback = new PfnAudioCallback(&Fill),
            Userdata = (void*)GCHandle.ToIntPtr(self),
        };
        AudioSpec obtained;
        device = sdl.OpenAudioDevice((byte*)null, 0, &wanted, &obtained, 0);
        if (device == 0)
        {
            return Problem = $"SDL could not open a sound output device: {sdl.GetErrorS()} Check that one is connected and turned on.";
        }
        deviceRate = rate;
        Problem = null;
        return null;
    }

    // SDL's audio thread asks for length bytes of float stereo.
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Fill(void* userdata, byte* stream, int length)
    {
        Span<float> output = new(stream, length / sizeof(float));
        try
        {
            if (GCHandle.FromIntPtr((IntPtr)userdata).Target is SdlSoundPlayer player)
            {
                player.Mix(output);
                return;
            }
        }
        catch (InvalidOperationException)
        {
        }
        output.Clear();
    }

    private void Mix(Span<float> output)
    {
        int frames = output.Length / OutputChannels;
        int written = 0;
        lock (gate)
        {
            if (sound is { } current && playing && !paused)
            {
                float[] samples = current.Samples;
                int channels = current.Channels;
                long total = current.Frames;
                SoundLoop? repeat = loop is { } candidate && candidate.Fits(current) ? candidate : null;
                while (written < frames)
                {
                    // Before the loop's end, play up to it; past it, as
                    // after a seek into the outro, play to the end.
                    long end = repeat is { } part && position < part.End ? part.End : total;
                    if (position >= end)
                    {
                        playing = false;
                        position = total;
                        break;
                    }
                    int take = (int)Math.Min(frames - written, end - position);
                    for (int index = 0; index < take; ++index)
                    {
                        long at = (position + index) * channels;
                        float left = samples[at];
                        float right = channels > 1 ? samples[at + 1] : left;
                        output[(written + index) * 2] = left * volume;
                        output[(written + index) * 2 + 1] = right * volume;
                    }
                    written += take;
                    position += take;
                    if (repeat is { } again && end == again.End && position == again.End)
                    {
                        position = again.Start;
                    }
                }
            }
        }
        output[(written * OutputChannels)..].Clear();
    }
}
