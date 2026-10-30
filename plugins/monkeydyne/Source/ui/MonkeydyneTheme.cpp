#include "MonkeydyneTheme.h"

#include "MonkeydyneMotif.h"

namespace monkeydyne::ui
{
animalaudio::ui::Palette makePalette()
{
    // Tropical canopy on a warm charcoal: hot pink notes, banana yellow for what is selected (and, turned toward
    // orange and darkened, the monkey's fur), jungle lime for formant.
    animalaudio::ui::Palette palette;
    palette.background = juce::Colour(0xff110e0d);
    palette.panel = juce::Colour(0xff1a1615);
    palette.panelRaised = juce::Colour(0xff27211f);
    palette.outline = juce::Colour(0xff3b322f);
    palette.text = juce::Colour(0xfff8f2ea);
    palette.textDim = juce::Colour(0xffaea29b);
    palette.accent = juce::Colour(0xffff52dc);
    palette.accentAlt = juce::Colour(0xffffd23f);
    palette.onAccent = juce::Colour(0xff220619);
    palette.detail = juce::Colour(0xffa6e43c);
    palette.ambient = juce::Colour(0xff0a0807);
    return palette;
}

animalaudio::ui::Theme makeTheme()
{
    animalaudio::ui::Theme theme;
    theme.palette = makePalette();
    theme.name = "MONKEYDYNE";
    theme.tagline = juce::String(juce::CharPointer_UTF8("NOTES  \xc2\xb7  PITCH  \xc2\xb7  TIME  \xc2\xb7  GLIDE"));
    theme.motif = std::make_shared<MonkeydyneMotif>();
    return theme;
}
} // namespace monkeydyne::ui
