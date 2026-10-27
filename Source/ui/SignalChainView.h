#pragma once

#include "ui/PedalLookAndFeel.h"

namespace pedal::ui
{

// The blocks of the signal chain, in signal order.
enum class BlockId { input, gate, drop, amp, cab, output };
inline constexpr BlockId allBlocks[] = { BlockId::input, BlockId::gate, BlockId::drop,
                                         BlockId::amp, BlockId::cab, BlockId::output };

struct BlockState
{
    juce::String title;
    juce::String detail;     // short summary under the title, e.g. "-2 st"
    bool canBypass = false;  // shows the power switch
    bool enabled = true;
};

// A row of pedal-like blocks joined by a cable, in the spirit of the Neural DSP
// plugins. Tapping a block selects it (its controls show below the row);
// tapping its power switch turns it on or off.
class SignalChainView : public juce::Component
{
public:
    SignalChainView();
    ~SignalChainView() override;

    std::function<void (BlockId)> onSelect;
    std::function<void (BlockId)> onTogglePower;

    void setBlockState (BlockId, const BlockState&);
    void setSelected (BlockId);
    BlockId getSelected() const noexcept { return selected; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Block;
    juce::OwnedArray<Block> blocks;
    BlockId selected = BlockId::amp;
};

} // namespace pedal::ui
