# Protocolo SENTINEL v1

Contrato JSON versionado y transport-independent de SENTINEL-LAB.

- Esquemas: `schemas/`
- Ejemplos válidos: `examples/valid/`
- Casos negativos: `examples/invalid/`
- Documentación: `../../docs/protocol-contract.md`

## Uso

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt

python3 validate_protocol.py examples/valid
python3 validate_protocol.py --expect-invalid examples/invalid
python3 -m unittest discover -s tests -v
```

La validación canónica rechaza campos desconocidos, números no finitos en
cualquier punto del mensaje, claves duplicadas, timestamps imposibles, versiones
no soportadas, incoherencias de `clientCount` y sobres de más de 16 KiB.

## Esquemas

Los diez esquemas publican `$id` HTTPS resolubles bajo el repositorio. Para
consumidores que validan sin red, `validate_protocol.build_registry()` devuelve
un `referencing.Registry` con el conjunto local:

```python
from jsonschema import Draft202012Validator, FormatChecker
from validate_protocol import build_registry, load_json
from pathlib import Path

validator = Draft202012Validator(
    load_json(Path("schemas/envelope.schema.json")),
    registry=build_registry(),
    format_checker=FormatChecker(),
)
```

`jsonschema` y `rfc3339-validator` son dependencias exclusivas de esta
herramienta. El firmware no depende de este paquete. Los adaptadores se
implementarán en etapas posteriores sin sustituir la API HTTP v0.3.1.
