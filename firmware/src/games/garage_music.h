#pragma once
#include "remote/buzzer.h"

namespace garage {
// Original novelty chiptune: bouncing bass, octave hops, and a chromatic turnaround.
inline constexpr BuzzerNote music[][6] = {
    {{NOTE_C4, 100}, {NOTE_C5, 100}, {NOTE_E5, 100}, {NOTE_C5, 100}, {NOTE_G4, 200}, {NOTE_REST, 100}},
    {{NOTE_D4, 100}, {NOTE_D5, 100}, {NOTE_F5, 100}, {NOTE_D5, 100}, {NOTE_A4, 200}, {NOTE_REST, 100}},
    {{NOTE_E4, 100}, {NOTE_E5, 100}, {NOTE_G5, 100}, {NOTE_E5, 100}, {NOTE_C5, 200}, {NOTE_REST, 100}},
    {{NOTE_G4, 100}, {NOTE_G5, 100}, {NOTE_GS5, 100}, {NOTE_A5, 100}, {NOTE_G5, 100}, {NOTE_E5, 200}},
    {{NOTE_F4, 100}, {NOTE_A4, 100}, {NOTE_C5, 100}, {NOTE_A4, 100}, {NOTE_F5, 200}, {NOTE_REST, 100}},
    {{NOTE_E4, 100}, {NOTE_G4, 100}, {NOTE_B4, 100}, {NOTE_G4, 100}, {NOTE_E5, 200}, {NOTE_REST, 100}},
    {{NOTE_D5, 100}, {NOTE_C5, 100}, {NOTE_A4, 100}, {NOTE_G4, 100}, {NOTE_E4, 100}, {NOTE_G4, 200}},
    {{NOTE_C5, 100}, {NOTE_E5, 100}, {NOTE_G5, 100}, {NOTE_C5, 100}, {NOTE_C4, 200}, {NOTE_REST, 100}},
};
}
