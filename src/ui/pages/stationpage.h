#ifndef STATIONPAGE_H
#define STATIONPAGE_H

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QWidget>

/**
 * @brief Options page for station / operator info: call sign, grid square, operator
 *        name, QTH, and the IARU region that drives the panadapter band-plan overlay
 *        (plus a show/hide toggle for that overlay). All fields persist via the
 *        RadioSettings singleton; SpectrumController reacts to the region/overlay
 *        signals — this page needs no controller.
 */
class StationPage : public QWidget {
    Q_OBJECT

public:
    explicit StationPage(QWidget *parent = nullptr);
    ~StationPage() = default;

private slots:
    void onIaruRegionChanged(int index);
    void onCallSignEdited();
    void onGridSquareEdited();
    void onOperatorNameEdited();
    void onQthEdited();
    void onBandPlanOverlayToggled(bool checked);

private:
    QComboBox *m_regionCombo = nullptr;
    QLineEdit *m_callSignEdit = nullptr;
    QLineEdit *m_gridSquareEdit = nullptr;
    QLineEdit *m_operatorNameEdit = nullptr;
    QLineEdit *m_qthEdit = nullptr;
    QCheckBox *m_bandPlanCheck = nullptr;
};

#endif // STATIONPAGE_H
