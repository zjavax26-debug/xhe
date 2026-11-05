# Integración del motor xHE-AAC

## Estado

La interfaz Android ya permite elegir un archivo y ajustar bitrate/frecuencia. La conversión todavía no está habilitada. Esta etapa prepara una verificación reproducible del codificador upstream antes de enlazarlo con Android.

## Codificador elegido

- Proyecto: [Ittiam libxaac](https://github.com/ittiam-systems/libxaac)
- Perfil requerido: USAC / xHE-AAC, AOT 42.
- API de encoder: `ixheaace_create`, `ixheaace_process`, `ixheaace_delete`.
- El CLI de referencia acepta WAV/PCM de 16 bits y parámetros como `-br` y `-aot:42`.

El código upstream se fija a un commit concreto en el workflow para que las compilaciones sean reproducibles. La licencia del código no equivale a una licencia general de patentes; esta aplicación se mantiene para uso personal.

## Orden de integración

1. Compilar y ejecutar el codificador de referencia en CI para confirmar que el perfil AOT 42 funciona.
2. Construir la biblioteca para Android NDK (ARM64 primero; ARMv7 solo si aporta compatibilidad útil).
3. Envolver la API de libxaac mediante JNI, evitando invocar procesos externos en Android.
4. Decodificar el audio de entrada a PCM usando APIs de Android y alimentar el encoder por bloques.
5. Empaquetar el flujo USAC en un contenedor MP4/M4A válido. No se debe renombrar un flujo elemental a `.m4a`.
6. Validar reproducción, duración, metadatos y búsqueda en Android.

## Bitrate y frecuencia

Los límites de la interfaz siguen siendo provisionales hasta que se caracterice el encoder para cada frecuencia y número de canales. No se debe anunciar que todos los valores de 1 kbps son válidos solo porque el control deslizante permita seleccionarlos. La aplicación debe rechazar combinaciones no soportadas con un mensaje claro.

## Pruebas de aceptación

- La salida se identifica como USAC/AOT 42, no AAC-LC.
- El archivo M4A se puede abrir y buscar en reproductores compatibles con xHE-AAC.
- La duración coincide con la fuente y no hay truncamiento de la última trama.
- Se prueban archivos mono y estéreo, tasas de muestreo distintas y bitrates bajos/altos.
- La conversión se ejecuta en segundo plano y muestra progreso/cancelación.
