#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pfl::ui
{

enum class ProductId
{
    DroneOrganism,
    BrokenConductor,
    RuinEngine,
    MemoryEater,
    PulseColony,
    SignalParasite
};

/** Shared dark chrome + per-plugin accent / motif colours. */
struct Theme
{
    ProductId product = ProductId::DroneOrganism;
    juce::Colour background { 0xff12141a };
    juce::Colour panel { 0xff1a1e28 };
    juce::Colour text { 0xffe8ecf4 };
    juce::Colour textDim { 0xff9aa3b5 };
    juce::Colour accent { 0xff7ec8c8 };
    juce::Colour accentDim { 0xff3a5a5a };
    juce::Colour strong { 0xffc87858 };   // destructive / COLLAPSE weight
    juce::Colour silence { 0xffe0a060 };
    juce::Colour track { 0xff2a303c };
    juce::Colour outline { 0xff3a4250 };
    juce::Colour motif { 0x28a0b8c8 };

    static Theme forProduct (ProductId id) noexcept
    {
        Theme t;
        t.product = id;
        switch (id)
        {
            case ProductId::DroneOrganism:
                t.background = juce::Colour (0xff0e1420);
                t.panel = juce::Colour (0xff162030);
                t.accent = juce::Colour (0xff6eb8c8);
                t.accentDim = juce::Colour (0xff2a5060);
                t.motif = juce::Colour (0x286eb8c8);
                t.strong = juce::Colour (0xffb87868);
                t.silence = juce::Colour (0xffd0a070);
                t.outline = juce::Colour (0xff2a4050);
                break;
            case ProductId::BrokenConductor:
                t.background = juce::Colour (0xff14120e);
                t.panel = juce::Colour (0xff1e1a14);
                t.accent = juce::Colour (0xffd0a050);
                t.accentDim = juce::Colour (0xff604820);
                t.motif = juce::Colour (0x28d0a050);
                t.strong = juce::Colour (0xffc06040);
                t.silence = juce::Colour (0xffe8c070);
                t.outline = juce::Colour (0xff4a3c28);
                break;
            case ProductId::RuinEngine:
                t.background = juce::Colour (0xff100e0c);
                t.panel = juce::Colour (0xff1a1612);
                t.accent = juce::Colour (0xffb07050);
                t.accentDim = juce::Colour (0xff503828);
                t.motif = juce::Colour (0x28b07050);
                t.strong = juce::Colour (0xffc05038);
                t.silence = juce::Colour (0xffd89860);
                t.outline = juce::Colour (0xff403028);
                break;
            case ProductId::MemoryEater:
                t.background = juce::Colour (0xff101418);
                t.panel = juce::Colour (0xff181e24);
                t.accent = juce::Colour (0xff88a0b8);
                t.accentDim = juce::Colour (0xff384858);
                t.motif = juce::Colour (0x2888a0b8);
                t.strong = juce::Colour (0xffa87868);
                t.silence = juce::Colour (0xffc0b090);
                t.outline = juce::Colour (0xff303848);
                break;
            case ProductId::PulseColony:
                t.background = juce::Colour (0xff0c1412);
                t.panel = juce::Colour (0xff14201c);
                t.accent = juce::Colour (0xff70c888);
                t.accentDim = juce::Colour (0xff285840);
                t.motif = juce::Colour (0x2870c888);
                t.strong = juce::Colour (0xffc87848);
                t.silence = juce::Colour (0xffe0b060);
                t.outline = juce::Colour (0xff284838);
                break;
            case ProductId::SignalParasite:
                t.background = juce::Colour (0xff120e16);
                t.panel = juce::Colour (0xff1c1622);
                t.accent = juce::Colour (0xffc070a0);
                t.accentDim = juce::Colour (0xff503050);
                t.motif = juce::Colour (0x28c070a0);
                t.strong = juce::Colour (0xffc85848);
                t.silence = juce::Colour (0xffd0a858);
                t.outline = juce::Colour (0xff403048);
                break;
        }
        return t;
    }

    void paintMotif (juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        g.setColour (motif);
        switch (product)
        {
            case ProductId::DroneOrganism:   paintDrone (g, bounds); break;
            case ProductId::BrokenConductor: paintConductor (g, bounds); break;
            case ProductId::RuinEngine:      paintRuin (g, bounds); break;
            case ProductId::MemoryEater:     paintMemory (g, bounds); break;
            case ProductId::PulseColony:     paintPulse (g, bounds); break;
            case ProductId::SignalParasite:  paintParasite (g, bounds); break;
        }
    }

private:
    static void paintDrone (juce::Graphics& g, juce::Rectangle<float> b)
    {
        const auto c = b.getCentre();
        for (int i = 1; i <= 4; ++i)
        {
            const float r = 28.0f * (float) i;
            g.drawEllipse (c.x - r, c.y - r * 0.55f, r * 2.0f, r * 1.1f, 1.0f);
        }
        for (int i = 0; i < 12; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::twoPi / 12.0f;
            g.fillEllipse (c.x + 90.0f * std::cos (a) - 1.5f,
                           c.y + 40.0f * std::sin (a) - 1.5f, 3.0f, 3.0f);
        }
    }

    static void paintConductor (juce::Graphics& g, juce::Rectangle<float> b)
    {
        const float y0 = b.getY() + 70.0f;
        for (int i = 0; i < 5; ++i)
        {
            const float y = y0 + (float) i * 14.0f;
            juce::Path p;
            p.startNewSubPath (b.getX() + 24.0f, y);
            p.lineTo (b.getCentreX() - 20.0f, y + ((i % 2) ? 4.0f : -3.0f));
            p.lineTo (b.getRight() - 30.0f, y + ((i % 3) ? -5.0f : 6.0f));
            g.strokePath (p, juce::PathStrokeType (1.1f));
        }
        for (int i = 0; i < 6; ++i)
            g.fillRect (b.getX() + 40.0f + (float) i * 70.0f, y0 - 8.0f, 2.0f, 72.0f);
    }

    static void paintRuin (juce::Graphics& g, juce::Rectangle<float> b)
    {
        for (int i = 0; i < 7; ++i)
        {
            const float y = b.getY() + 50.0f + (float) i * 28.0f;
            juce::Path p;
            p.startNewSubPath (b.getX(), y);
            for (float x = b.getX(); x < b.getRight(); x += 18.0f)
                p.lineTo (x, y + (((int) x / 18) % 2 ? 5.0f : -4.0f) * (1.0f + 0.15f * (float) i));
            g.strokePath (p, juce::PathStrokeType (1.2f));
        }
    }

    static void paintMemory (juce::Graphics& g, juce::Rectangle<float> b)
    {
        const auto c = b.getCentre();
        for (int i = 1; i <= 5; ++i)
        {
            g.setOpacity (0.35f / (float) i);
            const float r = 20.0f * (float) i;
            g.drawEllipse (c.x - r - (float) i * 4.0f, c.y - r, r * 2.0f, r * 2.0f, 1.0f);
        }
        g.setOpacity (1.0f);
        for (int i = 0; i < 8; ++i)
        {
            const float x = b.getX() + 40.0f + (float) i * 55.0f;
            g.drawLine (x, c.y - 30.0f, x + 30.0f, c.y + 20.0f, 1.0f);
        }
    }

    static void paintPulse (juce::Graphics& g, juce::Rectangle<float> b)
    {
        const juce::Point<float> nodes[] = {
            { b.getX() + 80.0f,  b.getCentreY() - 20.0f },
            { b.getX() + 160.0f, b.getCentreY() + 30.0f },
            { b.getX() + 240.0f, b.getCentreY() - 10.0f },
            { b.getX() + 320.0f, b.getCentreY() + 25.0f },
            { b.getX() + 400.0f, b.getCentreY() - 25.0f },
            { b.getX() + 480.0f, b.getCentreY() + 10.0f },
        };
        for (int i = 0; i < 5; ++i)
            g.drawLine (nodes[i].x, nodes[i].y, nodes[i + 1].x, nodes[i + 1].y, 1.1f);
        for (auto n : nodes)
            g.fillEllipse (n.x - 4.0f, n.y - 4.0f, 8.0f, 8.0f);
    }

    static void paintParasite (juce::Graphics& g, juce::Rectangle<float> b)
    {
        const float cx = b.getCentreX();
        const float cy = b.getCentreY();
        juce::Path wave;
        wave.startNewSubPath (cx - 80.0f, cy);
        for (float x = -80.0f; x <= 80.0f; x += 6.0f)
            wave.lineTo (cx + x, cy + 10.0f * std::sin (x * 0.18f));
        g.strokePath (wave, juce::PathStrokeType (1.4f));
        for (int i = 0; i < 5; ++i)
        {
            juce::Path t;
            const float a = -0.9f + (float) i * 0.35f;
            t.startNewSubPath (cx - 120.0f, cy + 40.0f);
            t.quadraticTo (cx - 40.0f, cy + a * 50.0f, cx + 10.0f, cy + a * 8.0f);
            g.strokePath (t, juce::PathStrokeType (1.0f));
        }
    }
};

using PflTheme = Theme;

/** Draws a product's procedural background motif. No assets, no animation. */
inline void paintMotif (juce::Graphics& g, juce::Rectangle<float> bounds, ProductId product)
{
    Theme::forProduct (product).paintMotif (g, bounds);
}

} // namespace pfl::ui
