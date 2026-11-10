#pragma once

// ============================================================================
// MemoGalleryWidget - the 随记 (memos) page of the right sidebar
// ============================================================================
// Shows every memo of the active notebook as a thumbnail grid. Clicking a
// thumbnail asks the window to open that memo as a floating card over the page;
// the sidebar itself is closed at that point (the window handles that). Hover
// controls / the context menu allow renaming, deleting and locating the page a
// memo was cut from.
//
// The widget owns no state beyond the widget tree: it renders whatever
// Document::memos() currently holds and resolves image files through
// Document::memoImage(), so a memo created before the document was first saved
// (image bytes still in memory) renders exactly like a persisted one.
// ============================================================================

#include <QString>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class Document;

class MemoGalleryWidget : public QWidget {
    Q_OBJECT

public:
    explicit MemoGalleryWidget(QWidget* parent = nullptr);
    ~MemoGalleryWidget() override = default;

    /**
     * @brief Point the gallery at a document and rebuild the grid.
     *
     * Passing nullptr (no document open) shows the empty state.
     */
    void setDocument(Document* document);

    /// Rebuild the grid from the current document (memo added / renamed /
    /// deleted, or a float moved).
    void refresh();

    void setDarkMode(bool darkMode);
    bool isDarkMode() const { return m_darkMode; }

signals:
    /// Open (or re-raise) the floating card for this memo.
    void memoOpenRequested(const QString& memoId);
    /// Delete the memo and its image file (the window confirms first).
    void memoDeleteRequested(const QString& memoId);
    /// Rename the memo (new title already trimmed and non-empty).
    void memoRenameRequested(const QString& memoId, const QString& newTitle);
    /// Jump to the notebook page the memo was captured from.
    void memoLocateRequested(int pageIndex);
    /// Ask the window to run the "import an image as a memo" file dialog.
    void importImageRequested();

private:
    void setupUi();
    void applyStyle();
    void clearCards();
    void showEmptyState(const QString& text);

    Document* m_document = nullptr;
    bool m_darkMode = false;

    QVBoxLayout* m_rootLayout = nullptr;
    QLabel* m_headerLabel = nullptr;
    QPushButton* m_importButton = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_gridHost = nullptr;
    QGridLayout* m_gridLayout = nullptr;
    QLabel* m_emptyLabel = nullptr;
};
