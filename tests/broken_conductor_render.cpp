#include "generative/ConductorEngine.h"
#include "generative/Scale.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using pfl::generative::ConductorEngine;
using pfl::generative::MidiMsgKind;
using pfl::generative::MidiTraceEvent;

static const char* noteName (int midi)
{
    static thread_local char buf[8];
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int pc = ((midi % 12) + 12) % 12;
    const int oct = midi / 12 - 1;
    std::snprintf (buf, sizeof buf, "%s%d", names[pc], oct);
    return buf;
}

static void writeTrace (const fs::path& path, const std::vector<MidiTraceEvent>& ev,
                        const ConductorEngine& eng)
{
    std::ofstream out (path);
    out << "# Broken Conductor Stage 2 trace\n";
    out << "# algorithm=" << ConductorEngine::kAlgorithmVersion
        << " seed=" << eng.masterSeed()
        << " density=" << eng.params().density
        << " mutation=" << eng.params().mutation << "\n";
    out << "# rhythmDNA " << eng.rhythm().dna().describe() << "\n";
    out << "# format: bar.beat.slot EVENT ...\n";

    for (const auto& e : eng.rhythm().traces())
        out << "# rhythm " << e.kind << " bar=" << e.bar << " " << e.detail << "\n";

    for (const auto& e : ev)
    {
        const double beatsPerBar = 4.0;
        const int bar = static_cast<int> (std::floor (e.ppq / beatsPerBar)) + 1;
        const double inBar = e.ppq - static_cast<double> (bar - 1) * beatsPerBar;
        const int beat = static_cast<int> (std::floor (inBar)) + 1;
        const int slot = static_cast<int> (std::lround ((inBar - std::floor (inBar)) / 0.25));
        char loc[32];
        std::snprintf (loc, sizeof loc, "%d.%d.%d", bar, beat, slot);

        if (e.kind == MidiMsgKind::NoteOn)
            out << loc << " NOTE " << noteName (e.note) << " midi=" << e.note
                << " vel=" << e.velocity << "\n";
        else if (e.kind == MidiMsgKind::NoteOff)
            out << loc << " OFF  " << noteName (e.note) << " midi=" << e.note << "\n";
    }
}

/** Minimal type-0 SMF writer (tempo 72 BPM hardcoded meta optional). */
static void writeSmf (const fs::path& path, const std::vector<MidiTraceEvent>& ev, double bpm)
{
    const int tpq = 480; // ticks per quarter
    const double beatsPerSec = bpm / 60.0;
    (void) beatsPerSec;

    struct MidiEv
    {
        int tick;
        uint8_t status;
        uint8_t d1;
        uint8_t d2;
    };
    std::vector<MidiEv> events;
    for (const auto& e : ev)
    {
        MidiEv m;
        m.tick = static_cast<int> (std::lround (e.ppq * tpq));
        if (e.kind == MidiMsgKind::NoteOn)
        {
            m.status = static_cast<uint8_t> (0x90 | ((e.channel - 1) & 0x0f));
            m.d1 = static_cast<uint8_t> (e.note);
            m.d2 = static_cast<uint8_t> (e.velocity);
        }
        else if (e.kind == MidiMsgKind::NoteOff)
        {
            m.status = static_cast<uint8_t> (0x80 | ((e.channel - 1) & 0x0f));
            m.d1 = static_cast<uint8_t> (e.note);
            m.d2 = 0;
        }
        else
            continue;
        events.push_back (m);
    }
    std::sort (events.begin(), events.end(), [] (const MidiEv& a, const MidiEv& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        // note-offs before note-ons at same tick
        const bool aOff = (a.status & 0xf0) == 0x80;
        const bool bOff = (b.status & 0xf0) == 0x80;
        return aOff && ! bOff;
    });

    auto writeU32 = [] (std::vector<uint8_t>& b, uint32_t v) {
        b.push_back (static_cast<uint8_t> ((v >> 24) & 0xff));
        b.push_back (static_cast<uint8_t> ((v >> 16) & 0xff));
        b.push_back (static_cast<uint8_t> ((v >> 8) & 0xff));
        b.push_back (static_cast<uint8_t> (v & 0xff));
    };
    auto writeU16 = [] (std::vector<uint8_t>& b, uint16_t v) {
        b.push_back (static_cast<uint8_t> ((v >> 8) & 0xff));
        b.push_back (static_cast<uint8_t> (v & 0xff));
    };
    auto writeVar = [] (std::vector<uint8_t>& b, uint32_t v) {
        uint8_t buf[5];
        int n = 0;
        buf[n++] = static_cast<uint8_t> (v & 0x7f);
        while ((v >>= 7) > 0)
            buf[n++] = static_cast<uint8_t> ((v & 0x7f) | 0x80);
        while (n--)
            b.push_back (buf[n]);
    };

    std::vector<uint8_t> track;
    // tempo meta
    writeVar (track, 0);
    track.push_back (0xff);
    track.push_back (0x51);
    track.push_back (0x03);
    const uint32_t usPerQuarter = static_cast<uint32_t> (std::lround (60000000.0 / bpm));
    track.push_back (static_cast<uint8_t> ((usPerQuarter >> 16) & 0xff));
    track.push_back (static_cast<uint8_t> ((usPerQuarter >> 8) & 0xff));
    track.push_back (static_cast<uint8_t> (usPerQuarter & 0xff));

    int lastTick = 0;
    for (const auto& e : events)
    {
        writeVar (track, static_cast<uint32_t> (std::max (0, e.tick - lastTick)));
        lastTick = e.tick;
        track.push_back (e.status);
        track.push_back (e.d1);
        track.push_back (e.d2);
    }
    writeVar (track, 0);
    track.push_back (0xff);
    track.push_back (0x2f);
    track.push_back (0x00);

    std::vector<uint8_t> file;
    file.insert (file.end(), { 'M', 'T', 'h', 'd' });
    writeU32 (file, 6);
    writeU16 (file, 0); // format 0
    writeU16 (file, 1);
    writeU16 (file, static_cast<uint16_t> (tpq));
    file.insert (file.end(), { 'M', 'T', 'r', 'k' });
    writeU32 (file, static_cast<uint32_t> (track.size()));
    file.insert (file.end(), track.begin(), track.end());

    std::ofstream out (path, std::ios::binary);
    out.write (reinterpret_cast<const char*> (file.data()), static_cast<std::streamsize> (file.size()));
}

struct Metrics
{
    int bars = 0;
    int noteOns = 0;
    int restsApprox = 0;
    double sumDur = 0.0;
    int durCount = 0;
    int offbeat = 0;
    int odd16 = 0;
    int velMin = 127;
    int velMax = 0;
    int rhythmGens = 0;
};

static Metrics computeMetrics (const std::vector<MidiTraceEvent>& ev, int bars, int rhythmGens)
{
    Metrics m;
    m.bars = bars;
    m.rhythmGens = rhythmGens;
    std::map<std::pair<int, int>, double> open;
    for (const auto& e : ev)
    {
        if (e.kind == MidiMsgKind::NoteOn)
        {
            ++m.noteOns;
            m.velMin = std::min (m.velMin, e.velocity);
            m.velMax = std::max (m.velMax, e.velocity);
            const double beatFrac = e.ppq - std::floor (e.ppq);
            if (std::abs (beatFrac - 0.5) < 1.0e-6)
                ++m.offbeat;
            const int slot = static_cast<int> (std::lround (e.ppq / 0.25));
            if (slot % 2 != 0)
                ++m.odd16;
            open[{ e.channel, e.note }] = e.ppq;
        }
        else if (e.kind == MidiMsgKind::NoteOff)
        {
            auto it = open.find ({ e.channel, e.note });
            if (it != open.end())
            {
                m.sumDur += e.ppq - it->second;
                ++m.durCount;
                open.erase (it);
            }
        }
    }
    return m;
}

struct RunResult
{
    std::vector<MidiTraceEvent> events;
    ConductorEngine engine;
};

static RunResult runFull (uint64_t seed, float density, float mutation, int bars)
{
    RunResult r;
    r.engine.setCapture (true);
    r.engine.setParams ({ density, mutation });
    r.engine.reseed (seed);
    r.engine.rhythm().setTraceEnabled (true);
    // Traces only capture future mutations; birth already happened — OK for metrics.

    const double bpm = 72.0;
    const double endPpq = static_cast<double> (bars) * 4.0;
    double ppq = 0.0;
    while (ppq < endPpq - 1.0e-12)
    {
        const double next = std::min (endPpq, ppq + 1.0);
        r.engine.clock().advance ({ true, ppq, bpm, 4, 4 });
        r.engine.processTimeRange (ppq, next, true);
        r.engine.drainPending();
        ppq = next;
    }
    r.events = r.engine.captured();
    return r;
}

int main (int argc, char** argv)
{
    const fs::path outDir = (argc > 1) ? fs::path (argv[1])
                                       : fs::path ("renders/broken-conductor");
    fs::create_directories (outDir);

    std::ofstream metrics (outDir / "stage2-metrics.txt");
    metrics << "Broken Conductor Stage 2 objective metrics (72 BPM, 64 bars)\n\n";

    struct Case
    {
        const char* label;
        uint64_t seed;
        float density;
        float mutation;
    };

    const Case cases[] = {
        { "seed-1001", 1001, 0.45f, 0.35f },
        { "seed-2002", 2002, 0.45f, 0.35f },
        { "seed-3003", 3003, 0.45f, 0.35f },
        { "seed-2002-d20", 2002, 0.20f, 0.35f },
        { "seed-2002-d50", 2002, 0.50f, 0.35f },
        { "seed-2002-d80", 2002, 0.80f, 0.35f },
        { "seed-2002-m10", 2002, 0.45f, 0.10f },
        { "seed-2002-m40", 2002, 0.45f, 0.40f },
        { "seed-2002-m80", 2002, 0.45f, 0.80f },
    };

    constexpr int kBars = 64;
    for (const auto& c : cases)
    {
        auto result = runFull (c.seed, c.density, c.mutation, kBars);
        const std::string base = std::string ("stage2-") + c.label;
        writeTrace (outDir / (base + ".txt"), result.events, result.engine);
        writeSmf (outDir / (base + ".mid"), result.events, 72.0);

        const auto m = computeMetrics (result.events, kBars, result.engine.rhythm().dna().generation);
        const double notesPerBar = static_cast<double> (m.noteOns) / static_cast<double> (kBars);
        const double meanDur = m.durCount > 0 ? m.sumDur / m.durCount : 0.0;
        const double pctOff = m.noteOns > 0 ? 100.0 * m.offbeat / m.noteOns : 0.0;
        const double pct16 = m.noteOns > 0 ? 100.0 * m.odd16 / m.noteOns : 0.0;

        metrics << c.label
                << " notes/bar=" << notesPerBar
                << " meanDur=" << meanDur
                << " offbeat%=" << pctOff
                << " odd16%=" << pct16
                << " vel=[" << m.velMin << "," << m.velMax << "]"
                << " rhythmGen=" << m.rhythmGens
                << " occupancy=" << result.engine.rhythm().dna().occupancy()
                << "\n";

        std::cout << "Wrote " << base << " (" << result.events.size() << " events)\n";
    }

    std::cout << "Metrics: " << (outDir / "stage2-metrics.txt") << "\n";
    return 0;
}
