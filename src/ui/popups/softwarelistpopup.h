#ifndef SOFTWARELISTPOPUP_H
#define SOFTWARELISTPOPUP_H

#include "ui/popups/k4popupbase.h"

#include <QMap>
#include <QString>

class QLabel;

// Display-only popup that shows the K4's parsed firmware/software version
// numbers in a two-column layout, mirroring the K4 front-panel "Software
// List" screen. Opened from the Fn popup's "SW LIST" button.
//
// No CAT interaction: version data is read on-demand from RadioState and
// pushed in via setVersions() when the popup is shown. The K4 sends RV.
// responses once at connect time and never echoes updates, so there is no
// signal to subscribe to.
class SoftwareListPopupWidget : public K4PopupBase {
    Q_OBJECT

public:
    explicit SoftwareListPopupWidget(QWidget *parent = nullptr);

    // Populate the title + 12 value cells from a firmwareVersions() map.
    // CAT keys are mapped to front-panel labels (see .cpp). Missing keys
    // render as an em-dash placeholder. The "R" key, if present, is shown
    // in the title as "SOFTWARE LIST ( <value> )".
    void setVersions(const QMap<QString, QString> &versions);

protected:
    QSize contentSize() const override;

private:
    void setupUi();

    QLabel *m_titleLabel = nullptr;
    // 12 value cells: left column rows 0-5, right column rows 6-11.
    QLabel *m_valueLabels[12] = {nullptr};
};

#endif // SOFTWARELISTPOPUP_H
