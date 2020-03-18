#include "MainWindow.h"
#include "ui_MainWindow.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
    ui(new Ui::MainWindow),
    urlProtocolHandler("dde4qt")
{
    urlProtocolHandler.install();
    ui->setupUi(this);
    connect(&urlProtocolHandler, &win32::QUrlProtocolHandler::activate, this, &MainWindow::onProtocolActivate);
}

MainWindow::~MainWindow()
{
  urlProtocolHandler.uninstall();
  delete ui;
}

void MainWindow::onProtocolActivate(const QUrl& url) {
    QColor color(url.path());
    setPalette(QPalette(color));
}
