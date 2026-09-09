#pragma once

#include <QtGlobal>  // for Q_OS_MACOS

#ifdef Q_OS_MACOS

#include <QObject>

class QMenuBar;
class QMenu;
class QAction;

/**
 * @brief macOS-only system menu bar (parent-less QMenuBar).
 *
 * Singleton constructed once from Main.cpp under #ifdef Q_OS_MACOS, after
 * QApplication. Builds the 9 top-level menus and populates the App menu with
 * About / Settings. Other menus are empty stubs to be populated by per-
 * category migrations in MAC.3-7.
 *
 * Per QA Q6.5.3: a single global QMenuBar (not per-window) so the menu bar
 * remains visible even when no window is open. Menu actions dispatch to the
 * currently focused MainWindow via MainWindow::activeMainWindow() (MAC.1).
 */
class MacMenuBar : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Get (or lazily create) the singleton instance.
     *
     * The QMenuBar is parent-less and lives until QApplication::quit().
     */
    static MacMenuBar* instance();

    // Per-menu accessors used by per-category migrations (MAC.3-7) to add
    // their actions to the appropriate menu.
    QMenu* appMenu() const      { return m_appMenu; }
    QMenu* fileMenu() const     { return m_fileMenu; }
    QMenu* editMenu() const     { return m_editMenu; }
    QMenu* viewMenu() const     { return m_viewMenu; }
    QMenu* documentMenu() const { return m_documentMenu; }
    QMenu* toolsMenu() const    { return m_toolsMenu; }
    QMenu* ocrMenu() const      { return m_ocrMenu; }
    QMenu* windowMenu() const   { return m_windowMenu; }
    QMenu* helpMenu() const     { return m_helpMenu; }
    QMenuBar* menuBar() const   { return m_menuBar; }

private:
    explicit MacMenuBar(QObject* parent = nullptr);

    void buildAppMenu();      // About + Settings (Quit/Hide auto-provided by Qt)
    void populateFileMenu();      // MAC.3: New/Open/Save/Save As/Export/Close + Relink PDF
    void populateHelpMenu();      // MAC.3: Keyboard Shortcuts + Visit GitHub + Report a Bug
    void populateEditMenu();      // MAC.4: Undo/Redo + Cut/Copy/Paste/Delete + Find
    void populateDocumentMenu();  // MAC.4: Add/Insert/Delete page (PagedOnly auto-disable)
    void populateViewMenu();      // MAC.5: Zoom + page nav (PagedOnly) + edgeless nav (EdgelessOnly) + layout + panes + fullscreen
    void populateToolsMenu();     // MAC.7: 6 direct tools + 5 submenus (Highlighter Style / Insert / Object / Layers / Links)
    void populateOcrMenu();       // MAC.6: Scan + checkable toggles + OCR Language / Lock All OCR Text
    void populateWindowMenu();    // MAC.6: Next Tab / Previous Tab (Qt auto-provides Minimize / Zoom / Bring All to Front / open-windows list)

    static MacMenuBar* s_instance;

    QMenuBar* m_menuBar    = nullptr;
    QMenu* m_appMenu       = nullptr;
    QMenu* m_fileMenu      = nullptr;
    QMenu* m_editMenu      = nullptr;
    QMenu* m_viewMenu      = nullptr;
    QMenu* m_documentMenu  = nullptr;
    QMenu* m_toolsMenu     = nullptr;
    QMenu* m_ocrMenu       = nullptr;
    QMenu* m_windowMenu    = nullptr;
    QMenu* m_helpMenu      = nullptr;
};

#endif // Q_OS_MACOS
