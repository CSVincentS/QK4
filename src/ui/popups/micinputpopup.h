#ifndef MICINPUTPOPUP_H
#define MICINPUTPOPUP_H

#include <QWidget>
#include <QPushButton>

/**
 * @brief Floating popup for choosing the mic input source (Front / Rear / LINE IN / Front+Line /
 *        Rear+Line). Mirrors the K4's MC command options; emits inputChanged(0-4).
 */
class MicInputPopupWidget : public QWidget {
    Q_OBJECT
public:
    explicit MicInputPopupWidget(QWidget *parent = nullptr);

    void setCurrentInput(int input); // 0-4
    int currentInput() const { return m_currentInput; }

    void showAboveWidget(QWidget *widget);
    void hidePopup();

signals:
    void inputChanged(int input);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setupUi();
    void updateButtonStyles();

    QPushButton *m_titleLabel;
    QPushButton *m_frontBtn;     // 0 = Front mic
    QPushButton *m_rearBtn;      // 1 = Rear mic
    QPushButton *m_lineInBtn;    // 2 = LINE IN
    QPushButton *m_frontLineBtn; // 3 = Front + LINE IN
    QPushButton *m_rearLineBtn;  // 4 = Rear + LINE IN
    QPushButton *m_closeBtn;

    QWidget *m_referenceWidget = nullptr;
    int m_currentInput = 0;
};

#endif // MICINPUTPOPUP_H
