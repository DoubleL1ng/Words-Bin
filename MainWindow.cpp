#include "MainWindow.h"
#include "TrayIcon.h"
#include "CaptureTool.h"
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QClipboard>
#include <QMimeData>
#include <QDateTime>
#include <QIcon>
#include <QMessageBox>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), isDocked(false), normalWidth(280)
{
    captureTool = new CaptureTool(this);
    // 关键修复：必须初始化 TrayIcon 才能显示图标！
    tray = new TrayIcon(this); 
    
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    
    QScreen *screen = QApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->geometry();
        setGeometry(screenGeometry.width() - normalWidth, 50, normalWidth, screenGeometry.height() - 100);
    }

    setupUI();

    animation = new QPropertyAnimation(this, "geometry", this);
    animation->setDuration(200);
    
    edgeTimer = new QTimer(this);
    connect(edgeTimer, &QTimer::timeout, this, &MainWindow::checkEdgeDocking);
    edgeTimer->start(500);

    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::onClipboardChanged);
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI()
{
    centralWidget = new QWidget(this);
    centralWidget->setObjectName("centralWidget");
    centralWidget->setStyleSheet(
        "#centralWidget { background-color: #2D2D2D; border-left: 2px solid #3D3D3D; border-top-left-radius: 10px; border-bottom-left-radius: 10px; }"
        "QPushButton { background-color: #3D3D3D; color: white; border: none; padding: 12px; border-radius: 5px; text-align: left; }"
        "QPushButton:hover { background-color: #007AFF; }"
        "QListWidget { background-color: transparent; border: none; color: #CCCCCC; outline: none; }"
        "QListWidget::item { background-color: #383838; margin-bottom: 5px; border-radius: 5px; padding: 10px; }"
        "QListWidget::item:selected { background-color: #4A4A4A; border: 1px solid #007AFF; }"
    );

    mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(15, 20, 15, 20);

    // 按钮添加
    auto addBtn = [&](const QString &text, auto func) {
        QPushButton *btn = new QPushButton(text, this);
        connect(btn, &QPushButton::clicked, captureTool, func);
        mainLayout->addWidget(btn);
    };

    addBtn(QStringLiteral("\u533a\u57df\u622a\u56fe"), &CaptureTool::captureRegion);
    addBtn(QStringLiteral("\u7a97\u53e3\u622a\u56fe"), &CaptureTool::captureWindow);
    addBtn(QStringLiteral("\u5168\u5c4f\u622a\u56fe"), &CaptureTool::captureFullScreen);

    mainLayout->addSpacing(10);
    mainLayout->addWidget(new QLabel(QStringLiteral("\u526a\u8d34\u677f\u5386\u53f2"), this));

    historyList = new QListWidget(this);
    historyList->setIconSize(QSize(200, 120));
    historyList->setContextMenuPolicy(Qt::CustomContextMenu); // 启用右键菜单
    mainLayout->addWidget(historyList);

    // 连接列表交互
    connect(historyList, &QListWidget::itemClicked, this, &MainWindow::onItemClicked);
    connect(historyList, &QListWidget::customContextMenuRequested, this, &MainWindow::onCustomContextMenu);

    setCentralWidget(centralWidget);

    connect(captureTool, &CaptureTool::screenshotTaken, this, [this](const QPixmap &pixmap){
        QApplication::clipboard()->setPixmap(pixmap);
    });
}

void MainWindow::onItemClicked(QListWidgetItem *item)
{
    // 左键点击：重新存入剪贴板
    QVariant data = item->data(Qt::UserRole);
    if (data.canConvert<QPixmap>()) {
        QApplication::clipboard()->setPixmap(data.value<QPixmap>());
    } else {
        QApplication::clipboard()->setText(item->text().split('\n').first());
    }
    // 提示用户已复制（可选）
}

void MainWindow::onCustomContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = historyList->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QAction *delAction = menu.addAction(QStringLiteral("\u5220\u9664"));
    
    QAction *selected = menu.exec(historyList->mapToGlobal(pos));
    if (selected == delAction) {
        auto res = QMessageBox::question(this, QStringLiteral("\u786e\u8ba4"), QStringLiteral("\u786e\u5b9a\u8981\u5220\u9664\u8fd9\u6761\u8bb0\u5f55\u5417\uff1f"));
        if (res == QMessageBox::Yes) {
            delete historyList->takeItem(historyList->row(item));
        }
    }
}

void MainWindow::onClipboardChanged()
{
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData) return;
    if (mimeData->hasImage()) {
        QPixmap pix = qvariant_cast<QPixmap>(mimeData->imageData());
        addClipboardItem(pix);
    } else if (mimeData->hasText()) {
        QString text = mimeData->text().trimmed();
        if (!text.isEmpty()) addClipboardItem(text);
    }
}

void MainWindow::addClipboardItem(const QVariant &data)
{
    QListWidgetItem *item = new QListWidgetItem();
    item->setData(Qt::UserRole, data); // 存储原始数据用于点击复制
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");

    if (data.typeId() == QMetaType::QPixmap) {
        QPixmap pix = data.value<QPixmap>();
        item->setIcon(QIcon(pix));
        item->setText(QStringLiteral("[%1] \u56fe\u7247").arg(timeStr));
    } else {
        QString text = data.toString();
        item->setText(QStringLiteral("%1\n(%2)").arg(text.left(30)).arg(timeStr));
    }
    historyList->insertItem(0, item);
}

void MainWindow::enterEvent(QEnterEvent *event) {
    if (isDocked) {
        QRect rect = geometry();
        animation->setStartValue(rect);
        rect.setX(QApplication::primaryScreen()->geometry().width() - normalWidth);
        animation->setEndValue(rect);
        animation->start();
        isDocked = false;
    }
    QMainWindow::enterEvent(event);
}

void MainWindow::leaveEvent(QEvent *event) { QMainWindow::leaveEvent(event); }

void MainWindow::checkEdgeDocking() {
    if (!geometry().contains(QCursor::pos()) && !isDocked) {
        QRect rect = geometry();
        int hideX = QApplication::primaryScreen()->geometry().width() - 5; 
        animation->setStartValue(rect);
        rect.setX(hideX);
        animation->setEndValue(rect);
        animation->start();
        isDocked = true;
    }
}