#include "dsp/DCBlocker.h"
#include "dsp/SafetyLimiter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int failures = 0;

#define EXPECT(cond) \
    do { \
        if (! (cond)) { \
            std::fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures; \
        } \
    } while (0)

int main()
{
    // DC blocker removes constant offset over time
    {
        pfl::dsp::DCBlocker dc;
        dc.prepare (48000.0);
        float y = 0.0f;
        for (int i = 0; i < 20000; ++i)
            y = dc.processSample (1.0f);
        EXPECT (std::abs (y) < 0.05f);
    }

    // Safety limiter bounds extreme input and rejects non-finite
    {
        pfl::dsp::SafetyLimiter lim;
        lim.prepare (48000.0);

        float maxAbs = 0.0f;
        for (int i = 0; i < 4096; ++i)
        {
            const float out = lim.processSample (1000.0f);
            EXPECT (std::isfinite (out));
            maxAbs = std::max (maxAbs, std::abs (out));
        }
        EXPECT (maxAbs <= 0.99f + 1.0e-6f);

        const float nanOut = lim.processSample (std::nanf (""));
        EXPECT (std::isfinite (nanOut));
        EXPECT (nanOut == 0.0f || std::abs (nanOut) <= 0.99f);
    }

    if (failures != 0)
    {
        std::fprintf (stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }

    std::puts ("dsp_smoke: ok");
    return EXIT_SUCCESS;
}
