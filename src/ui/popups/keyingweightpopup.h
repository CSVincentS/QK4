#ifndef KEYINGWEIGHTPOPUP_H
#define KEYINGWEIGHTPOPUP_H

#include "ui/widgets/wheelaccumulator.h"
#include <QPushButton>
#include <QWidget>

/**
 * @brief Floating popup for CW keying-weight adjustment. Wraps the raw KP3 value (range 090-125,
 *        representing dit/dah ratio × 100) in a +/- interface. Emits `weightChanged(raw)` back to
 *        MainWindow which sends the `KP3` CAT command.
 */
class KeyingWeightPopupWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeyingWeightPopupWidget(QWidget *parent = nullptr);

    void setWeight(int weight); // Raw 090-125 value
    int weight() const { return m_weight; }

    void showAboveWidget(QWidget *widget);
    void hidePopup();

signals:
    void weightChanged(int weight); // Raw 090-125 value

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void updateValueDisplay();
    void adjustValue(int delta);

    QPushButton *m_titleLabel;
    QPushButton *m_valueLabel;
    QPushButton *m_decrementBtn;
    QPushButton *m_incrementBtn;
    QPushButton *m_closeBtn;

    QWidget *m_referenceWidget = nullptr;

    int m_weight = 100; // Default 1.00 ratio (range 090-125)
    WheelAccumulator m_wheelAccumulator;
};

#endif // KEYINGWEIGHTPOPUP_H
