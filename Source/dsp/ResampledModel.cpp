#include "dsp/ResampledModel.h"

// AudioDSPTools' resampler comes from iPlug2 and expects two of its constants.
namespace iplug { inline constexpr double PI = 3.14159265358979323846; }
inline constexpr int DEFAULT_BLOCK_SIZE = 1024;
#include "dsp/ResamplingContainer/ResamplingContainer.h"

#include <cmath>

namespace pedal
{
namespace
{
class ResampledModel : public nam::DSP
{
public:
    explicit ResampledModel (std::unique_ptr<nam::DSP> innerModel)
        : nam::DSP (1, 1, innerModel->GetExpectedSampleRate()),
          inner (std::move (innerModel)),
          modelSampleRate (inner->GetExpectedSampleRate())
    {
    }

    bool SupportsArbitrarySampleRate() override { return true; }

    void Reset (const double sampleRate, const int maxBufferSize) override
    {
        passThrough = std::abs (sampleRate - modelSampleRate) < 1.0;

        if (passThrough)
        {
            resampler.reset();
            inner->Reset (modelSampleRate, maxBufferSize);
            return;
        }

        // The inner model sees blocks that are longer when upsampling; leave a
        // couple of samples of headroom for the resampler's rounding.
        const int innerMaxBlock = (int) std::ceil (maxBufferSize * modelSampleRate / sampleRate) + 2;
        inner->Reset (modelSampleRate, innerMaxBlock);

        resampler = std::make_unique<Resampler> (modelSampleRate);
        resampler->Reset (sampleRate, maxBufferSize);
    }

    void process (NAM_SAMPLE** input, NAM_SAMPLE** output, const int numFrames) override
    {
        if (passThrough || resampler == nullptr)
        {
            inner->process (input, output, numFrames);
            return;
        }

        auto* model = inner.get(); // captured by pointer so std::function does not allocate
        resampler->ProcessBlock (input, output, numFrames,
                                 [model] (NAM_SAMPLE** in, NAM_SAMPLE** out, int n) { model->process (in, out, n); });
    }

private:
    using Resampler = dsp::ResamplingContainer<NAM_SAMPLE, 1, 12>;

    std::unique_ptr<nam::DSP> inner;
    std::unique_ptr<Resampler> resampler;
    const double modelSampleRate;
    bool passThrough = true;
};
} // namespace

std::unique_ptr<nam::DSP> runAtAnySampleRate (std::unique_ptr<nam::DSP> model)
{
    if (model == nullptr || model->SupportsArbitrarySampleRate() || model->GetExpectedSampleRate() <= 0.0
        || model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
        return model;

    return std::make_unique<ResampledModel> (std::move (model));
}

} // namespace pedal
