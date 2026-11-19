<!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:4d3be10ef2de7a7212c2229c3f076431a208aaec6bbe6efd8de080421be9073a -->

<!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:d21b3b9c76524e802d3788d930e331e8781902c777d27f083147809e070f95d8 -->

# Input Align External — alpha 0.2.0

<!-- /coderabbit-ai-generated:v1 -->

Mod nuevo y experimental para **Windows x64, Geometry Dash 2.2081 y Geode 5.11.2**. Se distribuye con otra identidad: `sketch.inputalign_external`. No modifica la DLL del InputAlign original ni utiliza su codigo fuente.

<!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:8c63a8d7edd6004917a5212d4476ab519efdd0d77830ad49e877a8194c44b2d7 -->

## Cambios de 0.2

Se habilitan nave y dash en el puente, ademas de los otros siete modos; hay casos sinteticos de correccion para los ocho vehiculos. El motor externo admite Move, Toggle, Spawn y Stop en sus variantes representadas, asi como teletransporte vertical por portal. Se corrigieron la repeticion de Move por Spawn, el primer Stop, las consultas de colision de ramas diferentes, los objetos dinamicos ya consumidos y la deteccion persistente de un cronograma truncado. El empaquetador verifica los hashes del fork.

Una pulsacion entre pasos fisicos ya no se confunde con divergencia: la entrada actual se aplica en la siguiente simulacion. Una mecanica encontrada solo en una rama hipotetica cancela esa consulta y conserva la sincronizacion del estado real. Esto cambia intencionalmente la politica de 0.1, que bloqueaba el intento. <!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:62297bc4035b3bee467de6ece59b243e228e02b375c04cc9e7cc7f7b9df9315b -->

La biblioteca y los scripts usan el <!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:ba36c815b371184b8b793112d0d5dfe7922880980783c373ef0d3202a546d4cc -->

adaptador version 4

<!-- /coderabbit-ai-generated:v1 --> y el nucleo ABI3; hay que reemplazar **toda** la carpeta de consola al actualizar. La inspeccion admite mas de 128 objetos dinamicos consumidos y el procesamiento de Stop no vuelve a recorrer segmentos ya terminados. En Windows se usa el UCRT del sistema para sin/cos/pow, manteniendo la misma referencia matematica que Python.

<!-- /coderabbit-ai-generated:v1 -->

**Esta version aun no cumple equivalencia total con InputAlign ni garantiza correccion universal.** Tener una tabla de objetos y ocho modos no implementa todos los triggers, dual ni CBF. Las pruebas sinteticas no prueban la fidelidad frente al juego ni los FPS.

## Que hace

<!-- /coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:dce1fc607d74b633f128e11df454188c43552893764047e4df1944ea2e0d1c33 -->

El mod envia estados pequenos por UDP a una consola local. La busqueda de ramas se ejecuta en C++ dentro de `gdsim.dll` en ese proceso; Python gestiona el nivel y la comunicacion. El bucle Python se conserva como referencia independiente para las pruebas, y la consola distribuida exige la DLL ya compilada.

<!-- /coderabbit-ai-generated:v1 --> El simulador independiente busca una pulsacion o liberacion retrasada que sobreviva al horizonte configurado cuando la accion inmediata moriria. El juego usa solo respuestas que ya han llegado, del mismo intento, tick, estado e historial de entradas. No espera a la consola, no ejecuta simulaciones de 400 ticks dentro del juego y no lee memoria de otros procesos.

Una respuesta ausente, tardia o no verificable deja pasar la entrada original. Si se pierde la validacion durante una espera ya iniciada, la entrada pendiente se entrega como entrada original y no se cuenta como correccion verificada. Una nueva accion fisica cancela la anterior pendiente. Una pulsacion muy corta, soltada antes de su turno, puede quedar cancelada por ese comportamiento del buffer. No reproduce macros, ni genera clics sin una entrada tuya previa.

**El contador Corrections cuenta entradas retrasadas con coincidencia del modelo, no muertes evitadas demostradas. No se garantiza una mejora de FPS ni precision perfecta.** La consola consume CPU de otro proceso, con prioridad inferior a la normal. Se mantiene el presupuesto externo predeterminado de 6 ms; Performance Mode lo divide por dos.

## Instalacion del paquete de Windows

1. Desactiva InputAlign original y cualquier otro buffer de entrada. La deteccion de `ai.predictivebuffer` suspende este predictor. Desactiva CBF para probar la correccion.
2. Copia `sketch.inputalign_external.geode` a `Geometry Dash/geode/mods/` y abre el juego con Geode 5.11.2.
3. Entra a un nivel, pausa y pulsa **IAX > Abrir CMD**. La primera vez se abre la carpeta `worker` de datos del mod.
4. Copia **el contenido completo** de `InputAlignWorker` del ZIP dentro de esa carpeta `worker`. Incluye `python.exe`, sus DLL, `python312.zip`, `python312._pth`, `gdsim.dll`, los scripts y `vendor`. No necesitas instalar Python.
5. Vuelve a pulsar **Abrir CMD**, deja la consola abierta y **reinicia el intento desde el principio**. El menu IAX muestra el estado del simulador.
6. Al cambiar de nivel, abre una consola nueva desde IAX. La anterior deja de servir al cerrarse su puente. El boton evita abrir dos consolas del mismo nivel.

Los cambios de configuracion se recargan al pausar y durante el juego. Las grabaciones `attempt-*.json` quedan en **IAX > Carpeta de datos**. Se escriben fuera del hilo del juego y se completan al salir del nivel. <!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:0af9221095570fe68e404522c3f2dad11cd3e322419a6d7ec3bd541e88068b92 -->

La cola esta limitada a 16 archivos pendientes y cada intento a 100000 entradas; si se llena, se descarta la grabacion nueva. Los archivos se publican solo tras cerrar y comprobar la escritura; los incompletos llevan `.partial`. Si falla el disco, se avisa y se retienen hasta 16 intentos en memoria para reintentar al guardar de nuevo. Si el error persiste al salir del nivel, esa memoria se pierde.

<!-- /coderabbit-ai-generated:v1 -->

## Compatibilidad y diferencias conocidas

Se conservan las **38 claves, tipos, rangos y valores predeterminados** del paquete del repositorio, pero esto no es una reproduccion completa de su comportamiento binario:

- `cbf-sync` se conserva como opcion de compatibilidad **reservada**. No implementa sincronizacion CBF. Con CBF detectado, pasos parciales o un paso fisico distinto de 1/240 s, los clics pasan directamente independientemente del valor de esa opcion.
- <!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:a6f21f1381f8b235d45423c0604117961e39c2bfe04f7fa7d3299038a4fc823e -->

<!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:3308c8f16449946ee77c093481eed8cf9f8c47d7e0518b8612202445a4fd731c -->

Dual activo, dos jugadores, platformer, start positions activos, checkpoints cargados, teleport orbs con destino de grupo, portales espejo y geometria controlada por triggers no compatibles no reciben correccion. Se permite cargar portales Dual/Solo para predecir prefijos que no los contacten; el primer contacto real suspende el intento, sin reentrada automatica. Los start positions inactivos no rechazan por si solos el nivel. Consulta [IMPLEMENTATION-NOTES.md](IMPLEMENTATION-NOTES.md) para los limites, la cancelacion de consultas y el adaptador v3 requerido.

<!-- /coderabbit-ai-generated:v1 -->

<!-- /coderabbit-ai-generated:v1 --> Un reinicio desde el principio permite volver a sincronizar. <!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:a77a2cf903982d458ae4fea554bb5f87293c7ed89825b76f759a0a88cafe8ebf -->

<!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:0b743438ef04fa98032f249747546bfb1d7db40a07fcf9836af4398b46b91a6a -->

Se admiten los triggers visuales enumerados y Move/Toggle/Spawn/Stop con parametros representables. Touch triggers, Move lock-Y/to-target, Rotate, Follow, Scale, area y keyframes siguen sin implementacion fiel. Un objeto desconocido rechaza la prediccion del nivel completo; los movimientos y clics del juego permanecen directos. Una rama futura no representable no deja bloqueado el intento real. Si esa mecanica afecta el estado real, el modelo deja de corregir hasta reiniciar.

<!-- /coderabbit-ai-generated:v1 --> El cargador limita posiciones y bordes a 10000000 unidades, el nivel a 250000 objetos y el indice a 200000 entradas de buckets; rechaza geometria no finita antes de reservar ese indice.

<!-- /coderabbit-ai-generated:v1 -->
- La reconstruccion debe coincidir desde el estado inicial; no inventa un estado incompleto copiando solo posicion y velocidad. Si faltan mas de 64 ticks de historial o el modelo diverge, se suspende hasta el siguiente intento.
- Se anticipa la posible accion cuatro ticks. Un clic para el que no haya una respuesta exacta disponible no recibe correccion. Esto limita la cobertura, especialmente si la consola esta ocupada.
- `normal-ticks` y `wave-ticks` limitan tanto el retraso buscado como el horizonte de supervivencia comprobado tras cada accion. Sobrevivir ese horizonte no demuestra que el resto del nivel sea seguro.
- `invisible-trajectory` apagado conserva el contrato documentado del original: entrada directa. No se dibujan trayectorias.
- Noclip y velocidad se aplican solo en practica. La correccion se suspende con noclip. El contador de noclip cuenta llamadas de colision suprimidas; un contacto continuo puede sumar varias.
- JSON de referencia: `inputs` con `frame`, `down`, opcional `button`, `2p`/`player2` y `framerate` global (240 por defecto). TXT/CSV: una linea `frame down|up|press|release|1|0` a 240 Hz. Solo se compara el boton 1 del jugador 1, misma accion, dentro de 80 ticks. `referenceDelta` se guarda en el registro; no hay reproduccion ni ajuste automatico de limites.
- No incluye la pagina Estilo ni recomendaciones automaticas del menu original. El color, imagen, opacidad, escala, arrastre del titulo, cierre, idioma y HUD se controlan desde el panel y los ajustes de Geode. Los textos de diagnostico del simulador estan en espanol.

## Comprobaciones disponibles

En Linux, sin Geode ni Geometry Dash:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DIAX_BUILD_MOD=OFF
cmake --build build-native --parallel 2
ctest --test-dir build-native --output-on-failure
GDGEN_SIM_LIB="$PWD/build-native/libgdsim.so" IAX_GATE_TEST="$PWD/build-native/iax_gate_tests" python -m unittest discover -s tests -v
```

La suite comprueba formato binario entre C++ y Python, respuestas antiguas, cambios de intento/estado, perdida de historial, plazo agotado, acciones directas seguras, una correccion con el simulador real y comunicacion UDP entre procesos. Los resultados sinteticos no prueban el comportamiento de los hooks dentro de GD.

## Compilar en Windows

Necesitas Visual Studio 2022 actualizado (MSVC >=19.44 y Windows SDK), CMake >=3.25, Geode CLI, SDK **5.11.2** con sus binarios instalados y Python **3.12**. Define `GEODE_SDK` segun la instalacion oficial de Geode. No uses el SDK 6 del otro proyecto del repositorio.

Desde PowerShell:

```powershell
./scripts/build-windows.ps1
```

<!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:3b3b5e0f73be788de585dd63d0a80027cab8d19c497dd74bcb8cf8cb6e384c68 -->

El script fija los bindings a `6f163558a5e2ac47b9121c6721fd32322c8e188b`, compila el puente y la DLL del simulador, ejecuta las pruebas y descarga el Python embebible oficial 3.12.10. Deja `build-windows/InputAlignExternal-windows.zip`, sin dependencias de pip. `scripts/package-windows.py` tambien permite empaquetar un directorio ya compilado con el ZIP oficial descargado previamente.

<!-- /coderabbit-ai-generated:v1 -->

```sh
python scripts/package-windows.py --build build-windows --python-runtime python-3.12.10-embed-amd64.zip --output InputAlignExternal-windows.zip
```

## Opciones conservadas

| Clave | Valor inicial |
|---|---|
| `enabled` | True |
| `invisible-trajectory` | True |
| `cbf-sync` | True |
| `language` | Español |
| `normal-ticks` | 400 |
| `wave-ticks` | 400 |
| `spam-guard` | False |
| `spam-interval` | 20 |
| `buffer-normal` | True |
| `buffer-wave` | True |
| `buffer-practice-only` | False |
| `macro-record` | True |
| `macro-file` |  |
| `macro-compare` | True |
| `background-image` |  |
| `background-opacity` | 100 |
| `background-dim` | 30 |
| `noclip` | False |
| `game-speed` | 1.0 |
| `auto-close` | 15 |
| `menu-color` | #1D1813 |
| `menu-opacity` | 90 |
| `menu-scale` | 85 |
| `menu-lock` | False |
| `hud-fps` | False |
| `hud-cps` | False |
| `hud-clicks` | False |
| `hud-saved` | False |
| `hud-noclip-deaths` | False |
| `hud-scale` | 70 |
| `hud-opacity` | 80 |
| `hud-corner` | Arriba izq. |
| `no-wave-trail` | False |
| `performance-mode` | False |
| `max-correction-ms` | 6 |
| `button-corner` | Abajo der. |
| `button-scale` | 0.7 |
| `button-opacity` | 100 |

## Procedencia y licencias

Puente, protocolo y predictor: MIT, copyright 2026 dadjdjxjfjdjd-sketch. Simulador independiente: [kholkink/gd-levelgen](https://github.com/kholkink/gd-levelgen/tree/9496709eadd22fc3a130f89043169c17da4d5b9c), MIT, revision `9496709eadd22fc3a130f89043169c17da4d5b9c`; <!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:567d2a75d4dd5dcccecb2a194f6268025c666d04ba892ce4793535a03344e8bc -->

se incluyen su licencia y el registro del fork en `worker/vendor/UPSTREAM.json`, con hashes originales y modificados. La version 0.2 aplica correcciones locales comprobables al motor de triggers y a la propiedad de estados; ya no se presenta como una copia sin cambios.

<!-- /coderabbit-ai-generated:v1 --> El autor original atribuye el simulador a GD 2.2081 y documenta limitaciones; esa atribucion no equivale a validacion de este mod en el juego.

El paquete de consola incluye Python bajo su licencia, conservada en `InputAlignWorker/LICENSE.txt`. `build-provenance.json` identifica procedencia, hashes y el estado de prueba del paquete. <!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:b448fcb9cbf457ae39807be14987c6a4ba513f2ed04d4caf8dabdc93fe004335 -->

No se redistribuyen Geometry Dash ni sus recursos.

### Licencia del puente

MIT License

Copyright (c) 2026 dadjdjxjfjdjd-sketch

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


<!-- /coderabbit-ai-generated:v1 -->


<!-- /coderabbit-ai-generated:v1 -->