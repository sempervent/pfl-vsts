# PFL Signal Parasite — Stage 2

An audio-reactive generative collaborator. It listens to whatever you feed it,
decides for itself when something is worth answering, and answers with its own
generated sound. It is not a processor: the wet path never contains your input.

Stage 2 gives it a *relationship* with the source: it remembers what it has
been offered lately and how that went, and it lurks, attaches, gets into a
conversation or backs off accordingly.

- Identity: `SignalParasite`, `Sig1`, `com.pfl.signalparasite`, manufacturer `PflG`
- Formats: AU, VST3 (Fx), stereo in / stereo out, no MIDI
- Algorithm version: **2**

## Status

**Stage 2 COMPLETE — CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS.**
Tags: `signal-parasite-stage2-complete` (PR #22), `signal-parasite-stage1-complete` (PR #21).

## CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS (Stage 2)

Confirmed by human Ableton testing:

- Stage 2 behavioral relationship model works as engineered
- LURKING / ATTACHED / ANSWERING / WITHDRAWN behavior is accepted
- SENSITIVITY, HUNGER, and MUTATION remain meaningfully distinct
- dense-source restraint works; sparse-source engagement works
- changing sustained material works
- editor-sizing defect is fixed; normal plugin editor use no longer requires scrolling
- footer/mojibake defects are fixed; shared editor change is accepted

**Product decision:** SIGNAL PARASITE STAGE 2 RELATIONSHIP MODEL IS ACCEPTED.

## CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS (Stage 1)

Confirmed by human Ableton testing: the audio/reactive implementation behaves
successfully. Listening, stimulus detection, and generated responses are accepted.

## The idea

Feed it drums and it interjects — a short burst answering a kick, a sixteenth
late, panned off to one side. Feed it a pad and it responds to the moments the
pad *changes* rather than to any onset. Feed it silence and it says nothing,
because it has no voice of its own.

Two things make it feel like a collaborator rather than an effect. First, it
refuses most of what it hears: at full appetite it still answers well under half
the stimuli it detects, and it always leaves a musical gap. Second, its manner
drifts — the delays it favours, the durations it picks, how bright and how wide
it answers — so a long take does not repeat itself.

## Controls

| Control | Default | What it does |
|---------|---------|--------------|
| MIX | 0.50 | Wet/dry blend. At 0 you hear only your input; the parasite still listens and still runs. |
| SENSITIVITY | 0.50 | **How much of the source it hears.** Low: only clear, strong events. High: ghost notes and small timbre moves too. |
| HUNGER | 0.35 | **How often it answers what it heard.** 0 is completely silent. 1 is talkative but still leaves space. |
| MUTATION | 0.25 | **How fast its manner drifts.** 0 freezes the grammar forever. 1 evolves it every 4 bars. It never drives the relationship. |
| OUTPUT | 0.85 | Final trim. |
| SEED | 2002 | The whole personality at fixed macros. Same seed and same input give the same performance every time. |

The three character controls are deliberately independent. SENSITIVITY changes
what it notices and nothing else; HUNGER changes how often it speaks and nothing
else; MUTATION changes how it speaks and never how much. On the drums fixture:

| Sweep | Result |
|-------|--------|
| SENSITIVITY 0.2 → 0.5 → 0.9 | 32 → 63 → 94 stimuli detected |
| HUNGER 0 → 0.35 → 0.7 → 1.0 | 0 → 7 → 15 → 21 answers, stimuli pinned at 63 |
| MUTATION 0 → 0.25 → 1.0 | DNA generation 0 → 1 → 3, answers pinned at 24 |

## How it listens

Analysis runs on the **original input**, sampled before the mix, so the parasite
can never trigger on its own output. Everything is one-pole IIR work in the
sample domain — no FFT, no pitch tracking.

It extracts a fast (8 ms, two cascaded stages) and a slow (200 ms) power
envelope, an attack ratio between them, brightness as the power ratio either
side of an 800 Hz split, a level-and-timbre change measure against a longer
baseline, stereo balance, and a "how continuously filled is this source" probe.

Two kinds of event come out of that:

- **ATTACK** — a transient. This is what drums produce.
- **SHIFT** — the material changed without a transient: a swell, a filter move,
  a new timbre. This is what pads produce.

The distinction is real, not cosmetic: on the shipped fixtures the drum render
detects 62 attacks and 1 shift, and the pad render detects 1 attack and 29
shifts.

Each path is edge-triggered with its own re-arm rule, so one hit is one event and
one swell is one event. A hit has to decay before another can be recognised, or a
louder one has to arrive; a change has to settle or deepen. Below −40 dBFS
nothing registers at all, at any sensitivity.

## How it answers

A detected stimulus is a candidate, not a command. It has to pass, in order: a
strength floor, a staleness check (2 beats), voice availability, a minimum
musical gap set by HUNGER, and finally a capped accept probability. The cap is
0.62, which is why HUNGER 1 is still selective.

An accepted stimulus is scheduled at an absolute sample offset — a delay drawn
from `{0, 1/16, 1/8, 1/4, 1/2, 1}` beats — so the answer lands on the same sample
regardless of the host's buffer size. Duration comes from
`{1/16, 1/8, 1/4, 1/2, 1}` beats.

The single voice is generated: white noise into a resonant one-pole low-pass
whose cutoff chirps over the note, through a linear attack/hold/release, a tanh
saturator, a constant-power pan, a DC blocker and a safety limiter. Resonance is
capped at 0.72 and pan at 0.85. The stimulus's own energy, brightness, change and
stereo balance map onto the answer's level, cutoff, chirp direction, resonance
and position, which is what makes the answer feel related to what provoked it.

### DNA

Its manner is a small DNA record: weights over the delay and duration
vocabularies, a gap preference, echo and brightness and energy biases, a stereo
mode, and a chirp direction. DNA is born from the seed at a bar boundary and,
when MUTATION is above zero, one bounded mutation operator is applied every
lifespan — 16 bars at low MUTATION down to 4 bars at full.

Mutation can retune the grammar but not the density. In particular the gap
preference cannot drift more than ±0.20 from its birth value, which is what stops
MUTATION from becoming a second HUNGER.

## Host behaviour

Transport stop clears pending answers and pauses DNA evolution; the voice
releases safely. A seek clears queued stimuli and scheduled answers, resets the
analysis baselines, and re-arms a 50 ms warm-up. A seek is never itself treated
as a stimulus, and neither is the moment the plugin is inserted: the first sample
after a reset primes every baseline, and the attack and change measures stay at
zero until their baselines have converged (600 ms and 2500 ms respectively).

Changing SEED regenerates DNA at the next bar boundary rather than mid-phrase.

## Determinism

Same input, same seed, same macros, same timeline gives the same stimuli and the
same answers — verified byte-identical across buffer sizes 64, 127, 128, 255,
256, 511, 512 and 1024, and across two independent runs. All randomness comes
from isolated derived streams, and the analysis and detection stages consume no
randomness at all, so changing SENSITIVITY cannot shift the response draws.

## Renders

`renders/signal-parasite/stage2/` — drums, pad, sparse and busy at dry / wet /
0.50 mix; SENSITIVITY, HUNGER and MUTATION sweeps; two seeds; silence; the
sparse → busy → sparse relationship journey wet, at mix, and with DNA frozen so
every difference in the trace is the state; and the 64-bar drums journey.

Each `.wav` has a matching `-trace.txt` with counts, suppression reasons,
structural fingerprints and the full event lists, plus Stage 2 diagnostics:
state occupancy, the five pressures, the history window, the transition list
with beat positions, and the state each answer was issued from.

`renders/signal-parasite/stage1/` is kept for comparison.

## Scope

Deliberately absent through Stage 2: MIDI and controllers, performance verbs,
FFT or pitch tracking, more than one voice, any wet path that processes the
input, and any memory of audio.

## The relationship (Stage 2)

Two things sit on top of Stage 1: a bounded memory of recent stimuli, and a
state over it.

The memory is a 16-slot ring of *descriptors* — when a stimulus arrived, how
strong and how bright it was, whether it was an ATTACK or a SHIFT, whether the
parasite answered it, and whether it arrived in the shadow of an answer. There
is no audio in it. Memory Eater is the plugin that remembers sound; the
parasite remembers only that things happened.

From that window it derives five bounded pressures: how **busy** the source is,
how **attachable** it looks (interesting, with room, and answering it has been
working), whether a **conversation** is going, how **withdrawn** it wants to
be, and how **fatigued** it is from its own answers.

### The four states

| State | What it means | How it answers |
|-------|---------------|----------------|
| **LURKING** | Present and listening. It has not committed. | Sparingly, late, a little quieter. Starts here. |
| **ATTACHED** | It has decided this source is worth following. | Exactly as Stage 1 did. |
| **ANSWERING** | An exchange is going. | Sooner, slightly longer, a touch louder. |
| **WITHDRAWN** | It has backed off — a wall of sound, or its own fatigue. | Rarely, latest, shortest, quietest. |

`LURKING → ATTACHED → ANSWERING / WITHDRAWN`, and `WITHDRAWN → LURKING /
ATTACHED`. A wall of sound can also drive it straight from LURKING to
WITHDRAWN, because attachment needs room and a wall has none.

Everything runs on musical time. Every beat it takes stock and may retreat;
bonding and stepping forward need a **major opportunity**, which comes round
every 4 to 16 beats. Each state has a dwell floor, so nothing flickers, and
withdrawal builds over about ten beats — backing off is a decision, not a
reflex, so the peak of a swelling pad will not chase it away.

### What the state does and does not touch

It changes *manner*, inside the Stage 1 vocabularies: how likely it is to
accept a stimulus, how much space it insists on, which half of the delay and
duration sets it draws from, and how loud the answer is. DNA is still the
personality; the state is a mood on top of it.

It does not change what the parasite hears, and it cannot make a sound on its
own. **No stimulus, no response — ANSWERING included.** HUNGER 0 is still
silent, HUNGER's minimum gap is still a hard floor the state can only lengthen,
and the 0.62 accept cap survives even the most forward state.

### On the fixtures

Same ears, same appetite (SENSITIVITY 0.50, HUNGER 0.70), 96 beats:

| Partner | Where it spends its time | Stimuli | Answers | Answered share |
|---------|--------------------------|---------|---------|----------------|
| sparse hits | ANSWERING 73 %, ATTACHED 10 %, LURKING 17 % | 69 | 21 | 30 % |
| busy sixteenths | WITHDRAWN 80 %, LURKING 20 % | 380 | 21 | 5.5 % |
| pad | LURKING 68 %, ATTACHED 25 %, ANSWERING 7 % | 41 | 9 | 22 % |
| silence | LURKING 100 % | 0 | 0 | — |

The busy partner offers five times as much and gets the same number of answers.

Over the `journey` fixture — sparse, then busy, then sparse again — it attaches
at beat 16, gets into a conversation at 26, withdraws at 72 once the busy
section has been going for eight beats, comes back to attached at 138 and to
answering at 144.

## Host behaviour (Stage 2 additions)

A seek clears the history and drops the state to LURKING; the DNA is
reconstructed for absolute musical time exactly as before, not reseeded.
Transport stop **pauses** the relationship — its clock only advances on played
samples, so there is no wall clock and nothing ages while you are parked. A
SEED change starts a new personality with no history, so it goes back to
LURKING. Controls and SEED are saved with the project; the history and the
state are not, so a reload starts LURKING too.

## Stage 3

Performance intervention (FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE) —
authorized after Stage 2 Ableton PASS. Intended final planned software stage;
park Signal Parasite if accepted rather than auto-continuing into tonal listening.
