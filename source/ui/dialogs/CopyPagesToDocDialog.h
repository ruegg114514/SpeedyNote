#ifndef COPYPAGESTODOCDIALOG_H
#define COPYPAGESTODOCDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

class QComboBox;
class QRadioButton;
class QSpinBox;

/**
 * @brief Dialog to choose a destination document and insert position for a
 *        cross-document page copy (Plan D1).
 *
 * UI-only: it knows nothing about Document/viewport types. The caller supplies
 * a list of candidate destinations (label + page count) and the number of pages
 * being copied. After exec() returns QDialog::Accepted, read selectedDocIndex()
 * (index into the supplied list) and insertIndex() (0-based destination index).
 */
class CopyPagesToDocDialog : public QDialog {
    Q_OBJECT

public:
    /// A candidate destination document.
    struct DestEntry {
        QString label;    ///< Display name (already disambiguated).
        int pageCount;    ///< Current page count of the destination.
    };

    /**
     * @param entries Candidate destinations (must be non-empty).
     * @param copyCount Number of pages being copied (for the title/prompt).
     * @param parent Parent widget.
     */
    CopyPagesToDocDialog(const QList<DestEntry>& entries, int copyCount,
                         QWidget* parent = nullptr);

    /**
     * @brief Index into the supplied entries list for the chosen destination.
     */
    int selectedDocIndex() const;

    /**
     * @brief 0-based insertion index within the chosen destination.
     *
     * Append -> pageCount; Before page N -> N-1; After page N -> N.
     */
    int insertIndex() const;

private slots:
    void onDocChanged(int index);
    void onPositionChanged();

private:
    void updateSpinRange();

    QList<DestEntry> m_entries;
    QComboBox* m_docCombo = nullptr;
    QRadioButton* m_appendRadio = nullptr;
    QRadioButton* m_beforeRadio = nullptr;
    QRadioButton* m_afterRadio = nullptr;
    QSpinBox* m_pageSpin = nullptr;
};

#endif // COPYPAGESTODOCDIALOG_H
