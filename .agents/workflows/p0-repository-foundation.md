# Flujo P0 · Fundación del repositorio

Objetivo: convertir el proyecto local existente en la base de SENTINEL-LAB sin
perder el firmware funcional v0.3.1.

## Entrada

- Directorio local existente con firmware, pruebas, documentación e integraciones.
- Remoto vacío `https://github.com/Santy401/SENTINEL-LAB.git`.

## Pasos

1. Auditar archivos, PlatformIO, pruebas, secretos y hashes antes de escribir.
2. Verificar que el remoto no contenga historial.
3. Crear `.gitignore`, `.editorconfig`, README, gobernanza y plantillas GitHub.
4. Crear CI solo para componentes que ya existen.
5. Revisar archivos staged y ejecutar el chequeo de secretos.
6. Crear el commit inicial bootstrap en `main`.
7. Publicar `main` sin force push.

## Salida

- Repositorio inicializado con remote correcto.
- Inventario y estado real documentados.
- Checks locales y de CI reproducibles.
- Firmware v0.3.1 intacto.
