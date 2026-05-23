#ifndef MICCONFIGPOPUP_H
#define MICCONFIGPOPUP_H

#include "ui/popups/k4popupbase.h"
#include <QPushButton>

// Floating popup for per-mic (Front/Rear) bias, pre-amp, and extra
// buttons configuration. Layout adapts to mic type: Front exposes an
// extra "Buttons" control that Rear does not.
class MicConfigPopupWidget : public K4PopupBase {
    Q_OBJECT
public:
    enum MicType { Front = 0, Rear = 1 };

    explicit MicConfigPopupWidget(QWidget *parent = nullptr);

    void setMicType(MicType type);
    MicType micType() const { return m_micType; }

    // Set values from RadioState
    void setBias(int bias);       // 0=OFF, 1=ON
    void setPreamp(int preamp);   // Front: 0-2, Rear: 0-1
    void setButtons(int buttons); // 0=disabled, 1=enabled (Front only)

    int bias() const { return m_bias; }
    int preamp() const { return m_preamp; }
    int buttons() const { return m_buttons; }

signals:
    void biasChanged(int bias);
    void preampChanged(int preamp);
    void buttonsChanged(int buttons);

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &contentRect) override;

private:
    void setupUi();
    void updateLayout();
    void updateButtonLabels();

    QPushButton *m_titleLabel;
    QPushButton *m_biasBtn;
    QPushButton *m_preampBtn;
    QPushButton *m_buttonsBtn; // Only visible for Front mic
    QPushButton *m_closeBtn;

    MicType m_micType = Front;
    int m_bias = 0;
    int m_preamp = 0;
    int m_buttons = 0;
};

#endif // MICCONFIGPOPUP_H
