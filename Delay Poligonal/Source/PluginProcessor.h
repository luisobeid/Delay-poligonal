// ─────────────────────────────────────────────────────────────────────────────
// PluginProcessor.h
// ─────────────────────────────────────────────────────────────────────────────
//
// Processor del plugin: núcleo de procesamiento de audio.
//
// Cadena de señal por canal (L y R independientes):
//   entrada → distorsión → secuenciador (gate) → delay buffer → filtro
//   salida  = dry * entrada + wet * señal_filtrada_del_delay
//
// El Processor es dueño de:
//   - Todos los AudioParameter (los sliders/combos de la UI)
//   - Los dos StepSequencer (L y R)
//   - Los buffers de delay circular
//   - Los coeficientes de filtro LP/HP
//   - El estado de reverse delay
//   - La serialización de presets (getStateInformation / setStateInformation)
//
// El Editor accede al Processor a través de la referencia que recibe
// en su constructor. Nunca al revés: el Processor no conoce al Editor.
//
// ─────────────────────────────────────────────────────────────────────────────

#pragma once
#include <JuceHeader.h>
#include "StepSequencer.h"

class FilterDelayAudioProcessor : public juce::AudioProcessor
{
public:
    FilterDelayAudioProcessor();
    ~FilterDelayAudioProcessor() override;

    // ── Ciclo de vida del plugin (JUCE) ───────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FilterDelay"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    // ── Serialización de estado (presets) ─────────────────────────────────
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    // ── Parámetros públicos ───────────────────────────────────────────────
    // Son públicos para que el Editor los lea/escriba con operator=.

    // Distorsión (drive = intensidad, distMode = tipo de curva)
    juce::AudioParameterFloat*  driveL;
    juce::AudioParameterFloat*  driveR;
    juce::AudioParameterChoice* distModeL;    // Soft / Fuzz / WaveFold
    juce::AudioParameterChoice* distModeR;

    // Delay canal L
    juce::AudioParameterFloat*  delayTimeL;    // tiempo en ms (modo Libre)
    juce::AudioParameterFloat*  feedbackL;     // feedback 0.0 – 0.99
    juce::AudioParameterFloat*  wetPercentL;   // mezcla dry/wet 0 – 100%
    juce::AudioParameterFloat*  cutoffHzL;     // frecuencia de corte del filtro
    juce::AudioParameterChoice* filterTypeL;   // Low Pass Filter / High Pass Filter
    juce::AudioParameterInt*    numStepsL;     // pasos del secuenciador 4 – 32
    juce::AudioParameterChoice* syncModeL;     // Libre / Sync al DAW
    juce::AudioParameterChoice* syncDivisionL; // subdivisión rítmica

    // Delay canal R (misma estructura)
    juce::AudioParameterFloat*  delayTimeR;
    juce::AudioParameterFloat*  feedbackR;
    juce::AudioParameterFloat*  wetPercentR;
    juce::AudioParameterFloat*  cutoffHzR;
    juce::AudioParameterChoice* filterTypeR;
    juce::AudioParameterInt*    numStepsR;
    juce::AudioParameterChoice* syncModeR;
    juce::AudioParameterChoice* syncDivisionR;

    // Link: si true, R copia todo de L
    juce::AudioParameterBool*   sequencersSync;  // Resultando en dos delay independendiente actuando igual, la idea es generar un delay "pseudo" mono.

    // Getters para que el Editor acceda a los secuenciadores
    StepSequencer& getStepSequencerL() { return stepSequencerL; }
    StepSequencer& getStepSequencerR() { return stepSequencerR; }

private:
    // ── Funciones auxiliares ──────────────────────────────────────────────
    double getDivisionMultiplier (int ch) const;   // subdiv → multiplicador, obtiene la subdivision de cada paso.
    void   updateCoefficients();                    // recalcula filtro LP/HP
    float  processSampleLP (float x, float& prev, float a0c, float b1c);  // filtros
    float  processSampleHP (float x, float& prev, float a0c, float b1c);
    int    computeStepLengthSamples (double bpm, int ch) const;
    void   initReverseState (int ch);                                     // inicializacion del sequencer en modo reverse
    float  processReverseSample (int ch, float input, float fb, float wet, float dry); // procesamiento del reverse delay

    // ── Secuenciadores ────────────────────────────────────────────────────
    StepSequencer stepSequencerL;
    StepSequencer stepSequencerR;

    // ── Delay circular (4 segundos de buffer) ─────────────────────────────
    std::vector<float> delayBufferL, delayBufferR;
    int    writePosL = 0, writePosR = 0;
    int    delaySamplesL = 0, delaySamplesR = 0;
    int    maxDelaySamples = 0;

    // ── Reverse delay (dos segmentos alternados) ──────────────────────────
    int  writeSegmentL = 0,    writeSegmentR = 0;
    int  reverseWritePosL = 0, reverseWritePosR = 0;
    int  reverseReadPosL = 0,  reverseReadPosR = 0;
    int  segmentCounterL = 0,  segmentCounterR = 0;
    int  segmentLengthL = 0,   segmentLengthR = 0;

    // ── Filtro de primer orden ────────────────────────────────────────────
    float prevValueL = 0.0f, prevValueR = 0.0f;   // estado z-1
    float a0L = 1.0f, b1L = 0.0f;
    float a0R = 1.0f, b1R = 0.0f;

    // ── Noise gate del delay (fade automático para evitar clicks de audio) ────────────────────────────
    float readGainL = 0.0f, readTargetL = 0.0f;
    float readGainR = 0.0f, readTargetR = 0.0f;

    double currentSampleRate = 44100.0;  // definimos el sample rate
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterDelayAudioProcessor)
};
