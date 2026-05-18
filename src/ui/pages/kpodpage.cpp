#include "ui/pages/kpodpage.h"
#include "ui/styling/k4styles.h"
#include "hardware/kpoddevice.h"
#include "hardware/kpodplusdevice.h"
#include "settings/radiosettings.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QStringList>
#include <QVector>

KpodPage::KpodPage(KpodDevice *kpodDevice, KpodPlusDevice *kpodPlusDevice, QWidget *parent)
    : QWidget(parent), m_kpodDevice(kpodDevice), m_kpodPlusDevice(kpodPlusDevice) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Status indicator
    auto *statusLayout = new QHBoxLayout();
    auto *statusLabel = new QLabel("Status:", this);
    statusLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    statusLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_kpodStatusLabel = new QLabel("Not Detected", this);
    statusLayout->addWidget(statusLabel);
    statusLayout->addWidget(m_kpodStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Separator line
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Device Summary title
    auto *titleLabel = new QLabel("Device Summary", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    // Device info table using grid layout
    auto *tableWidget = new QWidget(this);
    auto *tableLayout = new QGridLayout(tableWidget);
    tableLayout->setContentsMargins(0, K4Styles::Dimensions::PaddingMedium, 0, K4Styles::Dimensions::PaddingMedium);
    tableLayout->setHorizontalSpacing(K4Styles::Dimensions::DialogMargin);
    tableLayout->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    // Table styling
    QString headerStyle = QString("color: %1; font-size: %2px; font-weight: bold; padding: 5px;")
                              .arg(K4Styles::Colors::TextGray)
                              .arg(K4Styles::Dimensions::FontSizeButton);

    // Create labels with property names
    QStringList properties = {"Product Name", "Manufacturer",     "Vendor ID", "Product ID",
                              "Device Type",  "Firmware Version", "Device ID"};
    QVector<QLabel **> valueLabels = {&m_kpodProductLabel,   &m_kpodManufacturerLabel, &m_kpodVendorIdLabel,
                                      &m_kpodProductIdLabel, &m_kpodDeviceTypeLabel,   &m_kpodFirmwareLabel,
                                      &m_kpodDeviceIdLabel};

    for (int row = 0; row < properties.size(); ++row) {
        auto *propLabel = new QLabel(properties[row], tableWidget);
        propLabel->setStyleSheet(headerStyle);

        *valueLabels[row] = new QLabel("N/A", tableWidget);

        tableLayout->addWidget(propLabel, row, 0, Qt::AlignLeft);
        tableLayout->addWidget(*valueLabels[row], row, 1, Qt::AlignLeft);
    }

    tableLayout->setColumnStretch(1, 1);
    layout->addWidget(tableWidget);

    // Another separator
    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Enable checkbox
    m_kpodEnableCheckbox = new QCheckBox("Enable K-Pod", this);
    m_kpodEnableCheckbox->setChecked(RadioSettings::instance()->kpodEnabled());

    connect(m_kpodEnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setKpodEnabled(checked); });

    layout->addWidget(m_kpodEnableCheckbox);

    // KPOD+ keyer configuration section (hidden until KPOD+ detected)
    setupKeyerConfigSection(layout);

    // Help text
    m_kpodHelpLabel = new QLabel("Connect a K-Pod device to enable this feature.", this);
    m_kpodHelpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    m_kpodHelpLabel->setWordWrap(true);
    layout->addWidget(m_kpodHelpLabel);

    layout->addStretch();

    // Connect to device signals for real-time status updates
    if (m_kpodDevice) {
        connect(m_kpodDevice, &KpodDevice::deviceConnected, this, &KpodPage::updateKpodStatus);
        connect(m_kpodDevice, &KpodDevice::deviceDisconnected, this, &KpodPage::updateKpodStatus);
    }
    if (m_kpodPlusDevice) {
        connect(m_kpodPlusDevice, &KpodPlusDevice::deviceConnected, this, &KpodPage::updateKpodStatus);
        connect(m_kpodPlusDevice, &KpodPlusDevice::deviceDisconnected, this, &KpodPage::updateKpodStatus);
    }

    // Reactive refresh: when HardwareController mirrors a K4 echo into RadioSettings
    // (K4 → settings as source of truth), the page widgets need to repaint without
    // re-firing their valueChanged signals back to the K4. This path is separate from
    // kpodPlusSettingsChanged (which is the user-action path).
    connect(RadioSettings::instance(), &RadioSettings::kpodPlusSettingsExternallyUpdated, this,
            &KpodPage::refreshKeyerConfigFromSettings);

    // Initialize with current status
    updateKpodStatus();
}

void KpodPage::refreshKeyerConfigFromSettings() {
    auto *s = RadioSettings::instance();
    // QSignalBlocker on each widget so the programmatic setValue doesn't fire
    // valueChanged → kpodPlusSettingsChanged → re-send to K4.
    if (m_wpmSpinner) {
        QSignalBlocker block(m_wpmSpinner);
        m_wpmSpinner->setValue(s->kpodPlusKeyerSpeed());
    }
    if (m_pitchSpinner) {
        QSignalBlocker block(m_pitchSpinner);
        m_pitchSpinner->setValue(s->kpodPlusCwPitch());
    }
    if (m_iambicModeCombo) {
        QSignalBlocker block(m_iambicModeCombo);
        m_iambicModeCombo->setCurrentIndex(s->kpodPlusIambicMode());
    }
    if (m_paddleOrientCombo) {
        QSignalBlocker block(m_paddleOrientCombo);
        m_paddleOrientCombo->setCurrentIndex(s->kpodPlusPaddleReversed() ? 1 : 0);
    }
}

void KpodPage::setupKeyerConfigSection(QVBoxLayout *parentLayout) {
    m_keyerConfigWidget = new QWidget(this);
    auto *keyerLayout = new QVBoxLayout(m_keyerConfigWidget);
    keyerLayout->setContentsMargins(0, 0, 0, 0);
    keyerLayout->setSpacing(K4Styles::Dimensions::PaddingMedium);

    // Section title
    auto *keyerTitle = new QLabel("Keyer Configuration", m_keyerConfigWidget);
    keyerTitle->setStyleSheet(K4Styles::Dialog::titleLabel());
    keyerLayout->addWidget(keyerTitle);

    // Grid for keyer controls
    auto *gridWidget = new QWidget(m_keyerConfigWidget);
    auto *grid = new QGridLayout(gridWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(K4Styles::Dimensions::DialogMargin);
    grid->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    QString labelStyle = QString("color: %1; font-size: %2px; font-weight: bold; padding: 5px;")
                             .arg(K4Styles::Colors::TextGray)
                             .arg(K4Styles::Dimensions::FontSizeButton);

    auto *settings = RadioSettings::instance();
    int row = 0;

    // WPM spinner (8-100)
    auto *wpmLabel = new QLabel("Keyer Speed (WPM)", gridWidget);
    wpmLabel->setStyleSheet(labelStyle);
    m_wpmSpinner = new QSpinBox(gridWidget);
    m_wpmSpinner->setRange(8, 100);
    m_wpmSpinner->setValue(settings->kpodPlusKeyerSpeed());
    grid->addWidget(wpmLabel, row, 0, Qt::AlignLeft);
    grid->addWidget(m_wpmSpinner, row, 1, Qt::AlignLeft);
    row++;

    // CW Pitch spinner (400-1000 Hz, step 10)
    auto *pitchLabel = new QLabel("CW Pitch (Hz)", gridWidget);
    pitchLabel->setStyleSheet(labelStyle);
    m_pitchSpinner = new QSpinBox(gridWidget);
    m_pitchSpinner->setRange(400, 1000);
    m_pitchSpinner->setSingleStep(10);
    m_pitchSpinner->setValue(settings->kpodPlusCwPitch());
    grid->addWidget(pitchLabel, row, 0, Qt::AlignLeft);
    grid->addWidget(m_pitchSpinner, row, 1, Qt::AlignLeft);
    row++;

    // Iambic Mode: A / B
    auto *iambicLabel = new QLabel("Iambic Mode", gridWidget);
    iambicLabel->setStyleSheet(labelStyle);
    m_iambicModeCombo = new QComboBox(gridWidget);
    m_iambicModeCombo->addItem("Iambic A", 0);
    m_iambicModeCombo->addItem("Iambic B", 1);
    m_iambicModeCombo->setCurrentIndex(settings->kpodPlusIambicMode());
    grid->addWidget(iambicLabel, row, 0, Qt::AlignLeft);
    grid->addWidget(m_iambicModeCombo, row, 1, Qt::AlignLeft);
    row++;

    // Paddle Orientation: Normal / Reversed
    auto *paddleLabel = new QLabel("Paddle Orientation", gridWidget);
    paddleLabel->setStyleSheet(labelStyle);
    m_paddleOrientCombo = new QComboBox(gridWidget);
    m_paddleOrientCombo->addItem("Normal", 0);
    m_paddleOrientCombo->addItem("Reversed", 1);
    m_paddleOrientCombo->setCurrentIndex(settings->kpodPlusPaddleReversed() ? 1 : 0);
    grid->addWidget(paddleLabel, row, 0, Qt::AlignLeft);
    grid->addWidget(m_paddleOrientCombo, row, 1, Qt::AlignLeft);
    row++;

    // Encode Mode: Element (KZ) / ASCII (KX)
    auto *encodeLabel = new QLabel("Encode Mode", gridWidget);
    encodeLabel->setStyleSheet(labelStyle);
    m_encodeModeCombo = new QComboBox(gridWidget);
    m_encodeModeCombo->addItem("Element (KZ)", 0);
    m_encodeModeCombo->addItem("ASCII (KX)", 1);
    m_encodeModeCombo->setCurrentIndex(settings->kpodPlusEncodeMode());
    grid->addWidget(encodeLabel, row, 0, Qt::AlignLeft);
    grid->addWidget(m_encodeModeCombo, row, 1, Qt::AlignLeft);
    row++;

    // Stuck Timeout spinner (5-600 sec)
    auto *timeoutLabel = new QLabel("Stuck Timeout (sec)", gridWidget);
    timeoutLabel->setStyleSheet(labelStyle);
    m_stuckTimeoutSpinner = new QSpinBox(gridWidget);
    m_stuckTimeoutSpinner->setRange(5, 600);
    m_stuckTimeoutSpinner->setValue(settings->kpodPlusStuckTimeout());
    grid->addWidget(timeoutLabel, row, 0, Qt::AlignLeft);
    grid->addWidget(m_stuckTimeoutSpinner, row, 1, Qt::AlignLeft);

    grid->setColumnStretch(1, 1);
    keyerLayout->addWidget(gridWidget);

    // Connect controls → settings + device immediately on change
    connect(m_wpmSpinner, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int wpm) {
        RadioSettings::instance()->setKpodPlusKeyerSpeed(wpm);
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setKeyerSpeed(wpm);
        }
    });

    connect(m_pitchSpinner, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int hz) {
        RadioSettings::instance()->setKpodPlusCwPitch(hz);
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setCwPitch(hz);
        }
    });

    connect(m_iambicModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        RadioSettings::instance()->setKpodPlusIambicMode(index);
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setKeyerParams(index, m_paddleOrientCombo->currentIndex() == 1);
        }
    });

    connect(m_paddleOrientCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        RadioSettings::instance()->setKpodPlusPaddleReversed(index == 1);
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setKeyerParams(m_iambicModeCombo->currentIndex(), index == 1);
        }
    });

    connect(m_encodeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        RadioSettings::instance()->setKpodPlusEncodeMode(index);
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setEncodeMode(index);
        }
    });

    connect(m_stuckTimeoutSpinner, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int sec) {
        RadioSettings::instance()->setKpodPlusStuckTimeout(sec);
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setStuckTimeout(sec);
        }
    });

    // Separator before keyer section
    auto *line3 = new QFrame(this);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(K4Styles::Dialog::separator());
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    parentLayout->addWidget(line3);
    parentLayout->addWidget(m_keyerConfigWidget);

    // Initially hidden
    line3->setVisible(false);
    m_keyerConfigWidget->setVisible(false);
}

void KpodPage::refresh() {
    updateKpodStatus();
}

void KpodPage::updateKpodStatus() {
    if (!m_kpodStatusLabel)
        return;

    // Check both KPOD and KPOD+ — they can coexist
    bool kpodDetected = m_kpodDevice && m_kpodDevice->isDetected();
    bool kpodPlusDetected = m_kpodPlusDevice && m_kpodPlusDevice->isDetected();
    bool anyDetected = kpodDetected || kpodPlusDetected;

    // Styling
    QString valueStyle = QString("color: %1; font-size: %2px; padding: 5px;")
                             .arg(K4Styles::Colors::TextWhite)
                             .arg(K4Styles::Dimensions::FontSizeButton);
    QString notDetectedStyle = QString("color: %1; font-size: %2px; font-style: italic; padding: 5px;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(K4Styles::Dimensions::FontSizeButton);

    // Update status label — show which device is detected
    QString statusText;
    if (kpodPlusDetected && kpodDetected) {
        statusText = "KPOD + KPOD+ Detected";
    } else if (kpodPlusDetected) {
        statusText = "KPOD+ Detected";
    } else if (kpodDetected) {
        statusText = "Detected";
    } else {
        statusText = "Not Detected";
    }
    QString statusColor = anyDetected ? K4Styles::Colors::StatusGreen : K4Styles::Colors::ErrorRed;
    m_kpodStatusLabel->setText(statusText);
    m_kpodStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(statusColor));

    // Prefer KPOD+ info if both are detected, otherwise show KPOD
    auto setLabel = [&](QLabel *label, const QString &value) {
        QString displayValue = value.isEmpty() ? "N/A" : value;
        label->setText(displayValue);
        label->setStyleSheet(displayValue == "N/A" ? notDetectedStyle : valueStyle);
    };

    if (kpodPlusDetected) {
        KpodPlusDeviceInfo info = m_kpodPlusDevice->deviceInfo();
        setLabel(m_kpodProductLabel, info.productName);
        setLabel(m_kpodManufacturerLabel, info.manufacturer);
        setLabel(m_kpodVendorIdLabel,
                 QString("%1 (0x%2)").arg(info.vendorId).arg(info.vendorId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodProductIdLabel,
                 QString("%1 (0x%2)").arg(info.productId).arg(info.productId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodDeviceTypeLabel, "USB Vendor-Specific (Keyer)");
        setLabel(m_kpodFirmwareLabel, info.firmwareVersion);
        setLabel(m_kpodDeviceIdLabel, info.deviceId);
    } else if (kpodDetected) {
        KpodDeviceInfo info = m_kpodDevice->deviceInfo();
        setLabel(m_kpodProductLabel, info.productName);
        setLabel(m_kpodManufacturerLabel, info.manufacturer);
        setLabel(m_kpodVendorIdLabel,
                 QString("%1 (0x%2)").arg(info.vendorId).arg(info.vendorId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodProductIdLabel,
                 QString("%1 (0x%2)").arg(info.productId).arg(info.productId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodDeviceTypeLabel, "USB HID (Human Interface Device)");
        setLabel(m_kpodFirmwareLabel, info.firmwareVersion);
        setLabel(m_kpodDeviceIdLabel, info.deviceId);
    } else {
        setLabel(m_kpodProductLabel, "");
        setLabel(m_kpodManufacturerLabel, "");
        setLabel(m_kpodVendorIdLabel, "");
        setLabel(m_kpodProductIdLabel, "");
        setLabel(m_kpodDeviceTypeLabel, "");
        setLabel(m_kpodFirmwareLabel, "");
        setLabel(m_kpodDeviceIdLabel, "");
    }

    // Update checkbox enabled state and styling
    m_kpodEnableCheckbox->setEnabled(anyDetected);
    m_kpodEnableCheckbox->setStyleSheet(anyDetected ? K4Styles::Dialog::checkBox()
                                                    : K4Styles::Dialog::checkBoxDisabled());

    // Show/hide keyer configuration section based on KPOD+ detection
    if (m_keyerConfigWidget) {
        m_keyerConfigWidget->setVisible(kpodPlusDetected);
        // Also show the separator above it
        QWidget *sep = m_keyerConfigWidget->parentWidget();
        if (sep) {
            // Find the separator line just before keyer config
            int idx = static_cast<QVBoxLayout *>(layout())->indexOf(m_keyerConfigWidget);
            if (idx > 0) {
                QLayoutItem *item = layout()->itemAt(idx - 1);
                if (item && item->widget()) {
                    item->widget()->setVisible(kpodPlusDetected);
                }
            }
        }
    }

    // Update help text
    if (kpodPlusDetected) {
        m_kpodHelpLabel->setText("KPOD+ keyer is active. Paddle, keyer, and sidetone are handled by the device.");
    } else if (kpodDetected) {
        m_kpodHelpLabel->setText("When enabled, the K-Pod VFO knob and buttons will control the radio.");
    } else {
        m_kpodHelpLabel->setText("Connect a K-Pod device to enable this feature.");
    }
}
