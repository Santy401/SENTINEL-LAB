# Experimentos

Los experimentos RF se guardan en RAM y se exportan con `/api/export.csv`.

Antes de calibrar:

1. Fijar canal, orientación del teléfono, altura del AP y obstáculos.
2. Medir la distancia real con una referencia física.
3. Registrar muestras de calibración en al menos dos distancias distintas.
4. Validar con distancias nuevas, no usadas en el ajuste.
5. Exportar CSV antes de reiniciar.

RSSI crudo, RSSI filtrado y distancia estimada son magnitudes distintas. No se
deben sustituir unas por otras en el informe.
