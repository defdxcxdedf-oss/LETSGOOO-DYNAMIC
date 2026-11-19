<!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:d6f308e560e657e3c278cf7aeced961d34128707c60894ce3c4828e31fc4ea20 -->

# Implementación: prefijos seguros y cancelación de consultas

Código editable en `InputAlignExternal/`, extraído del ZIP original. Experimental; GD 2.2081 Windows x64 / Geode 5.11.2 sigue siendo el objetivo. No se ha generado ni probado un binario Windows nuevo.

## Cambios

- El cargador permite portales Dual/Solo como barreras de simulación. Una predicción puede aprovechar un prefijo anterior, siempre que el prefijo de espera y el horizonte no activen esos portales. Ambos se marcan unsupported al contactar; no hay simulación dual ni reentrada automática.
- Se mantiene el rechazo de dual inicial, objetos desconocidos, mecánicas no modeladas y datos inválidos.
- Deadline cooperativo durante la construcción de timelines, expansión Spawn, geometría dinámica y bucles de colisión. El adaptador captura la cancelación y devuelve deadline sin publicar una decisión parcial.
- Las timelines se construyen bajo RAII: una cancelación destruye el trabajo parcial y no lo introduce en la caché.
- Los plazos se restauran al salir, también por excepción; nunca se propaga la excepción de deadline a una llamada C de replay posterior.
- Backoff de 50 ms por historial de velocidad cuya timeline agotó el plazo, máximo 64 entradas. Solo afecta a consultas predictivas. La reconstrucción real no omite trabajo por este backoff.
- Adaptador requerido v3: no se permite mezclar scripts nuevos con una DLL antigua que ignora el portal Solo.

## Cambio intencionado de contrato

La prueba original exigía rechazo total por presencia de DualPortal. Ahora se reemplaza esa expectativa por pruebas de corrección antes de un portal lejano, rechazo de futuros que lo contacten y suspensión si la trayectoria real lo contacta. No se eliminan portales ni se supone que son decoración. Las demás expectativas de rechazo permanecen.

## Bloques personalizados

Un objeto personalizado guardado en el editor puede ser una composición de objetos normales: en ese caso importan los IDs, transformaciones, grupos y triggers que realmente contiene, no su apariencia. La geometría conocida ya se procesa a través de la tabla incluida y las transformaciones soportadas.

Un mod puede introducir un ID nuevo o cambiar la física de un ID conocido. Ningún motor externo puede deducir con garantías ese comportamiento mirando solo el level string. No se ha añadido un fallback de cajas genéricas, porque produciría predicciones falsas. Para soportar una física nueva hace falta un adaptador versionado, geometría/estado/orden de eventos verificables y trazas del juego que sirvan de oráculo. Si falta esa evidencia, permanece suspendida.

## Límites que siguen abiertos

- No garantiza un límite duro de pared: asignaciones, ordenaciones, limpieza de memoria y planificación del OS pueden sobrepasar el deadline. No se publica éxito si el chequeo final detecta vencimiento.
- La reconstrucción real del historial y preparación inicial todavía pueden ser costosas. Este cambio acota partes de la consulta especulativa, no toda la vida del worker.
- La comprobación de estado del protocolo sigue siendo parcial y `Corrections` conserva su semántica previa. No se declara verificación completa del juego.
- Afinidad CPU, telemetría completa, dual activo, CBF sub-tick y nuevos triggers físicos siguen pendientes.
- Los controles del reloj añaden coste. Medir tanto consultas sencillas como densas; ninguna cifra sintética acredita una mejora de FPS.

## Validación

Se añadieron pruebas antes de la implementación: el cargador original falla los casos de prefijo, y el test nativo de deadline no compila sin el mecanismo nuevo. Las suites finales y las mediciones se registran en el resumen de entrega. La prueba antigua del rechazo total por dual se actualiza por el cambio de contrato descrito arriba.

Construcción Linux:

```sh
cmake -S InputAlignExternal -B InputAlignExternal/build-native -DCMAKE_BUILD_TYPE=Release -DIAX_BUILD_MOD=OFF
cmake --build InputAlignExternal/build-native --parallel 2
ctest --test-dir InputAlignExternal/build-native --output-on-failure
cd InputAlignExternal
GDGEN_SIM_LIB="$PWD/build-native/libgdsim.so" IAX_GATE_TEST="$PWD/build-native/iax_gate_tests" python -m unittest discover -s tests -v
```

En Windows, usar `scripts/build-windows.ps1` con el SDK y toolchain documentados. Recompilar DLL y scripts juntos por el requisito de adaptador v3.


<!-- coderabbit-ai-generated:v1 -->

<!-- coderabbit-ai-generated-section:v1:919fcd579fda6e7710be5b90b8dd00261513bd9cac4a6b120e5801548b6f3d82 -->

## Revisión de esta entrega

CodeRabbit completó dos pasadas: 19 hallazgos en la primera y 9 en la segunda. Se corrigieron el fallo de socket no bloqueante, el alcance de grupos movidos, selección ambigua de DLL, aceptación de runtimes incompletos, consumo excesivo de memoria al parsear antes de comprobar el límite, la versión de adaptador en README y una expectativa temporal demasiado estricta en el test de deadline.

El hallazgo crítico de tabla de objetos ausente es un falso positivo: `worker/vendor/gdgen/data/objects_2.2081.json` existe, tiene el SHA-256 esperado `a5b62af3f3bee2523f8b7704429abd2142d6c5075eca92e474ce85a4b37198d3`, está en el ZIP y se utiliza en las pruebas. El propio revisor la excluye del diff como archivo binario por `.gitattributes`.

Persisten avisos heredados sobre grabaciones al salir, bindings por entorno, descargas parciales, panel, sustitución del worker, geometría degenerada/orbs móviles, hash estricto de todos los archivos vendor y la búsqueda general find_path. Esta búsqueda no se usa para corregir entradas; TeleportOrb está rechazado por el cargador. La política wave/pendiente no se altera sin trazas que resuelvan la contradicción. No se declara una revisión sin hallazgos ni verificación completa del juego.

Resultado final: 44 pruebas Python aprobadas, una omitida por faltar el paquete original esperado, y tres pruebas nativas aprobadas. Las regresiones de prefijos, alcance, empaquetado y parser fallaron antes de implementarse. La segunda pasada se hizo después de las primeras reparaciones; las reparaciones posteriores se validaron localmente, sin tercera pasada automática. Windows y gameplay siguen pendientes.


<!-- /coderabbit-ai-generated:v1 -->