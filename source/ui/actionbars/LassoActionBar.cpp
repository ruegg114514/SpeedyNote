#include "LassoActionBar.h"
#include "../widgets/ActionBarButton.h"
#include "../widgets/ColorPresetButton.h"

LassoActionBar::LassoActionBar(QWidget* parent)
    : ActionBar(parent)
{
    setupButtons();
}

void LassoActionBar::setupButtons()
{
    // Recolor swatch (top): sized to match the 36px ActionBarButtons.
    // ColorPresetButton already implements the click-once-then-edit pattern
    // we want (clicked() always, editRequested() only when already selected),
    // so we just toggle the selected state ourselves to switch click 1 vs
    // click 2 semantics.
    m_recolorButton = new ColorPresetButton(this, /*buttonSize*/ 36);
    m_recolorButton->setToolTip(tr("Recolor selection (click again to pick color)"));
    addButton(m_recolorButton);
    connect(m_recolorButton, &ColorPresetButton::clicked, this, [this]() {
        emit recolorRequested(m_recolorButton->color());
        // Mark "selected" so the next click is interpreted as edit by the widget.
        m_recolorButton->setSelected(true);
    });
    connect(m_recolorButton, &ColorPresetButton::editRequested,
            this, &LassoActionBar::recolorEditRequested);

    // Create Cut button
    // Note: Using cross icon as cut icon is not available
    m_cutButton = new ActionBarButton(this);
    m_cutButton->setIconName("cut");
    m_cutButton->setToolTip(tr("Cut (Ctrl+X)"));
    addButton(m_cutButton);
    connect(m_cutButton, &ActionBarButton::clicked, this, &LassoActionBar::cutRequested);
    
    // Create Copy button
    m_copyButton = new ActionBarButton(this);
    m_copyButton->setIconName("copy");
    m_copyButton->setToolTip(tr("Copy (Ctrl+C)"));
    addButton(m_copyButton);
    connect(m_copyButton, &ActionBarButton::clicked, this, &LassoActionBar::copyRequested);
    
    // Create Paste button (initially hidden - shown only if clipboard has strokes)
    m_pasteButton = new ActionBarButton(this);
    m_pasteButton->setIconName("paste");
    m_pasteButton->setToolTip(tr("Paste (Ctrl+V)"));
    m_pasteButton->hide();  // Hidden by default
    addButton(m_pasteButton);
    connect(m_pasteButton, &ActionBarButton::clicked, this, &LassoActionBar::pasteRequested);

    // Memo (随记) actions. "Keep" leaves the strokes on the page and stores a
    // copy; "cut" removes them from the page and stores that. Both render the
    // selection (with any live rotation/scale) into a transparent PNG.
    m_memoKeepButton = new ActionBarButton(this);
    m_memoKeepButton->setIconName("memo_keep");
    m_memoKeepButton->setToolTip(tr("Add to memos, keep it on the page"));
    addButton(m_memoKeepButton);
    connect(m_memoKeepButton, &ActionBarButton::clicked, this, [this]() {
        emit addToMemoRequested(false);
    });

    m_memoCutButton = new ActionBarButton(this);
    m_memoCutButton->setIconName("memo_cut");
    m_memoCutButton->setToolTip(tr("Cut into memos (remove it from the page)"));
    addButton(m_memoCutButton);
    connect(m_memoCutButton, &ActionBarButton::clicked, this, [this]() {
        emit addToMemoRequested(true);
    });

    // Create Delete button (bottom)
    m_deleteButton = new ActionBarButton(this);
    m_deleteButton->setIconName("trash");
    m_deleteButton->setToolTip(tr("Delete"));
    addButton(m_deleteButton);
    connect(m_deleteButton, &ActionBarButton::clicked, this, &LassoActionBar::deleteRequested);
}

void LassoActionBar::updateButtonStates()
{
    // Recolor swatch, Cut, Copy, Delete: visible only when selection exists
    if (m_recolorButton) {
        m_recolorButton->setVisible(m_hasSelection);
        // When the selection goes away, drop the "selected" state so the
        // next selection starts fresh (click 1 = apply, not edit).
        if (!m_hasSelection) {
            m_recolorButton->setSelected(false);
        }
    }
    if (m_cutButton) {
        m_cutButton->setVisible(m_hasSelection);
    }
    if (m_copyButton) {
        m_copyButton->setVisible(m_hasSelection);
    }
    if (m_deleteButton) {
        m_deleteButton->setVisible(m_hasSelection);
    }
    if (m_memoKeepButton) {
        m_memoKeepButton->setVisible(m_hasSelection);
    }
    if (m_memoCutButton) {
        m_memoCutButton->setVisible(m_hasSelection);
    }

    // Paste button: visible when clipboard has strokes
    if (m_pasteButton) {
        m_pasteButton->setVisible(m_hasStrokesInClipboard);
    }

    // Trigger re-layout to adjust height
    adjustSize();
    updateGeometry();
}

void LassoActionBar::setOverrideColor(const QColor& color)
{
    if (!color.isValid()) return;
    m_overrideColor = color;
    if (m_recolorButton) {
        m_recolorButton->setColor(color);
    }
}

void LassoActionBar::setHasStrokesInClipboard(bool hasStrokes)
{
    if (m_hasStrokesInClipboard != hasStrokes) {
        m_hasStrokesInClipboard = hasStrokes;
        updateButtonStates();
    }
}

void LassoActionBar::setHasSelection(bool hasSelection)
{
    if (m_hasSelection != hasSelection) {
        m_hasSelection = hasSelection;
        updateButtonStates();
    }
}

void LassoActionBar::setDarkMode(bool darkMode)
{
    // Call base class implementation (updates background, shadow, separators)
    ActionBar::setDarkMode(darkMode);
    
    // Propagate to all buttons
    if (m_copyButton) {
        m_copyButton->setDarkMode(darkMode);
    }
    if (m_cutButton) {
        m_cutButton->setDarkMode(darkMode);
    }
    if (m_pasteButton) {
        m_pasteButton->setDarkMode(darkMode);
    }
    if (m_memoKeepButton) {
        m_memoKeepButton->setDarkMode(darkMode);
    }
    if (m_memoCutButton) {
        m_memoCutButton->setDarkMode(darkMode);
    }
    if (m_deleteButton) {
        m_deleteButton->setDarkMode(darkMode);
    }
}

