#include "generative/Composer.h"
#include "generative/DeterministicRNG.h"
#include "generative/Scale.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int failures = 0;

#define EXPECT(cond) \
    do { \
        if (! (cond)) { \
            std::fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures; \
        } \
    } while (0)

static std::vector<pfl::generative::MusicalEvent>
runComposition (uint64_t seed, float density, float mutation, double bpm, int bars, int bufferSamples, double sampleRate)
{
    pfl::generative::Composer c;
    c.setEventCapture (true);
    pfl::generative::ComposerParams p;
    p.density = density;
    p.mutation = mutation;
    c.setParams (p);
    c.reseed (seed);

    const double beatsPerBar = 4.0;
    const double totalBeats = static_cast<double> (bars) * beatsPerBar;
    const double beatsPerSec = bpm / 60.0;
    const double blockBeats = (static_cast<double> (bufferSamples) / sampleRate) * beatsPerSec;

    double ppq = 0.0;
    while (ppq < totalBeats)
    {
        const double end = std::min (totalBeats, ppq + blockBeats);
        pfl::generative::ClockSnapshot snap;
        snap.playing = true;
        snap.ppq = ppq;
        snap.tempoBpm = bpm;
        snap.timeSigNumerator = 4;
        c.clock().advance (snap);
        c.processTimeRange (ppq, end, true);
        ppq = end;
    }
    return c.capturedEvents();
}

static bool eventsEqual (const std::vector<pfl::generative::MusicalEvent>& a,
                         const std::vector<pfl::generative::MusicalEvent>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].voice != b[i].voice)
            return false;
        if (a[i].midiNote != b[i].midiNote)
            return false;
        if (static_cast<int> (a[i].type) != static_cast<int> (b[i].type))
            return false;
        if (std::abs (a[i].beat - b[i].beat) > 1.0e-6)
            return false;
    }
    return true;
}

static std::string fingerprint (const std::vector<pfl::generative::MusicalEvent>& e)
{
    std::string s;
    for (auto& x : e)
    {
        char buf[64];
        std::snprintf (buf, sizeof buf, "%d:%d:%d:%.3f;",
                       x.voice, x.midiNote, static_cast<int> (x.type), x.beat);
        s += buf;
    }
    return s;
}

int main()
{
    // Scale sanity
    {
        EXPECT (pfl::generative::Scale::toMidi (0, 2) == 38); // D2
        EXPECT (pfl::generative::Scale::toMidi (3, 2) == 45); // A2
        EXPECT (pfl::generative::Scale::toMidi (0, 3) == 50); // D3
    }

    // Determinism: same seed twice
    {
        auto a = runComposition (12345, 0.45f, 0.35f, 72.0, 64, 256, 48000.0);
        auto b = runComposition (12345, 0.45f, 0.35f, 72.0, 64, 256, 48000.0);
        EXPECT (eventsEqual (a, b));
        EXPECT (a.size() >= 2);
    }

    // Multiple seeds deterministic
    for (uint64_t seed : { 1001ull, 2002ull, 3003ull, 424242ull })
    {
        auto a = runComposition (seed, 0.5f, 0.4f, 72.0, 32, 128, 44100.0);
        auto b = runComposition (seed, 0.5f, 0.4f, 72.0, 32, 128, 44100.0);
        EXPECT (eventsEqual (a, b));
    }

    // Different seeds differ
    {
        auto a = runComposition (1001, 0.45f, 0.35f, 72.0, 64, 256, 48000.0);
        auto b = runComposition (1002, 0.45f, 0.35f, 72.0, 64, 256, 48000.0);
        EXPECT (fingerprint (a) != fingerprint (b));
    }

    // Buffer-size independence
    {
        auto e64 = runComposition (777, 0.45f, 0.35f, 72.0, 48, 64, 48000.0);
        auto e128 = runComposition (777, 0.45f, 0.35f, 72.0, 48, 128, 48000.0);
        auto e256 = runComposition (777, 0.45f, 0.35f, 72.0, 48, 256, 48000.0);
        auto e512 = runComposition (777, 0.45f, 0.35f, 72.0, 48, 512, 48000.0);
        auto e1024 = runComposition (777, 0.45f, 0.35f, 72.0, 48, 1024, 48000.0);
        EXPECT (eventsEqual (e64, e128));
        EXPECT (eventsEqual (e64, e256));
        EXPECT (eventsEqual (e64, e512));
        EXPECT (eventsEqual (e64, e1024));
    }

    // Tempo: same number of bars → comparable structure in musical time (bar-based)
    // Event beats should land on bar boundaries regardless of BPM.
    {
        auto slow = runComposition (55, 0.5f, 0.4f, 40.0, 16, 256, 48000.0);
        auto fast = runComposition (55, 0.5f, 0.4f, 180.0, 16, 256, 48000.0);
        EXPECT (eventsEqual (slow, fast)); // decisions are bar-based, not wall-clock
    }

    // RNG stream isolation: timbre draws must not alter pitch events
    {
        pfl::generative::Composer a, b;
        a.setEventCapture (true);
        b.setEventCapture (true);
        pfl::generative::ComposerParams p { 0.5f, 0.4f };
        a.setParams (p);
        b.setParams (p);
        a.reseed (999);
        b.reseed (999);

        for (int i = 0; i < 50; ++i)
            (void) b.consumeTimbreRandom();

        const double totalBeats = 64.0 * 4.0;
        for (double ppq = 0; ppq < totalBeats; ppq += 0.5)
        {
            a.processTimeRange (ppq, ppq + 0.5, true);
            b.processTimeRange (ppq, ppq + 0.5, true);
        }
        EXPECT (eventsEqual (a.capturedEvents(), b.capturedEvents()));
    }

    // Restart from beginning reproduces
    {
        auto first = runComposition (2002, 0.45f, 0.35f, 72.0, 24, 256, 48000.0);
        auto second = runComposition (2002, 0.45f, 0.35f, 72.0, 24, 256, 48000.0);
        EXPECT (eventsEqual (first, second));
    }

    // Density 0%: single foundation voice; 100%: three voices
    {
        auto sparse = runComposition (2002, 0.0f, 0.35f, 72.0, 16, 256, 48000.0);
        auto dense = runComposition (2002, 1.0f, 0.35f, 72.0, 16, 256, 48000.0);
        int enterS = 0, enterD = 0;
        for (auto& e : sparse)
            if (e.type == pfl::generative::EventType::VoiceEnter)
                ++enterS;
        for (auto& e : dense)
            if (e.type == pfl::generative::EventType::VoiceEnter)
                ++enterD;
        EXPECT (enterS == 1);
        EXPECT (enterD == 3);
        EXPECT (fingerprint (sparse) != fingerprint (dense));
    }

    // Mutation changes activity (usually)
    {
        auto low = runComposition (3003, 0.5f, 0.05f, 72.0, 64, 256, 48000.0);
        auto high = runComposition (3003, 0.5f, 0.9f, 72.0, 64, 256, 48000.0);
        int chL = 0, chH = 0;
        for (auto& e : low)
            if (e.type == pfl::generative::EventType::NoteChange)
                ++chL;
        for (auto& e : high)
            if (e.type == pfl::generative::EventType::NoteChange)
                ++chH;
        EXPECT (chH >= chL);
    }

    // Long run: 30 minutes @ 72 BPM = 2160 beats = 540 bars — composer only
    {
        pfl::generative::Composer c;
        c.setParams ({ 0.5f, 0.4f });
        c.reseed (4242);
        const double totalBeats = 30.0 * 60.0 * (72.0 / 60.0);
        double ppq = 0.0;
        const double step = 1.0; // 1 beat steps
        while (ppq < totalBeats)
        {
            const double end = std::min (totalBeats, ppq + step);
            c.processTimeRange (ppq, end, true);
            for (int i = 0; i < 3; ++i)
            {
                const int n = c.voice (i).pitch.midiNote;
                EXPECT (n >= 20 && n <= 90);
            }
            ppq = end;
        }
        int active = 0;
        for (int i = 0; i < 3; ++i)
            if (c.voice (i).active)
                ++active;
        EXPECT (active >= 1 && active <= 3);
    }

    // Seek policy: handleSeek then continue is finite / defined
    {
        pfl::generative::Composer c;
        c.setEventCapture (true);
        c.setParams ({ 0.45f, 0.35f });
        c.reseed (50);
        c.processTimeRange (0.0, 16.0, true);
        c.handleSeek (64.0); // jump forward
        c.processTimeRange (64.0, 80.0, true);
        EXPECT (! c.capturedEvents().empty());
    }

    if (failures != 0)
    {
        std::fprintf (stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }

    std::puts ("composer_tests: ok");
    return EXIT_SUCCESS;
}
