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
python3 -m unittest discover -s tests -v
```

La validación canónica rechaza campos desconocidos, números no finitos, claves
duplicadas, timestamps inválidos y sobres de más de 16 KiB.

El firmware no depende de este paquete. Los adaptadores se implementarán en
etapas posteriores sin sustituir la API HTTP v0.3.1.
