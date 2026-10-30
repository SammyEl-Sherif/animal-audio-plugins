#include "MonkeydyneTheme.h"

#include "MonkeydyneMotif.h"

namespace monkeydyne::ui
{
animalaudio::ui::Palette makePalette()
{
    // Tropical canopy on a warm charcoal: banana yellow notes (turned toward orange and darkened, the monkey's
    // fur), hibiscus coral for what is selected, jungle lime for formant.
    animalaudio::ui::Palette palette;
    palette.background = juce::Colour(0xff110e0d);
    palette.panel = juce::Colour(0xff1a1615);
    palette.panelRaised = juce::Colour(0xff27211f);
    palette.outline = juce::Colour(0xff3b322f);
    palette.text = juce::Colour(0xfff8f2ea);
    palette.textDim = juce::Colour(0xffaea29b);
    palette.accent = juce::Colour(0xffffdb3a);
    palette.accentAlt = juce::Colour(0xffff6178);
    palette.onAccent = juce::Colour(0xff2b2105);
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
