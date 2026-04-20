#ifndef HALIKEYV14WORKER_H
#define HALIKEYV14WORKER_H

#include "halikeyworkerbase.h"

/**
 * @brief HaliKey worker for the V1.4 native-hardware variant. Opens the serial port with
 *        platform-specific native APIs (HANDLE on Windows, fd on POSIX) and polls the
 *        modem-control pins at ~500 µs intervals with 2-sample debounce (~1 ms). `prepareShutdown`
 *        toggles the port state to unblock any pending read so the monitor loop can exit.
 */
class HaliKeyV14Worker : public HaliKeyWorkerBase {
    Q_OBJECT

public:
    explicit HaliKeyV14Worker(const QString &portName, QObject *parent = nullptr);
    ~HaliKeyV14Worker() override;

    void prepareShutdown() override;

public slots:
    void start() override; // Opens port, enters monitor loop

private:
    void monitorLoop(); // Platform-specific main loop
    bool openNativePort();
    void closeNativePort();
    bool readPinState(bool &ditState, bool &dahState);

    // Debounce: 2 consecutive reads at ~500us = ~1ms
    static constexpr int DEBOUNCE_COUNT = 2;

    // Native port handle
#ifdef Q_OS_WIN
    void *m_handle = nullptr; // HANDLE
#else
    int m_fd = -1;
#endif
};

#endif // HALIKEYV14WORKER_H
