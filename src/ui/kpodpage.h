#ifndef KPODPAGE_H
#define KPODPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLabel>

class KpodDevice;

/**
 * @brief OptionsDialog "KPOD" tab. Toggles KPOD USB tuning-knob integration and shows
 *        probe/descriptor info (vendor id, product id, firmware) from the connected device.
 */
class KpodPage : public QWidget {
    Q_OBJECT

public:
    explicit KpodPage(KpodDevice *kpodDevice, QWidget *parent = nullptr);

    void refresh();

private:
    void updateKpodStatus();

    KpodDevice *m_kpodDevice;
    QCheckBox *m_kpodEnableCheckbox = nullptr;
    QLabel *m_kpodStatusLabel = nullptr;
    QLabel *m_kpodProductLabel = nullptr;
    QLabel *m_kpodManufacturerLabel = nullptr;
    QLabel *m_kpodVendorIdLabel = nullptr;
    QLabel *m_kpodProductIdLabel = nullptr;
    QLabel *m_kpodDeviceTypeLabel = nullptr;
    QLabel *m_kpodFirmwareLabel = nullptr;
    QLabel *m_kpodDeviceIdLabel = nullptr;
    QLabel *m_kpodHelpLabel = nullptr;
};

#endif // KPODPAGE_H
