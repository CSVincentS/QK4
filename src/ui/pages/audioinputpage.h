#ifndef AUDIOINPUTPAGE_H
#define AUDIOINPUTPAGE_H

#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>

class AudioController;

/**
 * @brief OptionsDialog "Audio Input" tab. Selects the system microphone device and tunes mic
 *        gain. Writes selections back to AudioEngine via AudioController's task-level API.
 */
class AudioInputPage : public QWidget {
    Q_OBJECT

public:
    explicit AudioInputPage(AudioController *audioController, QWidget *parent = nullptr);
    ~AudioInputPage() = default;

    void refresh();

private slots:
    void onMicDeviceChanged(int index);
    void onMicGainChanged(int value);

private:
    void populateMicDevices();

    AudioController *m_audioController;
    QComboBox *m_micDeviceCombo = nullptr;
    QSlider *m_micGainSlider = nullptr;
    QLabel *m_micGainValueLabel = nullptr;
};

#endif // AUDIOINPUTPAGE_H
