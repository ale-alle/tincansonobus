// SPDX-License-Identifier: GPLv3-or-later
// CanDuet colours, matched to the tincanstudios.ca "night" palette (site repo src/index.css, D-021).
// Change colours here rather than in the views. Meter, mute, solo and error colours keep their
// own meanings and are not part of this palette.

#pragma once

#include "JuceHeader.h"

namespace TinCanTheme
{
    // backgrounds, darkest first
    inline const juce::Colour night        { 0xff0a0f1e }; // window background (site --color-night)
    inline const juce::Colour surface      { 0xff121a2e }; // panels and cards (--color-night-surface)
    inline const juce::Colour raised       { 0xff1a2440 }; // buttons, menus, selected rows (between surface and line)
    inline const juce::Colour field        { 0xff070b16 }; // text fields, slightly darker than night

    // lines
    inline const juce::Colour line         { 0xff233052 }; // borders (--color-night-line)
    inline const juce::Colour lineStrong   { 0xff3a4a75 }; // focused borders, scroll thumbs (--color-night-line-strong)

    // text
    inline const juce::Colour text         { 0xffe8ecf6 }; // (--color-night-text)
    inline const juce::Colour muted        { 0xffb4bed6 }; // (--color-night-muted)
    inline const juce::Colour subtle       { 0xff9aa6c4 }; // (--color-night-subtle)

    // the red string
    inline const juce::Colour string       { 0xffe5484d }; // accents; not behind white text (--color-string)
    inline const juce::Colour stringLight  { 0xffff6b70 }; // red text and marks on night (--color-string-light)
    inline const juce::Colour stringStrong { 0xffd7263d }; // filled "on" buttons with white text (--color-string-strong)
}
