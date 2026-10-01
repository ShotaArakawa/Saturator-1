#pragma once

#include <JuceHeader.h>

//==============================================================================
class Saturator1AudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    Saturator1AudioProcessor();
    ~Saturator1AudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ホストのネイティブなバイパスUI/オートメーションと連携させるため、
    // どのパラメータがバイパスを表すかを明示する
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // パラメータIDの定義
    static constexpr auto bypassID  = "bypass";
    static constexpr auto driveID   = "drive";
    static constexpr auto toneID    = "tone";
    static constexpr auto mixID     = "mix";
    static constexpr auto volumeID  = "volume";

    // Toneの正規化値(0〜1)をローパスのカットオフ周波数に変換する(800Hz * 20^tone → 800Hz〜16kHz)
    static float toneCutoffHz (float toneNorm) { return 800.0f * std::pow (20.0f, toneNorm); }

    juce::AudioProcessorValueTreeState apvts;

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // パラメータへの参照(生ポインタ、所有権はapvts側)
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* driveParam  = nullptr;
    std::atomic<float>* toneParam   = nullptr;
    std::atomic<float>* mixParam    = nullptr;
    std::atomic<float>* volumeParam = nullptr;

    // getBypassParameter() で返す実体(所有権はapvts側)
    juce::RangedAudioParameter* bypassParameter = nullptr;

    // トーン用のローパスフィルター(チャンネルごとに保持、オーバーサンプリング外で処理)
    juce::dsp::IIR::Filter<float> toneFilterL, toneFilterR;

    // 非線形処理(tanhクリッピング)のみをこの区間内で行う4倍オーバーサンプリング
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    // ドライ信号を保持するバッファ(Mix/バイパスクロスフェード用)
    juce::AudioBuffer<float> dryBuffer;

    // オーバーサンプリングのレイテンシ分だけドライ信号を遅延させ、
    // ウェット信号との位相ズレ(コムフィルタ)を防ぐための遅延ライン
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelayLine;

    // パラメータのサンプル単位スムージング(20msランプ)
    juce::SmoothedValue<float> driveSmoothed, toneSmoothed, mixSmoothed, volumeSmoothed;

    // バイパスのクリック音対策(10msクロスフェード。1=エフェクト有効, 0=バイパス)
    juce::SmoothedValue<float> bypassSmoothed;

    // Drive正規化係数の一時バッファ(ブロック内でtanh適用前後の値をやり取りするため)
    std::vector<float> normaliseValues;

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Saturator1AudioProcessor)
};
