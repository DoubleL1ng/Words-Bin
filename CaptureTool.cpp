#include "CaptureTool.h"
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QWidget>
#include <QMenu>
#include <QAction>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_EXTENDED_FRAME_BOUNDS
#define DWMWA_EXTENDED_FRAME_BOUNDS 9
#endif
#endif

// --- 1. 截图预览对话框 ---
class CapturePreviewDialog : public QDialog {
public:
    explicit CapturePreviewDialog(const QPixmap &pixmap, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("\u622a\u56fe\u9884\u89c8"));
        setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
        setModal(true);
        resize(760, 520);

        QVBoxLayout *layout = new QVBoxLayout(this);
        QLabel *previewLabel = new QLabel(this);
        previewLabel->setAlignment(Qt::AlignCenter);
        previewLabel->setPixmap(pixmap.scaled(720, 420, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        layout->addWidget(previewLabel, 1);

        QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("\u5b8c\u6210"));
        buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("\u53d6\u6d88"));
        layout->addWidget(buttonBox);

        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
};

// --- 2. 长截图预览挂件 ---
class LongshotPreview : public QWidget {
public:
    explicit LongshotPreview(CaptureTool *tool) : QWidget(nullptr), tool(tool) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setFocusPolicy(Qt::StrongFocus);
        resize(180, 240);
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) move(screen->geometry().right() - 200, 40);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setBrush(QColor(255, 255, 255, 220));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect(), 12, 12);
        p.setPen(QColor(40, 40, 40));
        p.drawText(QRect(12, 12, width() - 24, 20), Qt::AlignCenter, QStringLiteral("\u957f\u622a\u56fe"));

        if (!tool->longshotSegments.isEmpty()) {
            int w = width() - 24, y = 38;
            for (const QImage &img : tool->longshotSegments) {
                if (img.width() <= 0) continue;
                QPixmap pm = QPixmap::fromImage(img).scaled(w, img.height() * w / img.width(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p.drawPixmap(12, y, pm);
                y += pm.height() + 4;
                if (y > height() - 48) break;
            }
        }
        QRect btnRect(width() - 70, height() - 38, 60, 28);
        p.setBrush(QColor(0, 122, 255));
        p.drawRoundedRect(btnRect, 8, 8);
        p.setPen(Qt::white);
        p.drawText(btnRect, Qt::AlignCenter, QStringLiteral("\u5b8c\u6210"));
    }

    void mousePressEvent(QMouseEvent *e) override {
        QRect btnRect(width() - 70, height() - 38, 60, 28);
        if (btnRect.contains(e->pos())) { tool->finishLongshot(); close(); }
    }
private:
    CaptureTool *tool;
};

// --- 3. 窗口选择器 ---
class WindowSelector : public QWidget {
public:
    explicit WindowSelector(CaptureTool *tool) : QWidget(nullptr), tool(tool) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowState(Qt::WindowFullScreen);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setBrush(QColor(0, 0, 0, 50));
        p.drawRect(rect());
        if (highlightRect.isValid()) {
            p.setPen(QPen(Qt::red, 3));
            p.setBrush(Qt::NoBrush);
            p.drawRect(highlightRect);
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override {
#ifdef Q_OS_WIN
        POINT pt = { static_cast<LONG>(e->globalPosition().x()), static_cast<LONG>(e->globalPosition().y()) };
        HWND hwnd = WindowFromPoint(pt);
        if (hwnd) {
            hwnd = GetAncestor(hwnd, GA_ROOT);
            RECT rc;
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc)))) {
                highlightRect = QRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
                update();
            }
        }
#endif
    }

    void mousePressEvent(QMouseEvent *) override {
        if (highlightRect.isValid()) {
            this->hide();
            QGuiApplication::processEvents();
            tool->captureWindowRegion(highlightRect);
            this->close();
        }
    }

private:
    CaptureTool *tool;
    QRect highlightRect;
};

// --- 4. 区域选择器 ---
class SelectionOverlay : public QWidget {
public:
    explicit SelectionOverlay(CaptureTool *tool) : QWidget(nullptr), tool(tool) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowState(Qt::WindowFullScreen);
        setCursor(Qt::CrossCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setBrush(QColor(0, 0, 0, 80));
        p.drawRect(rect());
        if (tool->selecting) {
            p.setPen(QPen(Qt::blue, 2));
            p.drawRect(QRect(tool->selectionStart, tool->selectionEnd).normalized());
        }
    }
    void mousePressEvent(QMouseEvent *e) {
        tool->selecting = true;
        tool->selectionStart = tool->selectionEnd = e->globalPosition().toPoint();
    }
    void mouseMoveEvent(QMouseEvent *e) {
        if (tool->selecting) { tool->selectionEnd = e->globalPosition().toPoint(); update(); }
    }
    void mouseReleaseEvent(QMouseEvent *) {
        this->hide();
        QGuiApplication::processEvents();
        tool->finishSelection();
    }
private:
    CaptureTool *tool;
};

// --- 5. CaptureTool 成员实现 ---
CaptureTool::CaptureTool(QObject *parent) : QObject(parent), overlay(nullptr), selecting(false), longshotPreview(nullptr), longshotMode(false) {}

void CaptureTool::startCapture() { captureWindow(); }

void CaptureTool::captureFullScreen() {
    QScreen *s = QGuiApplication::primaryScreen();
    if (s) confirmAndEmit(s->grabWindow(0));
}

void CaptureTool::captureWindow() { (new WindowSelector(this))->show(); }

void CaptureTool::captureRegion() {
    if (overlay) overlay->close();
    overlay = new SelectionOverlay(this);
    overlay->show();
}

void CaptureTool::captureWindowRegion(const QRect &rect) {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    if (longshotMode) {
        longshotRect = rect;
        QImage img = screen->grabWindow(0, rect.x(), rect.y(), rect.width(), rect.height()).toImage();
        if (!img.isNull()) { longshotSegments.append(img); updateLongshotPreview(); }
    } else {
        confirmAndEmit(screen->grabWindow(0, rect.x(), rect.y(), rect.width(), rect.height()));
    }
}

void CaptureTool::finishSelection() {
    QRect selRect = QRect(selectionStart, selectionEnd).normalized();
    if (overlay) { overlay->close(); overlay = nullptr; }
    selecting = false;
    if (selRect.width() < 5 || selRect.height() < 5) return;
    QScreen *s = QGuiApplication::primaryScreen();
    if (s) confirmAndEmit(s->grabWindow(0, selRect.x(), selRect.y(), selRect.width(), selRect.height()));
}

void CaptureTool::captureLongScreenshot() {
    longshotMode = true; longshotSegments.clear();
    WindowSelector *selector = new WindowSelector(this);
    connect(selector, &QWidget::destroyed, this, [this]() {
        // 关键修复：使用 CaptureTool 成员变量 longshotRect
        if (longshotMode && longshotRect.isValid()) showLongshotPreview();
    });
    selector->show();
}

void CaptureTool::showLongshotPreview() {
    longshotPreview = new LongshotPreview(this);
    longshotPreview->show();
    captureWindowRegion(longshotRect);
}

void CaptureTool::finishLongshot() {
    if (longshotSegments.isEmpty()) return;
    int w = longshotSegments.first().width(), h = 0;
    for (const QImage &img : longshotSegments) h += img.height();
    QImage res(w, h, QImage::Format_ARGB32);
    QPainter p(&res);
    int currY = 0;
    for (const QImage &img : longshotSegments) { p.drawImage(0, currY, img); currY += img.height(); }
    p.end();
    confirmAndEmit(QPixmap::fromImage(res));
    clearLongshot();
}

void CaptureTool::clearLongshot() { longshotSegments.clear(); longshotMode = false; }

void CaptureTool::confirmAndEmit(const QPixmap &pixmap) {
    if (pixmap.isNull()) return;
    CapturePreviewDialog dlg(pixmap);
    if (dlg.exec() == QDialog::Accepted) emit screenshotTaken(pixmap);
}

void CaptureTool::updateLongshotPreview() { if (longshotPreview) longshotPreview->update(); }

bool CaptureTool::eventFilter(QObject *obj, QEvent *event) { return QObject::eventFilter(obj, event); }