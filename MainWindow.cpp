#include "MainWindow.h"
#include "AppSettings.h"
#include "TrayIcon.h"
#include "CaptureTool.h"
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QClipboard>
#include <QCryptographicHash>
#include <QMimeData>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDesktopServices>
#include <QEasingCurve>
#include <QByteArrayView>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <QSignalBlocker>
#include <QSettings>
#include <QSvgRenderer>
#include <QToolButton>
#include <QUrl>
#include <QWindow>

namespace {
constexpr qint64 kClipboardDuplicateWindowMs = 500;
constexpr int kSidebarVerticalMargin = 10;
constexpr int kSidebarPreferredTopOffset = 50;

int sidebarExpandedTop(const QRect &screenGeometry, int panelHeight)
{
    const int minTop = screenGeometry.top() + kSidebarVerticalMargin;
    const int maxTop = screenGeometry.bottom() - panelHeight + 1 - kSidebarVerticalMargin;
    return qBound(minTop, screenGeometry.top() + kSidebarPreferredTopOffset, maxTop);
}

QRect resolveAvailableGeometry(const QWidget *widget)
{
    if (widget) {
        if (QWindow *window = widget->windowHandle(); window && window->screen()) {
            return window->screen()->availableGeometry();
        }

        if (QScreen *screen = QApplication::screenAt(widget->frameGeometry().center())) {
            return screen->availableGeometry();
        }
    }

    if (QScreen *screen = QApplication::screenAt(QCursor::pos())) {
        return screen->availableGeometry();
    }

    if (QScreen *screen = QApplication::primaryScreen()) {
        return screen->availableGeometry();
    }

    return QRect();
}

QString normalizeThemeModeValue(QString mode)
{
    mode = mode.trimmed().toLower();
    if (mode != QStringLiteral("light")) {
        return QStringLiteral("dark");
    }
    return mode;
}

QIcon loadThemeAwareSvgIcon(const QString &resourcePath, const QColor &color)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QIcon(resourcePath);
    }

    QByteArray svgData = file.readAll();
    svgData.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) {
        return QIcon(resourcePath);
    }

    QIcon icon;
    const QList<int> sizes = {14, 16, 18, 20, 24};
    for (const int size : sizes) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&painter, QRectF(0, 0, size, size));
        painter.end();
        icon.addPixmap(pixmap);
    }

    return icon;
}

QString controlsStyleSheet(const AppSettings::ThemePalette &palette)
{
    return QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; padding: 10px 12px; border-radius: 6px; text-align: center; font-family: 'Microsoft YaHei'; font-size: 14px; font-weight: 500; }"
        "QPushButton#captureActionButton { padding: 12px 12px; font-size: 17px; font-weight: 600; text-align: center; }"
        "QPushButton:hover { background-color: %4; color: #FFFFFF; }"
        "QPushButton:pressed { background-color: %5; color: #FFFFFF; }"
        "QToolButton#topActionButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 12px; min-width: 24px; min-height: 24px; max-width: 24px; max-height: 24px; font-family: 'Microsoft YaHei'; font-size: 13px; font-weight: 600; }"
        "QToolButton#topActionButton:hover { background-color: %4; color: #FFFFFF; }"
        "QToolButton#topActionButton:checked { background-color: %4; color: #FFFFFF; }"
        "QToolButton#topActionButton:pressed { background-color: %5; color: #FFFFFF; }"
        "QToolButton#topActionButton:disabled { background-color: %6; color: %7; }"
        "QWidget#historyDivider { background-color: %3; border: none; border-radius: 2px; }"
        "QListWidget { background-color: transparent; border: none; outline: none; }"
        "QListWidget::item { background-color: %6; margin-bottom: 8px; border-radius: 6px; }"
        "QListWidget::item:selected { background-color: %1; border: 1px solid %4; }"
        "QLabel#historyTitleLabel { color: %2; font-family: 'YouYuan', 'Microsoft YaHei UI', 'Microsoft YaHei'; font-weight: 700; font-size: 20px; qproperty-alignment: AlignHCenter|AlignVCenter; }"
           "QLabel#historyTimeLabel { background-color: %8; color: %2; border: 1px solid %4; border-radius: 7px; padding: 3px 8px; font-family: 'Microsoft YaHei'; font-weight: 700; font-size: 11px; }"
           "QLabel#historyTextLabel { font-family: '\u5B8B\u4F53', 'Times New Roman'; font-size: 12px; }")
        .arg(palette.buttonBackground,
             palette.text,
             palette.border,
             palette.buttonHover,
             palette.buttonPressed,
             palette.secondaryBackground,
               palette.secondaryText,
               palette.background);
}

QString expandedPanelStyleSheet(const AppSettings::ThemePalette &palette)
{
    return QStringLiteral(
               "#centralWidget { background-color: %1; border: 1.5px solid %2; border-radius: 10px; }")
               .arg(palette.secondaryBackground, palette.border) +
           controlsStyleSheet(palette);
}

QString dockStripStyleSheet(const AppSettings::ThemePalette &palette,
                            const QString &stripColor,
                            int stripBorderRadius)
{
    return QStringLiteral(
               "#centralWidget { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
               .arg(stripColor)
               .arg(palette.border)
               .arg(stripBorderRadius) +
           controlsStyleSheet(palette);
}

bool savePixmapToConfiguredPath(const QPixmap &pixmap, QString *savedPath, QString *errorText)
{
    if (pixmap.isNull()) {
        if (errorText) {
            *errorText = QStringLiteral("\u622a\u56fe\u6570\u636e\u4e3a\u7a7a\u3002");
        }
        return false;
    }

    QSettings settings = AppSettings::createSettings();
    const QString dirPath = AppSettings::normalizeSavePath(
        settings.value(AppSettings::kSavePath, AppSettings::defaultSavePath()).toString());

    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorText) {
            *errorText = QStringLiteral("\u4fdd\u5b58\u76ee\u5f55\u4e0d\u5b58\u5728\u4e14\u65e0\u6cd5\u521b\u5efa\uff1a\n%1")
                             .arg(QDir::toNativeSeparators(dirPath));
        }
        return false;
    }

    const QString format = AppSettings::normalizeSaveFormat(
        settings.value(AppSettings::kSaveFormat, AppSettings::kDefaultSaveFormat).toString());
    const QString extension =
        (format == QStringLiteral("JPG")) ? QStringLiteral("jpg") : QStringLiteral("png");

    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString baseName = QStringLiteral("Silo_%1").arg(timestamp);

    QString filePath = dir.filePath(QStringLiteral("%1.%2").arg(baseName, extension));
    int suffix = 1;
    while (QFileInfo::exists(filePath)) {
        filePath =
            dir.filePath(QStringLiteral("%1_%2.%3").arg(baseName).arg(suffix++).arg(extension));
    }

    const QByteArray formatBytes = format.toLatin1();
    if (!pixmap.save(filePath, formatBytes.constData())) {
        if (errorText) {
            *errorText = QStringLiteral("\u5199\u5165\u6587\u4ef6\u5931\u8d25\uff1a\n%1")
                             .arg(QDir::toNativeSeparators(filePath));
        }
        return false;
    }

    if (savedPath) {
        *savedPath = filePath;
    }
    if (errorText) {
        errorText->clear();
    }
    return true;
}

QString normalizeTextForClipboardSignature(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text.trimmed();
}

QString buildMultilineTextPreview(QString text,
                                  const QFontMetrics &fontMetrics,
                                  const QFont &font,
                                  int maxWidth,
                                  int maxLines)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    if (text.isEmpty() || maxWidth <= 0 || maxLines <= 0) {
        return QString();
    }

    QStringList wrappedLines;
    bool truncated = false;
    const QStringList paragraphs = text.split(QLatin1Char('\n'));

    for (int i = 0; i < paragraphs.size(); ++i) {
        const QString paragraph = paragraphs.at(i);
        if (paragraph.isEmpty()) {
            if (wrappedLines.size() < maxLines) {
                wrappedLines.append(QString());
                continue;
            }
            truncated = true;
            break;
        }

        QTextLayout textLayout(paragraph, font);
        textLayout.beginLayout();
        while (true) {
            QTextLine line = textLayout.createLine();
            if (!line.isValid()) {
                break;
            }

            line.setLineWidth(maxWidth);
            const QString segment = paragraph.mid(line.textStart(), line.textLength());
            if (wrappedLines.size() < maxLines) {
                wrappedLines.append(segment);
            } else {
                truncated = true;
                break;
            }
        }
        textLayout.endLayout();

        if (truncated) {
            break;
        }

        if (i < paragraphs.size() - 1 && wrappedLines.size() >= maxLines) {
            truncated = true;
            break;
        }
    }

    if (wrappedLines.isEmpty()) {
        return QString();
    }

    if (truncated) {
        wrappedLines.last() = fontMetrics.elidedText(wrappedLines.last() + QStringLiteral(" ..."),
                                                     Qt::ElideRight,
                                                     maxWidth);
    }

    return wrappedLines.join(QLatin1Char('\n'));
}

QString clipboardSignatureForPixmap(const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        return QString();
    }

    QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    if (image.isNull()) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),
                                image.sizeInBytes()));
    return QStringLiteral("img:%1:%2x%3")
        .arg(QString::fromLatin1(hash.result().toHex()))
        .arg(image.width())
        .arg(image.height());
}

void resetTopActionButtonVisual(QToolButton *button)
{
    if (!button) {
        return;
    }

    button->setDown(false);
    button->clearFocus();

    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(button, &leaveEvent);

    button->update();
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), isDocked(false), normalWidth(280), ignoreClipboardChange(false)
{
    captureTool = new CaptureTool(this);
    tray = new TrayIcon(this); 
    tray->setCaptureTool(captureTool); 
    connect(tray, &TrayIcon::settingsUpdated, this, &MainWindow::onSettingsUpdated);
    connect(tray, &TrayIcon::clearHistoryRequested, this, &MainWindow::clearClipboardHistory);
    connect(tray,
        &TrayIcon::settingsDialogVisibilityChanged,
        this,
        &MainWindow::onSettingsDialogVisibilityChanged);
    connect(tray, &TrayIcon::trayLeftClicked, this, &MainWindow::onTrayLeftClicked);
    connect(captureTool,
        &CaptureTool::regionCaptureStateChanged,
        this,
        &MainWindow::onRegionCaptureStateChanged);
    connect(captureTool,
        &CaptureTool::previewDialogStateChanged,
        this,
        &MainWindow::onPreviewDialogStateChanged);

    dockTriggerWidth = AppSettings::kDockTriggerWidth;
    dockTriggerHeight = AppSettings::kDockTriggerHeight;
    rightMargin = AppSettings::kSidebarRightMargin;
    {
        const QSettings settings = AppSettings::createSettings();
        sidebarPinned =
            settings.value(AppSettings::kSidebarPinned, AppSettings::kDefaultSidebarPinned).toBool();
        themeMode = normalizeThemeModeValue(
            settings.value(AppSettings::kThemeMode, AppSettings::kDefaultThemeMode).toString());
        dockStripTop =
            settings.value(AppSettings::kDockStripTop, AppSettings::kDefaultDockStripTop).toInt();
    }
    
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    
    const QRect screenGeometry = resolveAvailableGeometry(this);
    if (screenGeometry.isValid()) {
        const int sidebarHeight = qMax(320, screenGeometry.height() - 100);
        const int x = screenGeometry.right() - rightMargin - normalWidth + 1;
        const int y = sidebarExpandedTop(screenGeometry, sidebarHeight);
        setGeometry(x, y, normalWidth, sidebarHeight);
    }

    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setFixedWidth(normalWidth);

    setupUI();
    updatePinnedUi();

    animation = new QPropertyAnimation(this, "geometry", this);
    animation->setDuration(AppSettings::kSidebarAnimationDurationMs);
    animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(animation, &QPropertyAnimation::finished, this, [this]() {
        setDockContentVisible(!isDocked);
        updateDockMask();
        if (!isDocked) {
            relayoutHistoryItems();
        }
    });
    
    hoverRevealTimer = new QTimer(this);
    hoverRevealTimer->setSingleShot(true);
    connect(hoverRevealTimer, &QTimer::timeout, this, [this]() {
        if (shouldSuppressSidebar() || sidebarPinned || !isDocked) {
            return;
        }
        if (geometry().contains(QCursor::pos())) {
            expandSidebar(true);
        }
    });

    leaveDockTimer = new QTimer(this);
    leaveDockTimer->setSingleShot(true);
    connect(leaveDockTimer, &QTimer::timeout, this, [this]() {
        if (shouldSuppressSidebar() || sidebarPinned || trayRevealHoldActive || isDocked) {
            return;
        }
        if (!geometry().contains(QCursor::pos())) {
            dockSidebar(false);
        }
    });

    edgeTimer = new QTimer(this);
    connect(edgeTimer, &QTimer::timeout, this, &MainWindow::checkEdgeDocking);
    edgeTimer->start(AppSettings::kEdgeCheckIntervalMs);

    trayRevealHoldTimer = new QTimer(this);
    trayRevealHoldTimer->setSingleShot(true);
    connect(trayRevealHoldTimer, &QTimer::timeout, this, [this]() {
        trayRevealHoldActive = false;
        if (shouldSuppressSidebar() || isDocked || sidebarPinned) {
            return;
        }
        if (!geometry().contains(QCursor::pos()) && leaveDockTimer) {
            leaveDockTimer->start(AppSettings::kSidebarAutoDockDelayMs);
        }
    });

    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::onClipboardChanged);
}

MainWindow::~MainWindow() {}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    relayoutHistoryItems();

    if (!startupHoldApplied && !shouldSuppressSidebar()) {
        startupHoldApplied = true;
        revealSidebarWithHold();
    }
}

void MainWindow::setupUI()
{
    centralWidget = new QWidget(this);
    centralWidget->setObjectName("centralWidget");
    centralWidget->setStyleSheet(QString());

    auto *shadow = new QGraphicsDropShadowEffect(centralWidget);
    shadow->setBlurRadius(22);
    shadow->setOffset(-2, 0);
    shadow->setColor(QColor(0, 0, 0, 120));
    centralWidget->setGraphicsEffect(shadow);

    mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(15, 20, 15, 20);

    QHBoxLayout *topBarLayout = new QHBoxLayout();
    topBarLayout->setContentsMargins(0, 0, 0, 0);
    topBarLayout->setSpacing(8);
    topBarLayout->addStretch();

    githubButton = new QToolButton(this);
    githubButton->setObjectName("topActionButton");
    githubButton->setIconSize(QSize(14, 14));
    githubButton->setToolTip(QStringLiteral("\u6253\u5f00 GitHub"));
    githubButton->setCursor(Qt::PointingHandCursor);
    connect(githubButton, &QToolButton::clicked, this, &MainWindow::onOpenGitHub);

    themeToggleButton = new QToolButton(this);
    themeToggleButton->setObjectName("topActionButton");
    themeToggleButton->setText(QString());
    themeToggleButton->setIconSize(QSize(14, 14));
    themeToggleButton->setToolTip(QStringLiteral("\u5207\u6362\u4e3b\u9898"));
    themeToggleButton->setCursor(Qt::PointingHandCursor);
    connect(themeToggleButton, &QToolButton::clicked, this, &MainWindow::onToggleTheme);

    pinButton = new QToolButton(this);
    pinButton->setObjectName("topActionButton");
    pinButton->setCheckable(true);
    pinButton->setText(QString());
    pinButton->setIcon(QIcon(QStringLiteral(":/icons/pin.svg")));
    pinButton->setIconSize(QSize(14, 14));
    pinButton->setCursor(Qt::PointingHandCursor);
    connect(pinButton, &QToolButton::toggled, this, &MainWindow::onTogglePinned);

    settingsButton = new QToolButton(this);
    settingsButton->setObjectName("topActionButton");
    settingsButton->setIconSize(QSize(14, 14));
    settingsButton->setToolTip(QStringLiteral("\u8bbe\u7f6e"));
    settingsButton->setCursor(Qt::PointingHandCursor);
    connect(settingsButton, &QToolButton::clicked, this, &MainWindow::onOpenSettings);

    exitButton = new QToolButton(this);
    exitButton->setObjectName("topActionButton");
    exitButton->setIconSize(QSize(14, 14));
    exitButton->setToolTip(QStringLiteral("\u9000\u51fa"));
    exitButton->setCursor(Qt::PointingHandCursor);
    connect(exitButton, &QToolButton::clicked, this, &MainWindow::onRequestExit);

    topBarLayout->addWidget(githubButton);
    topBarLayout->addWidget(themeToggleButton);
    topBarLayout->addWidget(pinButton);
    topBarLayout->addWidget(settingsButton);
    topBarLayout->addWidget(exitButton);
    mainLayout->addLayout(topBarLayout);
    mainLayout->addSpacing(8);

    // 区域截图按钮
    QPushButton *btnRegion = new QPushButton(QStringLiteral("\u533a\u57df\u622a\u56fe"), this);
    btnRegion->setObjectName("captureActionButton");
    QFont captureButtonFont(QStringLiteral("Microsoft YaHei"), 17, QFont::DemiBold);
    captureButtonFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    btnRegion->setFont(captureButtonFont);
    connect(btnRegion, &QPushButton::clicked, this, [this]() {
        const QSettings settings = AppSettings::createSettings();
        const bool hideSidebar =
            settings.value(AppSettings::kHideSidebar, AppSettings::kDefaultHideSidebar).toBool();
        regionCaptureShouldHideSidebar = hideSidebar;

        if (hideSidebar) {
            if (!isDocked) {
                dockSidebar(false);
            }
            QTimer::singleShot(AppSettings::kPreCaptureDelayMs, this, [this]() {
                captureTool->captureRegion();
            });
            return;
        }

        captureTool->captureRegion();
    });
    mainLayout->addWidget(btnRegion);

    // 全屏截图按钮 (必定隐藏侧边栏)
    QPushButton *btnFull = new QPushButton(QStringLiteral("\u5168\u5c4f\u622a\u56fe"), this);
    btnFull->setObjectName("captureActionButton");
    btnFull->setFont(captureButtonFont);
    connect(btnFull, &QPushButton::clicked, this, [this]() {
        if (!isDocked) {
            dockSidebar(false);
        }
        QTimer::singleShot(AppSettings::kPreCaptureDelayMs, this, [this]() {
            captureTool->captureFullScreen();
        });
    });
    mainLayout->addWidget(btnFull);

    mainLayout->addSpacing(10);
    QWidget *historyDivider = new QWidget(this);
    historyDivider->setObjectName("historyDivider");
    historyDivider->setFixedSize(182, 3);
    mainLayout->addWidget(historyDivider, 0, Qt::AlignHCenter);
    mainLayout->addSpacing(4);

    historyList = new QListWidget(this);
    historyList->setContextMenuPolicy(Qt::CustomContextMenu); 
    mainLayout->addWidget(historyList);

    connect(historyList, &QListWidget::itemClicked, this, &MainWindow::onItemClicked);
    connect(historyList, &QListWidget::customContextMenuRequested, this, &MainWindow::onCustomContextMenu);

    setCentralWidget(centralWidget);

    connect(captureTool, &CaptureTool::screenshotTaken, this, [this](const QPixmap &pixmap){
        ignoreClipboardChange = true;
        QApplication::clipboard()->setPixmap(pixmap);
        addClipboardItem(pixmap);
        QTimer::singleShot(100, this, [this](){ ignoreClipboardChange = false; });
    });

    applyTheme();
}

void MainWindow::onItemClicked(QListWidgetItem *item) { historyList->clearSelection(); }

void MainWindow::onCustomContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = historyList->itemAt(pos);
    if (!item) return;

    historyList->setCurrentItem(item); 

    const QVariant data = item->data(Qt::UserRole);
    const bool isImageItem =
        data.typeId() == QMetaType::QPixmap || data.typeId() == QMetaType::Type::QPixmap ||
        data.canConvert<QPixmap>();

    QMenu menu(this);
    QAction *copyAction = menu.addAction(QStringLiteral("\u590d\u5236")); 
    QAction *saveAction = nullptr;
    if (isImageItem) {
        saveAction = menu.addAction(QStringLiteral("\u4fdd\u5b58"));
    }
    menu.addSeparator(); 
    QAction *delAction = menu.addAction(QStringLiteral("\u5220\u9664")); 
    
    QAction *selected = menu.exec(historyList->mapToGlobal(pos));
    if (selected == copyAction) {
        ignoreClipboardChange = true;
        if (data.canConvert<QPixmap>()) {
            QApplication::clipboard()->setPixmap(data.value<QPixmap>());
        } else {
            QApplication::clipboard()->setText(data.toString());
        }
        
        // 【关键修复】: 不直接 takeItem 然后 insert，因为这会销毁自定义 Widget！
        // 正确做法：读取数据 -> 彻底删除旧项 -> 使用数据重新生成一个新项在最顶端。
        int row = historyList->row(item);
        if (row != 0) {
            delete historyList->takeItem(row);
            addClipboardItem(data);
        }
        historyList->clearSelection(); 
        QTimer::singleShot(100, this, [this](){ ignoreClipboardChange = false; });
    } else if (saveAction && selected == saveAction) {
        const QPixmap pixmap = data.value<QPixmap>();
        QString savedPath;
        QString errorText;
        if (savePixmapToConfiguredPath(pixmap, &savedPath, &errorText)) {
            QMessageBox::information(this,
                                     QStringLiteral("\u4fdd\u5b58\u6210\u529f"),
                                     QStringLiteral("\u5df2\u4fdd\u5b58\u5230\uff1a\n%1")
                                         .arg(QDir::toNativeSeparators(savedPath)));
        } else {
            QMessageBox::warning(this,
                                 QStringLiteral("\u4fdd\u5b58\u5931\u8d25"),
                                 errorText.isEmpty()
                                     ? QStringLiteral("\u65e0\u6cd5\u4fdd\u5b58\u56fe\u7247\u6587\u4ef6\u3002")
                                     : errorText);
        }
    } else if (selected == delAction) {
        delete historyList->takeItem(historyList->row(item));
    }
}

void MainWindow::onClipboardChanged()
{
    if (ignoreClipboardChange) return;

    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData) return;

    QVariant data;
    QString signature;

    if (mimeData->hasImage()) {
        QPixmap pix = qvariant_cast<QPixmap>(mimeData->imageData());
        if (pix.isNull()) {
            const QImage image = qvariant_cast<QImage>(mimeData->imageData());
            if (!image.isNull()) {
                pix = QPixmap::fromImage(image);
            }
        }

        if (pix.isNull()) {
            return;
        }

        data = pix;
        signature = clipboardSignatureForPixmap(pix);
    } else if (mimeData->hasText()) {
        const QString text = normalizeTextForClipboardSignature(mimeData->text());
        if (text.isEmpty()) {
            return;
        }

        data = text;
        signature = QStringLiteral("txt:%1").arg(text);
    } else {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!signature.isEmpty() && signature == lastClipboardSignature &&
        lastClipboardChangeAtMs > 0 && nowMs >= lastClipboardChangeAtMs &&
        (nowMs - lastClipboardChangeAtMs) <= kClipboardDuplicateWindowMs) {
        return;
    }

    addClipboardItem(data);
    if (!signature.isEmpty()) {
        lastClipboardSignature = signature;
        lastClipboardChangeAtMs = nowMs;
    }
}

void MainWindow::addClipboardItem(const QVariant &data)
{
    const int viewportWidth =
        (historyList && historyList->viewport()) ? historyList->viewport()->width() : 0;
    const int listWidth = historyList ? historyList->width() : normalWidth;
    const int resolvedListWidth = qMax(160, qMax(viewportWidth, listWidth));
    const int itemWidth = qMax(160, resolvedListWidth - 12);
    const int contentWidth = qMax(120, itemWidth - 16);

    QListWidgetItem *item = new QListWidgetItem();
    item->setData(Qt::UserRole, data); 
    historyList->insertItem(0, item);

    QWidget *widget = new QWidget(historyList);
    widget->setObjectName("historyItemWidget");
    QVBoxLayout *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm");
    QLabel *timeLabel = new QLabel(timeStr);
    timeLabel->setObjectName("historyTimeLabel");
    timeLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    timeLabel->setAlignment(Qt::AlignCenter);
    const AppSettings::ThemePalette palette =
        isLightTheme() ? AppSettings::getLightThemePalette() : AppSettings::getDarkThemePalette();
    timeLabel->setStyleSheet(
        QStringLiteral("background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 3px 8px;"
                       "font-family: 'Microsoft YaHei'; font-weight: 700; font-size: 11px;")
            .arg(palette.background, palette.text, palette.buttonHover));
    layout->addWidget(timeLabel);

    bool isTextItem = false;
    int textBlockHeight = 0;

    if (data.typeId() == QMetaType::QPixmap || data.typeId() == QMetaType::Type::QPixmap) {
        layout->setContentsMargins(3, 3, 3, 3);
        layout->setSpacing(2);

        QPixmap pix = data.value<QPixmap>();
        QLabel *imgLabel = new QLabel();
        imgLabel->setAlignment(Qt::AlignCenter);
        const int previewMaxWidth = qMax(120, itemWidth - 10);
        const int previewMaxHeight = 260;
        const QPixmap preview =
            pix.scaled(previewMaxWidth, previewMaxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        imgLabel->setPixmap(preview);
        layout->addWidget(imgLabel);
    } else {
        isTextItem = true;
        const int previewWidth = contentWidth;
        QLabel *textLabel = new QLabel();
        textLabel->setObjectName("historyTextLabel");
        textLabel->setWordWrap(true);
        textLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        textLabel->setFixedWidth(contentWidth);
        textLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        QFont textFont(QStringLiteral("\u5B8B\u4F53"), 12);
        textFont.setStyleStrategy(QFont::PreferAntialias);
        textLabel->setFont(textFont);
        const QString textColor = isLightTheme() ? QStringLiteral("#2D2D2D") : QStringLiteral("#E8E8E8");
        textLabel->setStyleSheet(QStringLiteral("color: %1;").arg(textColor));
        textLabel->setText(
            buildMultilineTextPreview(data.toString(), textLabel->fontMetrics(), textLabel->font(), previewWidth, 4));
        const int lineHeight = textLabel->fontMetrics().lineSpacing();
        const int maxTextHeight = (lineHeight * 4) + 6;
        textBlockHeight = maxTextHeight;
        textLabel->setMinimumHeight(lineHeight);
        textLabel->setFixedHeight(maxTextHeight);
        layout->addWidget(textLabel);
    }

    int finalItemHeight = qMax(72, layout->sizeHint().height() + 4);
    if (isTextItem) {
        finalItemHeight = qMax(finalItemHeight, textBlockHeight + 34);
    }

    item->setSizeHint(QSize(itemWidth, finalItemHeight));
    historyList->setItemWidget(item, widget);
    enforceHistoryLimit();

    // Two-phase relayout: immediate queued pass + delayed pass after geometry settles.
    QTimer::singleShot(0, this, [this]() { relayoutHistoryItems(); });
    QTimer::singleShot(AppSettings::kSidebarAnimationDurationMs + 30,
                       this,
                       [this]() { relayoutHistoryItems(); });
}

void MainWindow::enterEvent(QEnterEvent *event) {
    if (leaveDockTimer) {
        leaveDockTimer->stop();
    }

    if (trayRevealHoldActive && !isDocked && geometry().contains(QCursor::pos())) {
        trayRevealHoldActive = false;
        if (trayRevealHoldTimer) {
            trayRevealHoldTimer->stop();
        }
    }

    if (isDocked && !shouldSuppressSidebar() && !sidebarPinned && hoverRevealTimer) {
        hoverRevealTimer->start(AppSettings::kSidebarExpandDebounceMs);
    }
    QMainWindow::enterEvent(event);
}

void MainWindow::leaveEvent(QEvent *event)
{
    if (hoverRevealTimer) {
        hoverRevealTimer->stop();
    }

    if (!shouldSuppressSidebar() && !isDocked) {
        if (sidebarPinned || trayRevealHoldActive) {
            QMainWindow::leaveEvent(event);
            return;
        }

        if (leaveDockTimer && !geometry().contains(QCursor::pos())) {
            leaveDockTimer->start(AppSettings::kSidebarAutoDockDelayMs);
        }
    }
    QMainWindow::leaveEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event && event->button() == Qt::LeftButton && dockDragEnabled && isDocked &&
        dockTriggerRect().contains(event->pos())) {
        dockDragging = true;
        dockDragOffsetY = event->pos().y();

        if (hoverRevealTimer) {
            hoverRevealTimer->stop();
        }
        if (leaveDockTimer) {
            leaveDockTimer->stop();
        }

        event->accept();
        return;
    }

    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (dockDragging && event) {
        const QRect screenGeometry = resolveAvailableGeometry(this);
        if (screenGeometry.isValid()) {
            QRect rect = geometry();
            rect.setX(screenGeometry.right() - rightMargin - rect.width() + 1);

            const int minTop = screenGeometry.top() + kSidebarVerticalMargin;
            const int maxTop = screenGeometry.bottom() - rect.height() + 1 - kSidebarVerticalMargin;
            const int targetTop = event->globalPosition().toPoint().y() - dockDragOffsetY;
            rect.moveTop(qBound(minTop, targetTop, maxTop));
            setGeometry(rect);
            dockStripTop = rect.top();
        }

        event->accept();
        return;
    }

    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (dockDragging && event && event->button() == Qt::LeftButton) {
        dockDragging = false;
        saveDockStripTop(dockStripTop);
        event->accept();
        return;
    }

    QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::checkEdgeDocking() {
    if (sidebarPinned || trayRevealHoldActive || shouldSuppressSidebar() || !animation ||
        animation->state() == QAbstractAnimation::Running) {
        return;
    }

    if (!isDocked && !geometry().contains(QCursor::pos()) && leaveDockTimer &&
        !leaveDockTimer->isActive()) {
        leaveDockTimer->start(AppSettings::kSidebarAutoDockDelayMs);
    }
}

void MainWindow::onSettingsUpdated()
{
    {
        const QSettings settings = AppSettings::createSettings();
        dockDragEnabled = settings.value(AppSettings::kDockStripAllowDrag,
                                         AppSettings::kDefaultDockStripAllowDrag)
                              .toBool();
    }
    if (!dockDragEnabled) {
        dockDragging = false;
    }

    enforceHistoryLimit();
    if (isDocked) {
        dockSidebar(false);
    } else if (centralWidget) {
        const AppSettings::ThemePalette palette =
            isLightTheme() ? AppSettings::getLightThemePalette()
                           : AppSettings::getDarkThemePalette();
        centralWidget->setStyleSheet(expandedPanelStyleSheet(palette));
    }
}

void MainWindow::clearClipboardHistory()
{
    if (historyList) {
        historyList->clear();
    }
}

int MainWindow::currentHistoryLimit() const
{
    QSettings settings = AppSettings::createSettings();
    const int limit =
        settings.value(AppSettings::kHistoryMaxItems, AppSettings::kDefaultHistoryMaxItems).toInt();
    return AppSettings::normalizeHistoryMaxItems(limit);
}

void MainWindow::enforceHistoryLimit()
{
    if (!historyList) {
        return;
    }

    const int limit = currentHistoryLimit();
    while (historyList->count() > limit) {
        delete historyList->takeItem(historyList->count() - 1);
    }
}

void MainWindow::onSettingsDialogVisibilityChanged(bool visible)
{
    settingsDialogVisible = visible;
    applySuppressionState();
}

void MainWindow::onRegionCaptureStateChanged(bool active)
{
    regionCaptureActive = active;
    if (active) {
        const QSettings settings = AppSettings::createSettings();
        regionCaptureShouldHideSidebar =
            settings.value(AppSettings::kHideSidebar, AppSettings::kDefaultHideSidebar).toBool();
    } else {
        regionCaptureShouldHideSidebar = false;
    }
    applySuppressionState();
}

void MainWindow::onPreviewDialogStateChanged(bool visible)
{
    previewDialogVisible = visible;
    applySuppressionState();
}

void MainWindow::onOpenSettings()
{
    if (tray) {
        tray->openSettingsDialog();
    }

    resetTopActionButtonVisual(githubButton);
    resetTopActionButtonVisual(themeToggleButton);
    resetTopActionButtonVisual(pinButton);
    resetTopActionButtonVisual(settingsButton);
    resetTopActionButtonVisual(exitButton);
}

void MainWindow::onOpenGitHub()
{
    QDesktopServices::openUrl(QUrl(AppSettings::kGitHubUrl));
}

void MainWindow::onTogglePinned(bool checked)
{
    sidebarPinned = checked;

    QSettings settings = AppSettings::createSettings();
    settings.setValue(AppSettings::kSidebarPinned, sidebarPinned);

    updatePinnedUi();

    if (sidebarPinned) {
        trayRevealHoldActive = false;
        if (trayRevealHoldTimer) {
            trayRevealHoldTimer->stop();
        }
        if (leaveDockTimer) {
            leaveDockTimer->stop();
        }
        if (hoverRevealTimer) {
            hoverRevealTimer->stop();
        }

        if (!shouldSuppressSidebar() && isDocked) {
            expandSidebar(true);
        }
        return;
    }

    if (!shouldSuppressSidebar() && !isDocked && !geometry().contains(QCursor::pos()) &&
        leaveDockTimer) {
        leaveDockTimer->start(AppSettings::kSidebarAutoDockDelayMs);
    }
}

void MainWindow::onRequestExit()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("\u9000\u51fa\u786e\u8ba4"));
    dialog.setModal(true);
    dialog.setMinimumWidth(320);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto *label = new QLabel(QStringLiteral("\u786e\u8ba4\u9000\u51fa\u4fa7\u5f71\u5417\uff1f"), &dialog);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(label);

    auto *buttonBox = new QDialogButtonBox(&dialog);
    auto *confirmButton = buttonBox->addButton(QStringLiteral("\u786e\u8ba4"), QDialogButtonBox::AcceptRole);
    auto *cancelButton = buttonBox->addButton(QStringLiteral("\u53d6\u6d88"), QDialogButtonBox::RejectRole);
    Q_UNUSED(confirmButton);
    Q_UNUSED(cancelButton);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QApplication::quit();
    }
}

void MainWindow::updatePinnedUi()
{
    if (!pinButton) {
        return;
    }

    const QSignalBlocker blocker(pinButton);
    pinButton->setChecked(sidebarPinned);
    pinButton->setToolTip(sidebarPinned ? QStringLiteral("\u5df2\u56fa\u5b9a\u4fa7\u8fb9\u680f")
                                        : QStringLiteral("\u56fa\u5b9a\u4fa7\u8fb9\u680f"));
}

void MainWindow::onTrayLeftClicked()
{
    show();
    raise();
    activateWindow();

    if (!shouldSuppressSidebar()) {
        revealSidebarWithHold();
    }
}

void MainWindow::revealSidebarWithHold()
{
    const QRect screenGeometry = resolveAvailableGeometry(this);
    if (!screenGeometry.isValid()) {
        return;
    }

    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setFixedWidth(normalWidth);
    const int expandedHeight = qMax(320, screenGeometry.height() - 100);
    setFixedHeight(expandedHeight);

    QRect rect = geometry();
    const int maxWidth = qMax(220, screenGeometry.width() - (rightMargin * 2));
    rect.setWidth(qMin(width(), maxWidth));
    rect.setHeight(expandedHeight);
    rect.setX(screenGeometry.right() - rightMargin - rect.width() + 1);
    rect.moveTop(sidebarExpandedTop(screenGeometry, rect.height()));

    if (animation) {
        animation->stop();
    }
    if (hoverRevealTimer) {
        hoverRevealTimer->stop();
    }
    if (leaveDockTimer) {
        leaveDockTimer->stop();
    }

    clearMask();
    if (centralWidget) {
        const AppSettings::ThemePalette palette =
            isLightTheme() ? AppSettings::getLightThemePalette()
                           : AppSettings::getDarkThemePalette();
        centralWidget->setStyleSheet(expandedPanelStyleSheet(palette));
    }
    setGeometry(rect);
    isDocked = false;
    setDockContentVisible(true);
    if (centralWidget && centralWidget->graphicsEffect()) {
        centralWidget->graphicsEffect()->setEnabled(true);
    }
    relayoutHistoryItems();
    updateDockMask();

    trayRevealHoldActive = !sidebarPinned;
    if (trayRevealHoldTimer && trayRevealHoldActive) {
        trayRevealHoldTimer->start(AppSettings::kTrayRevealHoldMs);
    } else if (trayRevealHoldTimer) {
        trayRevealHoldTimer->stop();
    }
}

void MainWindow::setDockContentVisible(bool visible)
{
    if (!centralWidget) {
        return;
    }

    const QList<QWidget *> children =
        centralWidget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : children) {
        if (child) {
            child->setVisible(visible);
        }
    }
}

bool MainWindow::shouldSuppressSidebar() const
{
    return settingsDialogVisible || previewDialogVisible ||
           (regionCaptureActive && regionCaptureShouldHideSidebar);
}

QRect MainWindow::dockTriggerRect() const
{
    const int triggerHeight = qMin(dockTriggerHeight, height());
    const int triggerTop = (height() - triggerHeight) / 2;
    return QRect(0, triggerTop, dockTriggerWidth, triggerHeight);
}

void MainWindow::updateDockMask()
{
    if (!isDocked) {
        clearMask();
        return;
    }

    // QRegion mask is binary and causes visible jagged edges on narrow rounded strips.
    // Keep the top-level window unmasked and rely on stylesheet rounded corners instead.
    clearMask();
}

void MainWindow::dockSidebar(bool animated)
{
    Q_UNUSED(animated);

    const QRect screenGeometry = resolveAvailableGeometry(this);
    if (!screenGeometry.isValid()) {
        return;
    }

    const QSettings settings = AppSettings::createSettings();
    const int stripWidth = settings.value(AppSettings::kDockStripWidth, AppSettings::kDefaultDockStripWidth).toInt();
    const int stripHeight = settings.value(AppSettings::kDockStripHeight, AppSettings::kDefaultDockStripHeight).toInt();
    const int stripBorderRadius = settings.value(AppSettings::kDockStripBorderRadius, AppSettings::kDefaultDockStripBorderRadius).toInt();

    dockTriggerWidth = qBound(AppSettings::kMinDockStripWidth, stripWidth, AppSettings::kMaxDockStripWidth);
    dockTriggerHeight = qBound(AppSettings::kMinDockStripHeight, stripHeight, AppSettings::kMaxDockStripHeight);
    const int safeStripRadius = qBound(0, stripBorderRadius, qMin(dockTriggerWidth, dockTriggerHeight) / 2);

    const AppSettings::ThemePalette palette =
        isLightTheme() ? AppSettings::getLightThemePalette() : AppSettings::getDarkThemePalette();
    const QString stripColor = palette.buttonBackground;

    // Apply dock strip style (color and border radius)
    if (centralWidget) {
        centralWidget->setStyleSheet(dockStripStyleSheet(palette, stripColor, safeStripRadius));
    }

    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setFixedWidth(dockTriggerWidth);
    setFixedHeight(dockTriggerHeight);

    QRect targetRect = geometry();
    targetRect.setWidth(width());
    targetRect.setHeight(height());
    targetRect.setX(screenGeometry.right() - rightMargin - targetRect.width() + 1);
    int preferredTop = dockStripTop;
    if (preferredTop < 0) {
        const int centerY = geometry().center().y();
        preferredTop = centerY - (targetRect.height() / 2);
    }
    targetRect.moveTop(preferredTop);

    const int minTop = screenGeometry.top() + 10;
    const int maxTop = screenGeometry.bottom() - targetRect.height() + 1 - 10;
    targetRect.moveTop(qBound(minTop, targetRect.y(), maxTop));

    if (animation) {
        animation->stop();
    }
    setGeometry(targetRect);
    dockStripTop = targetRect.top();
    saveDockStripTop(dockStripTop);
    isDocked = true;
    setDockContentVisible(false);
    if (centralWidget && centralWidget->graphicsEffect()) {
        centralWidget->graphicsEffect()->setEnabled(false);
    }
    updateDockMask();
}

void MainWindow::expandSidebar(bool force)
{
    const bool visuallyDocked = isDocked || (geometry().width() <= dockTriggerWidth + 2);
    if (!visuallyDocked || shouldSuppressSidebar()) {
        return;
    }

    if (!force && animation && animation->state() == QAbstractAnimation::Running) {
        return;
    }

    const QRect screenGeometry = resolveAvailableGeometry(this);
    if (!screenGeometry.isValid()) {
        return;
    }

    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setFixedWidth(normalWidth);
    const int expandedHeight = qMax(320, screenGeometry.height() - 100);
    setFixedHeight(expandedHeight);

    QRect rect = geometry();
    rect.setWidth(width());
    rect.setHeight(expandedHeight);
    rect.setX(screenGeometry.right() - rightMargin - rect.width() + 1);
    rect.moveTop(sidebarExpandedTop(screenGeometry, rect.height()));

    clearMask();
    if (animation) {
        animation->stop();
    }
    if (centralWidget) {
        const AppSettings::ThemePalette palette =
            isLightTheme() ? AppSettings::getLightThemePalette()
                           : AppSettings::getDarkThemePalette();
        centralWidget->setStyleSheet(expandedPanelStyleSheet(palette));
    }

    if (force) {
        setGeometry(rect);
        isDocked = false;
        setDockContentVisible(true);
        if (centralWidget && centralWidget->graphicsEffect()) {
            centralWidget->graphicsEffect()->setEnabled(true);
        }
        relayoutHistoryItems();
        return;
    }

    if (!animation) {
        setGeometry(rect);
        isDocked = false;
        setDockContentVisible(true);
        if (centralWidget && centralWidget->graphicsEffect()) {
            centralWidget->graphicsEffect()->setEnabled(true);
        }
        relayoutHistoryItems();
        return;
    }

    animation->setEasingCurve(QEasingCurve::InOutCubic);
    animation->setStartValue(geometry());
    animation->setEndValue(rect);
    isDocked = false;
    setDockContentVisible(false);
    if (centralWidget && centralWidget->graphicsEffect()) {
        centralWidget->graphicsEffect()->setEnabled(true);
    }
    animation->start();
}

void MainWindow::applySuppressionState()
{
    if (shouldSuppressSidebar()) {
        if (hoverRevealTimer) {
            hoverRevealTimer->stop();
        }
        if (leaveDockTimer) {
            leaveDockTimer->stop();
        }
        trayRevealHoldActive = false;
        if (trayRevealHoldTimer) {
            trayRevealHoldTimer->stop();
        }
        dockSidebar(false);
        return;
    }

    if (sidebarPinned) {
        trayRevealHoldActive = false;
        if (trayRevealHoldTimer) {
            trayRevealHoldTimer->stop();
        }
        if (isDocked) {
            expandSidebar(true);
        }
        return;
    }

    updateDockMask();
}

void MainWindow::onToggleTheme()
{
    themeMode = isLightTheme() ? QStringLiteral("dark") : QStringLiteral("light");

    QSettings settings = AppSettings::createSettings();
    settings.setValue(AppSettings::kThemeMode, themeMode);
    settings.sync();

    applyTheme();
}

void MainWindow::applyTheme()
{
    themeMode = normalizeThemeModeValue(themeMode);
    updateTopBarIcons();
    updateThemeToggleUi();
    refreshHistoryItemsTextColor();

    if (!centralWidget) {
        return;
    }

    const AppSettings::ThemePalette palette =
        isLightTheme() ? AppSettings::getLightThemePalette() : AppSettings::getDarkThemePalette();

    if (isDocked) {
        const QSettings settings = AppSettings::createSettings();
        const int stripBorderRadius = settings.value(AppSettings::kDockStripBorderRadius,
                                                     AppSettings::kDefaultDockStripBorderRadius)
                                        .toInt();
        const int safeStripRadius = qBound(0, stripBorderRadius, qMin(width(), height()) / 2);
        const QString stripColor = palette.buttonBackground;
        centralWidget->setStyleSheet(dockStripStyleSheet(palette, stripColor, safeStripRadius));
        updateDockMask();
    } else {
        centralWidget->setStyleSheet(expandedPanelStyleSheet(palette));
    }

    relayoutHistoryItems();
}

void MainWindow::updateTopBarIcons()
{
    const QColor iconColor = isLightTheme() ? QColor(QStringLiteral("#202020"))
                                             : QColor(QStringLiteral("#F2F2F2"));

    if (githubButton) {
        githubButton->setIcon(loadThemeAwareSvgIcon(QStringLiteral(":/icons/github.svg"), iconColor));
    }

    if (pinButton) {
        pinButton->setIcon(loadThemeAwareSvgIcon(QStringLiteral(":/icons/pin.svg"), iconColor));
    }

    if (settingsButton) {
        settingsButton->setIcon(loadThemeAwareSvgIcon(QStringLiteral(":/icons/settings.svg"), iconColor));
    }

    if (exitButton) {
        exitButton->setIcon(loadThemeAwareSvgIcon(QStringLiteral(":/icons/shutdown.svg"), iconColor));
    }
}

bool MainWindow::isLightTheme() const
{
    return currentThemeMode() == QStringLiteral("light");
}

void MainWindow::refreshHistoryItemsTextColor()
{
    if (!historyList || !historyList->viewport()) {
        return;
    }

    const AppSettings::ThemePalette palette =
        isLightTheme() ? AppSettings::getLightThemePalette() : AppSettings::getDarkThemePalette();
    const QString textColor = isLightTheme() ? QStringLiteral("#2D2D2D") : QStringLiteral("#E8E8E8");
    const QString timeLabelStyle =
        QStringLiteral("background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 3px 8px;"
                       "font-family: 'Microsoft YaHei'; font-weight: 700; font-size: 11px;")
            .arg(palette.background, palette.text, palette.buttonHover);

    const int viewportWidth = historyList->viewport()->width();
    const int listWidth = historyList->width();

    for (int i = 0; i < historyList->count(); ++i) {
        QListWidgetItem *item = historyList->item(i);
        if (!item) {
            continue;
        }

        QWidget *widget = historyList->itemWidget(item);
        if (!widget) {
            continue;
        }

        QLabel *timeLabel = widget->findChild<QLabel *>(QStringLiteral("historyTimeLabel"));
        if (timeLabel) {
            timeLabel->setStyleSheet(timeLabelStyle);
        }

        QLabel *textLabel = widget->findChild<QLabel *>(QStringLiteral("historyTextLabel"));
        if (textLabel) {
            QFont textFont(QStringLiteral("\u5B8B\u4F53"), 12);
            textFont.setStyleStrategy(QFont::PreferAntialias);
            textLabel->setFont(textFont);
            textLabel->setStyleSheet(QStringLiteral("color: %1;").arg(textColor));
        }
    }
    historyList->viewport()->update();
}

void MainWindow::relayoutHistoryItems()
{
    if (!historyList || !historyList->viewport()) {
        return;
    }

    const int viewportWidth = historyList->viewport()->width();
    const int listWidth = historyList->width();
    const int resolvedListWidth = qMax(160, qMax(viewportWidth, listWidth));
    const int itemWidth = qMax(160, resolvedListWidth - 12);
    const int contentWidth = qMax(120, itemWidth - 16);

    for (int i = 0; i < historyList->count(); ++i) {
        QListWidgetItem *item = historyList->item(i);
        if (!item) {
            continue;
        }

        QWidget *widget = historyList->itemWidget(item);
        if (!widget) {
            continue;
        }

        QLabel *textLabel = widget->findChild<QLabel *>(QStringLiteral("historyTextLabel"));
        if (textLabel) {
            textLabel->setFixedWidth(contentWidth);
            const QString textData = item->data(Qt::UserRole).toString();
            textLabel->setText(buildMultilineTextPreview(textData,
                                                         textLabel->fontMetrics(),
                                                         textLabel->font(),
                                                         contentWidth,
                                                         4));

            const int lineHeight = textLabel->fontMetrics().lineSpacing();
            const int maxTextHeight = (lineHeight * 4) + 6;
            textLabel->setFixedHeight(maxTextHeight);

            const int finalItemHeight = qMax(72, qMax(widget->sizeHint().height() + 4, maxTextHeight + 34));
            item->setSizeHint(QSize(itemWidth, finalItemHeight));
        } else {
            item->setSizeHint(QSize(itemWidth, qMax(72, widget->sizeHint().height() + 4)));
        }
    }

    historyList->doItemsLayout();
    historyList->viewport()->update();
}

void MainWindow::saveDockStripTop(int top)
{
    if (top < 0) {
        return;
    }

    QSettings settings = AppSettings::createSettings();
    settings.setValue(AppSettings::kDockStripTop, top);
    settings.sync();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    relayoutHistoryItems();
}

void MainWindow::updateThemeToggleUi()
{
    if (!themeToggleButton) {
        return;
    }

    if (isLightTheme()) {
        const QIcon icon = loadThemeAwareSvgIcon(QStringLiteral(":/icons/theme_light.svg"),
                                                 QColor(QStringLiteral("#202020")));
        themeToggleButton->setIcon(icon);
        themeToggleButton->setText(icon.isNull() ? QStringLiteral("\u25CB") : QString());
        themeToggleButton->setToolTip(
            QStringLiteral("\u5f53\u524d\uff1a\u6d45\u8272\u6a21\u5f0f\uff08\u70b9\u51fb\u5207\u6362\u6df1\u8272\uff09"));
    } else {
        const QIcon icon = loadThemeAwareSvgIcon(QStringLiteral(":/icons/theme_dark.svg"),
                                                 QColor(QStringLiteral("#F2F2F2")));
        themeToggleButton->setIcon(icon);
        themeToggleButton->setText(icon.isNull() ? QStringLiteral("\u263E") : QString());
        themeToggleButton->setToolTip(
            QStringLiteral("\u5f53\u524d\uff1a\u6df1\u8272\u6a21\u5f0f\uff08\u70b9\u51fb\u5207\u6362\u6d45\u8272\uff09"));
    }
}

QString MainWindow::currentThemeMode() const
{
    return normalizeThemeModeValue(themeMode);
}