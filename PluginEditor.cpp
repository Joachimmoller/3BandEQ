#include "PluginProcessor.h"
#include "PluginEditor.h"
//==============================================================================
void LookAndFeel::drawRotarySlider(juce::Graphics & g, 
                                   int x, 
                                   int y, 
                                   int width, 
                                   int height, 
                                   float sliderPosProportional, 
                                   float rotaryStartAngle, 
                                   float rotaryEndAngle,
                                   juce::Slider & slider) 
{
  using namespace juce;
  
  auto bounds = Rectangle<float>(x, y, width, height);
  
  g.setColour(Colour(97u, 18u, 167u));
  g.fillEllipse(bounds);
  
  g.setColour(Colour(225u,154u,1u));
  g.drawEllipse(bounds, 1.f);
  
  auto center = bounds.getCentre();
  
  Path p;
  
  Rectangle<float> r;
  r.setLeft(center.getX() - 2);
  r.setRight(center.getX() + 2);
  r.setTop(bounds.getY());
  r.setBottom(center.getY());
  
  p.addRectangle(r);

  jassert(rotaryStartAngle < rotaryEndAngle);
  
  auto sliderAngRad = jmap(sliderPosProportional, 0.f, 1.f, rotaryStartAngle,rotaryEndAngle);
  
  p.applyTransform(AffineTransform().rotated(sliderAngRad, center.getX(), center.getY()));
  
  g.fillPath(p);
  
}

//==============================================================================

void RotarySliderWithLabels::paint(juce::Graphics &g) 
{
    using namespace juce;
    
	auto startAng = degreesToRadians(fabs(180.f + 45.f));
        auto endAng = degreesToRadians(180 + 45.f) + MathConstants<float>::twoPi;
        
        auto range = getRange();
        
        auto sliderBounds = getSliderBounds();
        
        g.setColour(Colours::red);
        g.drawRect(getLocalBounds());
        g.setColour(Colours::yellow);
        g.drawRect(sliderBounds);
        
        getLookAndFeel().drawRotarySlider(g,
                                          sliderBounds.getX(),
                                          sliderBounds.getY(),
                                          sliderBounds.getWidth(),
                                          sliderBounds.getHeight(), 
                                          jmap(getValue(), range.getStart(), range.getEnd(), 
                                               0.0, 1.0), startAng, endAng, *this);
}

juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const 
{
        auto bounds = getLocalBounds();
        auto size = juce::jmin(bounds.getWidth(),bounds.getHeight());
        size -= getTextHeight() * 2;
        
        juce::Rectangle<int> r;
        r.setSize(size, size);
        r.setCentre(bounds.getCentreX(), 0);
        r.setY(2);
        
        return r;
}
//==============================================================================
ResponseCurveComponent::ResponseCurveComponent(AudioPluginAudioProcessor& p) : processorRef(p) 
{
    const auto& params = processorRef.getParameters();
    for (auto param : params)
    {
		param->addListener(this);   
    }

    startTimer(60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    const auto& params = processorRef.getParameters();
    for (auto param : params)
    {
        param->removeListener(this);
    }
}
void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue)
{
    parametersChanged.set(true);
}

void ResponseCurveComponent::timerCallback()
{
    if (parametersChanged.compareAndSetBool(false,true))
    {
        // update the monochain
        auto chainSettings = getChainSettings(processorRef.apvts);
        auto peakCoefficients = makePeakFilter(chainSettings,processorRef.getSampleRate());
        updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);

        auto lowCutCoefficients = makeLowCutFilter(chainSettings, processorRef.getSampleRate());
        auto highCutCoefficients = makeHighCutFilter(chainSettings, processorRef.getSampleRate());
        updateCutFilter(monoChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
        updateCutFilter(monoChain.get<ChainPositions::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);
        // signal a repaint
        repaint();

    }
}

void ResponseCurveComponent::paint (juce::Graphics& g)
{
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (Colours::black);

    auto responseArea = getLocalBounds();

    auto w = responseArea.getWidth();

    auto& lowcut = monoChain.get<ChainPositions::LowCut>();
    auto& peak = monoChain.get<ChainPositions::Peak>();
    auto& highcut = monoChain.get<ChainPositions::HighCut>();

    auto sampleRate = processorRef.getSampleRate();

    std::vector<double> mags;

    mags.resize(w);

    for (int i = 0; i < w; ++i) {
        double mag = 1.f;
        auto freq = mapToLog10(double (i) / double (w), 20.0, 20000.0);

        if (!monoChain.isBypassed<ChainPositions::Peak>())
            mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);

        if (!lowcut.isBypassed<0>())
            mag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq,sampleRate);
        if (!lowcut.isBypassed<1>())
            mag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq,sampleRate);
        if (!lowcut.isBypassed<2>())
            mag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq,sampleRate);
        if (!lowcut.isBypassed<3>())
            mag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq,sampleRate);

        if (!highcut.isBypassed<0>())
            mag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq,sampleRate);
        if (!highcut.isBypassed<1>())
            mag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq,sampleRate);
        if (!highcut.isBypassed<2>())
            mag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq,sampleRate);
        if (!highcut.isBypassed<3>())
            mag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq,sampleRate);

        mags[i] = Decibels::gainToDecibels(mag);
    }

    Path responseCurve;

    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();
    auto map = [outputMin, outputMax](double input)
    {
        return jmap(input, -24.0, 24.0, outputMin, outputMax);
    };

    responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));

    for (size_t i = 1; i < mags.size(); ++i)
    {
        responseCurve.lineTo(responseArea.getX()+i, map(mags[i])) ;
    }

    g.setColour(Colours::orange);
    g.drawRoundedRectangle(responseArea.toFloat(),4.f,1.f);
    g.setColour(Colours::white);
    g.strokePath(responseCurve,PathStrokeType(2));
}
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), 
      peakFreqSlider(*processorRef.apvts.getParameter("Peak Freq"), "Hz"),
      peakGainSlider(*processorRef.apvts.getParameter("Peak Gain"),"dB"),
      peakQualitySlider(*processorRef.apvts.getParameter("Peak Quality"), ""),
      lowCutFreqSlider(*processorRef.apvts.getParameter("LowCut Freq"), "Hz"),
      highCutFreqSlider(*processorRef.apvts.getParameter("HighCut Freq"), "Hz"),
      lowCutSlopeSlider(*processorRef.apvts.getParameter("LowCut Slope"),"dB"),
      highCutSlopeSlider(*processorRef.apvts.getParameter("HighCut Slope"), "dB"),
      responseCurveComponent(processorRef),
      peakFreqSliderAttachment(processorRef.apvts, "Peak Freq", peakFreqSlider),
      peakGainSliderAttachment(processorRef.apvts,"Peak Gain", peakGainSlider),
      peakQualitySliderAttachment(processorRef.apvts,"Peak Quality", peakQualitySlider),
      lowCutFreqSliderAttachment(processorRef.apvts,"LowCut Freq", lowCutFreqSlider),
      highCutFreqSliderAttachment(processorRef.apvts,"HighCut Freq", highCutFreqSlider),
      lowCutSlopeSliderAttachment(processorRef.apvts,"LowCut Slope",lowCutSlopeSlider),
      highCutSlopeSliderAttachment(processorRef.apvts,"HighCut Slope", highCutSlopeSlider)
{
    // juce::ignoreUnused (processorRef);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    
    for (auto* comp : getComponents() )
    {
        addAndMakeVisible(comp);
    }
    
    setSize (600, 400);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor(){}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (Colours::black);
}



void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    auto bounds = getLocalBounds();
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * 0.33);
    
    responseCurveComponent.setBounds(responseArea);
    
    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);
    
    lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
    lowCutSlopeSlider.setBounds(lowCutArea);
    
    highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
    highCutSlopeSlider.setBounds(highCutArea);
    
    peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
    peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
    peakQualitySlider.setBounds(bounds);
}

std::vector<juce::Component*> AudioPluginAudioProcessorEditor::getComponents() 
{
    return
    {
        &peakFreqSlider, 
        &peakGainSlider, 
        &peakQualitySlider, 
        &lowCutFreqSlider, 
        &highCutFreqSlider,
        &lowCutSlopeSlider,
        &highCutSlopeSlider,
        &responseCurveComponent
    };
}