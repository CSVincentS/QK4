#ifndef K4STYLES_H
#define K4STYLES_H

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QString>

#include "ui/styling/k4constants.h" // K4Styles::Colors and K4Styles::Dimensions

/**
 * @brief Centralized styling for QK4 UI components.
 *
 * This namespace provides consistent button and popup styles across all widgets,
 * eliminating duplicate CSS definitions and ensuring visual consistency.
 *
 * Style Categories:
 * - Popup buttons: Used in BandPopup, ModePopup, ButtonRowPopup, DisplayPopup
 * - Menu bar buttons: Used in BottomMenuBar, FeatureMenuBar
 * - Selected/Active states: Inverted colors for selected items
 */
namespace K4Styles {

// =============================================================================
// Popup Button Styles (BandPopup, ModePopup, ButtonRowPopup, DisplayPopup)
// =============================================================================

/**
 * @brief Standard dark gradient button for popup widgets.
 * Used for unselected band/mode buttons, popup action buttons.
 */
QString popupButtonNormal();

/**
 * @brief Light/white button for selected state in popups.
 * Used for currently selected band/mode.
 */
QString popupButtonSelected();

// =============================================================================
// Menu Bar Button Styles (BottomMenuBar, FeatureMenuBar)
// =============================================================================

/**
 * @brief Standard button for bottom menu bar (MENU, Fn, DISPLAY, BAND, etc.)
 * Includes padding suitable for text labels.
 */
QString menuBarButton();

/**
 * @brief Active/pressed state for menu bar buttons.
 * White background with dark text (inverted from normal).
 */
QString menuBarButtonActive();

/**
 * @brief Compact button for +/- controls in FeatureMenuBar.
 * Same gradient but no padding, larger font.
 */
QString menuBarButtonSmall();

/**
 * @brief PTT button pressed state.
 * Red background with white text for transmit indication.
 */
QString menuBarButtonPttPressed();

/**
 * @brief Standard horizontal slider stylesheet.
 * Amber handle on dark groove, uses Dimensions::Slider* constants.
 *
 * @param grooveColor Background color for the groove
 * @param handleColor Color for the handle and filled portion
 */
QString sliderHorizontal(const QString &grooveColor, const QString &handleColor);

/**
 * @brief Checkbox-style toggle button stylesheet.
 * Square button with gradient background, shows checkmark when checked.
 * Use with QPushButton::setCheckable(true).
 *
 * @param size Size of the checkbox (default 32x32)
 */
QString checkboxButton(int size = 32);

/**
 * @brief Radio button style for mode selection (DISPLAY ALL / USE SUBSET).
 * Circular indicator with gradient background.
 * Use with QPushButton::setCheckable(true).
 */
QString radioButton();

// =============================================================================
// Side Panel Button Styles
// =============================================================================

/**
 * @brief Compact button for small controls (MON, NORM, BAL).
 * Dark gradient with 1px border and 4px radius.
 */
QString compactButton();

/**
 * @brief Dark gradient button for side panel icons (?, globe).
 * Includes normal, hover, and pressed states.
 */
QString sidePanelButton();

/**
 * @brief Light gradient button for TX/PF function buttons.
 * Uses lighter grey gradient with hover border change only (no hover gradient).
 */
QString sidePanelButtonLight();

/**
 * @brief Panel button with disabled state support.
 * Used for KPA1500 panel buttons (STANDBY, ATU, ANT, TUNE).
 */
QString panelButtonWithDisabled();

// =============================================================================
// Dialog Button Styles
// =============================================================================

/**
 * @brief Standard dialog button style with all states.
 * Includes normal, hover, pressed, and disabled states.
 * Used for dialog action buttons (Connect, Save, Delete, etc.).
 */
QString dialogButton();

// =============================================================================
// Control Button Styles
// =============================================================================

/**
 * @brief Control button for decode windows.
 * @param selected If true, uses light/selected gradient. If false, uses dark gradient with disabled state.
 */
QString controlButton(bool selected = false);

// =============================================================================
// Common Style Constants
// =============================================================================
// Colors and Dimensions live in ui/styling/k4constants.h (included above).
// They remain reachable as K4Styles::Colors::* and K4Styles::Dimensions::*.

namespace Fonts {
// =============================================================================
// Font Family Names - centralized for easy maintenance
// =============================================================================
constexpr const char *Primary = "Inter"; // UI text, labels, buttons, menus
constexpr const char *Data = "Inter";    // Frequencies, numeric data (uses tabular figures)

/**
 * @brief Create a font for custom-painted widgets with pixel-based sizing.
 *
 * Uses setPixelSize() instead of setPointSize() so that fonts render at
 * the same logical size on both macOS (72 PPI) and Windows (96 PPI).
 * On macOS where 1pt = 1px, this produces identical output to setPointSize().
 *
 * @param pixelSize Font size in pixels
 * @param weight Font weight (default: Bold)
 * @return QFont configured with pixel-based sizing
 */
QFont paintFont(int pixelSize, QFont::Weight weight = QFont::Bold);

/**
 * @brief Create a data display font with tabular figures enabled.
 *
 * Tabular figures ensure all digits have equal width, preventing visual
 * shifting when numeric values change (e.g., frequency displays).
 *
 * @param pixelSize Font size in pixels
 * @param weight Font weight (default: Bold)
 * @return QFont configured with tabular figures
 */
QFont dataFont(int pixelSize, QFont::Weight weight = QFont::Bold);

} // namespace Fonts

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Draw a soft drop shadow behind popup content.
 *
 * Uses 8-layer blur technique for smooth shadow appearance.
 * Call this in paintEvent() before drawing content.
 *
 * @param painter The painter to draw with (should have NoPen set)
 * @param contentRect The rectangle of the popup content (not including shadow margin)
 * @param cornerRadius Corner radius for the shadow rounded rect
 */
void drawDropShadow(QPainter &painter, const QRect &contentRect, int cornerRadius = 8);

/**
 * @brief Create a standard button gradient for custom-painted widgets.
 *
 * @param top Y coordinate of gradient start
 * @param bottom Y coordinate of gradient end
 * @param hovered Whether button is in hover state
 * @return QLinearGradient configured with proper color stops
 */
QLinearGradient buttonGradient(int top, int bottom, bool hovered = false);

/**
 * @brief Create a selected/active button gradient.
 *
 * @param top Y coordinate of gradient start
 * @param bottom Y coordinate of gradient end
 * @return QLinearGradient configured with light/selected colors
 */
QLinearGradient selectedGradient(int top, int bottom);

/**
 * @brief Get the standard border color for buttons.
 * @return QColor for button borders
 */
QColor borderColor();

/**
 * @brief Get the selected/active border color.
 * @return QColor for selected button borders
 */
QColor borderColorSelected();

/**
 * @brief Create a standard meter gradient (green → yellow → red).
 *
 * Used for S-meter, mic level, ALC, compression, and similar meters.
 * Provides consistent visual language across all level indicators.
 *
 * @param x1 X coordinate of gradient start
 * @param y1 Y coordinate of gradient start
 * @param x2 X coordinate of gradient end
 * @param y2 Y coordinate of gradient end
 * @return QLinearGradient with standard meter color stops
 */
QLinearGradient meterGradient(qreal x1, qreal y1, qreal x2, qreal y2);

// =============================================================================
// Dialog/Options Stylesheet Helpers (cached static const QString)
// =============================================================================
namespace Dialog {

/// Page background-color
const QString &pageBackground();

/// Horizontal/vertical separator line
const QString &separator();

/// Amber bold title label (AccentAmber, FontSizeTitle)
const QString &titleLabel();

/// Gray form label (TextGray, FontSizePopup)
const QString &formLabel();

/// White bold value label (TextWhite, FontSizePopup)
const QString &formValue();

/// Gray italic help text (TextGray, FontSizeLarge)
const QString &helpText();

/// White bold section header (TextWhite, FontSizePopup)
const QString &sectionHeader();

/// Dynamic-color status label (bold, FontSizePopup)
const QString &statusLabel(const QString &color);

/// Generic `color: <c>; font-size: <n>px;` label style. Builds a fresh QString each call (dynamic
/// color/size, so not cached). Use this instead of inlining `QString("color: %1; font-size: %2px;")`
/// across widgets — single source of truth keeps styling consistent and makes global tweaks cheap.
QString labelText(const QString &color, int sizePx);

/// Bold variant of `labelText()`.
QString labelTextBold(const QString &color, int sizePx);

/// Full combo box styling (dark bg, arrow, item view)
const QString &comboBox();

/// Full line edit styling (dark bg, focus border)
const QString &lineEdit();

/// Checkbox with indicator sizing (enabled)
const QString &checkBox();

/// Checkbox with indicator sizing (disabled/gray text)
const QString &checkBoxDisabled();

/// Dialog action button (dark bg, padding 10px 20px)
const QString &actionButton();

/// Small action button (padding 6px 12px)
const QString &actionButtonSmall();

} // namespace Dialog

} // namespace K4Styles

#endif // K4STYLES_H
