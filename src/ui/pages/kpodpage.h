#ifndef KPODPAGE_H
#define KPODPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
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

    // KPOD+ keyer configuration controls
    QWidget *m_keyerConfigWidget = nullptr;
    QSpinBox *m_wpmSpinner = nullptr;
    QSpinBox *m_pitchSpinner = nullptr;
    QComboBox *m_iambicModeCombo = nullptr;
    QComboBox *m_paddleOrientCombo = nullptr;
    QComboBox *m_encodeModeCombo = nullptr;
    QSpinBox *m_stuckTimeoutSpinner = nullptr;
};

#endif // KPODPAGE_H
