#pragma once

// ============================================================================
// OutlineItemDelegate - Custom delegate for PDF outline tree items
// ============================================================================
// Part of the SpeedyNote document architecture (Phase E.2)
//
// Draws outline items with:
// - Title text (left-aligned, elided if too long)
// - Leader dots (stretch to fill space)
// - Page number (right-aligned)
//
// Visual layout:
//   Chapter 1 Introduction .............. 15
//   Section 1.1 Overview ................ 18
// ============================================================================

#include <QStyledItemDelegate>

/**
 * @brief Custom delegate for drawing PDF outline tree items.
 * 
 * Renders items with title, leader dots, and right-aligned page numbers
 * in a classic table-of-contents style.
 */
class OutlineItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit OutlineItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    /**
     * @brief Set dark mode for theming.
     * @param darkMode True for dark theme colors.
     */
    void setDarkMode(bool darkMode) { m_darkMode = darkMode; }

private:
    // Layout constants
    static constexpr int ROW_HEIGHT = 36;
    static constexpr int PAGE_NUMBER_WIDTH = 36;
    static constexpr int PADDING = 8;
    static constexpr int DOT_SPACING = 4;
    static constexpr int CHIP_WIDTH = 4;   // OUT1: leading per-source accent chip

    // Custom data roles (must match OutlinePanel)
    static constexpr int PageRole = Qt::UserRole;
    static constexpr int UnavailableRole = Qt::UserRole + 3;  // Plan A2: greyed/inert entry
    static constexpr int SourceSlotRole = Qt::UserRole + 5;   // OUT1: palette slot (-1 = none)

    bool m_darkMode = false;
};

