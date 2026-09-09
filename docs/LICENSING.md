# Licensing notes

This file records implications **before public distribution**. It is not legal advice.

## Project license

Repository `LICENSE` is **GPL-3.0**.

## JUCE

JUCE is dual-licensed:

- **AGPLv3** (open-source terms), or
- **Commercial JUCE license** (for closed-source distribution under JUCE's EULA)

Per JUCE's public guidance: creating and using in-house / pre-release software privately without conveying closed-source binaries outside the organization is typically discussed in the context of the GNU license path. **Distributing** closed-source plugins containing JUCE to third parties generally requires a commercial JUCE license **or** compliance with AGPLv3 (including source obligations).

### Prototype policy (current)

- Private local development and Ableton testing on the author machine: proceed under AGPLv3-compatible private use assumptions.
- Before shipping to other people, beta testers outside a single private context, or selling the plugin: re-check JUCE EULA / AGPLv3 and choose commercial license vs open-source release.

## VST3 SDK

JUCE embeds/uses Steinberg VST3 support. Distribution of VST3 plugins is subject to Steinberg's VST3 licensing/SDK agreement. Review before public release.

## Audio Unit

AU builds use Apple system frameworks; follow Apple's Audio Unit / notarization / distribution requirements if distributing outside personal use.
