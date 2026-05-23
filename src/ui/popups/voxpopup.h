#ifndef VOXPOPUP_H
#define VOXPOPUP_H

#include "ui/popups/k4popupbase.h"
#include "utils/wheelaccumulator.h"
#include <QPushButton>

// Floating popup for VOX gain / Anti-VOX (mode selected via PopupMode).
// Value range 0-60; title text + popup width adapt to voice vs data
// mode and VoxGain vs AntiVox. Emits valueChanged / voxToggled.
class VoxPopupWidget : public K4PopupBase {
    Q_OBJECT
public:
    enum PopupMode { VoxGain, AntiVox };

    explicit VoxPopupWidget(QWidget *parent = nullptr);

    void setPopupMode(PopupMode mode);
    PopupMode popupMode() const { return m_popupMode; }

    void setDataMode(bool isDataMode); // Affects title (VOICE vs DATA)
    void setValue(int value);          // 0-60
    void setVoxEnabled(bool enabled);  // VOX ON/OFF state

    int value() const { return m_value; }
    bool voxEnabled() const { return m_voxEnabled; }

signals:
    void valueChanged(int value);
    void voxToggled(bool enabled);

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &contentRect) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void updateTitle();
    void updateVoxButton();
    void updateValueDisplay();
    void adjustValue(int delta);

    QPushButton *m_titleLabel;
    QPushButton *m_voxBtn;
    QPushButton *m_valueLabel;
    QPushButton *m_decrementBtn;
    QPushButton *m_incrementBtn;
    QPushButton *m_closeBtn;

    PopupMode m_popupMode = VoxGain;
    bool m_isDataMode = false;
    int m_value = 0;
    bool m_voxEnabled = false;
    WheelAccumulator m_wheelAccumulator;
};

#endif // VOXPOPUP_H
