#pragma once

#include "NAM/dsp.h"

#include <memory>

namespace pedal
{

// Most NAM captures (all of TONE3000's) are trained at 48 kHz and sound wrong
// at any other rate. Some audio interfaces only run at 44.1 kHz (the M-VAVE
// MK-300, for example), so this wraps a model and resamples around it:
// host rate -> model rate -> model -> host rate. When the rates already
// match it is a plain pass-through.
//
// Reset() allocates and must be called off the audio thread, like any NAM model.
std::unique_ptr<nam::DSP> runAtAnySampleRate (std::unique_ptr<nam::DSP> model);

} // namespace pedal
