#include "MonkeydyneTheme.h"

#include "MonkeydyneMotif.h"

namespace monkeydyne::ui
{
animalaudio::ui::Palette makePalette()
{
    // Jungle at night: teal-black canopy, an aqua accent for notes, warm coral for what is selected (and, darkened,
    // the monkey's fur), violet for formant.
    animalaudio::ui::Palette palette;
    palette.background = juce::Colour(0xff081014);
    palette.panel = juce::Colour(0xff0e1a20);
    palette.panelRaised = juce::Colour(0xff16262e);
    palette.outline = juce::Colour(0xff22363f);
    palette.text = juce::Colour(0xffe9f7f8);
    palette.textDim = juce::Colour(0xff86a3a9);
    palette.accent = juce::Colour(0xff3fd8e2);
    palette.accentAlt = juce::Colour(0xffffa36b);
    palette.onAccent = juce::Colour(0xff04181b);
    palette.detail = juce::Colour(0xff9d8cff);
    palette.ambient = juce::Colour(0xff060d11);
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
