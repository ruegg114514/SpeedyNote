// ============================================================================
// DarkModeUtils - HSL lightness inversion for dark mode rendering
// ============================================================================
// Shared utility for PDF dark mode (canvas) and PDF export stroke darkening.
// Uses HSL lightness inversion which preserves hue and saturation while
// flipping light↔dark, avoiding the crude color shifts of full RGB inversion.
// ============================================================================

#ifndef DARKMODEUTILS_H
#define DARKMODEUTILS_H

#include <QColor>
#include <QImage>
#include <QRect>
#include <QVector>

namespace DarkModeUtils {

/**
 * @brief Invert the lightness of an image, skipping raster-image regions.
 *
 * Performs pure HSL lightness inversion (L' = 1-L) on every pixel that
 * is NOT inside one of the supplied @p imageRegions.  Hue and saturation
 * are preserved, avoiding the crude colour shifts of full RGB inversion.
 *
 * Pixels that fall inside any rectangle in @p imageRegions are left
 * completely untouched, so that photos and screenshots embedded in PDFs
 * are not colour-mangled.
 *
 * @param image         Must be Format_ARGB32 or similar; converted if needed.
 * @param imageRegions  Bounding rectangles of raster images (pixel coords).
 *                      Pass an empty vector to apply inversion everywhere.
 */
void invertImageLightness(QImage& image, const QVector<QRect>& imageRegions = {});

/**
 * @brief Invert the lightness of a single colour.
 *
 * HSL: L' = 1.0 - L, hue and saturation unchanged.
 */
QColor invertColorLightness(const QColor& color);

/**
 * @brief Conditionally darken a colour for print-friendly export.
 *
 * Only inverts lightness when L > 0.5 (i.e. light colours become dark).
 * Dark colours (L <= 0.5) are returned unchanged so that existing
 * dark strokes are not lightened.
 */
QColor darkenColorForExport(const QColor& color);

/**
 * @brief Neutral gray accent for a PDF source group (OUT1 multi-source outline).
 * @param slot     Zero-based source display slot (see Document::paletteSlotForSource).
 *                 Negative values return an invalid QColor (no accent).
 * @param darkMode True for the dark-theme palette.
 * @return An ordered, theme-appropriate gray shade; cycles when @p slot exceeds
 *         the palette size so any number of sources still gets a distinct-ish tint.
 *
 * Used to draw the leading chip / subtle row tint that visually distinguishes
 * outline entries by their originating PDF. The future enhanced scroll bar
 * (Plan SB1) reuses the same slot assignment.
 */
QColor sourceShade(int slot, bool darkMode);

/**
 * @brief Saturated theme-tinted accent color for a PDF source group.
 * @param slot     Zero-based source display slot (see Document::paletteSlotForSource).
 *                 Negative values return an invalid QColor (no accent).
 * @param darkMode True for the dark-theme palette.
 * @return A distinct, legible color for @p slot; cycles when @p slot exceeds
 *         the palette size. Uses the SAME slot index as sourceShade() so a
 *         source rendered as "gray shade N" in the outline panel reads as
 *         "color N" on the enhanced scroll bar (Plan SB2 / Q13.10).
 */
QColor sourceAccentColor(int slot, bool darkMode);

/**
 * @brief Fixed amber accent for search-hit ticks on the enhanced scroll bar.
 * @param darkMode True for the dark-theme variant.
 * @return A theme-adjusted amber used for every search tick (Plan SBS3). The
 *         current match's tick is emphasized by the bar itself (brighter/taller).
 */
QColor searchHitColor(bool darkMode);

} // namespace DarkModeUtils

#endif // DARKMODEUTILS_H
