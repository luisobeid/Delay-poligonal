// ─────────────────────────────────────────────────────────────────────────────
// PluginEditor.cpp — Implementación de la interfaz gráfica
// ─────────────────────────────────────────────────────────────────────────────
// Este archivo NO contiene lógica de audio. Solo UI:
//   - Configuración de sliders, combos y botones
//   - Callbacks que escriben parámetros en el Processor
//   - Dibujo de secuenciadores circulares
//   - Interacción de mouse para activar pasos
//   - Layout (posiciones y tamaños de controles)
//   - Guardar/cargar presets (.fsd)
// ─────────────────────────────────────────────────────────────────────────────

#include "PluginEditor.h"

// ═════════════════════════════════════════════════════════════════════════════
// FUNCIONES AUXILIARES DE ESTILO
// ═════════════════════════════════════════════════════════════════════════════

// Slider horizontales con caja de texto a la derecha.

static void styleSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 55, 18);
    s.setColour (juce::Slider::trackColourId,             juce::Colour (137, 180, 250));   // azul claro
    s.setColour (juce::Slider::thumbColourId,             juce::Colour (137, 180, 250));
    s.setColour (juce::Slider::backgroundColourId,        juce::Colour (49, 50, 68));      // gris oscuro
    s.setColour (juce::Slider::textBoxTextColourId,       juce::Colour (205, 214, 244));   // texto claro
    s.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (30, 30, 46));      // fondo oscuro
}

// Aplica estilo a un label: texto alineado a la izquierda, color claro.

static void styleLabel (juce::Label& l)
{
    l.setJustificationType (juce::Justification::centredLeft);
    l.setColour (juce::Label::textColourId, juce::Colour (205, 214, 244));
}

// Agrega las subdivisiones rítmicas a un ComboBox.
// IMPORTANTE: los IDs de ComboBox empiezan en 1 (JUCE reserva 0 = "nada seleccionado"),
// por eso los parámetros del Processor usan getSelectedId() - 1. !!!
static void addDivItems (juce::ComboBox& b)
{
    b.addItem ("1/2", 1); b.addItem ("1/4", 2); b.addItem ("1/8D", 3);
    b.addItem ("1/8", 4); b.addItem ("1/8T", 5);
    b.addItem ("1/16", 6); b.addItem ("1/16T", 7);
}

// ═════════════════════════════════════════════════════════════════════════════
// SINCRONIZACIÓN ENTRE CANALES
// ═════════════════════════════════════════════════════════════════════════════
// Cuando los secuenciadores están linkeados (sequencersSync = true),
// editar un canal copia automáticamente todos los valores al otro.

// syncRtoL: copia todos los valores del canal L al canal R.
// Se llama cuando se edita cualquier control de L estando linkeados.

// Flujo: actualiza sliders R -> escribe parámetros R -> copia dirección -> actualiza UI.

void FilterDelayAudioProcessorEditor::syncRtoL()
{
    // Copiar valores de sliders L a R (sin notificar para evitar loops infinitos)
    feedbackSliderR .setValue (feedbackSliderL .getValue(), juce::dontSendNotification);
    wetSliderR      .setValue (wetSliderL      .getValue(), juce::dontSendNotification);
    cutoffSliderR   .setValue (cutoffSliderL   .getValue(), juce::dontSendNotification);
    numStepsSliderR .setValue (numStepsSliderL .getValue(), juce::dontSendNotification);
    driveSliderR    .setValue (driveSliderL    .getValue(), juce::dontSendNotification);
    delaySliderR    .setValue (delaySliderL    .getValue(), juce::dontSendNotification);
    // Copiar combos
    filterTypeBoxR.setSelectedId (filterTypeBoxL.getSelectedId(), juce::dontSendNotification);
    syncModeBoxR  .setSelectedId (syncModeBoxL  .getSelectedId(), juce::dontSendNotification);
    syncDivBoxR   .setSelectedId (syncDivBoxL   .getSelectedId(), juce::dontSendNotification);
    distModeBoxR  .setSelectedId (distModeBoxL  .getSelectedId(), juce::dontSendNotification);
    // Escribir parámetros del Processor (el audio thread los lee)
    *processor.feedbackR     = (float)feedbackSliderR.getValue();
    *processor.wetPercentR   = (float)wetSliderR.getValue();
    *processor.cutoffHzR     = (float)cutoffSliderR.getValue();
    *processor.numStepsR     = (int)numStepsSliderR.getValue();
    *processor.driveR        = (float)driveSliderR.getValue();
    *processor.delayTimeR    = (float)delaySliderR.getValue();
    *processor.filterTypeR   = filterTypeBoxR.getSelectedId() - 1;   // -1: ID combo -> índice
    *processor.syncModeR     = syncModeBoxR.getSelectedId() - 1;
    *processor.syncDivisionR = syncDivBoxR.getSelectedId() - 1;
    *processor.distModeR     = distModeBoxR.getSelectedId() - 1;
    // Copiar dirección del secuenciador
    bool cwL = processor.getStepSequencerL().isClockwise();
    processor.getStepSequencerR().setClockwise (cwL);
    directionButtonR.setToggleState (cwL, juce::dontSendNotification);
    directionButtonR.setButtonText (cwL ? ">>>>" : "<<<<");
    directionButtonR.setColour (juce::TextButton::buttonColourId,
                                cwL ? juce::Colour (68, 71, 90) : juce::Colour (139, 0, 0));
    updateVisibilityR();
    resized();
}

// syncLtoR: copia todos los valores del canal R al canal L.
// Se llama cuando se edita cualquier control de R estando linkeados.
void FilterDelayAudioProcessorEditor::syncLtoR()
{
    feedbackSliderL .setValue (feedbackSliderR .getValue(), juce::dontSendNotification);
    wetSliderL      .setValue (wetSliderR      .getValue(), juce::dontSendNotification);
    cutoffSliderL   .setValue (cutoffSliderR   .getValue(), juce::dontSendNotification);
    numStepsSliderL .setValue (numStepsSliderR .getValue(), juce::dontSendNotification);
    driveSliderL    .setValue (driveSliderR    .getValue(), juce::dontSendNotification);
    delaySliderL    .setValue (delaySliderR    .getValue(), juce::dontSendNotification);
    filterTypeBoxL.setSelectedId (filterTypeBoxR.getSelectedId(), juce::dontSendNotification);
    syncModeBoxL  .setSelectedId (syncModeBoxR  .getSelectedId(), juce::dontSendNotification);
    syncDivBoxL   .setSelectedId (syncDivBoxR   .getSelectedId(), juce::dontSendNotification);
    distModeBoxL  .setSelectedId (distModeBoxR  .getSelectedId(), juce::dontSendNotification);
    *processor.feedbackL    = (float)feedbackSliderL.getValue();
    *processor.wetPercentL  = (float)wetSliderL.getValue();
    *processor.cutoffHzL    = (float)cutoffSliderL.getValue();
    *processor.numStepsL    = (int)numStepsSliderL.getValue();
    *processor.driveL       = (float)driveSliderL.getValue();
    *processor.delayTimeL   = (float)delaySliderL.getValue();
    *processor.filterTypeL  = filterTypeBoxL.getSelectedId() - 1;
    *processor.syncModeL    = syncModeBoxL.getSelectedId() - 1;
    *processor.syncDivisionL= syncDivBoxL.getSelectedId() - 1;
    *processor.distModeL    = distModeBoxL.getSelectedId() - 1;
    bool cwR = processor.getStepSequencerR().isClockwise();
    processor.getStepSequencerL().setClockwise (cwR);
    directionButtonL.setToggleState (cwR, juce::dontSendNotification);
    directionButtonL.setButtonText (cwR ? ">>>>" : "<<<<");
    directionButtonL.setColour (juce::TextButton::buttonColourId,
                                cwR ? juce::Colour (68, 71, 90) : juce::Colour (139, 0, 0));
    updateVisibilityL();
    resized();
}

// ═════════════════════════════════════════════════════════════════════════════
// CONSTRUCTOR — crea todos los controles y asigna callbacks
// ═════════════════════════════════════════════════════════════════════════════
// Cada control tiene un callback (onValueChange / onChange / onClick) que:
//   1. Escribe el nuevo valor en el parámetro del Processor
//   2. Si está linkeado, llama syncRtoL() o syncLtoR()
// Los valores iniciales se leen del Processor (por si se cargó un preset).

FilterDelayAudioProcessorEditor::FilterDelayAudioProcessorEditor (FilterDelayAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // ── Sliders L: estilo + rango + valor inicial + callback ──────────────
    styleSlider (delaySliderL);    styleSlider (feedbackSliderL);
    styleSlider (wetSliderL);      styleSlider (cutoffSliderL);
    styleSlider (numStepsSliderL); styleSlider (driveSliderL);

    delaySliderL.setRange (1.0, 2000.0, 1.0);           // 1ms – 2000ms, paso 1ms
    delaySliderL.setValue (processor.delayTimeL->get());
    delaySliderL.onValueChange = [this] {
        *processor.delayTimeL = (float)delaySliderL.getValue();
        if (processor.sequencersSync->get()) syncRtoL();
    };

    feedbackSliderL.setRange (0.0, 0.99, 0.01);         // 0% – 99%, paso 1%
    feedbackSliderL.setValue (processor.feedbackL->get());
    feedbackSliderL.onValueChange = [this] {
        *processor.feedbackL = (float)feedbackSliderL.getValue();
        if (processor.sequencersSync->get()) syncRtoL();
    };

    wetSliderL.setRange (0.0, 100.0, 1.0);              // 0% – 100%
    wetSliderL.setValue (processor.wetPercentL->get());
    wetSliderL.onValueChange = [this] {
        *processor.wetPercentL = (float)wetSliderL.getValue();
        if (processor.sequencersSync->get()) syncRtoL();
    };

    cutoffSliderL.setRange (20.0, 20000.0, 1.0);        // 20Hz – 20kHz
    cutoffSliderL.setSkewFactorFromMidPoint (1000.0);    // escala logarítmica centrada en 1kHz
    cutoffSliderL.setValue (processor.cutoffHzL->get());
    cutoffSliderL.onValueChange = [this] {
        *processor.cutoffHzL = (float)cutoffSliderL.getValue();
        if (processor.sequencersSync->get()) syncRtoL();
    };

    numStepsSliderL.setRange (4.0, 32.0, 1.0);          // 4 – 32 pasos
    numStepsSliderL.setValue (processor.numStepsL->get());
    numStepsSliderL.onValueChange = [this] {
        *processor.numStepsL = (int)numStepsSliderL.getValue();
        if (processor.sequencersSync->get()) syncRtoL();
    };

    driveSliderL.setRange (1.0, 10.0, 0.1);             // 1x – 10x distorsión
    driveSliderL.setValue (processor.driveL->get());
    driveSliderL.onValueChange = [this] {
        *processor.driveL = (float)driveSliderL.getValue();
        if (processor.sequencersSync->get()) syncRtoL();
    };

    // ── Sliders R (misma estructura, callback llama syncLtoR) ─────────────
    styleSlider (delaySliderR);    styleSlider (feedbackSliderR);
    styleSlider (wetSliderR);      styleSlider (cutoffSliderR);
    styleSlider (numStepsSliderR); styleSlider (driveSliderR);

    delaySliderR.setRange (1.0, 2000.0, 1.0);
    delaySliderR.setValue (processor.delayTimeR->get());
    delaySliderR.onValueChange = [this] {
        *processor.delayTimeR = (float)delaySliderR.getValue();
        if (processor.sequencersSync->get()) syncLtoR();
    };

    feedbackSliderR.setRange (0.0, 0.99, 0.01);
    feedbackSliderR.setValue (processor.feedbackR->get());
    feedbackSliderR.onValueChange = [this] {
        *processor.feedbackR = (float)feedbackSliderR.getValue();
        if (processor.sequencersSync->get()) syncLtoR();
    };

    wetSliderR.setRange (0.0, 100.0, 1.0);
    wetSliderR.setValue (processor.wetPercentR->get());
    wetSliderR.onValueChange = [this] {
        *processor.wetPercentR = (float)wetSliderR.getValue();
        if (processor.sequencersSync->get()) syncLtoR();
    };

    cutoffSliderR.setRange (20.0, 20000.0, 1.0);
    cutoffSliderR.setSkewFactorFromMidPoint (1000.0);
    cutoffSliderR.setValue (processor.cutoffHzR->get());
    cutoffSliderR.onValueChange = [this] {
        *processor.cutoffHzR = (float)cutoffSliderR.getValue();
        if (processor.sequencersSync->get()) syncLtoR();
    };

    numStepsSliderR.setRange (4.0, 32.0, 1.0);
    numStepsSliderR.setValue (processor.numStepsR->get());
    numStepsSliderR.onValueChange = [this] {
        *processor.numStepsR = (int)numStepsSliderR.getValue();
        if (processor.sequencersSync->get()) syncLtoR();
    };

    driveSliderR.setRange (1.0, 10.0, 0.1);
    driveSliderR.setValue (processor.driveR->get());
    driveSliderR.onValueChange = [this] {
        *processor.driveR = (float)driveSliderR.getValue();
        if (processor.sequencersSync->get()) syncLtoR();
    };

    // ── Combos L ──────────────────────────────────────────────────────────

    // Tipo de filtro: LPF (pasa bajos) o HPF (pasa altos)
    filterTypeBoxL.addItem ("LPF", 1); filterTypeBoxL.addItem ("HPF", 2);
    filterTypeBoxL.setSelectedId (processor.filterTypeL->getIndex() + 1);
    filterTypeBoxL.onChange = [this] {
        *processor.filterTypeL = filterTypeBoxL.getSelectedId() - 1;
        if (processor.sequencersSync->get()) syncRtoL();
    };

    // Modo de sync: NoSync (usa ms) o Sync (usa subdivisión del DAW)
    syncModeBoxL.addItem ("NoSync", 1); syncModeBoxL.addItem ("Sync", 2);
    syncModeBoxL.setSelectedId (processor.syncModeL->getIndex() + 1);
    syncModeBoxL.onChange = [this] {
        *processor.syncModeL = syncModeBoxL.getSelectedId() - 1;
        updateVisibilityL(); resized();  // mostrar/ocultar slider ms vs combo subdiv
        if (processor.sequencersSync->get()) syncRtoL();
    };

    // Subdivisión rítmica (solo visible en modo Sync)
    addDivItems (syncDivBoxL);
    syncDivBoxL.setSelectedId (processor.syncDivisionL->getIndex() + 1);
    syncDivBoxL.onChange = [this] {
        *processor.syncDivisionL = syncDivBoxL.getSelectedId() - 1;
        if (processor.sequencersSync->get()) syncRtoL();
    };

    // Tipo de distorsión
    distModeBoxL.addItem ("Soft", 1); distModeBoxL.addItem ("Fuzz", 2);
    distModeBoxL.addItem ("WaveFold", 3);
    distModeBoxL.setSelectedId (processor.distModeL->getIndex() + 1);
    distModeBoxL.onChange = [this] {
        *processor.distModeL = distModeBoxL.getSelectedId() - 1;
        if (processor.sequencersSync->get()) syncRtoL();
    };

    // ── Combos R (misma estructura, callback llama syncLtoR) ──────────────
    filterTypeBoxR.addItem ("LPF", 1); filterTypeBoxR.addItem ("HPF", 2);
    filterTypeBoxR.setSelectedId (processor.filterTypeR->getIndex() + 1);
    filterTypeBoxR.onChange = [this] {
        *processor.filterTypeR = filterTypeBoxR.getSelectedId() - 1;
        if (processor.sequencersSync->get()) syncLtoR();
    };

    syncModeBoxR.addItem ("NoSync", 1); syncModeBoxR.addItem ("Sync", 2);
    syncModeBoxR.setSelectedId (processor.syncModeR->getIndex() + 1);
    syncModeBoxR.onChange = [this] {
        *processor.syncModeR = syncModeBoxR.getSelectedId() - 1;
        updateVisibilityR(); resized();
        if (processor.sequencersSync->get()) syncLtoR();
    };

    addDivItems (syncDivBoxR);
    syncDivBoxR.setSelectedId (processor.syncDivisionR->getIndex() + 1);
    syncDivBoxR.onChange = [this] {
        *processor.syncDivisionR = syncDivBoxR.getSelectedId() - 1;
        if (processor.sequencersSync->get()) syncLtoR();
    };

    distModeBoxR.addItem ("Soft", 1); distModeBoxR.addItem ("Fuzz", 2);
    distModeBoxR.addItem ("WaveFold", 3);
    distModeBoxR.setSelectedId (processor.distModeR->getIndex() + 1);
    distModeBoxR.onChange = [this] {
        *processor.distModeR = distModeBoxR.getSelectedId() - 1;
        if (processor.sequencersSync->get()) syncLtoR();
    };

    // ── Botones de dirección ──────────────────────────────────────────────
    // Toggle: ON = horario (>>>>), OFF = antihorario (<<<<, activa reverse delay)
    // Color: gris = normal, rojo oscuro = reverse activo

    directionButtonL.setClickingTogglesState (true);
    directionButtonL.setToggleState (true, juce::dontSendNotification);
    directionButtonL.setButtonText (">>>>");
    directionButtonL.setColour (juce::TextButton::buttonColourId, juce::Colour (68, 71, 90));
    directionButtonL.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    directionButtonL.onClick = [this]() {
        bool cw = directionButtonL.getToggleState();
        processor.getStepSequencerL().setClockwise (cw);
        directionButtonL.setButtonText (cw ? ">>>>" : "<<<<");
        directionButtonL.setColour (juce::TextButton::buttonColourId,
                                    cw ? juce::Colour (68, 71, 90) : juce::Colour (139, 0, 0));
        // Si linkeados, copiar dirección al otro canal
        if (processor.sequencersSync->get())
        {
            processor.getStepSequencerR().setClockwise (cw);
            directionButtonR.setToggleState (cw, juce::dontSendNotification);
            directionButtonR.setButtonText (cw ? ">>>>" : "<<<<");
            directionButtonR.setColour (juce::TextButton::buttonColourId,
                                        cw ? juce::Colour (68, 71, 90) : juce::Colour (139, 0, 0));
        }
    };

    directionButtonR.setClickingTogglesState (true);
    directionButtonR.setToggleState (true, juce::dontSendNotification);
    directionButtonR.setButtonText (">>>>");
    directionButtonR.setColour (juce::TextButton::buttonColourId, juce::Colour (68, 71, 90));
    directionButtonR.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    directionButtonR.onClick = [this]() {
        bool cw = directionButtonR.getToggleState();
        processor.getStepSequencerR().setClockwise (cw);
        directionButtonR.setButtonText (cw ? ">>>>" : "<<<<");
        directionButtonR.setColour (juce::TextButton::buttonColourId,
                                    cw ? juce::Colour (68, 71, 90) : juce::Colour (139, 0, 0));
        if (processor.sequencersSync->get())
        {
            processor.getStepSequencerL().setClockwise (cw);
            directionButtonL.setToggleState (cw, juce::dontSendNotification);
            directionButtonL.setButtonText (cw ? ">>>>" : "<<<<");
            directionButtonL.setColour (juce::TextButton::buttonColourId,
                                        cw ? juce::Colour (68, 71, 90) : juce::Colour (139, 0, 0));
        }
    };

    // ── Botón Link ────────────────────────────────────────────────────────
    // Toggle: activa/desactiva la sincronización entre canales L y R.
    // Cuando se activa, copia inmediatamente L -> R con syncRtoL().
    // Verde = linkeados, gris = independientes.
    linkSeqButton.setClickingTogglesState (true);
    linkSeqButton.setToggleState (processor.sequencersSync->get(), juce::dontSendNotification);
    linkSeqButton.setButtonText ("Linked");
    linkSeqButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0, 180, 0));
    linkSeqButton.setColour (juce::TextButton::buttonColourId,
                             processor.sequencersSync->get() ? juce::Colour (0, 180, 0)
                                                             : juce::Colour (108, 112, 134));
    linkSeqButton.setColour (juce::TextButton::textColourOffId,
                             processor.sequencersSync->get() ? juce::Colour(255,255,255)
                                                             : juce::Colour(170,170,170));
    
    
    linkSeqButton.onClick = [this]() {
        bool s = linkSeqButton.getToggleState();
        *processor.sequencersSync = s;
        linkSeqButton.setColour (juce::TextButton::buttonColourId,
                                 s ? juce::Colour (0, 180, 0) : juce::Colour (108, 112, 134));
        linkSeqButton.setColour (juce::TextButton::textColourOffId,
                                 s ? juce::Colour (255, 255, 255) : juce::Colour (170,170,170));
        if (s) syncRtoL();  // al activar link, igualar canales inmediatamente
    };

    // ── Botones de presets ────────────────────────────────────────────────
    // Guardar: serializa todo el estado a un archivo .fsd
    savePresetButton.onClick = [this]() {
        fileChooser = std::make_unique<juce::FileChooser> ("Guardar preset",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fsd");
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode |
                                   juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File{})
                {
                    juce::MemoryBlock data;
                    processor.getStateInformation (data);  // serializar estado
                    file.withFileExtension ("fsd").replaceWithData (data.getData(), data.getSize());
                }
            });
    };

    // Cargar: lee un .fsd, restaura el estado y actualiza TODOS los controles de la UI
    loadPresetButton.onClick = [this]() {
        fileChooser = std::make_unique<juce::FileChooser> ("Cargar preset",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fsd");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode |
                                   juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File{})
                {
                    juce::MemoryBlock data;
                    file.loadFileAsData (data);
                    processor.setStateInformation (data.getData(), (int)data.getSize());
                    // Sincronizar TODOS los controles de la UI con los nuevos valores
                    delaySliderL    .setValue (processor.delayTimeL->get(),    juce::dontSendNotification);
                    delaySliderR    .setValue (processor.delayTimeR->get(),    juce::dontSendNotification);
                    feedbackSliderL .setValue (processor.feedbackL->get(),     juce::dontSendNotification);
                    feedbackSliderR .setValue (processor.feedbackR->get(),     juce::dontSendNotification);
                    wetSliderL      .setValue (processor.wetPercentL->get(),   juce::dontSendNotification);
                    wetSliderR      .setValue (processor.wetPercentR->get(),   juce::dontSendNotification);
                    cutoffSliderL   .setValue (processor.cutoffHzL->get(),     juce::dontSendNotification);
                    cutoffSliderR   .setValue (processor.cutoffHzR->get(),     juce::dontSendNotification);
                    numStepsSliderL .setValue (processor.numStepsL->get(),     juce::dontSendNotification);
                    numStepsSliderR .setValue (processor.numStepsR->get(),     juce::dontSendNotification);
                    driveSliderL    .setValue (processor.driveL->get(),        juce::dontSendNotification);
                    driveSliderR    .setValue (processor.driveR->get(),        juce::dontSendNotification);
                    filterTypeBoxL  .setSelectedId (processor.filterTypeL->getIndex()   + 1, juce::dontSendNotification);
                    filterTypeBoxR  .setSelectedId (processor.filterTypeR->getIndex()   + 1, juce::dontSendNotification);
                    syncModeBoxL    .setSelectedId (processor.syncModeL->getIndex()     + 1, juce::dontSendNotification);
                    syncModeBoxR    .setSelectedId (processor.syncModeR->getIndex()     + 1, juce::dontSendNotification);
                    syncDivBoxL     .setSelectedId (processor.syncDivisionL->getIndex() + 1, juce::dontSendNotification);
                    syncDivBoxR     .setSelectedId (processor.syncDivisionR->getIndex() + 1, juce::dontSendNotification);
                    distModeBoxL    .setSelectedId (processor.distModeL->getIndex()     + 1, juce::dontSendNotification);
                    distModeBoxR    .setSelectedId (processor.distModeR->getIndex()     + 1, juce::dontSendNotification);
                    // Actualizar botón de link
                    bool linked = processor.sequencersSync->get();
                    linkSeqButton.setToggleState (linked, juce::dontSendNotification);
                    linkSeqButton.setColour (juce::TextButton::buttonColourId,
                                            linked ? juce::Colour (0, 180, 0) : juce::Colour (108, 112, 134));
                    // Actualizar botones de dirección
                    bool cwL = processor.getStepSequencerL().isClockwise();
                    bool cwR = processor.getStepSequencerR().isClockwise();
                    directionButtonL.setToggleState (cwL, juce::dontSendNotification);
                    directionButtonL.setButtonText (cwL ? ">>>>" : "<<<<");
                    directionButtonL.setColour (juce::TextButton::buttonColourId,
                                               cwL ? juce::Colour (68, 71, 90) : juce::Colour (139, 0, 0));
                    directionButtonR.setToggleState (cwR, juce::dontSendNotification);
                    directionButtonR.setButtonText (cwR ? ">>>>" : "<<<<");
                    directionButtonR.setColour (juce::TextButton::buttonColourId,
                                               cwR ? juce::Colour (68, 71, 90) : juce::Colour (139, 0, 0));
                    updateVisibilityL(); updateVisibilityR();
                    resized(); repaint();
                }
            });
    };

    // ── Botón de polígonos ────────────────────────────────────────────────
    polygonButton.setClickingTogglesState (true);
    polygonButton.setToggleState (false, juce::dontSendNotification);
    polygonButton.setColour (juce::TextButton::buttonColourId, juce::Colour (108, 112, 134));
    polygonButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (255, 121, 198));
    polygonButton.setColour (juce::TextButton::textColourOffId, juce::Colour (170, 170, 170));
    polygonButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    polygonButton.onClick = [this]() {
        showPolygons = polygonButton.getToggleState();
        repaint();
    };

    // ── Estilo de labels ──────────────────────────────────────────────────
    styleLabel (delayLabelL);    styleLabel (feedbackLabelL);
    styleLabel (wetLabelL);      styleLabel (cutoffLabelL);
    styleLabel (numStepsLabelL); styleLabel (filterLabelL);
    styleLabel (modeLabelL);     styleLabel (divLabelL);
    styleLabel (driveLabelL);    styleLabel (distModeLabelL);

    styleLabel (delayLabelR);    styleLabel (feedbackLabelR);
    styleLabel (wetLabelR);      styleLabel (cutoffLabelR);
    styleLabel (numStepsLabelR); styleLabel (filterLabelR);
    styleLabel (modeLabelR);     styleLabel (divLabelR);
    styleLabel (driveLabelR);    styleLabel (distModeLabelR);

    // ── Registrar todos los componentes como visibles ─────────────────────
    addAndMakeVisible (delaySliderL);    addAndMakeVisible (feedbackSliderL);
    addAndMakeVisible (wetSliderL);      addAndMakeVisible (cutoffSliderL);
    addAndMakeVisible (numStepsSliderL); addAndMakeVisible (driveSliderL);
    addAndMakeVisible (filterTypeBoxL);  addAndMakeVisible (syncModeBoxL);
    addAndMakeVisible (syncDivBoxL);     addAndMakeVisible (distModeBoxL);
    addAndMakeVisible (directionButtonL);

    addAndMakeVisible (delaySliderR);    addAndMakeVisible (feedbackSliderR);
    addAndMakeVisible (wetSliderR);      addAndMakeVisible (cutoffSliderR);
    addAndMakeVisible (numStepsSliderR); addAndMakeVisible (driveSliderR);
    addAndMakeVisible (filterTypeBoxR);  addAndMakeVisible (syncModeBoxR);
    addAndMakeVisible (syncDivBoxR);     addAndMakeVisible (distModeBoxR);
    addAndMakeVisible (directionButtonR);

    addAndMakeVisible (linkSeqButton);
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (polygonButton);
    addAndMakeVisible (loadPresetButton);

    addAndMakeVisible (delayLabelL);     addAndMakeVisible (feedbackLabelL);
    addAndMakeVisible (wetLabelL);       addAndMakeVisible (cutoffLabelL);
    addAndMakeVisible (numStepsLabelL);  addAndMakeVisible (filterLabelL);
    addAndMakeVisible (modeLabelL);      addAndMakeVisible (divLabelL);
    addAndMakeVisible (driveLabelL);     addAndMakeVisible (distModeLabelL);

    addAndMakeVisible (delayLabelR);     addAndMakeVisible (feedbackLabelR);
    addAndMakeVisible (wetLabelR);       addAndMakeVisible (cutoffLabelR);
    addAndMakeVisible (numStepsLabelR);  addAndMakeVisible (filterLabelR);
    addAndMakeVisible (modeLabelR);      addAndMakeVisible (divLabelR);
    addAndMakeVisible (driveLabelR);     addAndMakeVisible (distModeLabelR);

    // Aplicar visibilidad inicial (ocultar slider ms o combo subdiv según modo)
    updateVisibilityL();
    updateVisibilityR();

    // Arrancar timer para refrescar los secuenciadores a 30fps
    startTimerHz (30);

    // Tamaño de la ventana del plugin
    setSize (572, 720);
}

FilterDelayAudioProcessorEditor::~FilterDelayAudioProcessorEditor()
{
    stopTimer();  // detener repaint antes de destruir
}

// ═════════════════════════════════════════════════════════════════════════════
// VISIBILIDAD CONDICIONAL
// ═════════════════════════════════════════════════════════════════════════════
// En modo Sync: ocultar slider de ms, mostrar combo de subdivisión
// En modo Libre: mostrar slider de ms, ocultar combo de subdivisión

void FilterDelayAudioProcessorEditor::updateVisibilityL()
{
    bool isSync = (syncModeBoxL.getSelectedId() == 2);
    delaySliderL.setVisible (!isSync);
    delayLabelL .setVisible (!isSync);
    syncDivBoxL .setVisible (isSync);
    divLabelL   .setVisible (isSync);
}

void FilterDelayAudioProcessorEditor::updateVisibilityR()
{
    bool isSync = (syncModeBoxR.getSelectedId() == 2);
    delaySliderR.setVisible (!isSync);
    delayLabelR .setVisible (!isSync);
    syncDivBoxR .setVisible (isSync);
    divLabelR   .setVisible (isSync);
}

// ═════════════════════════════════════════════════════════════════════════════
// TIMER — refresco de UI a 30fps
// ═════════════════════════════════════════════════════════════════════════════
// Solo repinta las áreas de los secuenciadores (no toda la ventana)
// para minimizar el costo de CPU del repaint.

void FilterDelayAudioProcessorEditor::timerCallback()
{
    repaint (seqAreaL.toNearestInt());
    repaint (seqAreaR.toNearestInt());
}

// ═════════════════════════════════════════════════════════════════════════════
// MOUSE DOWN — interacción con nodos del secuenciador
// ═════════════════════════════════════════════════════════════════════════════
// Detecta si el click cayó sobre un nodo (distancia < nodeR * 2).
// Si sí, toggle ese paso. Si linkeados, copia al otro canal.

void FilterDelayAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    // Lambda que maneja el click en un secuenciador dado
    auto handleSeq = [&](StepSequencer& seq, juce::Rectangle<float>& area)
    {
        int   nSteps   = seq.getNumSteps();
        auto  mousePos = event.getPosition().toFloat();
        auto  centre   = area.getCentre();
        float radius   = juce::jmin (area.getWidth(), area.getHeight()) * 0.42f;
        float nodeR    = 8.0f;  // radio visual del nodo

        for (int i = 0; i < nSteps; ++i)
        {
            // Calcular posición del nodo i en el círculo
            float angle = ((float)i / (float)nSteps)
                          * juce::MathConstants<float>::twoPi
                          - juce::MathConstants<float>::halfPi;
            juce::Point<float> nodePos (centre.x + radius * std::cos (angle),
                                        centre.y + radius * std::sin (angle));

            // Si el click está dentro del radio de tolerancia
            if (mousePos.getDistanceFrom (nodePos) < nodeR * 2.0f)
            {
                seq.toggleStep (i);  // invertir estado del paso

                // Si linkeados, copiar el nuevo estado al otro canal
                if (processor.sequencersSync->get())
                {
                    bool newState = seq.isStepActive (i);
                    if (&seq == &processor.getStepSequencerL())
                        processor.getStepSequencerR().setStep (i, newState);
                    else
                        processor.getStepSequencerL().setStep (i, newState);
                }
                repaint();
                break;  // solo un nodo por click
            }
        }
    };

    // Determinar en qué secuenciador cayó el click
    if (seqAreaL.contains (event.getPosition().toFloat()))
        handleSeq (processor.getStepSequencerL(), seqAreaL);
    else if (seqAreaR.contains (event.getPosition().toFloat()))
        handleSeq (processor.getStepSequencerR(), seqAreaR);
}

// ═════════════════════════════════════════════════════════════════════════════
// PAINT — dibujo principal
// ═════════════════════════════════════════════════════════════════════════════

void FilterDelayAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Fondo oscuro (Catppuccin Mocha base)
    g.fillAll (juce::Colour (30, 30, 46));

    // Fondos de los secuenciadores (rectángulos redondeados más oscuros)
    g.setColour (juce::Colour (24, 24, 37));
    g.fillRoundedRectangle (seqAreaL.expanded (4), 8.0f);
    g.fillRoundedRectangle (seqAreaR.expanded (4), 8.0f);

    // Título del plugin
    g.setColour (juce::Colour (203, 166, 247));  // lavanda
    g.setFont (juce::Font (15.0f).boldened());
    g.drawText ("Delay Poligonal", 0, 4, getWidth(), 24, juce::Justification::centred);

    // Línea separadora entre secuenciadores y controles
    g.setColour (juce::Colour (49, 50, 68));
    g.drawHorizontalLine ((int)seqAreaL.getBottom() + 8, 10.0f, (float)getWidth() - 10.0f);

    // Dibujar ambos secuenciadores circulares
    drawSequencer (g, seqAreaL, processor.getStepSequencerL(), "L");
    drawSequencer (g, seqAreaR, processor.getStepSequencerR(), "R");
}

// ═════════════════════════════════════════════════════════════════════════════
// DRAW SEQUENCER — dibuja un secuenciador circular
// ═════════════════════════════════════════════════════════════════════════════
// Nodos distribuidos uniformemente en un círculo. Colores según estado:
//   Blanco + rosa:      paso activo + actual (sonando ahora)
//   Gris claro + azul:  paso actual pero inactivo (silencio)
//   Rosa:               paso activo (sonará cuando llegue el marcador)
//   Gris oscuro:        paso inactivo

void FilterDelayAudioProcessorEditor::drawSequencer (juce::Graphics& g,
                                                      juce::Rectangle<float> area,
                                                      StepSequencer& seq,
                                                      const juce::String& label)
{
    int   nSteps = seq.getNumSteps();
    if (nSteps <= 0) return;

    auto  centre = area.getCentre();
    float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.42f;
    float nodeR  = juce::jmax (3.0f, juce::jmin (8.0f, radius * 0.16f));  // tamaño adaptativo
    bool  cw     = seq.isClockwise();

    // Círculo guía: gris si horario, rojo si reverse
    g.setColour (cw ? juce::Colour (69, 71, 90) : juce::Colour (90, 26, 26));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // Dibujar cada nodo
    for (int i = 0; i < nSteps; ++i)
    {
        // Posición en el círculo: distribuido uniformemente, empezando arriba (-halfPi)
        float angle = ((float)i / (float)nSteps)
                      * juce::MathConstants<float>::twoPi
                      - juce::MathConstants<float>::halfPi;
        juce::Point<float> pos (centre.x + radius * std::cos (angle),
                                centre.y + radius * std::sin (angle));

        bool isActive  = seq.isStepActive (i);
        bool isCurrent = (seq.getCurrentStep() == i) && seq.isRunning();

        // Seleccionar colores según estado
        juce::Colour fill, border;
        if      (isCurrent && isActive) { fill = juce::Colour (255,255,255); border = juce::Colour (255,121,198); }
        else if (isCurrent)             { fill = juce::Colour (205,214,244); border = juce::Colour (137,180,250); }
        else if (isActive)              { fill = juce::Colour (255,121,198); border = juce::Colour (255,160,220); }
        else                            { fill = juce::Colour (69, 71, 90);  border = juce::Colour (108,112,134); }

        g.setColour (fill);
        g.fillEllipse (pos.x - nodeR, pos.y - nodeR, nodeR * 2.0f, nodeR * 2.0f);
        g.setColour (border);
        g.drawEllipse (pos.x - nodeR, pos.y - nodeR, nodeR * 2.0f, nodeR * 2.0f, 1.0f);
    }

    // Polígono entre nodos activos
    if (showPolygons)
    {
        std::vector<juce::Point<float>> activePoints;
        for (int i = 0; i < nSteps; ++i)
        {
            if (seq.isStepActive (i))
            {
                float angle = ((float)i / (float)nSteps)
                              * juce::MathConstants<float>::twoPi
                              - juce::MathConstants<float>::halfPi;
                activePoints.push_back ({ centre.x + radius * std::cos (angle),
                                          centre.y + radius * std::sin (angle) });
            }
        }

        if (activePoints.size() >= 2)
        {
            // Relleno semitransparente
            juce::Path polyPath;
            polyPath.startNewSubPath (activePoints[0]);
            for (size_t i = 1; i < activePoints.size(); ++i)
                polyPath.lineTo (activePoints[i]);
            polyPath.closeSubPath();

            g.setColour (juce::Colour (255, 121, 198).withAlpha (0.06f));
            g.fillPath (polyPath);

            // Líneas del contorno
            g.setColour (juce::Colour (255, 121, 198).withAlpha (0.35f));
            for (size_t i = 0; i < activePoints.size(); ++i)
            {
                auto& a = activePoints[i];
                auto& b = activePoints[(i + 1) % activePoints.size()];
                g.drawLine (a.x, a.y, b.x, b.y, 1.2f);
            }
        }
    }

    // Label central ("L" o "R")
    g.setColour (juce::Colour (108, 112, 134));
    g.setFont (juce::Font (18.0f).boldened());
    g.drawText (label, juce::Rectangle<float> (centre.x - 12, centre.y - 12, 24, 24),
                juce::Justification::centred);
}

// ═════════════════════════════════════════════════════════════════════════════
// RESIZED — layout de todos los controles
// ═════════════════════════════════════════════════════════════════════════════
// Calcula posiciones de: secuenciadores (arriba), botones de dirección/link
// (medio), filas de controles L y R (abajo), botones de preset (fondo).
// Las dos columnas (L y R) están separadas por el ancho del botón de link.

void FilterDelayAudioProcessorEditor::resized()
{
    const int w      = getWidth();
    const int pad    = 8;        // padding general
    const int titleH = 30;       // alto del título
    const int labelW = 62;       // ancho de labels
    const int rowH   = 30;       // alto de cada fila de controles
    const int btnH   = 26;       // alto de botones
    const int syncW  = 44;       // ancho de la zona central (link button)
    const int comboH = 24;       // alto de combos

    // Calcular ancho de cada columna de canal
    int channelW = (w - pad * 2 - syncW) / 2;

    // Calcular alto disponible para los secuenciadores
    const int controlsH = btnH + rowH * 9 + btnH + 24;
    int seqH = getHeight() - titleH - controlsH - pad * 2;
    seqH = juce::jmax (seqH, 100);  // mínimo 100px

    // Áreas de los secuenciadores circulares (arriba)
    seqAreaL = juce::Rectangle<float> ((float)pad, (float)(titleH + pad), (float)channelW, (float)seqH);
    seqAreaR = juce::Rectangle<float> ((float)(pad + channelW + syncW), (float)(titleH + pad), (float)channelW, (float)seqH);

    int ctrlY = titleH + pad + seqH + 14;  // Y de inicio de controles

    // ── Fila 1: botones de dirección + link ───────────────────────────────
    int btnW3 = (w - pad * 2) / 3;
    directionButtonL.setBounds (pad,             ctrlY, btnW3, btnH);
    linkSeqButton   .setBounds (pad + btnW3,     ctrlY, btnW3, btnH);
    directionButtonR.setBounds (pad + btnW3 * 2, ctrlY, btnW3, btnH);
    ctrlY += btnH + 4;

    // ── Lambda para colocar una fila con control L y R ────────────────────
    auto placeRowLR = [&](juce::Component& cL, juce::Label& lL,
                           juce::Component& cR, juce::Label& lR)
    {
        juce::Rectangle<int> rowL (pad, ctrlY, channelW, rowH);
        lL.setBounds (rowL.removeFromLeft (labelW)); cL.setBounds (rowL);
        juce::Rectangle<int> rowR (pad + channelW + syncW, ctrlY, channelW, rowH);
        lR.setBounds (rowR.removeFromLeft (labelW)); cR.setBounds (rowR);
        ctrlY += rowH;
    };

 
    // ── Filas 1-5: feedback, wet, cutoff, pasos, drive ────────────────────
  
 
    placeRowLR (driveSliderL,    driveLabelL,    driveSliderR,    driveLabelR);
    placeRowLR (numStepsSliderL, numStepsLabelL, numStepsSliderR, numStepsLabelR);
    placeRowLR (cutoffSliderL,   cutoffLabelL,   cutoffSliderR,   cutoffLabelR);
    placeRowLR (feedbackSliderL, feedbackLabelL, feedbackSliderR, feedbackLabelR);
    placeRowLR (wetSliderL,      wetLabelL,      wetSliderR,      wetLabelR);
 
    
    // ── Fila 6: tipo de distorsión ────────────────────────────────────────
    {
        juce::Rectangle<int> rowL (pad, ctrlY, channelW, rowH);
        distModeLabelL.setBounds (rowL.removeFromLeft (labelW));
        distModeBoxL  .setBounds (rowL.removeFromTop (comboH).reduced (2, 0));
        juce::Rectangle<int> rowR (pad + channelW + syncW, ctrlY, channelW, rowH);
        distModeLabelR.setBounds (rowR.removeFromLeft (labelW));
        distModeBoxR  .setBounds (rowR.removeFromTop (comboH).reduced (2, 0));
        ctrlY += rowH;
    }
    
    // ── Fila 7: tipo de filtro ────────────────────────────────────────────
    {
        juce::Rectangle<int> rowL (pad, ctrlY, channelW, rowH);
        filterLabelL  .setBounds (rowL.removeFromLeft (labelW));
        filterTypeBoxL.setBounds (rowL.removeFromTop (comboH).reduced (2, 0));
        juce::Rectangle<int> rowR (pad + channelW + syncW, ctrlY, channelW, rowH);
        filterLabelR  .setBounds (rowR.removeFromLeft (labelW));
        filterTypeBoxR.setBounds (rowR.removeFromTop (comboH).reduced (2, 0));
        ctrlY += rowH;
    }

    
    // ── Fila 8: delay ms / subdivisión (depende del modo) ────────────────
    {
        bool syncL = (syncModeBoxL.getSelectedId() == 2);
        bool syncR = (syncModeBoxR.getSelectedId() == 2);
        {
            juce::Rectangle<int> row (pad, ctrlY, channelW, rowH);
            if (syncL) { divLabelL.setBounds (row.removeFromLeft (labelW)); syncDivBoxL.setBounds (row.removeFromTop (comboH).reduced (2, 0)); }
            else       { delayLabelL.setBounds (row.removeFromLeft (labelW)); delaySliderL.setBounds (row); }
        }
        {
            juce::Rectangle<int> row (pad + channelW + syncW, ctrlY, channelW, rowH);
            if (syncR) { divLabelR.setBounds (row.removeFromLeft (labelW)); syncDivBoxR.setBounds (row.removeFromTop (comboH).reduced (2, 0)); }
            else       { delayLabelR.setBounds (row.removeFromLeft (labelW)); delaySliderR.setBounds (row); }
        }
        ctrlY += rowH;
    }
    // ── Fila 9: modo de sync ─────────────────────────────────────────────
    {
        juce::Rectangle<int> rowL (pad, ctrlY, channelW, rowH);
        modeLabelL  .setBounds (rowL.removeFromLeft (labelW));
        syncModeBoxL.setBounds (rowL.removeFromTop (comboH).reduced (2, 0));
        juce::Rectangle<int> rowR (pad + channelW + syncW, ctrlY, channelW, rowH);
        modeLabelR  .setBounds (rowR.removeFromLeft (labelW));
        syncModeBoxR.setBounds (rowR.removeFromTop (comboH).reduced (2, 0));
        ctrlY += rowH;
    }


 

    // ── Fila final: botones de preset ─────────────────────────────────────
    int thirdW = (w - pad * 2) / 3;
    savePresetButton.setBounds (pad,                ctrlY, thirdW, btnH);
    polygonButton   .setBounds (pad + thirdW,       ctrlY, thirdW, btnH);
    loadPresetButton.setBounds (pad + thirdW * 2,   ctrlY, thirdW, btnH);
}
