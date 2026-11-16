# Haikei (背景)

Un motor de renderizado y gestor de ventanas ultraligero en **C** para servidores gráficos **X11**, desarrollado y mantenido como el backend oficial de **[Keshiki](https://github.com/Konqi0139/Keshiki)**, aunque es 100% compatible con cualquier reproductor multimedia (`mpv`, `gifview`, `glxgears`, etc.) o script personalizado.

Basado en el código histórico de `xwinwrap` (Novell / Shantanu Goel / Aaahh / mmhobi7), **Haikei** ("telón de fondo" en japonés) incorpora características nativas en C diseñadas para reducir el consumo de CPU a cero, gestionar múltiples pantallas sin esfuerzo y evitar conflictos con paquetes antiguos del sistema.

---

## ✨ Novedades y Mejoras Propias

Además de todas las funciones clásicas de `xwinwrap`, **Haikei** incorpora:

* **🖥️ Soporte Multi-Monitor Nativo (`-m <MONITOR>`):** Detecta automáticamente las pantallas conectadas (y monitores virtuales creados con `xrandr`) mediante la extensión `Xrandr`. Permite ajustar el fondo a una pantalla específica por nombre (ej. `-m HDMI-1`) o por índice numérico (ej. `-m 0`, `-m 1`) sin calcular geometrías manuales.
* **⏸️ Auto-Pausa Nativa vía Socket IPC (`-ap <SOCKET>`):** Escucha eventos directamente desde el servidor X11 (`PropertyNotify`) con **0.0% de uso de CPU** en reposo. Al detectar que la ventana activa está maximizada o en pantalla completa, envía el comando JSON `set_property pause` directo al socket UNIX de `mpv`, pausando y reanudando el audio y video sin desincronización ni procesos externos.
* **📄 Gestión Nativa de PID (`-p <ARCHIVO_PID>`):** Guarda el identificador del proceso real en un archivo (incluso al usar el modo demonio `-d`) y lo elimina automáticamente al cerrar el programa de forma limpia.
* **🛡️ Protección contra Cierres Inesperados:** Incorpora un manejador de errores X11 (`XSetErrorHandler`) que absorbe de forma segura eventos `BadWindow` cuando las ventanas activas se cierran repentinamente.
* **🌐 Interfaz de Ayuda en Español:** Documentación integrada (`haikei -h`) completamente traducida y detallada.

---

## 📦 Dependencias de Compilación

Para compilar **Haikei** desde el código fuente necesitás un compilador de C estándar (`gcc`, `make`) y las cabeceras de desarrollo de X11:

* **Debian / Ubuntu / Linux Mint:**
  ```bash
  sudo apt install git build-essential libx11-dev libxext-dev libxrender-dev libxrandr-dev
  ```

* **Arch Linux / Manjaro / EndeavourOS:**
  ```bash
  sudo pacman -S git base-devel libx11 libxext libxrender libxrandr
  ```

* **Fedora:**
  ```bash
  sudo dnf install git gcc make libX11-devel libXext-devel libXrender-devel libXrandr-devel
  ```

---

## 🛠️ Compilación e Instalación

Cloná este repositorio, compilá el binario e instalalo en tu sistema:

```bash
git clone https://github.com
cd haikei
make
sudo make install
```

Para desinstalarlo del sistema:
```bash
sudo make uninstall
```

---

## 🚀 Ejemplos de Uso

### Fondo de pantalla animado con Auto-Pausa IPC y archivo PID (Recomendado)
```bash
haikei -fs -sp -st -nf -s -fdt -p /tmp/haikei.pid -ap /tmp/mpv.sock -- \
  mpv -wid WID --loop --no-audio --input-ipc-server=/tmp/mpv.sock video.mp4
```

### Fondos independientes en múltiples monitores
```bash
# Monitor principal (índice 0)
haikei -m 0 -sp -st -nf -s -fdt -- mpv -wid WID --loop fondo1.mp4 &

# Segundo monitor por nombre de salida (ej. HDMI-1)
haikei -m HDMI-1 -sp -st -nf -s -fdt -- mpv -wid WID --loop fondo2.mp4 &
```

### Ventana circular flotante con transparencia
```bash
haikei -g 400x400+100+100 -ni -s -nf -b -un -argb -sh circle -- \
  mpv -wid WID --loop animacion.mp4
```

---

## 📋 Opciones Disponibles

| Bandera | Descripción |
| :--- | :--- |
| `-g {w}x{h}+{x}+{y}` | Especificar geometría manual (ancho, alto y coordenadas X/Y). |
| `-m <MONITOR>` | Ajustar automáticamente al monitor indicado por nombre (`HDMI-1`) o índice (`0`). |
| `-ap <SOCKET>` | Pausar `mpv` vía socket IPC al maximizar ventanas o usar pantalla completa. |
| `-p <ARCHIVO>` | Guardar el PID del proceso en un archivo y borrarlo al salir. |
| `-fs` | Pantalla completa (ocupa todo el escritorio X11). |
| `-fdt` | Forzar la ventana `WID` como tipo escritorio (`_NET_WM_WINDOW_TYPE_DESKTOP`). |
| `-ni` | Ignorar entradas de mouse y teclado. |
| `-nf` | Evitar que la ventana reciba el foco. |
| `-s` / `-st` / `-sp` | Fijar en todos los escritorios (`Sticky`), ocultar de barra de tareas y paginador. |
| `-a` / `-b` | Mantener siempre encima (`Above`) o siempre debajo (`Below`). |
| `-o <0.0-1.0>` | Nivel de opacidad de la ventana (ej. `-o 0.85`). |
| `-sh <FORMA>` | Forma de recorte (`rectangle`, `circle`, `triangle`). |
| `-d` | Ejecutar en segundo plano (*Daemonize*). |
| `-debug` | Mostrar mensajes de depuración en la salida estándar de error. |

---

## 📜 Créditos y Licencia

* **Autor original (`xwinwrap`):** David Reveman (Novell, Inc.)
* **Contribuidores históricos:** Shantanu Goel, Aaahh, mmhobi7
* **Desarrollo de Haikei y nuevas características:** [S.Konqi0139](https://github.com)

Distribuido bajo la licencia original de Novell / MIT-style (ver cabecera en `haikei.c`).
