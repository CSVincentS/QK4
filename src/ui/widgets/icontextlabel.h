#ifndef ICONTEXTLABEL_H
#define ICONTEXTLABEL_H

#include <QPixmap>
#include <QWidget>

class QLabel;

// Small composite widget for the top status bar: an icon followed by a value
// label (and optional unit label). Each new at-a-glance metric in the status
// bar should be one instance of this widget.
//
// Empty-state convention: clear() (or setValue("")) renders "--" so the slot
// reads as "no data" rather than blank space.
class IconTextLabel : public QWidget {
    Q_OBJECT

public:
    explicit IconTextLabel(QWidget *parent = nullptr);

    void setIcon(const QPixmap &pixmap);
    // Optional prefix label ("LPA", "PA", "FAN", ...). Rendered between the
    // icon and the value, in a muted color. Persists across clear() so the
    // disconnected state reads as e.g. "LPA --" instead of just "--".
    void setLabel(const QString &label);
    void setValue(const QString &text);
    void setUnit(const QString &unit);
    void setValueColor(const QColor &color);
    // Render the empty-state placeholder ("--"). Preserves any prefix label.
    void clear();

private:
    void applyValueStyle(const QColor &color);

    QLabel *m_iconLabel;
    QLabel *m_prefixLabel;
    QLabel *m_valueLabel;
    QLabel *m_unitLabel;
    QColor m_valueColor;
};

#endif // ICONTEXTLABEL_H
