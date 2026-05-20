#ifndef SSBBWPOPUP_H
#define SSBBWPOPUP_H

#include "ui/popups/k4popupbase.h"
#include "utils/wheelaccumulator.h"
#include <QPushButton>

// Floating popup for SSB / ESSB TX bandwidth. The valid range and title
// switch based on ESSB enabled (24-28 -> 2.4-2.8 kHz in SSB, 30-45 ->
// 3.0-4.5 kHz in ESSB). Emits raw bandwidth value for CAT translation.
class SsbBwPopupWidget : public K4PopupBase {
    Q_OBJECT
public:
    explicit SsbBwPopupWidget(QWidget *parent = nullptr);

    void setEssbEnabled(bool enabled); // Affects title and valid range
    void setBandwidth(int bw);         // SSB: 24-28 (2.4-2.8 kHz), ESSB: 30-45 (3.0-4.5 kHz)

    bool essbEnabled() const { return m_essbEnabled; }
    int bandwidth() const { return m_bandwidth; }

signals:
    void bandwidthChanged(int bw);

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &contentRect) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void updateTitle();
    void updateValueDisplay();
    void adjustValue(int delta);

    QPushButton *m_titleLabel;
    QPushButton *m_valueLabel;
    QPushButton *m_decrementBtn;
    QPushButton *m_incrementBtn;
    QPushButton *m_closeBtn;

    bool m_essbEnabled = false;
    int m_bandwidth = 28; // SSB default: 2.8 kHz
    WheelAccumulator m_wheelAccumulator;
};

#endif // SSBBWPOPUP_H
