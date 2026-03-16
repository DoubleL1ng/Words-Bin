#pragma once
#include <QMainWindow>
#include <QTimer>
#include <QPropertyAnimation>
#include <QEnterEvent>
#include <QVBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>

class TrayIcon;
class CaptureTool;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private slots:
    void checkEdgeDocking();
    void onClipboardChanged();
    void addClipboardItem(const QVariant &data);
    
    // 新增交互功能
    void onItemClicked(QListWidgetItem *item); // 左键点击复制
    void onCustomContextMenu(const QPoint &pos); // 右键菜单弹出

private:
    void setupUI();
    
    TrayIcon *tray; // 托盘指针
    CaptureTool *captureTool;
    
    QWidget *centralWidget;
    QVBoxLayout *mainLayout;
    QListWidget *historyList;
    
    QTimer *edgeTimer;
    QPropertyAnimation *animation;
    bool isDocked; 
    int normalWidth; 
};