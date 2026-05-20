#ifndef LINEOUTPOPUP_H
#define LINEOUTPOPUP_H

#include "ui/popups/k4popupbase.h"
#include "utils/wheelaccumulator.h"
#include <QPushButton>

// Floating popup for LINE OUT L/R levels (0-40) with optional "R = L"
// link. Emits leftLevelChanged / rightLevelChanged /
// rightEqualsLeftChanged for MainWindow -> CAT.
class LineOutPopupWidget : public K4PopupBase {
    Q_OBJECT

public:
    explicit LineOutPopupWidget(QWidget *parent = nullptr);

    void setLeftLevel(int level);  // 0-40
    void setRightLevel(int level); // 0-40
    void setRightEqualsLeft(bool enabled);

    int leftLevel() const { return m_leftLevel; }
    int rightLevel() const { return m_rightLevel; }
    bool rightEqualsLeft() const { return m_rightEqualsLeft; }

signals:
    void leftLevelChanged(int level);
    void rightLevelChanged(int level);
    void rightEqualsLeftChanged(bool enabled);

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &contentRect) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void updateButtonStyles();
    void updateValueLabels();

    int m_leftLevel = 10;
    int m_rightLevel = 10;
    bool m_rightEqualsLeft = false;
    bool m_leftSelected = true; // Which channel wheel adjusts

    QPushButton *m_titleLabel;
    QPushButton *m_leftBtn;
    QPushButton *m_leftValueLabel;
    QPushButton *m_rightBtn;
    QPushButton *m_rightValueLabel;
    QPushButton *m_rightEqualsLeftBtn;
    QPushButton *m_closeBtn;

    WheelAccumulator m_wheelAccumulator;
};

#endif // LINEOUTPOPUP_H
