#include "PluginEditor.h"

using namespace vfx::ui;

static constexpr int baseW = 1000;
static constexpr int baseH = 700;

//==============================================================================
juce::Font vfx::ui::uiFont (float size, bool bold, float tracking)
{
    juce::FontOptions opts (size);
   #if JUCE_WINDOWS
    opts = opts.withName ("Segoe UI");
   #endif
    opts = opts.withStyle (bold ? "Bold" : "Regular").withKerningFactor (tracking);
    return juce::Font (opts);
}

juce::String vfx::ui::T (const char* utf8) { return juce::String::fromUTF8 (utf8); }

static juce::Font logoFont (float size)
{
   #if JUCE_WINDOWS
    return juce::Font (juce::FontOptions ("Segoe Script", size, juce::Font::plain));
   #elif JUCE_MAC
    return juce::Font (juce::FontOptions ("Snell Roundhand", size, juce::Font::bold));
   #else
    return juce::Font (juce::FontOptions (size).withStyle ("Italic"));
   #endif
}

static void drawIcon (juce::Graphics& g, VbButton::Icon icon, juce::Rectangle<float> r)
{
    juce::Path p;
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    bool fill = false;
    switch (icon)
    {
        case VbButton::Icon::play:  p.addTriangle (x + w * 0.2f, y + h * 0.12f, x + w * 0.2f, y + h * 0.88f, x + w * 0.86f, y + h * 0.5f); fill = true; break;
        case VbButton::Icon::stop:  p.addRoundedRectangle (r.reduced (w * 0.16f), 1.5f); fill = true; break;
        case VbButton::Icon::rec:   p.addEllipse (r.reduced (w * 0.16f)); fill = true; break;
        case VbButton::Icon::open:
            p.startNewSubPath (x + w * 0.08f, y + h * 0.8f); p.lineTo (x + w * 0.08f, y + h * 0.2f); p.lineTo (x + w * 0.38f, y + h * 0.2f);
            p.lineTo (x + w * 0.48f, y + h * 0.32f); p.lineTo (x + w * 0.92f, y + h * 0.32f); p.lineTo (x + w * 0.92f, y + h * 0.8f); p.closeSubPath();
            break;
        case VbButton::Icon::clear:
            p.startNewSubPath (x + w * 0.22f, y + h * 0.22f); p.lineTo (x + w * 0.78f, y + h * 0.78f);
            p.startNewSubPath (x + w * 0.78f, y + h * 0.22f); p.lineTo (x + w * 0.22f, y + h * 0.78f);
            break;
        case VbButton::Icon::dice:  p.addRoundedRectangle (r.reduced (w * 0.06f), w * 0.2f); break;
        case VbButton::Icon::left:  p.startNewSubPath (x + w * 0.62f, y + h * 0.2f); p.lineTo (x + w * 0.33f, y + h * 0.5f); p.lineTo (x + w * 0.62f, y + h * 0.8f); break;
        case VbButton::Icon::right: p.startNewSubPath (x + w * 0.38f, y + h * 0.2f); p.lineTo (x + w * 0.67f, y + h * 0.5f); p.lineTo (x + w * 0.38f, y + h * 0.8f); break;
        case VbButton::Icon::none:  break;
    }
    if (fill) g.fillPath (p);
    else g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (icon == VbButton::Icon::dice)
    {
        const auto c = r.getCentre();
        const float d = w * 0.22f, s = w * 0.16f;
        for (auto pt : { c, c.translated (-d, -d), c.translated (d, d) }) g.fillEllipse (pt.x - s * 0.5f, pt.y - s * 0.5f, s, s);
    }
}

//==============================================================================
LookAndFeel::LookAndFeel() { setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack); }

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const float cy = (float) y + (float) h * 0.5f;
    const juce::Rectangle<float> track ((float) x, cy - 2.0f, (float) w, 4.0f);
    g.setColour (juce::Colour (0xff24324f));
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (col::gold.withAlpha (0.8f));
    g.fillRoundedRectangle (track.withRight (pos), 2.0f);
    const juce::Rectangle<float> thumb (pos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
    g.setColour (col::gold.withAlpha (0.35f));
    g.fillEllipse (thumb.expanded (3.0f));
    g.setColour (col::gold);
    g.fillEllipse (thumb);
    g.setColour (col::bgMid);
    g.drawEllipse (thumb, 2.0f);
}

//==============================================================================
VbButton::VbButton (const juce::String& text, Style s, Icon i) : juce::Button (text), style (s), icon (i)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void VbButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const float radius = r.getHeight() * 0.5f;
    juce::Colour fg = col::muted;
    switch (style)
    {
        case Style::pill:
            if (on) { g.setColour (accent.withAlpha (0.16f)); g.fillRoundedRectangle (r.reduced (0.5f), radius); fg = accent; }
            else if (icon == Icon::rec) fg = col::rec.withAlpha (0.9f);
            g.setColour (on ? accent : col::border);
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            break;
        case Style::gold:
            g.setGradientFill (juce::ColourGradient (col::gold.brighter (over ? 0.15f : 0.05f), 0, r.getY(), col::gold.darker (0.12f), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);
            fg = col::bgMid;
            break;
        case Style::segment:
            if (on) { g.setColour (juce::Colour (0xff1e2d4b)); g.fillRoundedRectangle (r, radius); fg = accent; }
            else fg = col::muted2;
            break;
        case Style::outline:
            g.setColour (col::border);
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            break;
        case Style::ghost: break;
    }
    if (over && ! on && style != Style::gold) fg = fg.brighter (0.35f);
    if (down) fg = fg.darker (0.2f);
    g.setColour (fg);

    const auto label = getButtonText();
    if (icon != Icon::none && label.isEmpty())
    {
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.44f;
        drawIcon (g, icon, r.withSizeKeepingCentre (s, s));
        return;
    }
    auto font = uiFont (10.5f, true, 0.12f);
    g.setFont (font);
    const float textW = juce::GlyphArrangement::getStringWidth (font, label);
    const float iconW = icon != Icon::none ? 12.0f : 0.0f, gap = icon != Icon::none ? 7.0f : 0.0f;
    float x0 = r.getCentreX() - (textW + iconW + gap) * 0.5f;
    if (icon != Icon::none) { drawIcon (g, icon, juce::Rectangle<float> (x0, r.getCentreY() - 6.0f, 12.0f, 12.0f)); x0 += iconW + gap; }
    g.drawText (label, juce::Rectangle<float> (x0, r.getY(), textW + 2.0f, r.getHeight()), juce::Justification::centredLeft);
}

//==============================================================================
ClipView::ClipView (VoxProcessor& p) : proc (p) {}

void ClipView::refresh()
{
    if (proc.getRenderVersion() != seen) { seen = proc.getRenderVersion(); r = proc.getRendered(); }
    setMouseCursor (r != nullptr ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void ClipView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto st = proc.getClipState();
    g.setColour (col::display);
    g.fillRoundedRectangle (b, 10.0f);

    auto centreText = [&] (const juce::String& big, const juce::String& small, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (uiFont (14.0f, true, 0.2f));
        g.drawText (big, b.withTrimmedBottom (22.0f), juce::Justification::centred);
        g.setColour (col::muted2);
        g.setFont (uiFont (11.5f));
        g.drawText (small, b.withTrimmedTop (30.0f), juce::Justification::centred);
    };

    if (st == VoxProcessor::ClipState::waitingForBar)
        centreText ("REC ARMED - STARTS ON THE NEXT BAR", T ("FL'de çal; vokal bir sonraki bardan itibaren kaydedilir"), col::rec);
    else if (st == VoxProcessor::ClipState::recording)
    {
        const bool blink = (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
        centreText (juce::String ("RECORDING  ") + juce::String (proc.getRecordSeconds(), 1) + " s", T ("Durdurmak için REC'e tekrar bas (en fazla 30 sn)"),
                    col::rec.withAlpha (blink ? 1.0f : 0.5f));
    }
    else if (st == VoxProcessor::ClipState::loading)
        centreText ("LOADING...", {}, col::ivory);
    else if (r == nullptr)
    {
        juce::Path border, dashed;
        border.addRoundedRectangle (b.reduced (6.0f), 8.0f);
        const float dashes[] { 6.0f, 5.0f };
        juce::PathStrokeType (1.1f).createDashedStroke (dashed, border, dashes, 2);
        g.setColour (col::borderHi);
        g.fillPath (dashed);
        if (st == VoxProcessor::ClipState::failed) centreText (proc.getClipMessage(), T ("Başka bir dosya dene"), col::rec);
        else centreText ("DROP A VOCAL HERE  -  OR PRESS REC",
                         T ("Efektler (sequencer + açık pad'ler) işlenir, önüne reverse swell eklenir; sonucu playlist'e sürükle"), col::ivory);
    }

    if (r != nullptr && st != VoxProcessor::ClipState::recording && st != VoxProcessor::ClipState::waitingForBar && st != VoxProcessor::ClipState::loading)
    {
        const auto area = b.reduced (8.0f, 10.0f);
        const int cols = (int) r->peaks.size();
        const int total = r->audio.getNumSamples();
        const float mid = area.getCentreY();
        const float xs = area.getX() + area.getWidth() * (float) r->clipStart / (float) juce::jmax (1, total);
        const float xe = area.getX() + area.getWidth() * (float) r->clipEnd / (float) juce::jmax (1, total);
        for (int x = 0; x < (int) area.getWidth(); x += 2)
        {
            const int c = juce::jlimit (0, cols - 1, (int) ((float) x / area.getWidth() * cols));
            const float h = juce::jmax (1.0f, r->peaks[(size_t) c] * area.getHeight() * 0.5f);
            const float px = area.getX() + (float) x;
            const juce::Colour colr = px < xs ? juce::Colour (0xff8fb3e8) : (px > xe ? col::muted2 : col::gold);
            g.setColour (colr.withAlpha (0.85f));
            g.fillRect (px, mid - h, 1.5f, h * 2.0f);
        }
        // vocal start marker
        if (r->clipStart > 0)
        {
            g.setColour (col::ivory.withAlpha (0.5f));
            g.fillRect (xs, area.getY(), 1.0f, area.getHeight());
            g.setFont (uiFont (9.5f, true, 0.15f));
            g.drawText ("SWELL", juce::Rectangle<float> (area.getX() + 4.0f, area.getY(), 60.0f, 12.0f), juce::Justification::centredLeft);
            g.drawText ("VOCAL", juce::Rectangle<float> (xs + 4.0f, area.getY(), 60.0f, 12.0f), juce::Justification::centredLeft);
        }
        if (proc.isPreviewing())
        {
            g.setColour (col::ivory);
            g.fillRect (area.getX() + area.getWidth() * (float) proc.getPreviewPos(), area.getY(), 1.5f, area.getHeight());
        }
        // chip
        const juce::Rectangle<float> chip (b.getRight() - 222.0f, b.getY() + 6.0f, 214.0f, 22.0f);
        g.setColour (col::bgBottom.withAlpha (0.8f));
        g.fillRoundedRectangle (chip, 11.0f);
        g.setColour (col::gold.withAlpha (0.9f));
        g.drawRoundedRectangle (chip.reduced (0.5f), 11.0f, 1.0f);
        g.setFont (uiFont (10.0f, true, 0.18f));
        g.drawText (st == VoxProcessor::ClipState::rendering ? "RENDERING..." : "HOLD & DRAG TO PLAYLIST", chip, juce::Justification::centred);
    }

    if (fileDragHover)
    {
        g.setColour (col::gold.withAlpha (0.12f));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (col::gold);
        g.drawRoundedRectangle (b.reduced (1.0f), 10.0f, 1.5f);
        g.setFont (uiFont (15.0f, true, 0.25f));
        g.drawText ("DROP THE VOCAL", b, juce::Justification::centred);
    }
}

void ClipView::mouseDown (const juce::MouseEvent&) { dragStarted = false; }

void ClipView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || r == nullptr || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    const auto f = proc.exportRendered();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void ClipView::mouseUp (const juce::MouseEvent&)
{
    if (dragStarted) { dragStarted = false; return; }
    if (r == nullptr) { if (onBrowse && proc.getClipState() != VoxProcessor::ClipState::recording) onBrowse(); return; }
    proc.setPreview (! proc.isPreviewing());
}

//==============================================================================
Pad::Pad (VoxProcessor& p, int f) : proc (p), fx (f)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Pad::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool active = proc.isFxActive (fx);
    if (active)
    {
        g.setColour (col::gold.withAlpha (0.25f));
        g.fillRoundedRectangle (r.expanded (1.0f), 11.0f);
    }
    juce::ColourGradient grad (active ? col::gold.withAlpha (0.9f) : juce::Colour (0xff1a2848), 0, r.getY(),
                               active ? col::gold.darker (0.35f) : juce::Colour (0xff0d172e), 0, r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (selected ? col::gold : col::border);
    g.drawRoundedRectangle (r, 10.0f, selected ? 1.6f : 1.0f);

    const auto fg = active ? col::bgBottom : col::ivory;
    g.setColour (fg);
    g.setFont (uiFont (13.0f, true, 0.12f));
    g.drawText (vfx::fxName (fx), r.reduced (12.0f, 10.0f).removeFromTop (20.0f), juce::Justification::centredLeft);
    g.setColour (active ? col::bgBottom.withAlpha (0.8f) : col::gold);
    g.setFont (uiFont (11.0f, true, 0.06f));
    g.drawText (detail, r.reduced (12.0f, 10.0f).removeFromBottom (18.0f), juce::Justification::centredLeft);
    // latch dot
    if (proc.getParam ("latch") > 0.5f)
    {
        g.setColour (active ? col::bgBottom : col::muted2.withAlpha (0.6f));
        g.fillEllipse (r.getRight() - 16.0f, r.getY() + 10.0f, 6.0f, 6.0f);
    }
}

void Pad::mouseDown (const juce::MouseEvent&)
{
    held = true;
    proc.padDown (fx);
    if (onSelect) onSelect (fx);
}

void Pad::mouseUp (const juce::MouseEvent&)
{
    if (held) proc.padUp (fx);
    held = false;
}

//==============================================================================
SeqGrid::SeqGrid (VoxProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

bool SeqGrid::cellAt (juce::Point<float> p, int& fx, int& step) const
{
    if (p.x < (float) labelW) return false;
    const float rowH = (float) getHeight() / (float) vfx::numFx;
    const float cellW = (float) (getWidth() - labelW) / 16.0f;
    fx = (int) (p.y / rowH);
    step = (int) ((p.x - (float) labelW) / cellW);
    return fx >= 0 && fx < vfx::numFx && step >= 0 && step < 16;
}

void SeqGrid::paint (juce::Graphics& g)
{
    const float rowH = (float) getHeight() / (float) vfx::numFx;
    const float cellW = (float) (getWidth() - labelW) / 16.0f;
    const int cur = proc.getCurrentStep();
    const bool seqOn = proc.getParam ("seqOn") > 0.5f;

    if (cur >= 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect ((float) labelW + cur * cellW, 0.0f, cellW, (float) getHeight());
    }
    for (int f = 0; f < vfx::numFx; ++f)
    {
        const float y = f * rowH;
        const bool act = proc.isFxActive (f);
        g.setColour (act ? col::gold : col::muted);
        g.setFont (uiFont (10.5f, true, 0.14f));
        g.drawText (vfx::fxName (f), juce::Rectangle<float> (4.0f, y, (float) labelW - 8.0f, rowH), juce::Justification::centredLeft);
        const auto bits = proc.getSeqRow (f);
        for (int s = 0; s < 16; ++s)
        {
            juce::Rectangle<float> c ((float) labelW + s * cellW + 2.0f, y + 2.5f, cellW - 4.0f, rowH - 5.0f);
            const bool on = (bits >> s) & 1;
            if (on)
            {
                g.setColour ((seqOn ? col::gold : col::muted2).withAlpha (s == cur ? 1.0f : 0.85f));
                g.fillRoundedRectangle (c, 4.0f);
            }
            else
            {
                g.setColour (s / 4 % 2 == 0 ? juce::Colour (0xff12203d) : juce::Colour (0xff0e1a33));
                g.fillRoundedRectangle (c, 4.0f);
            }
        }
    }
    // beat separators
    g.setColour (col::borderHi);
    for (int s = 4; s < 16; s += 4)
        g.fillRect ((float) labelW + s * cellW - 0.5f, 0.0f, 1.0f, (float) getHeight());
}

void SeqGrid::mouseDown (const juce::MouseEvent& e)
{
    int f, s;
    if (! cellAt (e.position, f, s)) return;
    paintValue = ! ((proc.getSeqRow (f) >> s) & 1);
    proc.setSeqCell (f, s, paintValue);
    lastFx = f; lastStep = s;
    repaint();
}

void SeqGrid::mouseDrag (const juce::MouseEvent& e)
{
    int f, s;
    if (! cellAt (e.position, f, s) || (f == lastFx && s == lastStep)) return;
    proc.setSeqCell (f, s, paintValue);
    lastFx = f; lastStep = s;
    repaint();
}

//==============================================================================
// settings page for one FX
struct ControlDef { enum Kind { seg, slider, toggle } kind; const char* id; const char* label; std::vector<const char*> opts; };

class FxPage : public juce::Component
{
public:
    FxPage (VoxProcessor& p, const char* descriptionUtf8, std::vector<ControlDef> defs) : proc (p), description (T (descriptionUtf8))
    {
        for (auto& d : defs)
        {
            auto* item = items.add (new Item());
            item->def = d;
            if (d.kind == ControlDef::seg)
            {
                for (int i = 0; i < (int) d.opts.size(); ++i)
                {
                    auto* b = item->segs.add (new VbButton (d.opts[(size_t) i], VbButton::Style::segment));
                    b->onClick = [item, i] { item->att->setValueAsCompleteGesture ((float) i); };
                    addAndMakeVisible (b);
                }
                item->att = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (d.id), [item] (float v)
                {
                    for (int i = 0; i < item->segs.size(); ++i) item->segs[i]->setToggleState (i == (int) v, juce::dontSendNotification);
                });
                item->att->sendInitialUpdate();
            }
            else if (d.kind == ControlDef::slider)
            {
                item->slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
                item->slider->onValueChange = [this] { repaint(); };
                addAndMakeVisible (*item->slider);
                item->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, d.id, *item->slider);
            }
            else
            {
                item->toggle = std::make_unique<VbButton> (d.label, VbButton::Style::pill);
                item->toggle->setClickingTogglesState (true);
                addAndMakeVisible (*item->toggle);
                item->bAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, d.id, *item->toggle);
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (col::muted);
        g.setFont (uiFont (11.5f));
        g.drawFittedText (description, juce::Rectangle<int> (0, 0, getWidth(), 34), juce::Justification::topLeft, 2, 1.0f);
        for (auto* it : items)
        {
            if (it->def.kind == ControlDef::toggle) continue;
            g.setColour (col::muted2);
            g.setFont (uiFont (10.0f, true, 0.3f));
            g.drawText (it->def.label, it->labelArea, juce::Justification::centredLeft);
            if (it->slider)
            {
                g.setColour (col::gold);
                g.setFont (uiFont (11.5f, true));
                g.drawText (juce::String ((int) it->slider->getValue()) + (juce::String (it->def.id) == "glitchSeed" ? "" : "%"), it->labelArea, juce::Justification::centredRight);
            }
        }
        for (auto* it : items)
            if (it->segs.size() > 0)
            {
                auto seg = it->segs.getFirst()->getBounds().getUnion (it->segs.getLast()->getBounds()).toFloat().expanded (3.0f);
                g.setColour (col::bgBottom);
                g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
                g.setColour (juce::Colour (0xff1e2d4b));
                g.drawRoundedRectangle (seg.reduced (0.5f), seg.getHeight() * 0.5f, 1.0f);
            }
    }

    void resized() override
    {
        int y = 40;
        for (auto* it : items)
        {
            if (it->def.kind == ControlDef::toggle)
            {
                it->toggle->setBounds (getWidth() - 100, y + 2, 96, 26);
                continue;
            }
            it->labelArea = { 0, y, getWidth(), 14 };
            y += 18;
            if (it->def.kind == ControlDef::seg)
            {
                const int n = it->segs.size();
                const int w = (getWidth() - 6) / n;
                for (int i = 0; i < n; ++i) it->segs[i]->setBounds (3 + i * w, y + 2, w, 24);
                y += 34;
            }
            else
            {
                it->slider->setBounds (0, y, getWidth(), 24);
                y += 32;
            }
        }
    }

    juce::String detailText() const
    {
        if (items.isEmpty()) return {};
        auto* it = items.getFirst();
        if (it->def.kind == ControlDef::seg) return it->def.opts[(size_t) juce::jlimit (0, (int) it->def.opts.size() - 1, (int) proc.getParam (it->def.id))];
        if (it->def.kind == ControlDef::slider) return juce::String ((int) proc.getParam (it->def.id)) + "%";
        return {};
    }

private:
    struct Item
    {
        ControlDef def;
        juce::OwnedArray<VbButton> segs;
        std::unique_ptr<juce::ParameterAttachment> att;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
        std::unique_ptr<VbButton> toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
        juce::Rectangle<int> labelArea;
    };
    VoxProcessor& proc;
    juce::String description;
    juce::OwnedArray<Item> items;
};

//==============================================================================
class VoxEditor::Panel : public juce::Component
{
public:
    Panel (VoxProcessor& p, std::function<void()> onScale, std::function<void()> onBrowse)
        : proc (p), clipView (p), grid (p),
          latchAtt (p.apvts, "latch", latchBtn),
          seqOnAtt (p.apvts, "seqOn", seqOnBtn),
          seqRateAtt (*p.apvts.getParameter ("seqRate"), [this] (float v) { for (int i = 0; i < 3; ++i) rateBtns[i]->setToggleState (i == (int) v, juce::dontSendNotification); }),
          swellAtt (*p.apvts.getParameter ("swell"), [this] (float v) { for (int i = 0; i < 4; ++i) swellBtns[i]->setToggleState (i == (int) v, juce::dontSendNotification); }),
          tailAtt (*p.apvts.getParameter ("tail"), [this] (float v) { for (int i = 0; i < 3; ++i) tailBtns[i]->setToggleState (i == (int) v, juce::dontSendNotification); })
    {
        addAndMakeVisible (clipView);
        clipView.onBrowse = onBrowse;
        addAndMakeVisible (grid);

        pages.add (new FxPage (p, "Bastığın anı grid'e göre tekrarlar. RAMP: 1/4'ten 1/64'e hızlanır (build-up).", { { ControlDef::seg, "stutDiv", "RATE", { "1/2", "1/4", "1/8", "1/16", "1/32", "RAMP" } } }));
        pages.add (new FxPage (p, "Son vuruşu ters çalar.", { { ControlDef::seg, "revDiv", "LENGTH", { "1/2", "1/4", "1/8", "1 BAR" } } }));
        pages.add (new FxPage (p, "Kaset gibi yavaşlayıp durur.", { { ControlDef::seg, "tapeLen", "TIME", { "1/4", "1/2", "1 BAR", "2 BARS" } } }));
        pages.add (new FxPage (p, "Her 1/16'da rastgele kesme, tekrar, ters, oktav düşürme (çart curt). VARIATION farklı bir desen seçer.",
                               { { ControlDef::slider, "glitchAmt", "AMOUNT", {} }, { ControlDef::slider, "glitchSeed", "VARIATION", {} } }));
        pages.add (new FxPage (p, "Vokali perdeden kaydırır. -12 = demon / drop vokali.", { { ControlDef::seg, "pitchSemi", "SEMITONES", { "-12", "-7", "-5", "+5", "+7", "+12" } } }));
        pages.add (new FxPage (p, "HP BUILD yükselen filtre, LP DROP kapanan filtre (1 bar), ACID 1/8 wobble, PHONE telefon sesi.",
                               { { ControlDef::seg, "filtMode", "MODE", { "HP BUILD", "LP DROP", "ACID", "PHONE" } }, { ControlDef::slider, "filtRes", "RESONANCE", {} } }));
        pages.add (new FxPage (p, "Bit ve sample rate düşürür: kirli, dijital ses.", { { ControlDef::slider, "crushAmt", "AMOUNT", {} } }));
        pages.add (new FxPage (p, "Ritmik kesme (trance gate).", { { ControlDef::seg, "gateRate", "RATE", { "1/8", "1/16", "1/32", "1/8T", "1/16T" } }, { ControlDef::slider, "gateDepth", "DEPTH", {} } }));
        pages.add (new FxPage (p, "Bastığın kısmı tempoya göre ping-pong yankıya atar; bırakınca yankı devam eder.",
                               { { ControlDef::seg, "dlyTime", "TIME", { "1/4", "1/8D", "1/8", "1/16" } }, { ControlDef::slider, "dlyFb", "FEEDBACK", {} } }));
        pages.add (new FxPage (p, "Bastığın kısmı büyük reverb'e atar, bırakınca kuyruk kalır. FREEZE reverb'i dondurur.",
                               { { ControlDef::slider, "verbSize", "SIZE", {} }, { ControlDef::toggle, "verbFreeze", "FREEZE", {} } }));
        for (auto* pg : pages) addChildComponent (pg);

        for (int f = 0; f < vfx::numFx; ++f)
        {
            auto* pad = pads.add (new Pad (p, f));
            pad->onSelect = [this] (int fx) { select (fx); };
            addAndMakeVisible (pad);
        }

        const char* rates[] { "1 BAR", "2 BARS", "4 BARS" };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = rateBtns.add (new VbButton (rates[i], VbButton::Style::segment));
            b->onClick = [this, i] { seqRateAtt.setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (b);
        }
        const char* swells[] { "OFF", "1/2", "1 BAR", "2 BARS" };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = swellBtns.add (new VbButton (swells[i], VbButton::Style::segment));
            b->onClick = [this, i] { swellAtt.setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (b);
        }
        const char* tails[] { "OFF", "1 BAR", "2 BARS" };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = tailBtns.add (new VbButton (tails[i], VbButton::Style::segment));
            b->onClick = [this, i] { tailAtt.setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (b);
        }

        latchBtn.setClickingTogglesState (true);
        seqOnBtn.setClickingTogglesState (true);
        recBtn.accent = col::rec;
        recBtn.onClick = [this] { proc.toggleRecord(); };
        openBtn.onClick = onBrowse;
        clearBtn.onClick = [this] { proc.clearClip(); };
        playBtn.onClick = [this] { proc.setPreview (! proc.isPreviewing()); };
        prevPreset.onClick = [this] { presetText = {}; preset = (preset + VoxProcessor::numPresets() - 1) % VoxProcessor::numPresets(); proc.applyPreset (preset); repaint(); };
        nextPreset.onClick = [this] { presetText = {}; preset = (preset + 1) % VoxProcessor::numPresets(); proc.applyPreset (preset); repaint(); };
        randomBtn.onClick = [this] { proc.randomizeSeq(); presetText = "Random"; repaint(); };
        seqClearBtn.onClick = [this] { proc.applyPreset (0); preset = 0; presetText = {}; repaint(); };
        scaleBtn.onClick = std::move (onScale);

        for (auto* c : { &latchBtn, &seqOnBtn, &recBtn, &openBtn, &clearBtn, &playBtn, &prevPreset, &nextPreset, &randomBtn, &seqClearBtn, &scaleBtn })
            addAndMakeVisible (c);

        seqRateAtt.sendInitialUpdate();
        swellAtt.sendInitialUpdate();
        tailAtt.sendInitialUpdate();
        select (0);
        refresh();
    }

    void select (int fx)
    {
        selected = fx;
        for (int i = 0; i < pages.size(); ++i) pages[i]->setVisible (i == fx);
        for (int i = 0; i < pads.size(); ++i) { pads[i]->selected = i == fx; pads[i]->repaint(); }
        repaint();
    }

    void setScaleText (int percent) { scaleBtn.setButtonText (juce::String (percent) + "%"); }
    ClipView& getClip() { return clipView; }

    void refresh()
    {
        clipView.refresh();
        grid.repaint();
        for (int i = 0; i < pads.size(); ++i)
        {
            pads[i]->detail = pages[i]->detailText();
            if (i == vfx::reverb && proc.getParam ("verbFreeze") > 0.5f) pads[i]->detail << "  FREEZE";
            pads[i]->repaint();
        }
        const auto st = proc.getClipState();
        const bool recOn = st == VoxProcessor::ClipState::recording || st == VoxProcessor::ClipState::waitingForBar;
        recBtn.setToggleState (recOn, juce::dontSendNotification);
        recBtn.setButtonText (recOn ? "STOP" : "REC");
        const bool hasClip = proc.getRendered() != nullptr;
        playBtn.setEnabled (hasClip); playBtn.setAlpha (hasClip ? 1.0f : 0.35f);
        playBtn.setToggleState (proc.isPreviewing(), juce::dontSendNotification);
        playBtn.setButtonText (proc.isPreviewing() ? "STOP" : "PREVIEW");
        playBtn.setIcon (proc.isPreviewing() ? VbButton::Icon::stop : VbButton::Icon::play);
        const auto bpm = juce::String (proc.getBpm(), proc.getBpm() == std::floor (proc.getBpm()) ? 0 : 1);
        if (bpm != bpmText) { bpmText = bpm; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        juce::ColourGradient bg (col::bgTop, b.getCentreX(), -60.0f, col::bgBottom, b.getCentreX(), b.getHeight() + 120.0f, true);
        bg.addColour (0.55, col::bgMid);
        g.setGradientFill (bg);
        g.fillRect (b);

        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.035f), 0, 0, juce::Colours::transparentWhite, 0, 64, false));
        g.fillRect (0.0f, 0.0f, b.getWidth(), 64.0f);
        g.setColour (col::line);
        g.fillRect (0.0f, 63.0f, b.getWidth(), 1.0f);
        const auto lf = logoFont (26.0f);
        const float vonW = juce::GlyphArrangement::getStringWidth (lf, "Von");
        g.setFont (lf);
        g.setColour (juce::Colours::white);
        g.drawText ("Von", juce::Rectangle<float> (22.0f, 14.0f, 60.0f, 38.0f), juce::Justification::centredLeft);
        g.setColour (col::gold);
        g.drawText ("Berg", juce::Rectangle<float> (22.0f + vonW, 14.0f, 80.0f, 38.0f), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff2a3a5c));
        g.fillRect (132.0f, 21.0f, 1.0f, 22.0f);
        g.setColour (col::gold);
        g.setFont (uiFont (12.0f, true, 0.4f));
        g.drawText ("VOX FX", juce::Rectangle<float> (146.0f, 18.0f, 100.0f, 28.0f), juce::Justification::centredLeft);
        g.setColour (col::muted2);
        g.setFont (uiFont (11.0f, false, 0.05f));
        g.drawText (T ("Pad'e basılı tut = efekt  ·  FL'de kayda alırsan otomasyon olur"), juce::Rectangle<float> (250.0f, 18.0f, 420.0f, 28.0f), juce::Justification::centredLeft);

        const juce::Rectangle<float> bpmR (790.0f, 16.0f, 120.0f, 32.0f);
        g.setColour (col::well);
        g.fillRoundedRectangle (bpmR, 16.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (bpmR.reduced (0.5f), 16.0f, 1.0f);
        g.setColour (col::muted2);
        g.setFont (uiFont (10.0f, true, 0.3f));
        g.drawText ("BPM", bpmR.withTrimmedLeft (14.0f), juce::Justification::centredLeft);
        g.setColour (col::ivory);
        g.setFont (uiFont (12.5f, true));
        g.drawText (bpmText, bpmR.withTrimmedRight (14.0f), juce::Justification::centredRight);

        // clip well
        const juce::Rectangle<float> well (20.0f, 76.0f, 960.0f, 132.0f);
        g.setColour (juce::Colours::white.withAlpha (0.02f));
        g.fillRoundedRectangle (well, 14.0f);
        g.setColour (juce::Colour (0xff1e2d4b));
        g.drawRoundedRectangle (well.reduced (0.5f), 14.0f, 1.0f);

        auto label = [&] (const juce::String& s, float x, float y, float w = 200.0f)
        {
            g.setColour (col::muted2);
            g.setFont (uiFont (10.0f, true, 0.3f));
            g.drawText (s, juce::Rectangle<float> (x, y, w, 16.0f), juce::Justification::centredLeft);
        };
        label ("REVERSE SWELL", 22.0f, 220.0f);
        label ("TAIL", 410.0f, 220.0f);

        // settings card
        const juce::Rectangle<float> card (640.0f, 252.0f, 340.0f, 158.0f);
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.fillRoundedRectangle (card, 12.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.0f);
        g.setColour (col::gold);
        g.setFont (uiFont (13.0f, true, 0.18f));
        g.drawText (vfx::fxName (selected), juce::Rectangle<float> (656.0f, 262.0f, 300.0f, 18.0f), juce::Justification::centredLeft);

        // sequencer header
        g.setColour (col::line);
        g.fillRect (20.0f, 424.0f, 960.0f, 1.0f);
        g.setColour (col::ivory);
        g.setFont (uiFont (12.0f, true, 0.3f));
        g.drawText ("SEQUENCER", juce::Rectangle<float> (22.0f, 436.0f, 120.0f, 26.0f), juce::Justification::centredLeft);
        const juce::Rectangle<float> pr (472.0f, 436.0f, 200.0f, 26.0f);
        g.setColour (col::well);
        g.fillRoundedRectangle (pr, 13.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (pr.reduced (0.5f), 13.0f, 1.0f);
        g.setColour (col::ivory);
        g.setFont (uiFont (11.5f, true));
        g.drawText (presetText.isNotEmpty() ? presetText : VoxProcessor::presetName (preset), pr, juce::Justification::centred);

        for (auto* group : { &rateBtns, &swellBtns, &tailBtns })
        {
            auto seg = (*group)[0]->getBounds().getUnion ((*group)[group->size() - 1]->getBounds()).toFloat().expanded (3.0f);
            g.setColour (col::bgBottom);
            g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
            g.setColour (juce::Colour (0xff1e2d4b));
            g.drawRoundedRectangle (seg.reduced (0.5f), seg.getHeight() * 0.5f, 1.0f);
        }
    }

    void resized() override
    {
        latchBtn.setBounds (700, 18, 80, 28);
        scaleBtn.setBounds (932, 18, 48, 28);

        recBtn.setBounds (32, 86, 100, 30);
        openBtn.setBounds (32, 124, 100, 30);
        clearBtn.setBounds (32, 162, 100, 30);
        clipView.setBounds (146, 84, 826, 116);

        for (int i = 0; i < 4; ++i) swellBtns[i]->setBounds (142 + i * 58, 217, 58, 24);
        for (int i = 0; i < 3; ++i) tailBtns[i]->setBounds (450 + i * 64, 217, 64, 24);
        playBtn.setBounds (850, 214, 130, 30);

        for (int i = 0; i < pads.size(); ++i)
            pads[i]->setBounds (20 + (i % 5) * 122, 254 + (i / 5) * 80, 116, 74);
        for (auto* pg : pages) pg->setBounds (656, 284, 310, 122);

        seqOnBtn.setBounds (130, 436, 56, 26);
        for (int i = 0; i < 3; ++i) rateBtns[i]->setBounds (204 + i * 76, 437, 76, 24);
        prevPreset.setBounds (440, 436, 28, 26);
        nextPreset.setBounds (676, 436, 28, 26);
        randomBtn.setBounds (720, 434, 120, 30);
        seqClearBtn.setBounds (850, 434, 130, 30);
        grid.setBounds (20, 474, 960, 214);
    }

private:
    VoxProcessor& proc;
    ClipView clipView;
    SeqGrid grid;
    juce::OwnedArray<FxPage> pages;
    juce::OwnedArray<Pad> pads;
    juce::OwnedArray<VbButton> rateBtns, swellBtns, tailBtns;

    VbButton latchBtn    { "LATCH",   VbButton::Style::pill };
    VbButton seqOnBtn    { "ON",      VbButton::Style::pill };
    VbButton recBtn      { "REC",     VbButton::Style::pill, VbButton::Icon::rec };
    VbButton openBtn     { "OPEN",    VbButton::Style::pill, VbButton::Icon::open };
    VbButton clearBtn    { "CLEAR",   VbButton::Style::pill, VbButton::Icon::clear };
    VbButton playBtn     { "PREVIEW", VbButton::Style::pill, VbButton::Icon::play };
    VbButton prevPreset  { "", VbButton::Style::ghost, VbButton::Icon::left };
    VbButton nextPreset  { "", VbButton::Style::ghost, VbButton::Icon::right };
    VbButton randomBtn   { "RANDOM",  VbButton::Style::gold, VbButton::Icon::dice };
    VbButton seqClearBtn { "CLEAR SEQ", VbButton::Style::pill, VbButton::Icon::clear };
    VbButton scaleBtn    { "100%",    VbButton::Style::outline };

    juce::AudioProcessorValueTreeState::ButtonAttachment latchAtt, seqOnAtt;
    juce::ParameterAttachment seqRateAtt, swellAtt, tailAtt;

    int selected = 0, preset = 0;
    juce::String bpmText, presetText;
};

//==============================================================================
VoxEditor::VoxEditor (VoxProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    panel = std::make_unique<Panel> (p,
        [this]
        {
            static const int steps[] { 100, 125, 150, 200 };
            int next = 100;
            for (int i = 0; i < 4; ++i) if (steps[i] == scalePercent) next = steps[(i + 1) % 4];
            applyScale (next);
        },
        [this] { browse(); });
    addAndMakeVisible (*panel);
    panel->setBounds (0, 0, baseW, baseH);
    applyScale ((int) proc.apvts.state.getProperty ("uiScale", 100));
    startTimerHz (30);
}

VoxEditor::~VoxEditor() { setLookAndFeel (nullptr); }

void VoxEditor::applyScale (int percent)
{
    scalePercent = juce::jlimit (50, 300, percent);
    proc.apvts.state.setProperty ("uiScale", scalePercent, nullptr);
    const float s = (float) scalePercent / 100.0f;
    panel->setTransform (juce::AffineTransform::scale (s));
    panel->setScaleText (scalePercent);
    setSize (juce::roundToInt (baseW * s), juce::roundToInt (baseH * s));
}

void VoxEditor::resized() {}
void VoxEditor::timerCallback() { panel->refresh(); }

void VoxEditor::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a vocal", juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.wma;*.aac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadFile (fc.getResult()); });
}

bool VoxEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (VoxProcessor::isSupportedFile (f)) return true;
    return false;
}
void VoxEditor::fileDragEnter (const juce::StringArray&, int, int) { panel->getClip().fileDragHover = true; panel->getClip().repaint(); }
void VoxEditor::fileDragExit (const juce::StringArray&)           { panel->getClip().fileDragHover = false; panel->getClip().repaint(); }
void VoxEditor::filesDropped (const juce::StringArray& files, int, int)
{
    panel->getClip().fileDragHover = false;
    for (auto& f : files) if (VoxProcessor::isSupportedFile (f)) { proc.loadFile (juce::File (f)); break; }
}
