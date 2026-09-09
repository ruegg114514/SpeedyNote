#pragma once

#include <QtGlobal>  // for Q_OS_MACOS

#ifdef Q_OS_MACOS

#include <QDialog>

/**
 * @brief Standalone About dialog for the macOS App menu's "About SpeedyNote".
 *
 * macOS-only per QA Q3.1 (user explicitly requested this dialog NOT appear in
 * Windows/Linux/Android/iPadOS builds; About info on those platforms remains
 * inside ControlPanelDialog's About tab).
 *
 * Visually mirrors ControlPanelDialog::createAboutTab(): icon (mainicon.svg
 * via QSvgRenderer), app name, version (APP_VERSION), description, author,
 * copyright, Qt version footer.
 */
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

#endif // Q_OS_MACOS
