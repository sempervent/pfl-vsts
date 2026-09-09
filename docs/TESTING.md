# Testing

## Commands

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./scripts/render.sh 90 renders
```

## Automated

| Test                   | Covers |
|------------------------|--------|
| `dsp_smoke`            | DC blocker; limiter NaN/bounds |
| `audio_engine_smoke`   | RNG streams; osc pitch sanity; drift bounds; hostile finite across 44.1/48k and buffers 64–1024; delay feedback bound |

## Renders (manual audition)

| File | Settings |
|------|----------|
| `renders/drone-organism-audio-engine-v0.1.wav` | 90s, drift 0.35, dirt 0.45, space 0.55, output 0.65 |
| `renders/clean.wav` | low macros |
| `renders/nominal.wav` | same as reference |
| `renders/hostile.wav` | all macros 100% |

## Ableton Live 11 — shortest validation

1. `./scripts/build.sh` (copies to `~/Library/Audio/Plug-Ins/...`)
2. Live → Preferences → Plug-Ins → Rescan (prefer **VST3** if AU missing)
3. MIDI track → add **PFL Drone Organism** (no MIDI clips required)
4. Press Play — drone should fade in (D2/A2/D3)
5. Move DRIFT / DIRT / SPACE / OUTPUT / DENSITY — each should change the sound
6. Press Stop — texture should release/fade, not click off
7. Save set → reopen → confirm parameter values restore

## Validation status

| Milestone | Status |
|-----------|--------|
| Build succeeded | Yes |
| Plugin validation (`auval`) | No (adhoc unsigned) |
| Host load (Ableton) | Manual — not agent-verified |
| Audio output (offline render) | Yes |
| Musical approval | Pending |
