#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
Saturator1AudioProcessor::Saturator1AudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    bypassParam = apvts.getRawParameterValue (bypassID);
    driveParam  = apvts.getRawParameterValue (driveID);
    toneParam   = apvts.getRawParameterValue (toneID);
    mixParam    = apvts.getRawParameterValue (mixID);
    volumeParam = apvts.getRawParameterValue (volumeID);

    bypassParameter = apvts.getParameter (bypassID);
}

Saturator1AudioProcessor::~Saturator1AudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout Saturator1AudioProcessor::createParameterLayout()
{
    using Attributes = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { bypassID, 1 }, "Bypass", false));

    // Driveは0〜10のスケールで表示(アナログ機材のダイヤル表記に合わせる)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { driveID, 1 }, "Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f,
        Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v * 10.0f, 1); })
                    .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 10.0f; })));

    // Toneは実際のローパスのカットオフ周波数(800Hz * 20^tone)で表示
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { toneID, 1 }, "Tone",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        Attributes().withStringFromValueFunction ([] (float v, int)
                    {
                        const auto hz = toneCutoffHz (v);
                        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz"
                                             : juce::String (juce::roundToInt (hz)) + " Hz";
                    })
                    .withValueFromStringFunction ([] (const juce::String& t)
                    {
                        auto hz = t.getFloatValue();
                        if (t.containsIgnoreCase ("k"))
                            hz *= 1000.0f;
                        return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (hz, 1.0f) / 800.0f) / std::log (20.0f));
                    })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { mixID, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attributes().withLabel ("%")
                    .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { volumeID, 1 }, "Volume",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        Attributes().withLabel ("dB")
                    .withStringFromValueFunction ([] (float v, int)
                    {
                        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
                    })));

    return { params.begin(), params.end() };
}

//==============================================================================
void Saturator1AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const auto numChannels = (size_t) juce::jmax (1, getTotalNumOutputChannels());

    // --- オーバーサンプリング (factor=2 → 4倍) ---
    // 線形位相のFIRハーフバンドを使い、ドライ信号を単純遅延するだけで全帯域の位相が揃うようにする
    // (IIRだと位相が非線形なため、Mixの中間値で高域にコムフィルタ的なズレが残る)。
    // useIntegerLatency=true でレイテンシを整数サンプルに丸めてドライ遅延を単純化
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    oversampling->reset();

    const auto oversamplingLatency = (int) std::round (oversampling->getLatencyInSamples());
    setLatencySamples (oversamplingLatency);

    // --- トーンフィルター(オーバーサンプリング外、チャンネルごとに1系統) ---
    juce::dsp::ProcessSpec toneSpec;
    toneSpec.sampleRate = sampleRate;
    toneSpec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    toneSpec.numChannels = 1;

    toneFilterL.prepare (toneSpec);
    toneFilterR.prepare (toneSpec);
    toneFilterL.reset();
    toneFilterR.reset();

    // --- ドライ信号の遅延ライン(オーバーサンプリングのレイテンシ分だけ遅延させ、
    //     Mix/バイパスクロスフェード時にウェットと位相を揃える) ---
    dryDelayLine.setMaximumDelayInSamples (oversamplingLatency + 8);

    juce::dsp::ProcessSpec delaySpec;
    delaySpec.sampleRate = sampleRate;
    delaySpec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    delaySpec.numChannels = (juce::uint32) numChannels;

    dryDelayLine.prepare (delaySpec);
    dryDelayLine.setDelay ((float) oversamplingLatency);
    dryDelayLine.reset();

    // --- パラメータのサンプル単位スムージング(20msランプ) ---
    constexpr double rampSeconds = 0.02;
    driveSmoothed.reset (sampleRate, rampSeconds);
    toneSmoothed.reset (sampleRate, rampSeconds);
    mixSmoothed.reset (sampleRate, rampSeconds);
    volumeSmoothed.reset (sampleRate, rampSeconds);

    // --- バイパスのクリック音対策(10msクロスフェード) ---
    bypassSmoothed.reset (sampleRate, 0.01);

    driveSmoothed.setCurrentAndTargetValue (driveParam->load());
    toneSmoothed.setCurrentAndTargetValue (toneParam->load());
    mixSmoothed.setCurrentAndTargetValue (mixParam->load() / 100.0f);
    volumeSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (volumeParam->load()));
    bypassSmoothed.setCurrentAndTargetValue (bypassParam->load() > 0.5f ? 0.0f : 1.0f);

    dryBuffer.setSize ((int) numChannels, samplesPerBlock);
    normaliseValues.resize ((size_t) samplesPerBlock);
}

void Saturator1AudioProcessor::releaseResources()
{
    if (oversampling != nullptr)
        oversampling->reset();

    dryDelayLine.reset();
    toneFilterL.reset();
    toneFilterR.reset();
}

bool Saturator1AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void Saturator1AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    // バイパス時もチェーンは動かし続け、10msクロスフェードでドライ/ウェットを
    // 切り替えることでクリック音を防ぐ(early returnはしない)
    const bool bypassRequested = bypassParam->load() > 0.5f;

    driveSmoothed.setTargetValue (driveParam->load());
    toneSmoothed.setTargetValue (toneParam->load());
    mixSmoothed.setTargetValue (mixParam->load() / 100.0f);
    volumeSmoothed.setTargetValue (juce::Decibels::decibelsToGain (volumeParam->load()));
    bypassSmoothed.setTargetValue (bypassRequested ? 0.0f : 1.0f);

    if ((int) normaliseValues.size() < numSamples)
        normaliseValues.resize ((size_t) numSamples);

    // --- ドライ信号をオーバーサンプリングのレイテンシ分だけ遅延させて保持
    //     (これをしないとMixが中間値のときにコムフィルタが発生する) ---
    dryBuffer.setSize (numChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* in  = buffer.getReadPointer (ch);
        auto* out = dryBuffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            dryDelayLine.pushSample (ch, in[i]);
            out[i] = dryDelayLine.popSample (ch);
        }
    }

    // --- Drive: 事前ゲイン(線形、サンプル単位でスムージング) ---
    // 正規化係数は後段(オーバーサンプリング区間の外)でtanh適用後に掛けるため保持しておく
    auto channelData = buffer.getArrayOfWritePointers();
    for (int i = 0; i < numSamples; ++i)
    {
        const float driveNorm = driveSmoothed.getNextValue();
        const float driveAmount = 1.0f + driveNorm * 24.0f;
        normaliseValues[(size_t) i] = 1.0f / std::tanh (driveAmount);

        for (int ch = 0; ch < numChannels; ++ch)
            channelData[ch][i] *= driveAmount;
    }

    // --- 非線形処理(tanhクリッピング)のみをオーバーサンプリング区間内で行う ---
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto oversampledBlock = oversampling->processSamplesUp (block);

        const auto numOSChannels = oversampledBlock.getNumChannels();
        const auto numOSSamples  = oversampledBlock.getNumSamples();

        for (size_t ch = 0; ch < numOSChannels; ++ch)
        {
            auto* data = oversampledBlock.getChannelPointer (ch);

            for (size_t i = 0; i < numOSSamples; ++i)
                data[i] = std::tanh (data[i]);
        }

        oversampling->processSamplesDown (block);
    }

    // --- ドライブ量に応じた正規化(オーバーサンプリング区間外) ---
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = channelData[ch];

        for (int i = 0; i < numSamples; ++i)
            data[i] *= normaliseValues[(size_t) i];
    }

    // --- Toneフィルター(ブロック単位で係数を更新。オーバーサンプリング外)
    //     toneSmoothedが20msでランプするため、ブロック単位更新でも急変ノイズは出ない。
    //     ArrayCoefficientsはstd::arrayを返すのでオーディオスレッドでヒープ確保が発生しない ---
    const float toneNormBlock = toneSmoothed.skip (numSamples);
    const float cutoffHz = juce::jmin (toneCutoffHz (toneNormBlock), (float) currentSampleRate * 0.45f);
    const auto coeffs = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (currentSampleRate, cutoffHz);
    *toneFilterL.coefficients = coeffs;
    *toneFilterR.coefficients = coeffs;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = channelData[ch];
        auto& filter = (ch == 0 ? toneFilterL : toneFilterR);

        for (int i = 0; i < numSamples; ++i)
            data[i] = filter.processSample (data[i]);
    }

    // --- Mix(ドライ/ウェットのブレンド) + Volume + バイパスクロスフェード ---
    for (int i = 0; i < numSamples; ++i)
    {
        const float mixNorm    = mixSmoothed.getNextValue();
        const float volumeGain = volumeSmoothed.getNextValue();
        const float active     = bypassSmoothed.getNextValue(); // 1=エフェクト有効 / 0=バイパス

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = dryBuffer.getReadPointer (ch)[i];
            float wet = channelData[ch][i] * mixNorm + dry * (1.0f - mixNorm);
            wet *= volumeGain;

            channelData[ch][i] = wet * active + dry * (1.0f - active);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* Saturator1AudioProcessor::createEditor()
{
    return new Saturator1AudioProcessorEditor (*this);
}

//==============================================================================
void Saturator1AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void Saturator1AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Saturator1AudioProcessor();
}
