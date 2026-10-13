// ─────────────────────────────────────────────────────────────────────────────
// StepSequencer.cpp
// ─────────────────────────────────────────────────────────────────────────────
// Implementación del secuenciador por pasos circular.
// Ver StepSequencer.h para la descripción general de la arquitectura.
// ─────────────────────────────────────────────────────────────────────────────

#include "StepSequencer.h"

// ── Constructor ───────────────────────────────────────────────────────────────
// Inicializa el vector de pasos con numSteps (16) posiciones, todas inactivas.

StepSequencer::StepSequencer()
{
    activeSteps.resize (numSteps, false);
}

// ═════════════════════════════════════════════════════════════════════════════
// SETUP — llamado desde FilterDelayAudioProcessor::prepareToPlay()
// ═════════════════════════════════════════════════════════════════════════════

// Configura todo lo que depende del sample rate: tamaño de la ventana RMS,
// su buffer circular, y la duración del fade en muestras.
void StepSequencer::prepare (double sr)
{
    sampleRate        = sr;

    // Ventana RMS: kRmsWindowSec (10ms) multiplicado por sampleRate.
    // jmax(1,...) evita que sea 0 si el sampleRate fuera absurdamente bajo.
    rmsWindowSize     = juce::jmax (1, (int)(kRmsWindowSec * sr));
    rmsBuffer.assign (rmsWindowSize, 0.0f);  // buffer lleno de ceros
    rmsWritePos       = 0;
    rmsSumSq          = 0.0f;

    // Fade: kFadeSec (5ms) multiplicado por sampleRate = muestras de rampa
    fadeLengthSamples = juce::jmax (1, (int)(kFadeSec * sr));
}

// Establece cuántas muestras dura cada paso. Este valor lo calcula el
// Processor en computeStepLengthSamples() según el modo (Libre/Sync).
void StepSequencer::setStepLengthSamples (int samples)
{
    stepLengthSamples = juce::jmax (1, samples);
}

// Cambia la cantidad de pasos. Si el paso actual queda fuera del nuevo
// rango, resetea al inicio. jlimit acota entre 4 y 32.
// El segundo argumento de resize (false) hace que los pasos nuevos
// arranquen inactivos.
void StepSequencer::setNumSteps (int steps)
{
    numSteps = juce::jlimit (4, 32, steps);
    activeSteps.resize (numSteps, false);
    if (currentStep >= numSteps)
        resetToStart();
}

// ═════════════════════════════════════════════════════════════════════════════
// CONTROL DE TRANSPORTE
// ═════════════════════════════════════════════════════════════════════════════

// Llamado desde processBlock cuando el DAW deja de reproducir.
// Congela el estado visual (currentStep no se resetea) para que el
// marcador quede donde estaba. Al detectar nuevo onset en processSample(),
// ahí sí se llama resetToStart() y arranca desde paso 0.
void StepSequencer::stop()
{
    running       = false;
    sampleCounter = 0;
}

// ═════════════════════════════════════════════════════════════════════════════
// DIRECCIÓN DE RECORRIDO
// ═════════════════════════════════════════════════════════════════════════════

// Cambia la dirección inmediatamente. El próximo advanceStep() ya usa
// la nueva dirección. El efecto de reverse delay (leer el buffer al revés)
// lo maneja el Processor en processReverseSample(), no el secuenciador.
void StepSequencer::setClockwise (bool cw)
{
    clockwise = cw;
}

// ═════════════════════════════════════════════════════════════════════════════
// RESET
// ═════════════════════════════════════════════════════════════════════════════

// Reinicia completamente el secuenciador:
//   - Paso 0, contador en 0
//   - Sin ganancia (silencio)
//   - Marcador visual en posición "12 en punto" (-pi/2 radianes)
//   - running = false (espera nuevo onset para arrancar)
void StepSequencer::resetToStart()
{
    currentStep   = 0;
    sampleCounter = 0;
    running       = false;
    currentGain   = 0.0f;
    targetGain    = 0.0f;
    markerAngle.store (-juce::MathConstants<float>::halfPi);
}

// ═════════════════════════════════════════════════════════════════════════════
// DETECTOR DE ONSET POR RMS
// ═════════════════════════════════════════════════════════════════════════════

// Calcula el RMS de la señal usando un buffer circular
// y un acumulador incremental.
//
// Algoritmo:
//   1. Saca la muestra más vieja del acumulador (resta su cuadrado)
//   2. Clampea a 0 (protección contra errores de punto flotante acumulados)
//   3. Escribe la muestra nueva en el buffer
//   4. Suma su cuadrado al acumulador
//   5. Avanza el puntero circular
//   6. Devuelve sqrt(sumaCuadrados / ventana) = RMS
//
// Como el delay tiene incorporado un secuenciador, este tiene que correr en el mismo momento
// en que el audio entra. El propósito es detectar cuándo "empieza" el audio sin depender del
// transporte del DAW, igual que un delay que reacciona a la señal de entrada.

float StepSequencer::computeRms (float sample)
{
    float old  = rmsBuffer[rmsWritePos];       // muestra que sale de la ventana
    rmsSumSq  -= old * old;                     // restar su cuadrado
    rmsSumSq   = juce::jmax (0.0f, rmsSumSq);  // protección: nunca negativo
    rmsBuffer[rmsWritePos] = sample;            // escribir muestra nueva
    rmsSumSq  += sample * sample;               // sumar su cuadrado
    rmsWritePos = (rmsWritePos + 1) % rmsWindowSize;  // avanzar (circular)
    return std::sqrt (rmsSumSq / (float)rmsWindowSize);
}

// ═════════════════════════════════════════════════════════════════════════════
// AVANCE DE PASO
// ═════════════════════════════════════════════════════════════════════════════

// Avanza al siguiente paso según la dirección:
//   Horario:     0 → 1 → 2 → ... → N-1 → 0 (wrap)
//   Antihorario: 0 → N-1 → N-2 → ... → 1 → 0 (wrap)
//
// Después de avanzar:
//   - Resetea el contador de muestras del paso
//   - Calcula el nuevo targetGain (1.0 si el paso está activo, 0.0 si no)
//   - Calcula fadeIncrement para la rampa lineal de suavizado
void StepSequencer::advanceStep()
{
    if (clockwise)
    {
        currentStep++;
        if (currentStep >= numSteps)
            currentStep = 0;
    }
    else
    {
        currentStep--;
        if (currentStep < 0)
            currentStep = numSteps - 1;
    }

    sampleCounter = 0;
    targetGain    = activeSteps[currentStep] ? 1.0f : 0.0f;
    fadeIncrement  = (targetGain - currentGain) / (float)fadeLengthSamples;
}

// ═════════════════════════════════════════════════════════════════════════════
// PROCESAMIENTO POR MUESTRA — función principal
// ═════════════════════════════════════════════════════════════════════════════

// Llamada desde processBlock() una vez por cada muestra de audio.
// Flujo:
//   1. Calcula RMS para detectar si hay señal
//   2. Si no está corriendo y hay señal → arranca desde paso 0
//   3. Actualiza el ángulo del marcador visual (para el Editor)
//   4. Aplica fade (rampa de ganancia) para evitar clicks
//   5. Avanza el contador; si se acabó el paso, avanza al siguiente
//   6. Devuelve currentGain (0.0 – 1.0) → el Processor multiplica la señal
float StepSequencer::processSample (float inputSample)
{
    // ── 1. Detección de onset ─────────────────────────────────────────────
    float rms = computeRms (inputSample);

    if (!running)
    {
        if (rms >= kOnsetThreshold)
        {
            // Nuevo onset detectado: resetear todo y arrancar desde paso 0
            resetToStart();
            running       = true;
            targetGain    = activeSteps[0] ? 1.0f : 0.0f;
            currentGain   = 0.0f;
            fadeIncrement  = (targetGain - currentGain) / (float)fadeLengthSamples;
        }
        else
        {
            // Sin señal suficiente: devolver silencio
            return 0.0f;
        }
    }

    // ── 2. Actualizar marcador visual ─────────────────────────────────────
    // Calcula qué fracción del ciclo completo se ha recorrido (t: 0→1)
    // y lo convierte a un ángulo en radianes.
    //   Horario:     ángulo crece (gira a la derecha en pantalla)
    //   Antihorario: ángulo decrece (gira a la izquierda)
    // El offset -halfPi posiciona el inicio en "las 12" del círculo.
    {
        float totalSamples = (float)(stepLengthSamples * numSteps);
        float elapsed      = (float)(currentStep * stepLengthSamples + sampleCounter);
        float t            = elapsed / totalSamples;  // progreso 0.0 – 1.0

        float angle;
        if (clockwise)
            angle =  t * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
        else
            angle = -t * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;

        markerAngle.store (angle);  // atomic: el Editor lo lee desde otro hilo
    }

    // ── 3. Fade (rampa de ganancia) ───────────────────────────────────────
    // Interpola linealmente entre currentGain y targetGain para evitar
    // clicks al cambiar de paso activo a mudo (o viceversa).
    // La rampa dura fadeLengthSamples muestras (~5ms).
    if (currentGain != targetGain)
    {
        currentGain += fadeIncrement;
        // Clampear para no pasarse del target
        if (fadeIncrement > 0.0f)
            currentGain = juce::jmin (currentGain, targetGain);
        else
            currentGain = juce::jmax (currentGain, targetGain);
        if (currentGain == targetGain)
            fadeIncrement = 0.0f;
    }

    // ── 4. Avanzar contador de muestras dentro del paso actual ────────────
    sampleCounter++;
    if (sampleCounter >= stepLengthSamples)
        advanceStep();

    return currentGain;  // 0.0 (silencio) o 1.0 (señal completa), con fade
}

// ═════════════════════════════════════════════════════════════════════════════
// INTERACCIÓN DESDE EL EDITOR
// ═════════════════════════════════════════════════════════════════════════════

// Toggle: invierte el estado de un paso (activo ↔ inactivo).
// Llamado cuando el usuario hace click en un nodo del secuenciador.
void StepSequencer::toggleStep (int index)
{
    if (index >= 0 && index < numSteps)
        activeSteps[index] = !activeSteps[index];
}

// Lectura segura del estado de un paso, con bounds checking.
bool StepSequencer::isStepActive (int i) const
{
    if (i >= 0 && i < (int)activeSteps.size())
        return activeSteps[i];
    return false;
}

// Set directo del estado de un paso. Usado por el sistema de link
// para copiar el patrón de un secuenciador al otro cuando están
// sincronizados (sequencersSync = true).
void StepSequencer::setStep (int index, bool active)
{
    if (index >= 0 && index < numSteps)
        activeSteps[index] = active;
}
