#include "ParrotTheme.h"

#include "ParrotMotif.h"

namespace parrot::ui
{
animalaudio::ui::Palette makePalette()
{
    // Scarlet macaw: red body, gold and blue wing bands, ivory beak, on a warm charcoal.
    animalaudio::ui::Palette palette;
    palette.background = juce::Colour(0xff110d0e);
    palette.panel = juce::Colour(0xff1a1416);
    palette.panelRaised = juce::Colour(0xff261d20);
    palette.outline = juce::Colour(0xff3a2d31);
    palette.text = juce::Colour(0xfff8f1ea);
    palette.textDim = juce::Colour(0xffad9e9d);
    palette.accent = juce::Colour(0xffff4b3e);
    palette.accentAlt = juce::Colour(0xffffc23d);
    palette.onAccent = juce::Colour(0xff1d0706);
    palette.detail = juce::Colour(0xff3f9dff);
    palette.ambient = juce::Colour(0xff0a0708);
    return palette;
}

animalaudio::ui::Theme makeTheme()
{
    animalaudio::ui::Theme theme;
    theme.palette = makePalette();
    theme.name = "PARROT";
    theme.tagline = juce::String(juce::CharPointer_UTF8("PITCH  \xc2\xb7  FORMANT  \xc2\xb7  DRIVE"));
    theme.motif = std::make_shared<ParrotMotif>();
    return theme;
}
} // namespace parrot::ui
