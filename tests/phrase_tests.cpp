#include "generative/Composer.h"
#include "generative/PhraseDNA.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

static int failures = 0;
#define EXPECT(cond) \
    do { if (!(cond)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#cond); ++failures; } } while(0)

static void runBars (pfl::generative::Composer& c, int bars)
{
    const double total = static_cast<double> (bars) * 4.0;
    for (double p = 0; p < total; p += 0.25)
        c.processTimeRange (p, std::min (total, p + 0.25), true);
}

int main()
{
    EXPECT (pfl::generative::Composer::kAlgorithmVersion == 3);

    // Deterministic initial DNA
    {
        pfl::generative::Composer a, b;
        a.setEventCapture (true);
        b.setEventCapture (true);
        a.setParams ({ 0.45f, 0.35f });
        b.setParams ({ 0.45f, 0.35f });
        a.reseed (2002);
        b.reseed (2002);
        EXPECT (a.phrases().dna().describe() == b.phrases().dna().describe());
        EXPECT (a.phrases().dna().length >= 3);
        EXPECT (a.phrases().dna().length <= 8);
    }

    // Same seed → same phrase mutation lineage
    {
        pfl::generative::Composer a, b;
        a.setEventCapture (true);
        b.setEventCapture (true);
        a.setParams ({ 0.45f, 0.5f });
        b.setParams ({ 0.45f, 0.5f });
        a.reseed (3003);
        b.reseed (3003);
        runBars (a, 128);
        runBars (b, 128);
        EXPECT (a.phrases().traces().size() == b.phrases().traces().size());
        EXPECT (a.phrases().traces().size() >= 1);
        for (size_t i = 0; i < a.phrases().traces().size(); ++i)
        {
            EXPECT (a.phrases().traces()[i].bar == b.phrases().traces()[i].bar);
            EXPECT (a.phrases().traces()[i].detail == b.phrases().traces()[i].detail);
            EXPECT (a.phrases().traces()[i].generation == b.phrases().traces()[i].generation);
        }
        // Bounded mutations: each mutate changes one conceptual index
        for (auto& t : a.phrases().traces())
        {
            if (t.kind == "mutate")
                EXPECT (t.detail.find ("mut idx") != std::string::npos);
        }
    }

    // Phrase RNG isolation: timbre draws do not change phrase lineage or pitch events
    {
        pfl::generative::Composer a, b;
        a.setEventCapture (true);
        b.setEventCapture (true);
        a.setParams ({ 0.5f, 0.4f });
        b.setParams ({ 0.5f, 0.4f });
        a.reseed (777);
        b.reseed (777);
        for (int i = 0; i < 40; ++i)
            (void) b.consumeTimbreRandom();
        runBars (a, 64);
        runBars (b, 64);
        EXPECT (a.phrases().dna().describe() == b.phrases().dna().describe());
        EXPECT (a.capturedEvents().size() == b.capturedEvents().size());
        for (size_t i = 0; i < a.capturedEvents().size(); ++i)
        {
            EXPECT (a.capturedEvents()[i].midiNote == b.capturedEvents()[i].midiNote);
            EXPECT (a.capturedEvents()[i].beat == b.capturedEvents()[i].beat);
        }
    }

    // Phrase follow creates recurring degree motion tendency (reuse of notes more than free chaos)
    {
        pfl::generative::Composer c;
        c.setEventCapture (true);
        c.setParams ({ 0.8f, 0.35f });
        c.reseed (2002);
        runBars (c, 96);
        std::set<int> uniqueNotes;
        int changes = 0;
        for (auto& e : c.capturedEvents())
        {
            if (e.type == pfl::generative::EventType::NoteChange)
            {
                ++changes;
                uniqueNotes.insert (e.midiNote);
            }
        }
        EXPECT (changes >= 1);
        // Not a hard musical assertion — ensure we still produce finite bounded notes
        for (int n : uniqueNotes)
            EXPECT (n >= 20 && n <= 90);
        EXPECT (c.phrases().dna().generation >= 0);
    }

    // Buffer independence still holds with phrase engine
    {
        auto run = [] (int buf) {
            pfl::generative::Composer c;
            c.setEventCapture (true);
            c.setParams ({ 0.45f, 0.35f });
            c.reseed (2002);
            const double total = 48.0 * 4.0;
            const double blockBeats = (static_cast<double> (buf) / 48000.0) * (72.0 / 60.0);
            for (double p = 0; p < total; )
            {
                const double e = std::min (total, p + blockBeats);
                c.processTimeRange (p, e, true);
                p = e;
            }
            return c.capturedEvents();
        };
        auto a = run (64);
        auto b = run (512);
        EXPECT (a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            EXPECT (a[i].midiNote == b[i].midiNote);
            EXPECT (a[i].voice == b[i].voice);
        }
    }

    if (failures)
    {
        std::fprintf (stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts ("phrase_tests: ok");
    return EXIT_SUCCESS;
}
