#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// アナログ機材(ヴィンテージのアウトボード)風の見た目にするためのカスタムLookAndFeel
class Saturator1LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    Saturator1LookAndFeel();

    // ローレット加工のスカート付きベークライト風ノブ
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    // パイロットランプ + バットハンドルのトグルスイッチ(バイパス)
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    juce::Font getSliderPopupFont (juce::Slider&) override;
    int getSliderPopupPlacement (juce::Slider&) override;
};

//==============================================================================
class Saturator1AudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit Saturator1AudioProcessorEditor (Saturator1AudioProcessor&);
    ~Saturator1AudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int defaultWidth  = 760;
    static constexpr int defaultHeight = 440;

    // ノブの可動域(7時〜5時)。目盛りの描画とスライダーで共有する
    static constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float rotaryEnd   = juce::MathConstants<float>::pi * 2.75f;

    Saturator1AudioProcessor& audioProcessor;
    Saturator1LookAndFeel lookAndFeel;

    juce::Slider driveSlider, toneSlider, mixSlider, volumeSlider;
    juce::ToggleButton bypassButton;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> driveAttachment, toneAttachment, mixAttachment, volumeAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    // パネルの地(ノイズ + ヘアライン)。リサイズ時に生成してキャッシュする
    juce::Image panelTexture;

    float scale = 1.0f;

    void setupKnob (juce::Slider& slider, const juce::String& paramID);

    // 基準サイズ(760x440)上の座標を現在のウィンドウサイズに合わせて拡縮する
    juce::Rectangle<float> scaled (float x, float y, float w, float h) const;

    void drawScrew (juce::Graphics&, juce::Point<float> centre, float angle) const;
    void drawKnobScale (juce::Graphics&, const juce::Slider&, const juce::StringArray& numbers,
                        const juce::String& minText, const juce::String& maxText) const;
    void drawEngravedText (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                           float fontHeight, juce::Justification, juce::Colour) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Saturator1AudioProcessorEditor)
};
