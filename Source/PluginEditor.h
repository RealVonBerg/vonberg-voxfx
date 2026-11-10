#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace vfx::ui
{
    namespace col
    {
        const juce::Colour bgTop    { 0xff15254a };
        const juce::Colour bgMid    { 0xff0b1428 };
        const juce::Colour bgBottom { 0xff070d1c };
        const juce::Colour line     { 0xff1c2a47 };
        const juce::Colour border   { 0xff22314f };
        const juce::Colour borderHi { 0xff33456b };
        const juce::Colour well     { 0xff0a1224 };
        const juce::Colour display  { 0xff08101f };
        const juce::Colour gold     { 0xffe2c27e };
        const juce::Colour ivory    { 0xffede6d6 };
        const juce::Colour muted    { 0xff9aa5be };
        const juce::Colour muted2   { 0xff8f9ab3 };
        const juce::Colour rec      { 0xffe5484d };
    }

    juce::Font uiFont (float size, bool bold = false, float tracking = 0.0f);
    juce::String T (const char* utf8);

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    };

    class VbButton : public juce::Button
    {
    public:
        enum class Style { pill, gold, segment, outline, ghost };
        enum class Icon  { none, play, stop, rec, open, clear, dice, left, right };
        VbButton (const juce::String& text, Style, Icon = Icon::none);
        void paintButton (juce::Graphics&, bool over, bool down) override;
        void setIcon (Icon i) { icon = i; repaint(); }
        juce::Colour accent = col::gold;
    private:
        Style style;
        Icon icon;
    };

    //==========================================================================
    class ClipView : public juce::Component
    {
    public:
        explicit ClipView (VoxProcessor&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void refresh();
        bool fileDragHover = false;
        std::function<void()> onBrowse;
    private:
        VoxProcessor& proc;
        std::shared_ptr<const VoxProcessor::Rendered> r;
        int seen = -1;
        bool dragStarted = false;
    };

    //==========================================================================
    class Pad : public juce::Component
    {
    public:
        Pad (VoxProcessor&, int fx);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        std::function<void (int)> onSelect;
        bool selected = false;
        juce::String detail;
    private:
        VoxProcessor& proc;
        int fx;
        bool held = false;
    };

    //==========================================================================
    class SeqGrid : public juce::Component
    {
    public:
        explicit SeqGrid (VoxProcessor&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        static constexpr int labelW = 112;
    private:
        bool cellAt (juce::Point<float>, int& fx, int& step) const;
        VoxProcessor& proc;
        bool paintValue = true;
        int lastFx = -1, lastStep = -1;
    };
}

//==============================================================================
class VoxEditor : public juce::AudioProcessorEditor,
                  public juce::FileDragAndDropTarget,
                  private juce::Timer
{
public:
    explicit VoxEditor (VoxProcessor&);
    ~VoxEditor() override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    class Panel;
    void timerCallback() override;
    void applyScale (int percent);
    void browse();

    VoxProcessor& proc;
    vfx::ui::LookAndFeel lnf;
    std::unique_ptr<Panel> panel;
    std::unique_ptr<juce::FileChooser> chooser;
    int scalePercent = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxEditor)
};
