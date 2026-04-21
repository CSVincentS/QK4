#include <QApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QSslSocket>
#include <QSysInfo>
#include <rhi/qrhi.h>
#ifdef Q_OS_MACOS
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformintegration.h>
#endif
#include "mainwindow.h"
#include "ui/styling/k4styles.h"

Q_LOGGING_CATEGORY(qk4App, "qk4.app")

// Filter out known benign Qt warnings on macOS
// QSocketNotifier::Exception is not supported by kqueue (macOS's event system)
// This warning comes from Qt's internal socket code and doesn't affect functionality
static QtMessageHandler originalHandler = nullptr;
void messageFilter(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
#ifdef Q_OS_MACOS
    if (msg.contains("QSocketNotifier::Exception is not supported")) {
        return; // Suppress this known benign warning
    }
#endif
    if (originalHandler) {
        originalHandler(type, context, msg);
    }
}

// Load embedded fonts and set application defaults
void setupFonts() {
    // Load Inter font family (screen-optimized sans-serif for all UI)
    int interRegular = QFontDatabase::addApplicationFont(":/fonts/Inter-Regular.ttf");
    int interMedium = QFontDatabase::addApplicationFont(":/fonts/Inter-Medium.ttf");
    int interSemiBold = QFontDatabase::addApplicationFont(":/fonts/Inter-SemiBold.ttf");
    int interBold = QFontDatabase::addApplicationFont(":/fonts/Inter-Bold.ttf");

    // Verify fonts loaded (only warn on failure)
    if (interRegular < 0 || interMedium < 0) {
        qCWarning(qk4App) << "Failed to load Inter font - using system default";
    }

    // Set Inter Medium as the default application font (crisper than Regular)
    // Use setPixelSize() for consistent sizing across macOS (72 PPI) and Windows (96 PPI)
    QFont defaultFont(K4Styles::Fonts::Primary);
    defaultFont.setPixelSize(K4Styles::Dimensions::FontSizeLarge);
    defaultFont.setWeight(QFont::Medium);
    defaultFont.setHintingPreference(QFont::PreferFullHinting);
    defaultFont.setStyleStrategy(QFont::PreferAntialias);
    QApplication::setFont(defaultFont);
}

int main(int argc, char *argv[]) {
    // Install message filter to suppress known benign Qt warnings
    originalHandler = qInstallMessageHandler(messageFilter);

    // Enable HiDPI scaling for crisp rendering on Retina/4K displays
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("QK4");
    app.setApplicationVersion(QK4_VERSION);
    app.setOrganizationName("AI5QK");
    app.setOrganizationDomain("ai5qk.com");

    // WHY a runtime SSL check here: the K4 connection requires TLS/PSK. Qt loads OpenSSL
    // dynamically via dlopen; on macOS with SIP enabled (every end-user Mac) DYLD_LIBRARY_PATH
    // cannot be injected after process start, so earlier attempts to prepend Homebrew paths via
    // qputenv() were ineffective. The correct fix is to bundle OpenSSL dylibs inside the .app
    // bundle's Frameworks/ directory (CMake bundle step). This check catches the case where
    // neither the bundle nor the system has OpenSSL available and tells the user directly
    // instead of failing silently when they try to connect.
    if (!QSslSocket::supportsSsl()) {
        QMessageBox::critical(
            nullptr, "OpenSSL required",
            QString("QK4 requires OpenSSL at runtime for TLS/PSK connections to the K4.\n\n"
                    "Qt build: %1\nRuntime: %2\n\n"
                    "macOS: install via Homebrew (`brew install openssl@3`) or reinstall a bundled build.\n"
                    "Windows / Linux: install the OpenSSL runtime for your platform.")
                .arg(QSslSocket::sslLibraryBuildVersionString(), QSslSocket::sslLibraryVersionString()));
        return 1;
    }

    // Load embedded Inter font family
    setupFonts();

    MainWindow window;
    window.show();

    return app.exec();
}
