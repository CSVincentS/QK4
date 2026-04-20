#ifndef AUDIOOUTPUTPAGE_H
#define AUDIOOUTPUTPAGE_H

#include <QWidget>
#include <QComboBox>

class AudioEngine;

/**
 * @brief OptionsDialog "Audio Output" tab. Selects the system speaker device; writes selection
 *        back to AudioEngine via the AudioController.
 */
class AudioOutputPage : public QWidget {
    Q_OBJECT

public:
    explicit AudioOutputPage(AudioEngine *audioEngine, QWidget *parent = nullptr);

    void refresh();

private slots:
    void onSpeakerDeviceChanged(int index);

private:
    void populateSpeakerDevices();

    AudioEngine *m_audioEngine;
    QComboBox *m_speakerDeviceCombo = nullptr;
};

#endif // AUDIOOUTPUTPAGE_H
