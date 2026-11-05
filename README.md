# xHE Audio Converter

Aplicación Android personal para convertir archivos de audio a M4A con codificación xHE-AAC (USAC).

## Estado actual

Esta primera base crea el proyecto Android y la interfaz de configuración:
- Selector de archivos mediante el selector del sistema Android.
- Control de bitrate con deslizador y entrada numérica sincronizados.
- Perfiles de frecuencia de muestreo.
- Interfaz ligera, nativa y en español.
- Android 10 (API 29) como versión mínima.

**La conversión todavía no está habilitada.** Falta integrar y probar el codificador libxaac (AOT 42 / USAC), la capa JNI y el empaquetado MP4/M4A. No se generará un archivo AAC convencional ni se cambiará solamente la extensión.

## Compilar

El proyecto usa Gradle 8.9, Android Gradle Plugin 8.7.3 y JDK 17. Abre el repositorio con Android Studio o ejecuta la tarea `assembleDebug`.

## Decisiones técnicas pendientes

- Confirmar los límites de bitrate válidos para cada frecuencia de muestreo y número de canales con libxaac.
- Integrar libxaac como biblioteca nativa para ARM64 y, si procede, armeabi-v7a.
- Crear el contenedor MP4/M4A y verificar reproducción y búsqueda.
- Probar con archivos de audio reales y teléfonos Android 10+.

## Licencias

La aplicación no se distribuye comercialmente en esta etapa. Antes de redistribuir binarios de terceros, revisar sus licencias, avisos y cualquier cuestión de patentes aplicable.
