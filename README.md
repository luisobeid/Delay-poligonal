# Delay-poligonal

Delay Poligonal o "DP" es un plugin de audio VST3/AU que combina un delay estéreo con modo reverse, filtros, distorsión y dos secuenciadores circulares por pasos que deciden dónde se activa el delay

Videos con ejemplos:
https://drive.google.com/file/d/1oCuH8kdsxl92qwZiDBgehMhvMpcaAa3P/view?usp=sharing
https://drive.google.com/file/d/1tdKNp1vO2XG4KNA6QSZ8SXnNTrOj886j/view?usp=sharing
https://youtu.be/JZh6nxtTxzk
https://youtu.be/b2c-JTtpbB8

Introducción:
Delay Poligonal o "DP" es un plugin de audio VST3 desarrollado con el framework JUCE que
combina un delay estéreo con modo reverse, filtros, distorsión y dos secuenciadores circulares por
pasos que deciden dónde se activa el delay y dónde no, permitiendo crear figuras rítmicas
complejas y tímbricas inesperadas a partir de una señal simple, mediante una interfaz gráfica que
permite representar patrones rítmicos mediante figuras geométricas.

Delay Poligonal está construido a partir de tres proyectos preexistentes: un delay con filtro
desarrollado durante el cursado de la materia, un waveshaper tomado de los ejemplos dados por
JUCE y un secuenciador por pasos de forma circular MIDI desarrollado completamente en
openFrameworks como trabajo final para la materia Imagen y Sonido en Tiempo Real de esta
misma especialidad, inspirado en el texto The Geometry of Musical Rhythm de Godfried Toussaint.
El trabajo fue hecho con la asistencia de la IA Claude, me resultó de mucha utilidad en el trabajo
de fusionar lo hecho en JUCE y lo hecho en openFrameworks, y luego para resolver problemas de
audio como ruidos, clicks.

Características Principales

Secuenciador por pasos: “DP” se encuentra en la intersección entre un eco y un secuenciador por
pasos. El secuenciador que controla el delay se activa por detección de señal del audio entrante,
sin depender de la barra de transporte del DAW ni de un botón de play propio. Esta decisión de
diseño busca preservar el comportamiento natural de un efecto de tipo delay: entra audio, el
efecto responde automáticamente. Activar el secuenciador desde el transporte hubiera obligado al
usuario a presionar play para que el efecto funcione, rompiendo esa lógica y generando posibles
problemas de sincronización. El secuenciador sí responde, sin embargo, al stop del transporte del
DAW.

Dos canales independientes (L y R): cada canal tiene su propio secuenciador, reverse delay, filtro
y distorsión. Pueden operar de forma independiente o linkeados como delay pseudo-mono.
Secuenciador circular de 4 a 32 pasos: actúa como gate o compuerta de ganancia. Los pasos
activos dejan pasar audio al delay; los inactivos lo silencian. Se visualiza como un círculo con
nodos clickeables.

Filtro de primer orden LP/HP: dentro del loop de feedback del delay. Cada repetición se filtra,
permitiendo alterar el tono de las repeticiones.

Distorsión: tres tipos (Soft, Fuzz, WaveFold) que se aplican antes del secuenciador, agregando
saturación a la señal que entra al delay.

Modo Reverse: invierte la lectura del buffer usando dos segmentos alternados (ping-pong). El
secuenciador gira y trabaja en sentido antihorario para indicar visualmente que estamos en ese
modo.

Sincronización al DAW: puede funcionar en modo libre (milisegundos) o sincronizado al tempo del
DAW con subdivisiones rítmicas (1/4, 1/8, 1/16, etc.).

Presets: sistema de guardado/carga de presets en formato .fsd que almacena todos los
parámetros y los patrones creados en el secuenciador.

Polígonos: visualización opcional que traza líneas entre los pasos activos del secuenciador,
formando figuras geométricas.

Arquitectura del Plugin

El plugin está organizado en tres componentes principales que se comunican entre sí:
PluginProcessor
Es el núcleo de procesamiento de audio. Se ejecuta en el thread de audio del DAW a 44100 Hz.
Es dueño de todos los parámetros, los buffers de delay, los secuenciadores y los coeficientes del
filtro. Nunca toca la interfaz gráfica.

PluginEditor

Es la interfaz gráfica. Muestra sliders, combos, botones y los dos secuenciadores circulares. Se
refresca a 30fps mediante un Timer de JUCE. Nunca toca audio. Lee el estado del Processor para
dibujar y escribe en los parámetros cuando el usuario interactúa.

StepSequencer

Clase compartida entre ambos. El Processor la llama desde el thread de audio (processSample) y
el Editor la lee desde el hilo de UI (getters). La comunicación entre threads del ángulo del
marcador usa std::atomic para evitar data races sin mutex.

Camino de la señal

Para cada muestra de audio, por canal, el procesamiento sigue esta secuencia:

Modo Normal (Horario)

Etapa Descripción

1. Entrada Se recibe la muestra cruda del DAW
2. Distorsión applyDist(input, drive, mode) aplica la distorsión seleccionada
3. Secuenciador processSample(input) devuelve seqGain (0.0 o 1.0 con fade de 5ms)
4. Lectura del delay Se lee del buffer circular: delayBuffer[writePos – delaySamples]
5. Filtro Se aplica LPF o HPF de primer orden a la muestra leída
6. Escritura delayBuffer[writePos] = distorsión × gate + filtrado × feedback
7. Mezcla final salida = dry × entrada + 1.6 × wet × filtrado × readGain

Modo Reverse (Antihorario)

En modo reverse, el buffer se divide en dos segmentos que se alternan (ping-pong). Mientras uno
se llena escribiendo hacia adelante, el otro se lee al revés. Cuando un segmento se completa, los
roles se intercambian con la operación wSeg = 1 - wSeg.

Buffer de Delay Circular
El delay usa un buffer circular de 4 segundos por canal (maxDelaySamples = 4 × sampleRate).
Dos punteros recorren el buffer:
writePos: donde se escribe la muestra nueva. Avanza con writePos = (writePos + 1) %
maxDelaySamples.

readPos: donde se lee el eco. Se calcula como readPos = (writePos - delaySamples +
maxDelaySamples) % maxDelaySamples. El + maxDelaySamples evita valores negativos, ya que
en C++ el operador módulo preserva el signo.

Feedback
La retroalimentación se genera al escribir en el buffer no solo la entrada nueva, sino también la
señal leída del buffer multiplicada por el coeficiente de feedback (0.0 a 0.99). Cada vuelta del
buffer, el eco se multiplica nuevamente por feedback, decayendo exponencialmente. Con
feedback 0.5, cada eco suena a la mitad del anterior.

Noise Gate
Un gate automático con ataque instantáneo y release de 20ms evita que se escuche ruido del
buffer cuando no hay señal significativa. Cuando la señal filtrada supera un umbral (0.0001),
readGain sube a 1.0 inmediatamente. Cuando baja, readGain decrece gradualmente en 20ms.

Secuenciador por Pasos
Cada secuenciador mantiene un array de booleanos (activeSteps) donde true significa que el paso
deja pasar audio y false lo silencia. El secuenciador no genera audio: actúa como gate y devuelve
un valor de ganancia (0.0 o 1.0) que el Processor usa como multiplicador.

Detección de Onset por RMS
El secuenciador arranca automáticamente cuando detecta señal de entrada, con el objetivo de
comportarse como un auténtico delay sin depender del transporte del DAW como ocurre con
buena parte de los secuenciadores, pero se detiene mediante el stop del transporte. Calcula la
energía promedio (RMS) en una ventana deslizante de 10ms usando un buffer circular y un
acumulador incremental. Cuando el RMS supera el umbral (0.01), el secuenciador arranca desde
el paso 0.

Avance de Pasos
Dos contadores anidados controlan el avance. El sampleCounter se incrementa en cada muestra
de audio. Cuando alcanza stepLengthSamples (la duración del paso en muestras), se llama a
advanceStep() que incrementa currentStep y resetea el contador. Es como los segundos que
disparan los minutos en un reloj.

Fade (Anti-click)
Para evitar clicks al cambiar entre paso activo y mudo, el secuenciador hace una rampa lineal de
5ms entre el gain actual y el target. fadeIncrement = (targetGain - currentGain) /
fadeLengthSamples. Esto suaviza las transiciones abruptas de 0 a 1 o de 1 a 0.

Cálculo del Ángulo Visual

El marcador visual se calcula como una fracción del ciclo completo convertida a radianes. El
progreso t = elapsed / totalSamples (0 a 1) se multiplica por 2π y se desplaza -π/2 para que el
inicio quede en la posición de las 12 en el círculo.

Distorsión
Se aplica antes del secuenciador, sobre la señal de entrada. El parámetro Drive controla la
intensidad (0 a 10) y hay tres modos disponibles:

Modo Comportamiento

Soft Clip: Saturación suave tipo overdrive.
Fuzz: Recorte agresivo, distorsión tipo fuzz.
WaveFold: Pliega la onda sobre sí misma, creando armónicos complejos.

Sincronización al DAW

El plugin tiene dos modos de tiempo:

Modo No Sync: la duración del paso se calcula directamente desde los milisegundos del slider:
stepLength = delay_ms × 0.001 × sampleRate.

Modo Sync: la duración se calcula a partir del BPM del DAW: stepSeconds = (60 / bpm) ×
divisionMultiplier. 
Las subdivisiones disponibles van desde 1/2 hasta 1/16T (con tresillos).
El resultado se limita entre 1 muestra y maxDelaySamples/2 - 1 (el -1 es margen de seguridad
para el modo reverse que usa dos segmentos).

Sistema de Presets

Los presets se guardan en archivos .fsd. Se almacenan todos los parámetros de ambos canales,
el pattern de pasos activos, el estado de link y la dirección de los secuenciadores. Al cargar un
preset, el Editor sincroniza todos los controles visuales con los nuevos valores.

Link entre Canales

Cuando el botón Linked está activo, el canal R copia todos los parámetros y el patrón de pasos del
canal L, resultando en un delay pseudo-mono donde ambos canales se comportan idénticamente.
Cualquier cambio en L se refleja automáticamente en R y viceversa.

Visualización de Polígonos

Funcionalidad visual opcional activable con el botón Polígonos. Traza líneas entre los pasos
activos del secuenciador, formando figuras geométricas. Con los pasos 0, 4, 8 y 12 activos en un
secuenciador de 16 pasos se forma un cuadrado; con 0, 5 y 10, un triángulo. El relleno es rosa
semitransparente y el contorno rosa más visible, coherente con la estética del plugin.

Tabla de Parámetros

Cada canal tiene los siguientes parámetros (los de R son idénticos a los de L):

Parámetro Rango y descripción

Delay Time 10 – 2000 ms. Tiempo de delay en modo libre.
Feedback 0.0 – 0.99. Intensidad de la retroalimentación.
Mix % 0 – 100%. Mezcla dry/wet de la salida.
Cutoff 20 – 20000 Hz. Frecuencia de corte del filtro.
Filter Type LPF / HPF. Tipo de filtro de primer orden.
Num Steps 4 – 32. Cantidad de pasos del secuenciador.
Drive 0.0 – 10.0. Intensidad de la distorsión.
Dist Mode Soft / Fuzz / WaveFold. Tipo de distorsión.
Sync Mode Libre / Sync. Modo de temporizado.
Sync Division 1/2 a 1/16T. Subdivisión rítmica (solo en modo Sync).
Direction >>>> (horario/normal) / <<<< (antihorario/reverse).
Linked On/Off. Sincroniza canal R con L.

Estructura de Archivos

Archivo Función

PluginProcessor.h/.cpp Núcleo de audio: delay, filtro, distorsión, reverse, serialización.

PluginEditor.h/.cpp Interfaz gráfica: sliders, secuenciadores circulares, polígonos.

StepSequencer.h/.cpp Secuenciador por pasos: onset RMS, avance, fade, ángulo visual.

Formato de salida: VST3 (Windows/Mac/Linux), AU (solo macOS).

Windows: copiar el .vst3 a C:\Program Files\Common Files\VST3\

macOS: copiar el .vst3 a ~/Library/Audio/Plug-Ins/VST3/

En Ableton Live, escanear plugins desde Preferencias > Plugins. El plugin aparece como
FilterDelay.
