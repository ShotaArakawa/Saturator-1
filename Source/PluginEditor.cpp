#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace Sat1Colours
{
    const juce::Colour panelTop     { 0xff2e2a27 };
    const juce::Colour panelBottom  { 0xff181615 };
    const juce::Colour header       { 0xff121110 };
    const juce::Colour cream        { 0xffe9dcbf };
    const juce::Colour creamDim     { 0xffa3967c };
    const juce::Colour red          { 0xffd2421f };
    const juce::Colour redGlow      { 0xffff7a45 };
    const juce::Colour redOff       { 0xff3d1009 };
    const juce::Colour knobCapLight { 0xff45403c };
    const juce::Colour knobCapDark  { 0xff0b0a09 };
    const juce::Colour metalLight   { 0xffd9d6cf };
    const juce::Colour metalDark    { 0xff6e6a64 };
}

namespace
{
    juce::Font sansFont (float height, bool bold = true, float kerning = 0.0f)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height,
                                              bold ? juce::Font::bold : juce::Font::plain))
                   .withExtraKerningFactor (kerning);
    }

    juce::Font serifFont (float height, int styleFlags)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), height, styleFlags));
    }

    // 掘り込み風の線(暗い線 + 1px下に明るいハイライト)
    void drawEngravedLine (juce::Graphics& g, juce::Line<float> line, float thickness)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawLine (line, thickness);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawLine ({ line.getStart().translated (thickness, thickness),
                      line.getEnd().translated (thickness, thickness) }, thickness);
    }

    // パネルの地に乗せるテクスチャ(粒状ノイズ + 横方向のヘアライン)。
    // 基準サイズで一度だけ生成し、描画時に拡縮して使う
    juce::Image createPanelTexture (int width, int height)
    {
        juce::Image image (juce::Image::ARGB, width, height, true);
        juce::Random random (0x5a71);

        {
            juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < height; ++y)
            {
                // ヘアライン: 行ごとにわずかに明るさを変える
                const auto rowShade = random.nextFloat() * 0.035f;

                for (int x = 0; x < width; ++x)
                {
                    const auto grain = random.nextFloat();
                    const auto alpha = rowShade + grain * 0.05f;
                    const auto colour = grain > 0.5f ? juce::Colours::white : juce::Colours::black;
                    data.setPixelColour (x, y, colour.withAlpha (alpha));
                }
            }
        }

        return image;
    }
}

//==============================================================================
Saturator1LookAndFeel::Saturator1LookAndFeel()
{
    setColour (juce::Slider::thumbColourId, Sat1Colours::cream);
    setColour (juce::Label::textColourId, Sat1Colours::cream);
    setColour (juce::BubbleComponent::backgroundColourId, Sat1Colours::header.withAlpha (0.94f));
    setColour (juce::BubbleComponent::outlineColourId, Sat1Colours::creamDim.withAlpha (0.6f));
    setColour (juce::TooltipWindow::textColourId, Sat1Colours::cream);
}

juce::Font Saturator1LookAndFeel::getSliderPopupFont (juce::Slider&)
{
    return sansFont (14.0f, true, 0.05f);
}

int Saturator1LookAndFeel::getSliderPopupPlacement (juce::Slider&)
{
    return juce::BubbleComponent::above;
}

void Saturator1LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                              juce::Slider&)
{
    auto fullBounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    auto side = juce::jmin (fullBounds.getWidth(), fullBounds.getHeight());
    auto bounds = fullBounds.withSizeKeepingCentre (side, side).reduced (side * 0.04f);
    auto radius = bounds.getWidth() * 0.5f;
    auto centre = bounds.getCentre();
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // 落ち影
    {
        juce::Path shadowPath;
        shadowPath.addEllipse (bounds);
        juce::DropShadow (juce::Colours::black.withAlpha (0.75f),
                          juce::roundToInt (radius * 0.28f),
                          { 0, juce::roundToInt (radius * 0.1f) }).drawForPath (g, shadowPath);
    }

    // スカート(上から光が当たっている金属調の外周)
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4d4945), centre.x, bounds.getY(),
                                             juce::Colour (0xff121110), centre.x, bounds.getBottom(), false));
    g.fillEllipse (bounds);

    // ローレット(外周の刻み)
    {
        constexpr int numRidges = 64;
        const auto ridgeThickness = juce::jmax (1.0f, radius * 0.022f);

        for (int i = 0; i < numRidges; ++i)
        {
            const auto a = juce::MathConstants<float>::twoPi * (float) i / (float) numRidges;
            const auto p1 = centre.getPointOnCircumference (radius * 0.80f, a);
            const auto p2 = centre.getPointOnCircumference (radius * 0.985f, a);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawLine ({ p1, p2 }, ridgeThickness);
        }
    }

    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawEllipse (bounds, juce::jmax (1.0f, radius * 0.02f));

    // キャップ(ベークライト風。左上からの光を放射グラデーションで表現)
    auto cap = bounds.reduced (radius * 0.2f);
    const auto capRadius = cap.getWidth() * 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (cap.expanded (radius * 0.035f));

    g.setGradientFill (juce::ColourGradient (Sat1Colours::knobCapLight,
                                             centre.x - capRadius * 0.35f, centre.y - capRadius * 0.45f,
                                             Sat1Colours::knobCapDark,
                                             centre.x + capRadius * 0.6f, centre.y + capRadius * 0.9f, true));
    g.fillEllipse (cap);

    // キャップ上面の縁のハイライト
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), centre.x, cap.getY(),
                                             juce::Colours::transparentWhite, centre.x, cap.getCentreY(), false));
    g.drawEllipse (cap.reduced (0.5f), juce::jmax (1.0f, radius * 0.025f));

    // ポインター(クリーム色のライン。キャップからスカートまで通して刻む)
    {
        const auto thickness = juce::jmax (2.0f, radius * 0.07f);
        const auto inner = centre.getPointOnCircumference (capRadius * 0.18f, angle);
        const auto outer = centre.getPointOnCircumference (radius * 0.94f, angle);

        juce::Path pointer;
        pointer.startNewSubPath (inner);
        pointer.lineTo (outer);
        const juce::PathStrokeType stroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.strokePath (pointer, stroke, juce::AffineTransform::translation (0.0f, thickness * 0.35f));
        g.setColour (Sat1Colours::cream);
        g.strokePath (pointer, stroke);
    }
}

void Saturator1LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                              bool shouldDrawButtonAsHighlighted, bool)
{
    auto bounds = button.getLocalBounds().toFloat();
    const auto w = bounds.getWidth();
    const bool isBypassed = button.getToggleState();

    // --- パイロットランプ(エフェクト有効時に赤く点灯)。ボタンの領域内に収める ---
    const auto lampDiameter = w * 0.36f;
    auto lampArea = bounds.removeFromTop (lampDiameter + w * 0.34f);
    auto lamp = lampArea.withSizeKeepingCentre (lampDiameter, lampDiameter);

    g.setGradientFill (juce::ColourGradient (Sat1Colours::metalLight, lamp.getX(), lamp.getY(),
                                             Sat1Colours::metalDark, lamp.getRight(), lamp.getBottom(), false));
    g.fillEllipse (lamp.expanded (lampDiameter * 0.14f));
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (lamp.expanded (lampDiameter * 0.14f), 1.0f);

    if (! isBypassed)
    {
        g.setColour (Sat1Colours::redGlow.withAlpha (0.18f));
        g.fillEllipse (lamp.expanded (lampDiameter * 0.45f));
        g.setGradientFill (juce::ColourGradient (Sat1Colours::redGlow.brighter (0.4f), lamp.getCentreX(), lamp.getCentreY() - lampDiameter * 0.15f,
                                                 Sat1Colours::red.darker (0.5f), lamp.getRight(), lamp.getBottom(), true));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (Sat1Colours::redOff.brighter (0.3f), lamp.getCentreX(), lamp.getCentreY() - lampDiameter * 0.15f,
                                                 Sat1Colours::redOff.darker (0.6f), lamp.getRight(), lamp.getBottom(), true));
    }

    g.fillEllipse (lamp);
    g.setColour (juce::Colours::white.withAlpha (isBypassed ? 0.12f : 0.45f));
    g.fillEllipse (lamp.getX() + lampDiameter * 0.25f, lamp.getY() + lampDiameter * 0.15f,
                   lampDiameter * 0.28f, lampDiameter * 0.2f);

    // --- トグルスイッチ(上=IN / 下=OUT) ---
    const auto textHeight = w * 0.2f;
    auto inText  = bounds.removeFromTop (textHeight);
    auto outText = bounds.removeFromBottom (textHeight);

    g.setFont (sansFont (textHeight * 0.85f, true, 0.1f));
    g.setColour (isBypassed ? Sat1Colours::creamDim : Sat1Colours::cream);
    g.drawText ("IN", inText, juce::Justification::centred);
    g.setColour (isBypassed ? Sat1Colours::cream : Sat1Colours::creamDim);
    g.drawText ("OUT", outText, juce::Justification::centred);

    const auto centre = bounds.getCentre();

    // 取付プレート
    auto plate = bounds.withSizeKeepingCentre (w * 0.5f, bounds.getHeight() * 0.92f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (plate.translated (0.0f, 2.0f), w * 0.06f);
    g.setGradientFill (juce::ColourGradient (Sat1Colours::metalLight.darker (0.2f), plate.getX(), plate.getY(),
                                             Sat1Colours::metalDark.darker (0.4f), plate.getRight(), plate.getBottom(), false));
    g.fillRoundedRectangle (plate, w * 0.06f);

    // ナット
    const auto nutDiameter = w * 0.36f;
    auto nut = juce::Rectangle<float> (nutDiameter, nutDiameter).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (Sat1Colours::metalLight, nut.getX(), nut.getY(),
                                             Sat1Colours::metalDark.darker (0.5f), nut.getRight(), nut.getBottom(), false));
    g.fillEllipse (nut);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (nut, 1.0f);

    // バットハンドル(根元が太く先端に向かって細くなる)
    const auto direction = isBypassed ? 1.0f : -1.0f;
    const auto length = plate.getHeight() * 0.48f;
    const auto tip = centre.translated (0.0f, direction * length);
    const auto baseWidth = w * 0.14f;
    const auto tipWidth  = w * 0.2f;

    juce::Path lever;
    lever.startNewSubPath (centre.x - baseWidth * 0.5f, centre.y);
    lever.lineTo (tip.x - tipWidth * 0.32f, tip.y);
    lever.lineTo (tip.x + tipWidth * 0.32f, tip.y);
    lever.lineTo (centre.x + baseWidth * 0.5f, centre.y);
    lever.closeSubPath();

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillPath (lever, juce::AffineTransform::translation (w * 0.04f, w * 0.05f));
    g.setGradientFill (juce::ColourGradient (Sat1Colours::metalDark, centre.x - baseWidth, 0.0f,
                                             Sat1Colours::metalLight, centre.x + baseWidth * 0.2f, 0.0f, false));
    g.fillPath (lever);

    auto ball = juce::Rectangle<float> (tipWidth, tipWidth).withCentre (tip);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, ball.getX() + tipWidth * 0.3f, ball.getY() + tipWidth * 0.3f,
                                             Sat1Colours::metalDark, ball.getRight(), ball.getBottom(), true));
    g.fillEllipse (ball);

    if (shouldDrawButtonAsHighlighted)
    {
        g.setColour (Sat1Colours::cream.withAlpha (0.08f));
        g.fillRoundedRectangle (plate, w * 0.06f);
    }
}

//==============================================================================
Saturator1AudioProcessorEditor::Saturator1AudioProcessorEditor (Saturator1AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    panelTexture = createPanelTexture (defaultWidth, defaultHeight);

    setupKnob (driveSlider,  Saturator1AudioProcessor::driveID);
    setupKnob (toneSlider,   Saturator1AudioProcessor::toneID);
    setupKnob (mixSlider,    Saturator1AudioProcessor::mixID);
    setupKnob (volumeSlider, Saturator1AudioProcessor::volumeID);

    // 大きいDriveノブは操作量に対して回りすぎないよう感度を下げる
    driveSlider.setMouseDragSensitivity (400);

    auto& apvts = audioProcessor.apvts;
    driveAttachment  = std::make_unique<SliderAttachment> (apvts, Saturator1AudioProcessor::driveID,  driveSlider);
    toneAttachment   = std::make_unique<SliderAttachment> (apvts, Saturator1AudioProcessor::toneID,   toneSlider);
    mixAttachment    = std::make_unique<SliderAttachment> (apvts, Saturator1AudioProcessor::mixID,    mixSlider);
    volumeAttachment = std::make_unique<SliderAttachment> (apvts, Saturator1AudioProcessor::volumeID, volumeSlider);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTitle ("Bypass");
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (apvts, Saturator1AudioProcessor::bypassID, bypassButton);

    // ダブルクリックでデフォルト値に戻す(アタッチメントがレンジを設定した後に行う)
    for (auto [slider, id] : { std::pair { &driveSlider,  Saturator1AudioProcessor::driveID },
                               std::pair { &toneSlider,   Saturator1AudioProcessor::toneID },
                               std::pair { &mixSlider,    Saturator1AudioProcessor::mixID },
                               std::pair { &volumeSlider, Saturator1AudioProcessor::volumeID } })
    {
        if (auto* param = apvts.getParameter (id))
            slider->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (defaultWidth * 0.6), juce::roundToInt (defaultHeight * 0.6),
                     juce::roundToInt (defaultWidth * 1.6), juce::roundToInt (defaultHeight * 1.6));
    getConstrainer()->setFixedAspectRatio ((double) defaultWidth / (double) defaultHeight);

    setSize (defaultWidth, defaultHeight);
}

Saturator1AudioProcessorEditor::~Saturator1AudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void Saturator1AudioProcessorEditor::setupKnob (juce::Slider& slider, const juce::String& paramID)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (rotaryStart, rotaryEnd, true);

    // アナログ機材らしく数値ボックスは置かず、ホバー/ドラッグ中だけ値をポップアップ表示する
    slider.setPopupDisplayEnabled (true, true, this);

    if (auto* param = audioProcessor.apvts.getParameter (paramID))
        slider.setTitle (param->getName (32));

    addAndMakeVisible (slider);
}

juce::Rectangle<float> Saturator1AudioProcessorEditor::scaled (float x, float y, float w, float h) const
{
    return { x * scale, y * scale, w * scale, h * scale };
}

//==============================================================================
void Saturator1AudioProcessorEditor::drawEngravedText (juce::Graphics& g, const juce::String& text,
                                                       juce::Rectangle<float> area, float fontHeight,
                                                       juce::Justification justification, juce::Colour colour) const
{
    g.setFont (sansFont (fontHeight * scale, true, 0.18f));
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawText (text, area.translated (0.0f, scale), justification, false);
    g.setColour (colour);
    g.drawText (text, area, justification, false);
}

void Saturator1AudioProcessorEditor::drawScrew (juce::Graphics& g, juce::Point<float> centre, float angle) const
{
    const auto d = 13.0f * scale;
    auto screw = juce::Rectangle<float> (d, d).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (screw.expanded (1.5f * scale).translated (0.0f, scale));
    g.setGradientFill (juce::ColourGradient (Sat1Colours::metalLight, screw.getX(), screw.getY(),
                                             Sat1Colours::metalDark.darker (0.6f), screw.getRight(), screw.getBottom(), false));
    g.fillEllipse (screw);

    // マイナス溝
    const auto slotA = centre.getPointOnCircumference (d * 0.38f, angle);
    const auto slotB = centre.getPointOnCircumference (d * 0.38f, angle + juce::MathConstants<float>::pi);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawLine ({ slotA, slotB }, 2.0f * scale);
}

void Saturator1AudioProcessorEditor::drawKnobScale (juce::Graphics& g, const juce::Slider& slider,
                                                    const juce::StringArray& numbers,
                                                    const juce::String& minText, const juce::String& maxText) const
{
    auto bounds = slider.getBounds().toFloat();
    const auto centre = bounds.getCentre();
    const auto knobRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.48f;

    constexpr int numTicks = 21;

    for (int i = 0; i < numTicks; ++i)
    {
        const auto proportion = (float) i / (float) (numTicks - 1);
        const auto angle = rotaryStart + proportion * (rotaryEnd - rotaryStart);
        const bool isMajor = (i % 2) == 0;

        const auto inner = knobRadius + 5.0f * scale;
        const auto outer = knobRadius + (isMajor ? 12.0f : 8.5f) * scale;

        g.setColour (isMajor ? Sat1Colours::cream : Sat1Colours::creamDim.withAlpha (0.7f));
        g.drawLine ({ centre.getPointOnCircumference (inner, angle),
                      centre.getPointOnCircumference (outer, angle) },
                    (isMajor ? 1.6f : 1.0f) * scale);
    }

    // 目盛り数字(Driveのみ 0〜10 を刻む)
    if (! numbers.isEmpty())
    {
        g.setFont (sansFont (12.0f * scale, true));
        g.setColour (Sat1Colours::cream);

        for (int i = 0; i < numbers.size(); ++i)
        {
            const auto proportion = (float) i / (float) (numbers.size() - 1);
            const auto angle = rotaryStart + proportion * (rotaryEnd - rotaryStart);
            const auto pos = centre.getPointOnCircumference (knobRadius + 24.0f * scale, angle);
            g.drawText (numbers[i], juce::Rectangle<float> (28.0f * scale, 16.0f * scale).withCentre (pos),
                        juce::Justification::centred, false);
        }
    }

    // 可動域の両端の表記(DARK/BRIGHT など)
    if (minText.isNotEmpty() || maxText.isNotEmpty())
    {
        g.setFont (sansFont (10.0f * scale, true, 0.08f));
        g.setColour (Sat1Colours::creamDim);

        const auto textRadius = knobRadius + 14.0f * scale;
        const auto minPos = centre.getPointOnCircumference (textRadius, rotaryStart).translated (0.0f, 9.0f * scale);
        const auto maxPos = centre.getPointOnCircumference (textRadius, rotaryEnd).translated (0.0f, 9.0f * scale);
        const auto textArea = juce::Rectangle<float> (60.0f * scale, 14.0f * scale);

        g.drawText (minText, textArea.withCentre (minPos), juce::Justification::centred, false);
        g.drawText (maxText, textArea.withCentre (maxPos), juce::Justification::centred, false);
    }
}

//==============================================================================
void Saturator1AudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // --- パネル地(上から光が当たったマットな黒塗装 + テクスチャ) ---
    g.setGradientFill (juce::ColourGradient (Sat1Colours::panelTop, 0.0f, 0.0f,
                                             Sat1Colours::panelBottom, 0.0f, bounds.getBottom(), false));
    g.fillAll();
    g.drawImage (panelTexture, bounds, juce::RectanglePlacement::stretchToFit);

    // 周囲のベベル
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRect (bounds.reduced (1.0f), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRect (bounds, 1.0f);

    // --- ヘッダー(ロゴ) ---
    auto headerArea = scaled (0.0f, 0.0f, (float) defaultWidth, 74.0f);
    g.setGradientFill (juce::ColourGradient (Sat1Colours::header.withAlpha (0.0f), 0.0f, headerArea.getY(),
                                             Sat1Colours::header.withAlpha (0.55f), 0.0f, headerArea.getBottom(), false));
    g.fillRect (headerArea);
    drawEngravedLine (g, { scaled (24, 74, 0, 0).getPosition(), scaled (736, 74, 0, 0).getPosition() }, 1.2f * scale);

    {
        juce::AttributedString logo;
        logo.append ("Saturator", serifFont (42.0f * scale, juce::Font::bold | juce::Font::italic), Sat1Colours::cream);
        logo.append ("-1", serifFont (42.0f * scale, juce::Font::bold | juce::Font::italic), Sat1Colours::red);
        logo.setJustification (juce::Justification::centredLeft);

        auto logoArea = scaled (40.0f, 12.0f, 400.0f, 56.0f);
        juce::TextLayout layout;
        layout.createLayout (logo, logoArea.getWidth());

        // 文字の下にわずかに影を落としてシルク印刷っぽい厚みを出す
        juce::AttributedString shadow (logo);
        shadow.setColour (juce::Colours::black.withAlpha (0.7f));
        juce::TextLayout shadowLayout;
        shadowLayout.createLayout (shadow, logoArea.getWidth());
        shadowLayout.draw (g, logoArea.translated (0.0f, 2.0f * scale));
        layout.draw (g, logoArea);
    }

    drawEngravedText (g, "ANALOG SATURATION STAGE", scaled (420, 22, 300, 18), 12.0f,
                      juce::Justification::centredRight, Sat1Colours::cream);
    drawEngravedText (g, "AUDION  /  MODEL S-1", scaled (420, 42, 300, 16), 10.0f,
                      juce::Justification::centredRight, Sat1Colours::creamDim);

    // --- セクションの仕切り線 ---
    drawEngravedLine (g, { scaled (232, 100, 0, 0).getPosition(), scaled (232, 410, 0, 0).getPosition() }, 1.2f * scale);
    drawEngravedLine (g, { scaled (528, 100, 0, 0).getPosition(), scaled (528, 410, 0, 0).getPosition() }, 1.2f * scale);

    // --- ノブの目盛りとラベル ---
    drawKnobScale (g, driveSlider, { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" }, {}, {});
    drawKnobScale (g, toneSlider,   {}, "DARK", "BRIGHT");
    drawKnobScale (g, mixSlider,    {}, "DRY", "WET");
    drawKnobScale (g, volumeSlider, {}, "-24", "+12");

    drawEngravedText (g, "DRIVE",  scaled (280, 394, 200, 26), 22.0f, juce::Justification::centred, Sat1Colours::cream);
    drawEngravedText (g, "TONE",   scaled (75, 240, 120, 20),  14.0f, juce::Justification::centred, Sat1Colours::cream);
    drawEngravedText (g, "MIX",    scaled (565, 240, 120, 20), 14.0f, juce::Justification::centred, Sat1Colours::cream);
    drawEngravedText (g, "OUTPUT", scaled (565, 398, 120, 20), 14.0f, juce::Justification::centred, Sat1Colours::cream);
    drawEngravedText (g, "BYPASS", scaled (75, 398, 120, 20),  14.0f, juce::Justification::centred, Sat1Colours::cream);

    // --- 四隅のネジ ---
    drawScrew (g, scaled (16, 16, 0, 0).getPosition(),   0.6f);
    drawScrew (g, scaled (744, 16, 0, 0).getPosition(),  2.1f);
    drawScrew (g, scaled (16, 424, 0, 0).getPosition(),  1.2f);
    drawScrew (g, scaled (744, 424, 0, 0).getPosition(), 0.2f);
}

void Saturator1AudioProcessorEditor::resized()
{
    // 現在のウィンドウサイズと基準サイズ(760x440)との比率で全要素を拡縮する
    scale = (float) getWidth() / (float) defaultWidth;

    driveSlider.setBounds  (scaled (260, 122, 240, 240).toNearestInt());
    toneSlider.setBounds   (scaled (80, 112, 110, 110).toNearestInt());
    mixSlider.setBounds    (scaled (570, 112, 110, 110).toNearestInt());
    volumeSlider.setBounds (scaled (570, 272, 110, 110).toNearestInt());
    bypassButton.setBounds (scaled (100, 268, 70, 124).toNearestInt());
}
