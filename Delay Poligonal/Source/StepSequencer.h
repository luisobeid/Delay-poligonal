// ─────────────────────────────────────────────────────────────────────────────
// StepSequencer.h
// ─────────────────────────────────────────────────────────────────────────────
//
// Secuenciador por pasos circular que actúa como compuerta de ganancia.
// No genera audio: devuelve un valor de gain (0.0 – 1.0) por muestra,
// que el Processor usa para decidir si la señal pasa o se silencia.
//
// Arquitectura:
//   - El Processor llama a processSample() en el hilo de audio (44100+ Hz)
//   - El Editor lee getters (getCurrentStep, isRunning, getMarkerAngle)
//     en el hilo de UI para dibujar el estado visual
//   - Ambos hilos acceden al mismo objeto; markerAngle usa std::atomic
//     para evitar data races sin locks
//
// Flujo de audio:
//   entrada → processSample() → devuelve gain → Processor multiplica señal
//
// ─────────────────────────────────────────────────────────────────────────────

#pragma once
#include <JuceHeader.h>

class StepSequencer
{
public:
    StepSequencer();

    // ── Setup (llamado desde prepareToPlay) ───────────────────────────────
    // Inicializa buffers internos y configura el detector de onset
    void prepare (double sampleRate);

    // Cuántas muestras dura cada paso (calculado por el Processor según
    // BPM y subdivisión rítmica, o milisegundos en modo libre)
    void setStepLengthSamples (int samples);

    // Cantidad de pasos del secuenciador (4 – 32). Si el paso actual
    // queda fuera del rango, resetea al inicio
    void setNumSteps (int steps);

    // ── Control de transporte ─────────────────────────────────────────────
    // Llamado desde processBlock cuando el DAW deja de reproducir.
    // Detiene el secuenciador pero mantiene el paso visual donde estaba.
    // Al detectar nuevo onset, resetea y arranca desde paso 0.
    void stop();

    // ── Dirección de recorrido ────────────────────────────────────────────
    // true  = horario (delay normal):     0 → 1 → 2 → ... → N-1 → 0
    // false = antihorario (reverse delay): 0 → N-1 → N-2 → ... → 1 → 0
    // El cambio es inmediato; el reverse delay en sí lo maneja el Processor.
    void setClockwise (bool cw);
    bool isClockwise() const { return clockwise; }

    // ── Audio thread ──────────────────────────────────────────────────────
    // Función principal: recibe una muestra de audio, actualiza el estado
    // interno (onset, paso actual, fade, ángulo visual) y devuelve el
    // valor de ganancia (0.0 = silencio, 1.0 = señal completa).
    // Se llama UNA VEZ POR MUESTRA desde processBlock.
    float processSample (float inputSample);

    // ── Getters para el Editor (hilo de UI, solo lectura) ─────────────────
    int   getCurrentStep()     const { return currentStep; }
    int   getNumSteps()        const { return numSteps; }
    bool  isStepActive (int i) const;  // ¿el paso i deja pasar audio?
    bool  isRunning()          const { return running; }

    // Ángulo del marcador visual en radianes. Usa std::atomic porque
    // lo escribe el hilo de audio y lo lee el hilo de UI.
    // -halfPi = posición "12 en punto" (arriba del círculo).
    float getMarkerAngle()     const { return markerAngle.load(); }

    // ── Interacción desde el Editor ───────────────────────────────────────
    // Toggle: activa/desactiva un paso al hacer click en la UI
    void toggleStep (int index);

    // Set directo: usado por el link entre secuenciadores para copiar
    // el estado de un paso de L a R o viceversa
    void setStep (int index, bool active);

private:
    // ── Detector de onset por RMS ─────────────────────────────────────────
    // Calcula la energía promedio de la señal en una ventana deslizante.
    // Si supera kOnsetThreshold, se interpreta como "llegó audio" y el
    // secuenciador arranca. Usa un buffer circular y un acumulador
    // incremental para costo O(1) por muestra.
    static constexpr float kOnsetThreshold = 0.01f;   // umbral de activación
    static constexpr float kRmsWindowSec   = 0.010f;  // ventana de 10ms

    float  computeRms (float sample);       // calcula RMS incremental
    double sampleRate    = 44100.0;
    int    rmsWindowSize = 441;             // kRmsWindowSec * sampleRate
    std::vector<float> rmsBuffer;           // buffer circular de muestras
    int    rmsWritePos   = 0;               // posición de escritura en el buffer
    float  rmsSumSq      = 0.0f;            // acumulador de suma de cuadrados
    // Nota: rmsSumSq puede volverse ligeramente negativo por errores de
    // punto flotante acumulados. computeRms() clampea a 0 para evitar
    // que sqrt() devuelva NaN.

    // ── Estado del secuenciador ───────────────────────────────────────────
    int  numSteps          = 16;            // cantidad total de pasos
    int  stepLengthSamples = 22050;         // muestras por paso (0.5s a 44.1kHz)
    int  currentStep       = 0;             // paso actual (0 a numSteps-1)
    int  sampleCounter     = 0;             // contador dentro del paso actual
    bool running           = false;         // ¿está recorriendo pasos?
    bool clockwise         = true;          // dirección de recorrido

    // Patrón de pasos: true = activo (deja pasar audio), false = mudo
    std::vector<bool> activeSteps;

    // ── Fade (suavizado de ganancia) ──────────────────────────────────────
    // Evita clicks al cambiar entre paso activo y mudo. Hace una rampa
    // lineal de kFadeSec segundos entre el gain actual y el target.
    static constexpr float kFadeSec = 0.005f;  // 5ms de fade
    int   fadeLengthSamples = 220;     // kFadeSec * sampleRate
    float currentGain       = 0.0f;    // gain actual (se interpola hacia target)
    float targetGain        = 0.0f;    // gain objetivo (0.0 o 1.0)
    float fadeIncrement     = 0.0f;    // (target - current) / fadeLengthSamples

    // ── Marcador visual ───────────────────────────────────────────────────
    // Ángulo en radianes que indica la posición del marcador en el círculo.
    // atomic<float> para acceso seguro entre hilos sin mutex.
    std::atomic<float> markerAngle { -juce::MathConstants<float>::halfPi };

    // ── Funciones internas ────────────────────────────────────────────────
    void resetToStart();   // todo a cero: paso 0, gain 0, ángulo arriba
    void advanceStep();    // avanza al siguiente paso según la dirección
};
