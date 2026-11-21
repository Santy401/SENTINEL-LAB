# Contribución

## Ramas

`main` es la rama estable. Después del bootstrap inicial no se desarrolla
directamente en ella.

```text
feat/<scope>-<description>
fix/<scope>-<description>
perf/<scope>-<description>
refactor/<scope>-<description>
docs/<description>
ci/<description>
chore/<description>
```

Ejemplos:

```text
feat/firmware-websocket
fix/motion-duplicate-samples
docs/validation-ios-pipeline
ci/firmware-validation
```

## Commits

Se usa Conventional Commit:

```text
type(scope): short imperative description
```

Tipos: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`,
`revert`.

Reglas:

- Modo imperativo.
- Sin punto final en el asunto.
- Menos de 72 caracteres recomendado.
- Un cambio coherente por commit.
- `BREAKING CHANGE:` solo cuando aplique.
- Nunca incluir secretos.

## Pull Requests

- Título en inglés y formato Conventional Commit.
- Descripción técnica en inglés.
- Documentación de usuario en español.
- Adjuntar evidencia de pruebas reales.
- Separar claramente software probado de hardware probado.

No se hace merge sin autorización del propietario.

## Pruebas obligatorias antes de proponer cambios

```bash
python3 tools/check_software.py
python3 scripts/check-repository.py
pio run -e sentinel_esp32s3_n16r8_rf
```

El flasheo nunca es automático. Requiere autorización explícita y verificación de
identidad, puerto e integridad del binario.

## Idioma

- Código y comentarios técnicos: inglés.
- Documentación, ADR e informes: español.
- No mezclar identificadores en español e inglés.

## Secretos

Nunca añadir `include/secrets.h`, `.env`, tokens, contraseñas, certificados ni
archivos de firma. Usar plantillas `*.example`.
