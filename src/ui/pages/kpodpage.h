#ifndef KPODPAGE_H
#define KPODPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>

class QVBoxLayout;
class KpodDevice;
class KpodPlusDevice;

/**
 * @brief OptionsDialog "KPOD" tab. Toggles KPOD USB tuning-knob integration and shows
 *        probe/descriptor info (vendor id, product id, firmware) from the connected device.
 *        When a KPOD+ is detected, shows additional keyer configuration controls.
 */
class KpodPage : public QWidget {
    Q_OBJECT

public:
    explicit KpodPage(KpodDevice *kpodDevice, KpodPlusDevice *kpodPlusDevice, QWidget *parent = nullptr);

    void refresh();

private:
    void updateKpodStatus();
    void setupKeyerConfigSection(QVBoxLayout *layout);

    KpodDevice *m_kpodDevice;
    KpodPlusDevice *m_kpodPlusDevice;

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

    // KPOD+ configuration controls. Keyer speed / CW pitch / iambic mode /
    // paddle orientation are no longer configured here — they mirror the K4
    // (see CwController). Encode mode has no K4 equivalent and stays manual.
    QWidget *m_keyerConfigWidget = nullptr;
    QComboBox *m_encodeModeCombo = nullptr;
};

#endif // KPODPAGE_H
