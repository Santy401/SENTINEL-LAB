# App iOS desde Arch Linux / Omarchy con xtool (sin Mac)

Compilar, firmar e instalar SENTINEL-LAB en el iPhone 12 desde Linux con
[xtool](https://github.com/xtool-org/xtool). El flujo se basa en
[omarchy-apple-dev](https://github.com/joshuaswarren/omarchy-apple-dev), pero
usa directamente las releases oficiales de xtool y Swift: sin instaladores de
terceros y sin `sudo`.

## Estado verificado (2026-11-22)

| Paso | Estado |
|---|---|
| Toolchain Swift 6.4.3 en Arch, sin `sudo` | **Verificado**: firma GPG de swift.org comprobada; `swift --version` |
| xtool 1.24.0 | **Verificado**: SHA-256 del release; AppImage extraída (Arch trae fuse3, no fuse2) |
| SentinelKit compilado y probado en Arch | **Verificado**: `swift test`, 32 tests, 0 fallos |
| Manifiesto `apps/ios/Package.swift` | **Verificado** en Linux (`swift package describe`) y compilado para iOS en CI macOS (job `xtool-package`) |
| xtool ve el iPhone por USB | **Verificado** a través del `usbmuxd` de `home-server` reenviado por SSH: `xtool devices` → `iPhone [usb]` |
| SDK de iOS (`xtool sdk install Xcode.xip`) | **Verificado**: `Xcode_27.1.xip` → iPhoneOS27.1.sdk; `xtool sdk status` → *Darwin SDK is installed* |
| Compilar la app SwiftUI para iOS en Arch | **Verificado**: `linux-install.sh --build-only` → *Build complete!* (52 s), `SentinelLab.ipa` con ejecutable Mach-O arm64 y `Info.plist` sin variables |
| Login Apple ID (`xtool auth login`) | **Pendiente**: interactivo, lo haces tú |
| Firmar e instalar en el iPhone | **Pendiente**: requiere el login |
| App ejecutándose en el iPhone | **No verificado** |

## Arquitectura

- **Adaptador SwiftPM** (`apps/ios/Package.swift`): xtool solo compila paquetes
  SwiftPM, no proyectos XcodeGen. El adaptador declara un único producto library
  `SentinelLab` con las mismas fuentes que `project.yml`, más la dependencia
  local `SentinelKit`. El proyecto Xcode sigue funcionando igual en CI macOS.
- **`apps/ios/xtool.yml`**: bundle id `lab.sentinel.app`, `Info.plist`
  superpuesto (`xtool-config/Info.plist`) e icono (`xtool-config/AppIcon.png`).
  - xtool genera su propio `Info.plist` y le superpone el nuestro, pero no
    expande variables de Xcode como `$(EXECUTABLE_NAME)`. Por eso existe un
    `Info.plist` específico, sin variables.
    `scripts/check-xtool-config.py` comprueba en CI que replica todas las demás
    claves del de Xcode.
  - En Linux no existe `actool`, así que el asset catalog no se compila: el
    icono sale del PNG de `xtool.yml`. La app no usa otros assets.
- **Toolchain** (`~/.local/share/sentinel-ios-toolchain`): Swift oficial de
  swift.org para Ubuntu 24.04 adaptado a Arch:
  - `libncurses.so.6` apunta a `libncursesw.so.6` del sistema.
  - `libxml2.so.2` y ICU 74 se copian de un Ubuntu 24.04 (`--ubuntu-host`) o
    vienen del paquete AUR `libxml2-legacy`.
  - `patchelf` (instalado en un venv privado) fija el `RUNPATH`, porque SwiftPM
    borra `LD_LIBRARY_PATH`.
  - Solo se modifica esa copia, nunca el sistema. LLDB queda fuera: no hace
    falta para compilar ni instalar.
- **iPhone en otro equipo**: `linux-install.sh --via home-server` reenvía el
  socket `/var/run/usbmuxd` por SSH. El iPhone puede seguir conectado a
  `home-server`; el emparejamiento lo sirve el `usbmuxd` de ese equipo.

## Pasos

1. **Toolchain** (ya ejecutado en este equipo; es idempotente):
   ```bash
   apps/ios/scripts/linux-toolchain.sh --ubuntu-host home-server
   echo 'export PATH="$HOME/.local/share/sentinel-ios-toolchain/bin:$PATH"' >> ~/.bashrc
   # (el directorio bin enlaza swift, swiftc y las herramientas de SwiftPM que usa xtool)
   ```
2. **SDK de iOS.** Descarga **Xcode 27.1** (`.xip`) desde
   <https://developer.apple.com/download/all/?q=Xcode> en el navegador, con tu
   Apple ID (vale una cuenta gratuita). El archivo correcto es
   «Xcode 27.1» → `Xcode_27.1.xip` (1,89 GB); no la RC, ni las betas, ni
   «Command Line Tools». Después:
   ```bash
   apps/ios/scripts/linux-toolchain.sh --sdk ~/Downloads/Xcode_27.1.xip
   # o, para buscarlo en ~/Downloads y ~/Descargas:
   apps/ios/scripts/linux-toolchain.sh --sdk auto
   ```
   Usa la versión que indica xtool: Swift 6.4 necesita el SDK de Xcode 27.
3. **Login, una sola vez y de forma interactiva:**
   ```bash
   xtool auth login --mode password
   ```
   Nunca pases `--password` en la línea de comandos. La sesión la guarda xtool
   fuera del repositorio. **No compartas la salida de `xtool auth status`**:
   imprime el token de sesión.
4. **iPhone:** desbloqueado, con el Modo de desarrollador activado (Ajustes →
   Privacidad y seguridad), y conectado por USB a este equipo o a `home-server`.
5. **Compilar, firmar e instalar:**
   ```bash
   apps/ios/scripts/linux-install.sh --via home-server   # o sin --via si está aquí
   ```
6. **En el iPhone:** confía en tu certificado de desarrollador (Ajustes → General
   → VPN y gestión de dispositivos), únete a la Wi-Fi `SENTINEL-LAB`, abre la app,
   acepta el permiso de red local y pega el token en Ajustes.

Solo para generar la `.ipa` sin firmar: `linux-install.sh --build-only`.

## Errores encontrados y corregidos

- `xtool sdk install` necesita `swiftc` en `PATH`, no solo `swift`. Si no lo
  encuentra, falla al 99 % con `Could not find executable 'swiftc' in PATH` y
  borra el bundle temporal. El directorio `bin` de la toolchain enlaza ahora
  `swift`, `swiftc` y las herramientas de SwiftPM, y el script comprueba
  `xtool sdk status` después de instalar.
- El `.xip` se guarda por defecto en `~/Downloads`; `--sdk auto` lo busca allí
  y en `~/Descargas`.

## Limitaciones y riesgos conocidos

- **Live Activity / widget no se instala con xtool.** xtool 1.24 no puede
  firmar extensiones de app (`Can't find free space of LoadCommands`, issue
  xtool-org/xtool#379). La 2.0.0-beta.1, que lo corrige según el issue, falla al
  hacer `strip` del `.appex` y además migra el SDK a `~/.config/xtool` (hay que
  devolverlo a `~/.config/swiftpm/swift-sdks/darwin.artifactbundle` para la
  1.24). Por eso `xtool.yml` excluye la extensión. El proyecto Xcode sí la
  compila, y la app funciona sin ella: la opción de Actividad en vivo informa
  de que no está disponible.

- **Login con Apple ID gratuito.** Usa APIs privadas de Apple. Desde el
  2026-10-12, Apple rechaza a Sideloader, AltStore, Sideloadly e Impactor
  (`-80045`). Lo comprobé el 2026-11-22: Sideloader falla antes de pedir el
  Apple ID. Con xtool 1.23.0, omarchy-apple-dev registró el 2026-10-23 que el
  error pasaba a `-20970` (credenciales inválidas con datos inventados), es
  decir, que pasa ese bloqueo. **No está comprobado con una cuenta real en este
  proyecto.** Si falla, la alternativa sin Mac es el login con API key, que
  requiere el programa de pago:
  `xtool auth login --mode key`.
- **Cuenta gratuita:** el perfil caduca a los **7 días**; para renovarlo, repite
  el paso 5. Máximo 3 apps instaladas así. Apple puede rechazar
  `lab.sentinel.app` si otra cuenta registró ese bundle id; en ese caso, cámbialo
  en `xtool.yml`.
- **Tamaño del `.xip`:** varios GB; `xtool sdk install` necesita espacio
  temporal adicional (`--slim` reduce el resultado).
- **Sin asset catalog en Linux:** el color de acento global de Xcode no se aplica;
  se usa el color por defecto del sistema.
- `Authorization: Bearer` en `URLSessionWebSocketTask` sigue sin probarse en un
  dispositivo.
- La versión de Sideloader descargada en `home-server:~/tools/sideloader` no
  funciona y no se usa. Incluye un comando de explotación (`trollsign`) que no
  debe ejecutarse. Puede borrarse.
