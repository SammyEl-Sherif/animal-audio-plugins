#include "MonkeydyneTheme.h"

#include "MonkeydyneMotif.h"

namespace monkeydyne::ui
{
animalaudio::ui::Palette makePalette()
{
    // Titanium and glass: graphite metal with a faint plum cast, orchid pearl for notes, champagne gold for what
    // is selected (and, darkened, the monkey's bronze), mint for formant.
    animalaudio::ui::Palette palette;
    palette.background = juce::Colour(0xff100e11);
    palette.panel = juce::Colour(0xff1b191d);
    palette.panelRaised = juce::Colour(0xff28252b);
    palette.outline = juce::Colour(0xff3e3a42);
    palette.text = juce::Colour(0xfff6f3f8);
    palette.textDim = juce::Colour(0xffa7a1ab);
    palette.accent = juce::Colour(0xffee86e4);
    palette.accentAlt = juce::Colour(0xfff3c77f);
    palette.onAccent = juce::Colour(0xff26091f);
    palette.detail = juce::Colour(0xff8ee8c4);
    palette.ambient = juce::Colour(0xff0a090b);
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
