#include "AboutDialog.h"

#ifdef Q_OS_MACOS

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSvgRenderer>
#include <QVBoxLayout>

// APP_VERSION is defined by CMake via target_compile_definitions
// (see CMakeLists.txt: APP_VERSION="${PROJECT_VERSION}"). It mirrors
// PROJECT_VERSION as a string literal.

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("About SpeedyNote"));
    setWindowIcon(QIcon(":/resources/icons/mainicon.svg"));
    setFixedSize(360, 420);

    auto* layout = new QVBoxLayout(this);
    layout->addSpacing(20);

    // Application icon — render the SVG explicitly via QSvgRenderer so it
    // works both as a raw executable and inside a .app bundle. Mirrors the
    // pattern used in ControlPanelDialog::createAboutTab().
    auto* iconLabel = new QLabel(this);
    QSvgRenderer renderer(QString(":/resources/icons/mainicon.svg"));
    if (renderer.isValid()) {
        const int iconSize = 96;
        const qreal dpr = iconLabel->devicePixelRatioF();
        QPixmap pm(QSize(iconSize, iconSize) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        renderer.render(&painter, QRectF(0, 0, iconSize, iconSize));
        painter.end();
        iconLabel->setPixmap(pm);
    } else {
        iconLabel->setText("📝");
        iconLabel->setStyleSheet("font-size: 64px;");
    }
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);

    layout->addSpacing(10);

    auto* appNameLabel = new QLabel(tr("SpeedyNote"), this);
    appNameLabel->setAlignment(Qt::AlignCenter);
    appNameLabel->setStyleSheet("font-size: 24px; font-weight: bold;");
    layout->addWidget(appNameLabel);

    layout->addSpacing(5);

    auto* versionLabel = new QLabel(tr("Version %1").arg(APP_VERSION), this);
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setStyleSheet("font-size: 14px; color: #7f8c8d;");
    layout->addWidget(versionLabel);

    layout->addSpacing(15);

    auto* descriptionLabel = new QLabel(
        tr("A fast and intuitive note-taking application with PDF annotation support"),
        this);
    descriptionLabel->setAlignment(Qt::AlignCenter);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setStyleSheet("font-size: 12px; padding: 0 20px;");
    layout->addWidget(descriptionLabel);

    layout->addSpacing(20);

    auto* authorLabel = new QLabel(
        tr("Developed by GitHub @alpha-liu-01 and various contributors"),
        this);
    authorLabel->setAlignment(Qt::AlignCenter);
    authorLabel->setWordWrap(true);  // wrap long author/contributor strings
    authorLabel->setStyleSheet("font-size: 12px; padding: 0 20px;");
    layout->addWidget(authorLabel);

    layout->addSpacing(10);

    auto* copyrightLabel = new QLabel(tr("© 2026 SpeedyNote. All rights reserved."), this);
    copyrightLabel->setAlignment(Qt::AlignCenter);
    copyrightLabel->setStyleSheet("font-size: 10px; color: #95a5a6;");
    layout->addWidget(copyrightLabel);

    layout->addStretch();

    auto* qtLabel = new QLabel(tr("Built with Qt %1").arg(QT_VERSION_STR), this);
    qtLabel->setAlignment(Qt::AlignCenter);
    qtLabel->setStyleSheet("font-size: 9px; color: #bdc3c7;");
    layout->addWidget(qtLabel);

    layout->addSpacing(10);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    closeBtn->setAutoDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    layout->addSpacing(10);
}

#endif // Q_OS_MACOS
