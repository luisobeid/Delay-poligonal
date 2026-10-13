// ─────────────────────────────────────────────────────────────────────────────
// PluginProcessor.cpp
// ─────────────────────────────────────────────────────────────────────────────
// Implementación del Processor. Contiene toda la lógica de audio:
//   - Delay estéreo con buffer circular
//   - Secuenciador por pasos asociado a un gate de ganancia
//   - Distorsión (Soft/Fuzz/WaveFold)
//   - Filtro LP/HP de primer orden en el loop de feedback
//   - Modo reverse delay (lectura invertida del buffer)
//   - Sincronización al tempo del DAW
//   - Serialización de presets
//
// Cadena de señal por canal (L y R independientes):
//   entrada -> distorsión -> x secuenciador (gate) -> delay buffer -> filtro
//   salida  = dry x entrada + wet x señal_filtrada_del_delay
// ─────────────────────────────────────────────────────────────────────────────

#include "PluginProcessor.h"
#include "PluginEditor.h"

// Nombres de las subdivisiones rítmicas para los combos del Editor.
// Índice 0 = "1/2" (blanca), ... , índice 6 = "1/16T" (tresillo de semicorchea).
static const juce::StringArray kDivNames { "1/2", "1/4", "1/8D",
                                            "1/8", "1/8T", "1/16", "1/16T" };

// ═════════════════════════════════════════════════════════════════════════════
// CONSTRUCTOR — registra todos los parámetros del plugin
// ═════════════════════════════════════════════════════════════════════════════
// Cada addParameter() crea un parámetro automatizable, lo registra en JUCE
// y guarda el puntero para acceso directo desde el Editor y processBlock.

FilterDelayAudioProcessor::FilterDelayAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // ── Parámetros canal L ────────────────────────────────────────────────
    addParameter (delayTimeL    = new juce::AudioParameterFloat  ("delayL",    "Delay L (ms)",    1.0f, 2000.0f, 400.0f));
    addParameter (feedbackL     = new juce::AudioParameterFloat  ("feedL",     "Feedback L",      0.0f, 0.99f,   0.0f));
    addParameter (wetPercentL   = new juce::AudioParameterFloat  ("wetL",      "Wet L %",         0.0f, 100.0f,  50.0f));
    addParameter (cutoffHzL     = new juce::AudioParameterFloat  ("cutoffL",   "Cutoff L (Hz)",   20.0f, 20000.0f, 2000.0f));
    addParameter (filterTypeL   = new juce::AudioParameterChoice ("ftypeL",    "Filter Type L",   { "Low Pass", "High Pass" }, 0));
    addParameter (numStepsL     = new juce::AudioParameterInt    ("numStepsL", "Num Steps L",     4, 32, 16));
    addParameter (syncModeL     = new juce::AudioParameterChoice ("syncModeL", "Sync Mode L",     { "Libre", "Sync" }, 1));
    addParameter (syncDivisionL = new juce::AudioParameterChoice ("syncDivL",  "Sync Division L", kDivNames, 1));
    addParameter (driveL        = new juce::AudioParameterFloat  ("driveL",    "Drive L",         1.0f, 10.0f, 1.0f));
    addParameter (distModeL     = new juce::AudioParameterChoice ("distModeL", "Dist Mode L",     { "Soft", "Fuzz", "WaveFold" }, 0));

    // ── Parámetros canal R (misma estructura que L) ───────────────────────
    addParameter (delayTimeR    = new juce::AudioParameterFloat  ("delayR",    "Delay R (ms)",    1.0f, 2000.0f, 400.0f));
    addParameter (feedbackR     = new juce::AudioParameterFloat  ("feedR",     "Feedback R",      0.0f, 0.99f,   0.0f));
    addParameter (wetPercentR   = new juce::AudioParameterFloat  ("wetR",      "Wet R %",         0.0f, 100.0f,  50.0f));
    addParameter (cutoffHzR     = new juce::AudioParameterFloat  ("cutoffR",   "Cutoff R (Hz)",   20.0f, 20000.0f, 2000.0f));
    addParameter (filterTypeR   = new juce::AudioParameterChoice ("ftypeR",    "Filter Type R",   { "Low Pass", "High Pass" }, 0));
    addParameter (numStepsR     = new juce::AudioParameterInt    ("numStepsR", "Num Steps R",     4, 32, 16));
    addParameter (syncModeR     = new juce::AudioParameterChoice ("syncModeR", "Sync Mode R",     { "Libre", "Sync" }, 1));
    addParameter (syncDivisionR = new juce::AudioParameterChoice ("syncDivR",  "Sync Division R", kDivNames, 1));
    addParameter (driveR        = new juce::AudioParameterFloat  ("driveR",    "Drive R",         1.0f, 10.0f, 1.0f));
    addParameter (distModeR     = new juce::AudioParameterChoice ("distModeR", "Dist Mode R",     { "Soft", "Fuzz", "WaveFold"  }, 0));

    // ── Parámetro compartido: link entre canales ──────────────────────────
    addParameter (sequencersSync = new juce::AudioParameterBool  ("seqSync",   "Sequencers Sync", false));
}

FilterDelayAudioProcessor::~FilterDelayAudioProcessor() {}

// ═════════════════════════════════════════════════════════════════════════════
// TABLA DE SUBDIVISIONES RÍTMICAS
// ═════════════════════════════════════════════════════════════════════════════
// Convierte el índice del combo de subdivisión a un multiplicador de beat.
// Se multiplica por la duración de un beat (60/bpm) para obtener segundos.
//
//   Índice  Nombre   Mult    Figura rítmica
//   0       "1/2"    2.0     Blanca
//   1       "1/4"    1.0     Negra (1 beat)
//   2       "1/8D"   0.75    Corchea con puntillo
//   3       "1/8"    0.5     Corchea
//   4       "1/8T"   1/3     Tresillo de corchea
//   5       "1/16"   0.25    Semicorchea
//   6       "1/16T"  1/6     Tresillo de semicorchea

double FilterDelayAudioProcessor::getDivisionMultiplier (int ch) const
{
    auto* div = (ch == 0) ? syncDivisionL : syncDivisionR;
    switch (div->getIndex())
    {
        case 0: return 2.0;   case 1: return 1.0;
        case 2: return 0.75;  case 3: return 0.5;
        case 4: return 1.0/3.0; case 5: return 0.25;
        case 6: return 1.0/6.0; default: return 1.0;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// CÁLCULO DE DURACIÓN DE PASO
// ═════════════════════════════════════════════════════════════════════════════
// Dos modos:
//   Sync (index 1): stepSeconds = (60/bpm) x multiplicador -> muestras
//   Libre (index 0): muestras = ms x 0.001 x sampleRate
// Se clampea a [1, maxDelaySamples/2 - 1] porque el modo reverse
// necesita dos segmentos en el buffer (cada uno de stepLength).

int FilterDelayAudioProcessor::computeStepLengthSamples (double bpm, int ch) const
{
    auto* mode  = (ch == 0) ? syncModeL  : syncModeR;
    auto* delay = (ch == 0) ? delayTimeL : delayTimeR;
    if (mode->getIndex() == 1 && bpm > 0.0)
    {
        double stepSeconds = (60.0 / bpm) * getDivisionMultiplier (ch); // cantidad de pasos por segundo
        return juce::jlimit (1, maxDelaySamples / 2 - 1, (int)(stepSeconds * currentSampleRate)); // cantidad de pasos en samples
    }
    return juce::jlimit (1, maxDelaySamples / 2 - 1,
                         (int)(delay->get() * 0.001 * currentSampleRate));
}

// ═════════════════════════════════════════════════════════════════════════════
// PREPARE TO PLAY — inicialización antes de procesar audio
// ═════════════════════════════════════════════════════════════════════════════
// Llamado por el host/DAW. Crea buffers de delay (4 segundos por canal),
// inicializa secuenciadores, filtros y estado del reverse.

void FilterDelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    // Buffer de delay: 4 segundos de audio por canal
    maxDelaySamples   = static_cast<int> (4.0 * sampleRate);  
    delayBufferL.assign (maxDelaySamples, 0.0f);
    delayBufferR.assign (maxDelaySamples, 0.0f);

    // Reset de posiciones y estados
    writePosL = writePosR = 0;
    prevValueL = prevValueR = 0.0f;
    readGainL = readGainR = readTargetL = readTargetR = 0.0f;
    wasPlaying = false;

    // Secuenciadores
    stepSequencerL.prepare (sampleRate);
    stepSequencerR.prepare (sampleRate);
    stepSequencerL.setNumSteps (numStepsL->get());
    stepSequencerR.setNumSteps (numStepsR->get());
    // Acá se "carga" el largo del paso que vamos a usar para dar la velocidad angular,
    // completar el circulo 
    
    stepSequencerL.setStepLengthSamples (computeStepLengthSamples (120.0, 0));
    stepSequencerR.setStepLengthSamples (computeStepLengthSamples (120.0, 1));

    // Reverse: inicializar segmentos alternados
    initReverseState (0);
    initReverseState (1);

    // Filtro: coeficientes iniciales
    updateCoefficients();
}

void FilterDelayAudioProcessor::releaseResources() {}

// ═════════════════════════════════════════════════════════════════════════════
// REVERSE DELAY — dos segmentos alternados
// ═════════════════════════════════════════════════════════════════════════════
// El buffer se divide en dos mitades (segmentos). Mientras una se llena
// escribiendo de izquierda a derecha, la otra se lee de derecha a izquierda
// (al revés). Cuando se llena un segmento, intercambian roles.
// Resultado: el audio sale invertido temporalmente.

// Inicializa posiciones de lectura/escritura de los segmentos.
void FilterDelayAudioProcessor::initReverseState (int ch)
{
    if (ch == 0) {
        writeSegmentL = segmentCounterL = 0;
        reverseWritePosL = 0;
        // Lectura empieza al final del segundo segmento
        reverseReadPosL  = segmentLengthL > 0 ? segmentLengthL * 2 - 1 : 0;
    } else {
        writeSegmentR = segmentCounterR = 0;
        reverseWritePosR = 0;
        reverseReadPosR  = segmentLengthR > 0 ? segmentLengthR * 2 - 1 : 0;
    }
}

// Procesa una muestra en modo reverse:
//   1. Lee del segmento de lectura (rPos decrece = lectura al revés)
//   2. Filtra la muestra leída (LP o HP)
//   3. Escribe en el segmento de escritura: entrada + feedback filtrado
//   4. Al llenar el segmento, intercambia roles (wSeg = 1 - wSeg)
//   5. Devuelve mezcla dry + wet con clamp a [-1, +1]
float FilterDelayAudioProcessor::processReverseSample (int ch, float input, float fb, float wet, float dry)
{
    // Seleccionar variables del canal correcto usando referencias
    auto& buf    = (ch == 0) ? delayBufferL     : delayBufferR;
    auto& wPos   = (ch == 0) ? reverseWritePosL : reverseWritePosR;
    auto& rPos   = (ch == 0) ? reverseReadPosL  : reverseReadPosR;
    auto& segCnt = (ch == 0) ? segmentCounterL  : segmentCounterR;
    auto& segLen = (ch == 0) ? segmentLengthL   : segmentLengthR;
    auto& wSeg   = (ch == 0) ? writeSegmentL    : writeSegmentR;
    auto& prev   = (ch == 0) ? prevValueL       : prevValueR;
    
    int   ftIdx  = (ch == 0) ? filterTypeL->getIndex() : filterTypeR->getIndex();
       float a0c    = (ch == 0) ? a0L : a0R;
       float b1c    = (ch == 0) ? b1L : b1R;
       // señal con reverse delay y luego se aplica el filtro
       float delayed  = buf[rPos];
       float filtered = (ftIdx == 0) ? processSampleLP (delayed, prev, a0c, b1c)
                                     : processSampleHP (delayed, prev, a0c, b1c);
    
    // señal de entrada + señal efecteada
    buf[wPos] = input + filtered * fb;
    
    wPos++; rPos--;   // 
    if (rPos < 0) rPos = 0;
    if (wPos >= maxDelaySamples) wPos = maxDelaySamples - 1;
    segCnt++;
    if (segCnt >= segLen)
    {
        segCnt = 0;
        wSeg = 1 - wSeg;                            // alternar segmento 0/1
        wPos = wSeg * segLen;                        // escribir al inicio del nuevo
        rPos = (1 - wSeg) * segLen + segLen - 1;     // leer desde el final del otro
    }
    // Mezcla final (1.6x = compensación de volumen wet ajustada a "oreja")
    return juce::jlimit (-1.0f, 1.0f, dry * input + 1.6f * wet * filtered);
}

// ═════════════════════════════════════════════════════════════════════════════
// FILTRO 
// ═════════════════════════════════════════════════════════════════════════════

// Recalcula coeficientes a partir de cutoffHz y sampleRate.
// Fórmula: wc = 2pi x fc / sr;  n = 1/(1+wc)
//   LP: a0 = wc*n,  b1 = (1-wc)*n  -> deja pasar graves, atenúa agudos
//   HP: a0 = n,      b1 = (1-wc)*n  -> deja pasar agudos, atenúa graves
void FilterDelayAudioProcessor::updateCoefficients()
{
    // Canal L
    // omegaCutoff ->  wc = 2pi * (fc /sampleRate)
    {
        double fc = juce::jlimit (20.0, currentSampleRate * 0.499, (double)cutoffHzL->get());
        double wc = 2.0 * juce::MathConstants<double>::pi * fc / currentSampleRate;
        double n  = 1.0 / (1.0 + wc);
        if (filterTypeL->getIndex() == 0) { a0L = (float)(wc*n); b1L = (float)((1.0-wc)*n); }
        else                              { a0L = (float)(n);     b1L = (float)((1.0-wc)*n); }
    }
    // Canal R
    {
        double fc = juce::jlimit (20.0, currentSampleRate * 0.499, (double)cutoffHzR->get());
        double wc = 2.0 * juce::MathConstants<double>::pi * fc / currentSampleRate;
        double n  = 1.0 / (1.0 + wc);
        if (filterTypeR->getIndex() == 0) { a0R = (float)(wc*n); b1R = (float)((1.0-wc)*n); }
        else                              { a0R = (float)(n);     b1R = (float)((1.0-wc)*n); }
    }
}

// y[n] = a0 * x[n] + b1 * y[n-1]   (prev almacena y[n-1])
float FilterDelayAudioProcessor::processSampleLP (float x, float& prev, float a0c, float b1c)
{ float y = a0c * x + b1c * prev; prev = y; return y; }

// y[n] = a0 * (x[n] - y[n-1]) + b1 * y[n-1]   (la resta elimina graves)
float FilterDelayAudioProcessor::processSampleHP (float x, float& prev, float a0c, float b1c)
{ float y = a0c * (x - prev) + b1c * prev; prev = y; return y; }

// ═════════════════════════════════════════════════════════════════════════════
// PROCESS BLOCK — loop principal de audio
// ═════════════════════════════════════════════════════════════════════════════
// Llamado por el DAW con un buffer de 64-1024 muestras.
// Para cada muestra, por canal:
//   1. Secuenciador devuelve gain (gate: 0.0 o 1.0 con fade)
//   2. Si reverse: segmentos alternados (escribir/leer invertido)
//   3. Si normal: delay circular + filtro + noise gate automático
//   4. Salida = dry*input + 1.6*wet*filtrado*readGain

void FilterDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;  // prevenir denormals en la CPU
    updateCoefficients();                  // recalcular filtro por si cambió cutoff

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ── Info del transporte del DAW ───────────────────────────────────────
    bool isPlaying = false;
    double bpm = 120.0;  // fallback si el DAW no reporta BPM
    if (auto* ph = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo pos;
        if (ph->getCurrentPosition (pos))
        {
            isPlaying = pos.isPlaying;
            if (pos.bpm > 0.0) bpm = pos.bpm;
        }
    }

    // Parar secuenciadores si el transporte no está en play
    if (!isPlaying && (stepSequencerL.isRunning() || stepSequencerR.isRunning()))
    { stepSequencerL.stop(); stepSequencerR.stop(); }
    wasPlaying = isPlaying;
    
    
    const bool linked = sequencersSync->get();

    // ── Configurar secuenciadores (pasos y duración) ──────────────────────
    int stepLenL = computeStepLengthSamples (bpm, 0);
    int stepLenR = linked ? stepLenL : computeStepLengthSamples (bpm, 1);

    stepSequencerL.setNumSteps (numStepsL->get());
    stepSequencerL.setStepLengthSamples (stepLenL);
    stepSequencerR.setNumSteps (linked ? numStepsL->get() : numStepsR->get());
    stepSequencerR.setStepLengthSamples (stepLenR);

    // Si cambió el largo del paso, reiniciar reverse
    if (stepLenL != segmentLengthL) { segmentLengthL = stepLenL; initReverseState (0); }
    if (stepLenR != segmentLengthR) { segmentLengthR = stepLenR; initReverseState (1); }

    delaySamplesL = stepLenL;
    delaySamplesR = stepLenR;

    // ── Leer parámetros L (una vez por bloque = eficiente) ────────────────
    const float fbL   = feedbackL->get();                          // recirculación
    const float tL    = wetPercentL->get() * 0.01f;                // 0-100% -> 0-1
    const float wetL  = std::sin (tL * juce::MathConstants<float>::halfPi);  // equal-power
    const float dryL  = std::cos (tL * juce::MathConstants<float>::halfPi);  // complementario
    const float drvL  = driveL->get();
    const int   modeL = distModeL->getIndex();
    bool reverseL     = !stepSequencerL.isClockwise();  // antihorario = reverse

    // ── Parámetros R (linked copia de L) ──────────────────────────────────
    const float fbR   = linked ? fbL   : feedbackR->get();
    const float tR    = linked ? tL    : wetPercentR->get() * 0.01f;
    const float wetR  = std::sin (tR * juce::MathConstants<float>::halfPi);
    const float dryR  = std::cos (tR * juce::MathConstants<float>::halfPi);
    const float drvR  = linked ? drvL  : driveR->get();
    const int   modeR = linked ? modeL : distModeR->getIndex();
    bool reverseR     = linked ? reverseL : !stepSequencerR.isClockwise();

    float* dataL = buffer.getWritePointer (0);
    float* dataR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // Entrada mono -> copiar a R
    if (dataR == nullptr)
    {
        buffer.setSize (2, numSamples, true, true, true);
        dataR = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i) dataR[i] = dataL[i];
    }

    // ── Función de distorsión (lambda con gain compensation) ──────────────
    // Cada modo compensa el volumen extra dividiendo por un factor del drive.
    //   case 0: Overdrive — tanh(x), saturación suave tipo válvulas
    //   case 1: Fuzz — tanh(x * sqrt(drive)), onda casi cuadrada
    //   case 2: WaveFold — sin(x * pi * 0.7), se pliega al superar +-1
    // jlimit final: red de seguridad, nada sale de [-1, +1].
    auto applyDist = [] (float x, float drive, int mode) -> float
    {
        x *= drive;  // amplificar (más drive = más saturación)
        float out;
        switch (mode)
        {
            case 0:  // Overdrive
                out = std::tanh(x);
                out /= std::max(drive * 0.35f, 1.0f);
                break;

            case 1:  // Fuzz
                out = std::tanh(x * std::sqrt(drive));
                out /= std::max(drive * 0.6f, 1.0f);
                break;

            case 2:  // Wavefolder
                out = std::sin(x * juce::MathConstants<float>::pi * 0.7f);
                out /= std::max(drive * 0.6f, 1.0f);
                break;

            default: out = x;
        }
        return juce::jlimit(-1.0f, 1.0f, out);
    };

    // ── Loop muestra a muestra ────────────────────────────────────────────
    for (int i = 0; i < numSamples; ++i)
    {
        // ── Canal L ───────────────────────────────────────────────────────
        {
            const float input   = dataL[i];
            float       seqGain = stepSequencerL.processSample (input);  // gate 0-1
            if (reverseL)
            {
                dataL[i] = processReverseSample (0, applyDist (input, drvL, modeL), fbL, wetL * seqGain, dryL);
            }
            else
            {
                // Leer del buffer (delaySamplesL muestras atrás)
                const int   rp  = (writePosL - delaySamplesL + maxDelaySamples) % maxDelaySamples;
                const float del = delayBufferL[rp];
                // Filtrar señal leída (LP o HP)
                const float fil = (filterTypeL->getIndex() == 0)
                                  ? processSampleLP (del, prevValueL, a0L, b1L)
                                  : processSampleHP (del, prevValueL, a0L, b1L);
                
                // Escribir: distorsión*gate + feedback filtrado
                
                // std::tanh satura suavemente cualquier valor que supere ±1, así el buffer nunca se desborda.
                // Esto te deja usar feedback alto sin que se descontrole.
                delayBufferL[writePosL] = std::tanh(applyDist(input, drvL, modeL) * seqGain + fil * fbL);
                writePosL = (writePosL + 1) % maxDelaySamples;
                
                // Noise gate: ataque instantáneo, release 20ms
                readTargetL = (std::abs(fil) > 0.0001f) ? 1.0f : 0.0f;
                if (readTargetL > readGainL) readGainL = readTargetL;
                else if (readGainL > 0.0f)  readGainL -= 1.0f / (float)(currentSampleRate * 0.020);
                readGainL = juce::jmax (0.0f, readGainL);
                // Salida: dry + wet (1.6x compensa volumen)
                // la salida tiene un limitador para controlar el volumen de salida
                dataL[i] = juce::jlimit (-1.0f, 1.0f, dryL * input + 1.6f * wetL * fil * readGainL);
            }
        }

        // ── Canal R (misma lógica) ────────────────────────────────────────
        {
            const float input   = dataR[i];
            float       seqGain = stepSequencerR.processSample (input);
            if (reverseR)
            {
                dataR[i] = processReverseSample (1, applyDist (input, drvR, modeR), fbR, wetR * seqGain, dryR);
    
            }
            else
            {
                const int   rp  = (writePosR - delaySamplesR + maxDelaySamples) % maxDelaySamples;
                const float del = delayBufferR[rp];
                const float fil = (filterTypeR->getIndex() == 0)
                                  ? processSampleLP (del, prevValueR, a0R, b1R)
                                  : processSampleHP (del, prevValueR, a0R, b1R);
                
                delayBufferR[writePosR] = std::tanh(applyDist (input, drvR, modeR) * seqGain + fil * fbR);

                writePosR = (writePosR + 1) % maxDelaySamples;
                readTargetR = (std::abs(fil) > 0.0001f) ? 1.0f : 0.0f;
                if (readTargetR > readGainR) readGainR = readTargetR;
                else if (readGainR > 0.0f)  readGainR -= 1.0f / (float)(currentSampleRate * 0.020);
                readGainR = juce::jmax (0.0f, readGainR);
                dataR[i] = juce::jlimit (-1.0f, 1.0f, dryR * input + 1.6f * wetR * fil * readGainR);
            }
        }
    }
}

juce::AudioProcessorEditor* FilterDelayAudioProcessor::createEditor()
{
    return new FilterDelayAudioProcessorEditor (*this);
}

// ═════════════════════════════════════════════════════════════════════════════
// SERIALIZACIÓN — guardar y cargar presets (.fsd)
// ═════════════════════════════════════════════════════════════════════════════
// IMPORTANTE: el orden de escritura y lectura debe ser IDÉNTICO.
// Graba: parámetros L + patrón L + parámetros R + patrón R + link.

void FilterDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream s (destData, true);

    // Canal L: parámetros + dirección + patrón de pasos activos
    s.writeFloat (delayTimeL->get());
    s.writeFloat (feedbackL->get());
    s.writeFloat (wetPercentL->get());
    s.writeFloat (cutoffHzL->get());
    s.writeInt   (filterTypeL->getIndex());
    s.writeInt   (numStepsL->get());
    s.writeInt   (syncModeL->getIndex());
    s.writeInt   (syncDivisionL->getIndex());
    s.writeFloat (driveL->get());
    s.writeInt   (distModeL->getIndex());
    s.writeBool  (stepSequencerL.isClockwise());
    for (int i = 0; i < numStepsL->get(); ++i)
        s.writeBool (stepSequencerL.isStepActive (i));

    // Canal R
    s.writeFloat (delayTimeR->get());
    s.writeFloat (feedbackR->get());
    s.writeFloat (wetPercentR->get());
    s.writeFloat (cutoffHzR->get());
    s.writeInt   (filterTypeR->getIndex());
    s.writeInt   (numStepsR->get());
    s.writeInt   (syncModeR->getIndex());
    s.writeInt   (syncDivisionR->getIndex());
    s.writeFloat (driveR->get());
    s.writeInt   (distModeR->getIndex());
    s.writeBool  (stepSequencerR.isClockwise());
    for (int i = 0; i < numStepsR->get(); ++i)
        s.writeBool (stepSequencerR.isStepActive (i));

    // Link entre canales
    s.writeBool (sequencersSync->get());
}

// Restaura estado. setValueNotifyingHost + convertTo0to1 convierte
// el valor real al rango [0,1] que JUCE usa internamente.
void FilterDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream s (data, static_cast<size_t>(sizeInBytes), false);

    // Canal L
    delayTimeL   ->setValueNotifyingHost (delayTimeL   ->convertTo0to1 (s.readFloat()));
    feedbackL    ->setValueNotifyingHost (feedbackL    ->convertTo0to1 (s.readFloat()));
    wetPercentL  ->setValueNotifyingHost (wetPercentL  ->convertTo0to1 (s.readFloat()));
    cutoffHzL    ->setValueNotifyingHost (cutoffHzL    ->convertTo0to1 (s.readFloat()));
    filterTypeL  ->setValueNotifyingHost (filterTypeL  ->convertTo0to1 ((float)s.readInt()));
    int stepsL = s.readInt();
    numStepsL    ->setValueNotifyingHost (numStepsL    ->convertTo0to1 ((float)stepsL));
    syncModeL    ->setValueNotifyingHost (syncModeL    ->convertTo0to1 ((float)s.readInt()));
    syncDivisionL->setValueNotifyingHost (syncDivisionL->convertTo0to1 ((float)s.readInt()));
    driveL       ->setValueNotifyingHost (driveL       ->convertTo0to1 (s.readFloat()));
    distModeL    ->setValueNotifyingHost (distModeL    ->convertTo0to1 ((float)s.readInt()));
    stepSequencerL.setClockwise (s.readBool());
    stepSequencerL.setNumSteps (stepsL);
    for (int i = 0; i < stepsL; ++i)
        stepSequencerL.setStep (i, s.readBool());

    // Canal R
    delayTimeR   ->setValueNotifyingHost (delayTimeR   ->convertTo0to1 (s.readFloat()));
    feedbackR    ->setValueNotifyingHost (feedbackR    ->convertTo0to1 (s.readFloat()));
    wetPercentR  ->setValueNotifyingHost (wetPercentR  ->convertTo0to1 (s.readFloat()));
    cutoffHzR    ->setValueNotifyingHost (cutoffHzR    ->convertTo0to1 (s.readFloat()));
    filterTypeR  ->setValueNotifyingHost (filterTypeR  ->convertTo0to1 ((float)s.readInt()));
    int stepsR = s.readInt();
    numStepsR    ->setValueNotifyingHost (numStepsR    ->convertTo0to1 ((float)stepsR));
    syncModeR    ->setValueNotifyingHost (syncModeR    ->convertTo0to1 ((float)s.readInt()));
    syncDivisionR->setValueNotifyingHost (syncDivisionR->convertTo0to1 ((float)s.readInt()));
    driveR       ->setValueNotifyingHost (driveR       ->convertTo0to1 (s.readFloat()));
    distModeR    ->setValueNotifyingHost (distModeR    ->convertTo0to1 ((float)s.readInt()));
    stepSequencerR.setClockwise (s.readBool());
    stepSequencerR.setNumSteps (stepsR);
    for (int i = 0; i < stepsR; ++i)
        stepSequencerR.setStep (i, s.readBool());

    // Compartido
    sequencersSync->setValueNotifyingHost (s.readBool() ? 1.0f : 0.0f);
}

// Función requerida por JUCE para instanciar el plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FilterDelayAudioProcessor();
}
