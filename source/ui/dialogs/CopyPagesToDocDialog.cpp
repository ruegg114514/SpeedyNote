#include "CopyPagesToDocDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QPushButton>

CopyPagesToDocDialog::CopyPagesToDocDialog(const QList<DestEntry>& entries,
                                           int copyCount, QWidget* parent)
    : QDialog(parent)
    , m_entries(entries)
{
    setWindowTitle(tr("Copy Pages to Document"));
    setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* prompt = new QLabel(
        tr("Copy %n page(s) to:", "", qMax(0, copyCount)), this);
    layout->addWidget(prompt);

    m_docCombo = new QComboBox(this);
    for (const DestEntry& entry : m_entries) {
        m_docCombo->addItem(entry.label);
    }
    layout->addWidget(m_docCombo);

    QLabel* posLabel = new QLabel(tr("Position:"), this);
    layout->addWidget(posLabel);

    m_appendRadio = new QRadioButton(tr("Append to end"), this);
    m_beforeRadio = new QRadioButton(tr("Before page"), this);
    m_afterRadio = new QRadioButton(tr("After page"), this);
    m_appendRadio->setChecked(true);

    layout->addWidget(m_appendRadio);

    QHBoxLayout* beforeRow = new QHBoxLayout();
    beforeRow->addWidget(m_beforeRadio);
    QHBoxLayout* afterRow = new QHBoxLayout();
    afterRow->addWidget(m_afterRadio);

    m_pageSpin = new QSpinBox(this);
    m_pageSpin->setMinimum(1);
    // The two "page N" radios share a single spin box (one position at a time).
    afterRow->addWidget(m_pageSpin);
    afterRow->addStretch(1);
    beforeRow->addStretch(1);

    layout->addLayout(beforeRow);
    layout->addLayout(afterRow);

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    QPushButton* cancelButton = new QPushButton(tr("Cancel"), this);
    QPushButton* okButton = new QPushButton(tr("Copy"), this);
    okButton->setDefault(true);
    buttons->addWidget(cancelButton);
    buttons->addWidget(okButton);
    layout->addLayout(buttons);

    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_docCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CopyPagesToDocDialog::onDocChanged);
    connect(m_appendRadio, &QRadioButton::toggled,
            this, &CopyPagesToDocDialog::onPositionChanged);
    connect(m_beforeRadio, &QRadioButton::toggled,
            this, &CopyPagesToDocDialog::onPositionChanged);
    connect(m_afterRadio, &QRadioButton::toggled,
            this, &CopyPagesToDocDialog::onPositionChanged);

    updateSpinRange();
    onPositionChanged();
}

int CopyPagesToDocDialog::selectedDocIndex() const
{
    return m_docCombo ? m_docCombo->currentIndex() : -1;
}

int CopyPagesToDocDialog::insertIndex() const
{
    const int idx = selectedDocIndex();
    if (idx < 0 || idx >= m_entries.size()) {
        return 0;
    }
    const int pageCount = m_entries[idx].pageCount;
    if (m_beforeRadio->isChecked()) {
        return qBound(0, m_pageSpin->value() - 1, pageCount);
    }
    if (m_afterRadio->isChecked()) {
        return qBound(0, m_pageSpin->value(), pageCount);
    }
    // Append (default).
    return pageCount;
}

void CopyPagesToDocDialog::onDocChanged(int /*index*/)
{
    updateSpinRange();
}

void CopyPagesToDocDialog::onPositionChanged()
{
    // The page spin box only matters for the before/after positions.
    const bool needsPage = m_beforeRadio->isChecked() || m_afterRadio->isChecked();
    if (m_pageSpin) {
        m_pageSpin->setEnabled(needsPage);
    }
}

void CopyPagesToDocDialog::updateSpinRange()
{
    if (!m_pageSpin) {
        return;
    }
    const int idx = selectedDocIndex();
    const int pageCount = (idx >= 0 && idx < m_entries.size())
                              ? qMax(1, m_entries[idx].pageCount)
                              : 1;
    const int prev = m_pageSpin->value();
    m_pageSpin->setMaximum(pageCount);
    m_pageSpin->setValue(qBound(1, prev, pageCount));
}
