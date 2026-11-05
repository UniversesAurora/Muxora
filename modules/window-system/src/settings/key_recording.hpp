// SPDX-License-Identifier: MIT
#pragma once

#include <QKeyEvent>
#include <QKeySequence>

namespace window_system {

// Records a key sequence one stroke at a time, as the shortcut editors do in
// either presentation. Strokes made within the pause add to the sequence, up
// to four; after the pause or at four a stroke starts a new one. Every stroke
// is recorded, so continuation letters and digits never become text.
class KeyRecording {
public:
    static constexpr int maximumStrokes = 4;
    static constexpr int pauseMs = 1500;

    enum class Result { Ignored, Cleared, Recorded };

    // The sequence after one key press. Modifier keys and repeats change
    // nothing; Backspace or Delete alone clears outside a recording.
    Result press(const QKeyEvent& event, QKeySequence& sequence)
    {
        switch (event.key()) {
        case Qt::Key_Control: case Qt::Key_Shift: case Qt::Key_Alt: case Qt::Key_Meta:
        case Qt::Key_AltGr: case Qt::Key_unknown:
            return Result::Ignored;
        default: break;
        }
        if (event.isAutoRepeat()) return Result::Ignored;
        if (!recording_ && event.modifiers() == Qt::NoModifier
            && (event.key() == Qt::Key_Backspace || event.key() == Qt::Key_Delete)) {
            sequence = {};
            return Result::Cleared;
        }
        // An empty stroke is zero; a default QKeyCombination is Key_unknown.
        QKeyCombination strokes[maximumStrokes] = {QKeyCombination::fromCombined(0), QKeyCombination::fromCombined(0),
            QKeyCombination::fromCombined(0), QKeyCombination::fromCombined(0)};
        int count = 0;
        if (recording_ && sequence.count() < maximumStrokes)
            for (; count < sequence.count(); ++count) strokes[count] = sequence[count];
        strokes[count++] = event.keyCombination();
        sequence = QKeySequence(strokes[0], strokes[1], strokes[2], strokes[3]);
        recording_ = count < maximumStrokes;
        return Result::Recorded;
    }
    // The pause ended or the editor lost focus: the next stroke starts anew.
    void stop() { recording_ = false; }
    bool recording() const { return recording_; }

private:
    bool recording_ = false;
};

} // namespace window_system
