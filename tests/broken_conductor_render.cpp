#include "generative/ConductorEngine.h"
#include "generative/EnsembleTypes.h"
#include "generative/Scale.h"
#include "performance/ConductorPerformanceController.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
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
    out << "# Broken Conductor Stage 3 ensemble trace\n";
    out << "# algorithm=" << ConductorEngine::kAlgorithmVersion
        << " seed=" << eng.masterSeed()
        << " density=" << eng.params().density
        << " mutation=" << eng.params().mutation << "\n";
    out << "# foundationRhythmDNA " << eng.rhythm().dna().describe() << "\n";
    out << "# format: bar.beat.slot ROLE EVENT ... [REASON]\n";

    std::map<std::tuple<int, int, int>, double> open;
    for (const auto& e : ev)
    {
        const double beatsPerBar = 4.0;
        const int bar = static_cast<int> (std::floor (e.ppq / beatsPerBar)) + 1;
        const double inBar = e.ppq - static_cast<double> (bar - 1) * beatsPerBar;
        const int beat = static_cast<int> (std::floor (inBar)) + 1;
        const int slot = static_cast<int> (std::lround ((inBar - std::floor (inBar)) / 0.25));
        char loc[32];
        std::snprintf (loc, sizeof loc, "%d.%d.%d", bar, beat, slot);
        const char* role = pfl::generative::voiceRoleName (
            static_cast<pfl::generative::VoiceRole> (std::clamp (e.voice, 0, 3)));
        const char* reason = pfl::generative::interactionReasonName (
            static_cast<pfl::generative::InteractionReason> (e.reason));

        if (e.kind == MidiMsgKind::NoteOn)
        {
            open[{ e.voice, e.channel, e.note }] = e.ppq;
            out << loc << " " << role << " note " << e.note
                << " vel " << e.velocity << " " << reason << "\n";
        }
        else if (e.kind == MidiMsgKind::NoteOff)
        {
            double dur = 0.0;
            auto key = std::tuple { e.voice, e.channel, e.note };
            auto it = open.find (key);
            if (it != open.end())
            {
                dur = e.ppq - it->second;
                open.erase (it);
            }
            out << loc << " " << role << " off  " << e.note
                << " dur " << dur << "\n";
        }
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

/** Type-1 SMF with four named tracks (diagnostic; all events still channel 1). */
static void writeSmfRoles (const fs::path& path, const std::vector<MidiTraceEvent>& ev, double bpm)
{
    const int tpq = 480;
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

    auto trackForRole = [&] (int role, const char* name) {
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
            if (e.voice != role)
                continue;
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
            const bool aOff = (a.status & 0xf0) == 0x80;
            const bool bOff = (b.status & 0xf0) == 0x80;
            return aOff && ! bOff;
        });

        std::vector<uint8_t> track;
        // track name
        writeVar (track, 0);
        track.push_back (0xff);
        track.push_back (0x03);
        track.push_back (static_cast<uint8_t> (std::strlen (name)));
        for (const char* p = name; *p; ++p)
            track.push_back (static_cast<uint8_t> (*p));

        if (role == 0)
        {
            writeVar (track, 0);
            track.push_back (0xff);
            track.push_back (0x51);
            track.push_back (0x03);
            const uint32_t usPerQuarter = static_cast<uint32_t> (std::lround (60000000.0 / bpm));
            track.push_back (static_cast<uint8_t> ((usPerQuarter >> 16) & 0xff));
            track.push_back (static_cast<uint8_t> ((usPerQuarter >> 8) & 0xff));
            track.push_back (static_cast<uint8_t> (usPerQuarter & 0xff));
        }

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
        return track;
    };

    const char* names[] = { "Foundation", "Pulse", "Wanderer", "Accent" };
    std::vector<std::vector<uint8_t>> tracks;
    for (int r = 0; r < 4; ++r)
        tracks.push_back (trackForRole (r, names[r]));

    std::vector<uint8_t> file;
    file.insert (file.end(), { 'M', 'T', 'h', 'd' });
    writeU32 (file, 6);
    writeU16 (file, 1); // format 1
    writeU16 (file, 4);
    writeU16 (file, static_cast<uint16_t> (tpq));
    for (const auto& track : tracks)
    {
        file.insert (file.end(), { 'M', 'T', 'r', 'k' });
        writeU32 (file, static_cast<uint32_t> (track.size()));
        file.insert (file.end(), track.begin(), track.end());
    }
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

static RunResult runFull (uint64_t seed, float density, float mutation, int bars,
                          pfl::generative::OutputRole role = pfl::generative::OutputRole::Ensemble)
{
    RunResult r;
    r.engine.setOutputRole (role);
    r.engine.setCapture (true);
    r.engine.setParams ({ density, mutation });
    r.engine.reseed (seed);
    r.engine.rhythm().setTraceEnabled (true);

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
    const bool stage5 = (argc > 1 && std::string (argv[1]) == "--stage5");
    const fs::path outDir = stage5
                                ? ((argc > 2) ? fs::path (argv[2]) : fs::path ("renders/broken-conductor/stage5"))
                                : ((argc > 1) ? fs::path (argv[1]) : fs::path ("renders/broken-conductor/stage4"));
    fs::create_directories (outDir);

    if (stage5)
    {
        using pfl::conductor_perf::Command;
        using pfl::conductor_perf::ConductorPerformanceController;
        using pfl::generative::OutputRole;

        std::ofstream metrics (outDir / "stage5-metrics.txt");
        metrics << "Broken Conductor Stage 5 performance (72 BPM, algo v"
                << ConductorEngine::kAlgorithmVersion << ", perf v"
                << pfl::conductor_perf::kPerformanceEngineVersion << ")\n";

        const std::vector<std::pair<double, Command>> cmds = {
            { 32.0, Command::FreezeOn },
            { 48.0, Command::Mutate },
            { 64.0, Command::Mutate },
            { 80.0, Command::FreezeOff },
            { 112.0, Command::Collapse },
            { 136.0, Command::Reseed },
            { 168.0, Command::SilenceOn },
            { 172.0, Command::SilenceOff },
        };

        auto runScript = [&] (OutputRole role) {
            ConductorEngine eng;
            ConductorPerformanceController perf;
            eng.setCapture (true);
            eng.setParams ({ 0.50f, 0.35f });
            eng.setOutputRole (role);
            eng.reseed (2002);
            perf.reset (2002);
            size_t ci = 0;
            double ppq = 0.0;
            const double endPpq = 192.0;
            int lastBar = -1;
            while (ppq < endPpq - 1.0e-12)
            {
                while (ci < cmds.size() && cmds[ci].first <= ppq + 1.0e-9)
                {
                    perf.trigger (cmds[ci].second, cmds[ci].first, eng);
                    ++ci;
                }
                const int bar = static_cast<int> (std::floor (ppq / 4.0));
                if (bar != lastBar)
                    lastBar = bar;
                perf.tick (ppq, bar, eng);
                const double next = std::min (endPpq, ppq + 0.25);
                pfl::generative::ClockSnapshot snap;
                snap.playing = true;
                snap.ppq = ppq;
                snap.tempoBpm = 72.0;
                snap.timeSigNumerator = 4;
                snap.timeSigDenominator = 4;
                eng.clock().advance (snap);
                eng.processTimeRange (ppq, next, true);
                eng.drainPending();
                ppq = next;
            }
            return eng.captured();
        };

        struct Proj { const char* label; OutputRole role; };
        const Proj projs[] = {
            { "performance-seed-2002", OutputRole::Ensemble },
            { "performance-foundation", OutputRole::Foundation },
            { "performance-pulse", OutputRole::Pulse },
            { "performance-wanderer", OutputRole::Wanderer },
            { "performance-accent", OutputRole::Accent },
        };
        for (const auto& p : projs)
        {
            auto ev = runScript (p.role);
            writeSmf (outDir / (std::string (p.label) + ".mid"), ev, 72.0);
            metrics << p.label << " events=" << ev.size() << "\n";
            std::cout << "Wrote " << p.label << "\n";
        }
        std::cout << "Metrics: " << (outDir / "stage5-metrics.txt") << "\n";
        return 0;
    }

    std::ofstream metrics (outDir / "stage4-metrics.txt");
    metrics << "Broken Conductor Stage 4 role projection (72 BPM, algorithm v"
            << ConductorEngine::kAlgorithmVersion << ")\n\n";

    using pfl::generative::OutputRole;
    struct Proj
    {
        const char* label;
        OutputRole role;
    };
    const Proj projs[] = {
        { "stage4-ensemble", OutputRole::Ensemble },
        { "stage4-foundation", OutputRole::Foundation },
        { "stage4-pulse", OutputRole::Pulse },
        { "stage4-wanderer", OutputRole::Wanderer },
        { "stage4-accent", OutputRole::Accent },
    };

    constexpr int kBars = 128;
    for (const auto& p : projs)
    {
        auto result = runFull (2002, 0.50f, 0.35f, kBars, p.role);
        writeTrace (outDir / (std::string (p.label) + ".txt"), result.events, result.engine);
        writeSmf (outDir / (std::string (p.label) + ".mid"), result.events, 72.0);
        metrics << p.label << " events=" << result.events.size()
                << " noteOns=" << computeMetrics (result.events, kBars, 0).noteOns
                << " ensembleEvents=" << result.engine.capturedEnsemble().size() << "\n";
        std::cout << "Wrote " << p.label << "\n";
    }

    // Automation projection check artifact (ensemble only + metrics note)
    {
        ConductorEngine eng;
        eng.setCapture (true);
        eng.setParams ({ 0.20f, 0.10f });
        eng.reseed (2002);
        double ppq = 0.0;
        while (ppq < 128.0)
        {
            const int bar = static_cast<int> (std::floor (ppq / 4.0));
            if (bar == 8)
                eng.setParams ({ 0.75f, 0.10f });
            if (bar == 16)
                eng.setParams ({ 0.75f, 0.90f });
            if (bar == 24)
                eng.setParams ({ 0.40f, 0.50f });
            const double next = std::min (128.0, ppq + 1.0);
            eng.clock().advance ({ true, ppq, 72.0, 4, 4 });
            eng.processTimeRange (ppq, next, true);
            eng.drainPending();
            ppq = next;
        }
        writeTrace (outDir / "stage4-automation-ensemble.txt", eng.captured(), eng);
        writeSmf (outDir / "stage4-automation-ensemble.mid", eng.captured(), 72.0);
        metrics << "automation-ensemble events=" << eng.captured().size() << "\n";
    }

    // Rough CPU: one ensemble vs four projections (same work ×4 by design)
    {
        using clock = std::chrono::steady_clock;
        auto once = [] (OutputRole role) {
            auto t0 = clock::now();
            (void) runFull (2002, 0.50f, 0.35f, 256, role);
            return std::chrono::duration<double, std::milli> (clock::now() - t0).count();
        };
        const double one = once (OutputRole::Ensemble);
        const double four = once (OutputRole::Foundation) + once (OutputRole::Pulse)
                            + once (OutputRole::Wanderer) + once (OutputRole::Accent);
        metrics << "cpu_ms_one_ensemble_256bars=" << one
                << " cpu_ms_four_projections_256bars=" << four
                << " ratio=" << (one > 0.0 ? four / one : 0.0) << "\n";
        std::cout << "CPU one=" << one << "ms four=" << four << "ms\n";
    }

    // Role gap timescales (beats) — hunger acceptance evidence
    {
        auto gapReport = [&] (float dens, int bars, const char* label) {
            auto result = runFull (2002, dens, 0.35f, bars, OutputRole::Ensemble);
            const double totalBeats = static_cast<double> (bars) * 4.0;
            metrics << "\n" << label << " dens=" << dens << " beats=" << totalBeats << "\n";
            for (int role = 0; role < 4; ++role)
            {
                std::vector<double> ons;
                for (const auto& e : result.events)
                    if (e.kind == MidiMsgKind::NoteOn && e.voice == role)
                        ons.push_back (e.ppq);
                metrics << "  " << pfl::generative::voiceRoleName (static_cast<pfl::generative::VoiceRole> (role))
                        << " events=" << ons.size()
                        << " per64beats=" << (ons.size() * 64.0 / totalBeats)
                        << " per256beats=" << (ons.size() * 256.0 / totalBeats);
                if (ons.size() >= 2)
                {
                    std::vector<double> gaps;
                    for (size_t i = 1; i < ons.size(); ++i)
                        gaps.push_back (ons[i] - ons[i - 1]);
                    std::sort (gaps.begin(), gaps.end());
                    double sum = 0;
                    for (double g : gaps)
                        sum += g;
                    const double mean = sum / static_cast<double> (gaps.size());
                    const double med = gaps[gaps.size() / 2];
                    const double p95 = gaps[std::min (gaps.size() - 1,
                                                      static_cast<size_t> (std::floor (0.95 * (gaps.size() - 1))))];
                    metrics << " gap_mean=" << mean << " median=" << med
                            << " p95=" << p95 << " max=" << gaps.back();
                }
                metrics << "\n";
            }
            // polyphony sample: mean active count over slots is expensive; report collision stats
            metrics << "  collisions attempted=" << result.engine.collisionStats().attemptedSamePitch
                    << " shifted=" << result.engine.collisionStats().shifted
                    << " suppressed=" << result.engine.collisionStats().suppressed << "\n";
        };
        gapReport (0.20f, 64, "gap-d0.2-64bars");
        gapReport (0.50f, 64, "gap-d0.5-64bars");
        gapReport (1.00f, 64, "gap-d1.0-64bars");
        gapReport (0.50f, 256, "gap-d0.5-1024beats");
    }

    std::cout << "Metrics: " << (outDir / "stage4-metrics.txt") << "\n";
    return 0;
}
