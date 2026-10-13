// ─────────────────────────────────────────────────────────────────────────────
// PluginEditor.h — Interfaz gráfica del plugin
// ─────────────────────────────────────────────────────────────────────────────
//
// NO procesa audio. Su trabajo es:
//   1. Mostrar controles al usuario (sliders, combos, botones)
//   2. Dibujar los dos secuenciadores circulares a 30fps (Timer)
//   3. Comunicar cambios al Processor cuando el usuario interactúa
//
// Comunicación entre hilos:
//   Editor -> Processor:  *processor.parametro = valor (escritura directa)
//   Processor -> Editor:  seq.getCurrentStep(), seq.getMarkerAngle() (lectura)
//   El Editor NUNCA toca audio. El Processor NUNCA dibuja.
//
// ─────────────────────────────────────────────────────────────────────────────

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class FilterDelayAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer  // timer para refrescar UI a 30fps
{
public:
    FilterDelayAudioProcessorEditor (FilterDelayAudioProcessor&);
    ~FilterDelayAudioProcessorEditor() override;

    // ── Overrides de JUCE ─────────────────────────────────────────────────
    void paint     (juce::Graphics&) override;          // dibuja fondo + secuenciadores
    void resized   () override;                          // calcula layout de todos los controles
    void mouseDown (const juce::MouseEvent&) override;   // click en nodos del secuenciador
    void timerCallback() override;                       // repaint zonas de secuenciadores a 30fps

private:
    // ── Funciones auxiliares ──────────────────────────────────────────────

    // Dibuja un secuenciador circular: nodos en círculo con colores según
    // estado (activo+actual=blanco, actual=gris, activo=rosa, inactivo=oscuro)
    void drawSequencer (juce::Graphics& g, juce::Rectangle<float> area,
                        StepSequencer& seq, const juce::String& label);

    // Muestra/oculta slider de ms o combo de subdivisión según modo Sync/Libre
    void updateVisibilityL();
    void updateVisibilityR();

    // Sincronización entre canales cuando están linkeados:
    //   syncRtoL: copia todos los valores de L a R
    //   syncLtoR: copia todos los valores de R a L
    void syncRtoL();
    void syncLtoR();

    // Referencia al Processor (dueño de datos y parámetros)
    FilterDelayAudioProcessor& processor;

    // ── Controles canal L ─────────────────────────────────────────────────
    // Cada slider tiene un onValueChange que escribe en el parámetro
    // del Processor y, si linked, sincroniza al otro canal.
    juce::Slider     delaySliderL, feedbackSliderL, wetSliderL, cutoffSliderL, numStepsSliderL, driveSliderL;
    juce::Label      delayLabelL    { {}, "miliSegs"  };
    juce::Label      feedbackLabelL { {}, "Feedback"  };
    juce::Label      wetLabelL      { {}, "Mix %"     };
    juce::Label      cutoffLabelL   { {}, "Cutoff" };
    juce::Label      numStepsLabelL { {}, "Pasos"     };
    juce::Label      filterLabelL   { {}, "Filtro"    };
    juce::Label      modeLabelL     { {}, "Modo"      };
    juce::Label      divLabelL      { {}, "subDiv"  };
    juce::Label      driveLabelL    { {}, "Drive"     };
    juce::ComboBox   filterTypeBoxL;     // LPF / HPF
    juce::ComboBox   syncModeBoxL;       // NoSync / Sync
    juce::ComboBox   syncDivBoxL;        // 1/2 ... 1/16T (subdivisiones rítmicas)
    juce::TextButton directionButtonL;   // >>>> (horario) / <<<< (antihorario/reverse)

    // ── Controles canal R (misma estructura que L) ────────────────────────
    juce::Slider     delaySliderR, feedbackSliderR, wetSliderR, cutoffSliderR, numStepsSliderR, driveSliderR;
    juce::Label      delayLabelR    { {}, "miliSegs"  };
    juce::Label      feedbackLabelR { {}, "Feedback"  };
    juce::Label      wetLabelR      { {}, "Mix %"     };
    juce::Label      cutoffLabelR   { {}, "Cutoff" };
    juce::Label      numStepsLabelR { {}, "Pasos"     };
    juce::Label      filterLabelR   { {}, "Filtro"    };
    juce::Label      modeLabelR     { {}, "Modo"      };
    juce::Label      divLabelR      { {}, "subDiv"  };
    juce::Label      driveLabelR    { {}, "Drive"     };
    juce::ComboBox   filterTypeBoxR;
    juce::ComboBox   syncModeBoxR;
    juce::ComboBox   syncDivBoxR;
    juce::TextButton directionButtonR;
    
    // ── Presets (.fsd) ────────────────────────────────────────────────────
    juce::TextButton savePresetButton { "Guardar" };
    juce::TextButton polygonButton    { "Poligonos" };
    juce::TextButton loadPresetButton { "Cargar"  };

    // ── Dibujo de polígonos ───────────────────────────────────────────────
    bool showPolygons = false;
    
    std::unique_ptr<juce::FileChooser> fileChooser;  // diálogo de archivo (async)

    // ── Distorsión ────────────────────────────────────────────────────────
    juce::ComboBox   distModeBoxL;   // Soft / Fuzz / WaveFold
    juce::ComboBox   distModeBoxR;
    juce::Label      distModeLabelL { {}, "Dist" };
    juce::Label      distModeLabelR { {}, "Dist" };

    // ── Compartido ────────────────────────────────────────────────────────
    juce::TextButton linkSeqButton;  // botón de link L<->R (verde = activo)

    // Áreas rectangulares donde se dibujan los secuenciadores circulares.
    // Se calculan en resized() y se usan en paint(), mouseDown(), timerCallback().
    juce::Rectangle<float> seqAreaL, seqAreaR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterDelayAudioProcessorEditor)
};
