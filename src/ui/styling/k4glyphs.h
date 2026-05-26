#ifndef K4GLYPHS_H
#define K4GLYPHS_H

#include "ui/styling/k4constants.h"

#include <QColor>
#include <QPixmap>
#include <functional>

// Procedurally-drawn UI glyphs used in the status bar and PowerStatusButton.
// Each factory returns a closure that, given a color, renders the glyph as a
// QPixmap. Storing the closure (rather than a single pre-rendered pixmap)
// lets callers re-tint the glyph whenever the value it sits next to changes
// state — e.g. PA temperature crossing into the orange/red threshold also
// re-renders the thermometer in the matching color.
//
// All glyphs are antialiased and have a transparent background, so they
// composite cleanly into any container (toolbar button icon slot, QLabel
// pixmap, custom-painted widget).
//
// No external asset files — keep this file self-contained.
namespace K4Glyphs {

using Glyph = std::function<QPixmap(const QColor &color)>;

// Open ring (lower 3/4 arc) with a vertical stroke through the top gap.
// Used by PowerStatusButton.
Glyph power(int sizePx = 22);

// Vertical thermometer body with a filled circular bulb at the bottom.
// Default size matches K4Styles::Dimensions::SmallIconSize.
Glyph thermometer(int sizePx = K4Styles::Dimensions::SmallIconSize);

// Stylized Z-shape lightning bolt, filled.
Glyph lightning(int sizePx = K4Styles::Dimensions::SmallIconSize);

} // namespace K4Glyphs

#endif // K4GLYPHS_H
