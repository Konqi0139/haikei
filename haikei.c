/*
 * Copyright © 2005 Novell, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Novell, Inc. not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Novell, Inc. makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * NOVELL, INC. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NOVELL, INC. BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: David Reveman <davidr@novell.com>
 */

/*
 * Modified by: Shantanu Goel
 * Tech Blog: http://tech.shantanugoel.com
 * Blog: http://blog.shantanugoel.com
 * Home Page: http://tech.shantanugoel.com/projects/linux/shantz-xwinwrap
 *
 * Changelog:
 * 15-Jan-09:   1. Fixed the bug where XFetchName returning a NULL for "name"
 *                 resulted in a crash.
 *              2. Provided an option to specify the desktop window name.
 *              3. Added debug messages
 *
 * 24-Aug-08:   1. Fixed the geometry option (-g) so that it works
 *              2. Added override option (-ov), for seamless integration with
 *                 desktop like a background in non-fullscreen modes
 *              3. Added shape option (-sh), to create non-rectangular windows.
 *                 Currently supporting circlular and triangular windows
 */

/*
 * Picked up by Aaahh, https://github.com/Aaahh/xwinwrap
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>

#include <X11/extensions/shape.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xrandr.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>

#define WIDTH 512
#define HEIGHT 384

#define OPAQUE 0xffffffff

#define NAME "haikei"
#define VERSION "1.0"

#define ATOM(a) XInternAtom(display, #a, False)

Display *display = NULL;
int display_width;
int display_height;
int screen;

typedef enum {
  SHAPE_RECT = 0,
  SHAPE_CIRCLE,
  SHAPE_TRIG,
} win_shape;

struct window {
  Window root, window, desktop;
  Drawable drawable;
  Visual *visual;
  Colormap colourmap;

  unsigned int width;
  unsigned int height;
  int x;
  int y;
} window;

bool debug = false;

static pid_t pid = 0;

static char *pidFile = NULL;

static void cleanupPidFile(void)
{
    if (pidFile != NULL) {
        unlink(pidFile);
    }
}

static char **childArgv = 0;
static int nChildArgv = 0;

static int addArguments(char **argv, int n) {
  char **newArgv;
  int i;

  newArgv = realloc(childArgv, sizeof(char *) * (nChildArgv + n));
  if (!newArgv)
    return 0;

  for (i = 0; i < n; i++)
    newArgv[nChildArgv + i] = argv[i];

  childArgv = newArgv;
  nChildArgv += n;

  return n;
}

static void setWindowOpacity(unsigned int opacity) {
  CARD32 o;

  o = opacity;

  XChangeProperty(display, window.window, ATOM(_NET_WM_WINDOW_OPACITY),
                  XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&o, 1);
}

static int x11_error_handler(Display *dpy, XErrorEvent *err) {
  /* Ignorar BadWindow (cuando una ventana se cierra justo al consultarla) */
  if (err->error_code == BadWindow || err->error_code == BadDrawable) {
    return 0;
  }
  char msg[256];
  XGetErrorText(dpy, err->error_code, msg, sizeof(msg));
  fprintf(stderr, NAME ": X11 Error: %s\n", msg);
  return 0;
}

static void init_x11() {
  display = XOpenDisplay(NULL);
  if (!display) {
    fprintf(stderr, NAME ": Error: no se pudo abrir el servidor X (display)\n");
    return;
  }
  XSetErrorHandler(x11_error_handler);
  screen = DefaultScreen(display);
  display_width = DisplayWidth(display, screen);
  display_height = DisplayHeight(display, screen);
}

static int get_argb_visual(Visual **visual, int *depth) {
  XVisualInfo visual_template;
  XVisualInfo *visual_list;
  int nxvisuals = 0, i;

  visual_template.screen = screen;
  visual_list =
      XGetVisualInfo(display, VisualScreenMask, &visual_template, &nxvisuals);
  for (i = 0; i < nxvisuals; i++) {
    if (visual_list[i].depth == 32 && (visual_list[i].red_mask == 0xff0000 &&
                                       visual_list[i].green_mask == 0x00ff00 &&
                                       visual_list[i].blue_mask == 0x0000ff)) {
      *visual = visual_list[i].visual;
      *depth = visual_list[i].depth;
      if (debug)
        fprintf(stderr, "Found ARGB Visual\n");
      XFree(visual_list);
      return 1;
    }
  }
  if (debug)
    fprintf(stderr, "No ARGB Visual found");
  XFree(visual_list);
  return 0;
}

static void sigHandler(int sig) {
  if (pid > 0) {
    kill(pid, sig);
  }
  cleanupPidFile();
  exit(0);
}

static void usage(void) {
    fprintf(stderr, "%s v%s \n", NAME, VERSION);
    fprintf(stderr, "\nUso: %s [-g {w}x{h}+{x}+{y}] [-ni] [-argb] [-fdt] [-fs] [-s] [-st] [-sp] [-a] [-b] [-nf] [-o OPACIDAD] [-sh FORMA] [-ov] [-d] [-p ARCHIVO_PID] [-debug] -- COMANDO ARG1...\n", NAME);
    fprintf(stderr, "Opciones:\n \
           -g         - Especificar geometría (w=ancho, h=alto, x=coord-x, y=coord-y. ej: -g 640x480+100+100)\n \
           -ni        - Ignorar entrada (clics y teclado)\n \
           -argb      - Canal alfa / transparencia ARGB\n \
           -fdt       - Forzar ventana WID como tipo escritorio (desktop)\n \
           -fs        - Pantalla completa\n \
           -un        - Sin bordes ni decoración de ventana\n \
           -s         - Fijar en todos los escritorios (Sticky)\n \
           -st        - Ocultar de la barra de tareas (Skip Taskbar)\n \
           -sp        - Ocultar del paginador de escritorios (Skip Pager)\n \
           -a         - Siempre encima (Above)\n \
           -b         - Siempre debajo (Below)\n \
           -nf        - No recibir foco (No Focus)\n \
           -o         - Valor de opacidad entre 0 y 1 (ej: -o 0.20)\n \
           -sh        - Forma de la ventana (elegir entre rectangle, circle o triangle. Por defecto: rectangle)\n \
           -ov        - Activar bandera override_redirect (para integración sin bordes fuera de pantalla completa)\n \
           -d         - Ejecutar en segundo plano (Daemonize)\n \
           -m         - Ajustar al monitor especificado por nombre o índice (ej: -m HDMI-1 o -m 0)\n \
           -ap <socketfile>  - Pausar automáticamente el proceso hijo al maximizar una ventana\n \
           -p         - Guardar el PID del proceso en un archivo\n \
           -debug     - Activar mensajes de depuración\n");
}

static char *monitorTarget = NULL;

static bool set_monitor_geometry(const char *target) {
  int nmonitors = 0;
  XRRMonitorInfo *monitors = XRRGetMonitors(display, DefaultRootWindow(display), False, &nmonitors);
  if (!monitors || nmonitors <= 0) {
    fprintf(stderr, NAME ": Error: no se pudieron obtener los monitores vía Xrandr\n");
    return false;
  }

  /* Comprobar si el usuario pasó un número (ej: -m 0 o -m 1) */
  char *endptr = NULL;
  long idx = strtol(target, &endptr, 10);
  if (*target != '\0' && *endptr == '\0') {
    if (idx >= 0 && idx < nmonitors) {
      window.x = monitors[idx].x;
      window.y = monitors[idx].y;
      window.width = monitors[idx].width;
      window.height = monitors[idx].height;
      XRRFreeMonitors(monitors);
      return true;
    } else {
      fprintf(stderr, NAME ": Error: índice de monitor %ld fuera de rango (disponibles: 0 a %d)\n", idx, nmonitors - 1);
      XRRFreeMonitors(monitors);
      return false;
    }
  }

  /* Buscar por nombre de salida (ej: -m HDMI-1 o -m VIRTUAL-LEFT) */
  for (int i = 0; i < nmonitors; i++) {
    char *monName = XGetAtomName(display, monitors[i].name);
    if (monName != NULL) {
      if (strcmp(monName, target) == 0) {
        window.x = monitors[i].x;
        window.y = monitors[i].y;
        window.width = monitors[i].width;
        window.height = monitors[i].height;
        XFree(monName);
        XRRFreeMonitors(monitors);
        return true;
      }
      XFree(monName);
    }
  }

  fprintf(stderr, NAME ": Error: no se encontró el monitor '%s'\n", target);
  XRRFreeMonitors(monitors);
  return false;
}
static char *autoPauseSocket = NULL;
static bool isPaused = false;
static Window currentActiveWin = None;

/* Envía el comando de pausa directo al socket IPC de mpv en C puro */
static void send_mpv_pause(bool pause_state) {
  if (autoPauseSocket == NULL) return;

  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) return;

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, autoPauseSocket, sizeof(addr.sun_path) - 1);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
    const char *cmd = pause_state
        ? "{ \"command\": [\"set_property\", \"pause\", true] }\n"
        : "{ \"command\": [\"set_property\", \"pause\", false] }\n";
    
    if (write(sock, cmd, strlen(cmd)) > 0) {
      /* Avisar a mpv que terminamos de escribir y leer su respuesta para evitar Broken pipe */
      shutdown(sock, SHUT_WR);
      char buf[128];
      read(sock, buf, sizeof(buf));
    }
  }
  close(sock);
}

/* Verifica si una ventana específica está maximizada o en pantalla completa */
static bool is_window_maximized_or_fs(Window win) {
  if (win == None || win == window.window || win == window.root || win == window.desktop)
    return false;

  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *prop = NULL;

  Atom net_wm_state = ATOM(_NET_WM_STATE);
  Atom fs = ATOM(_NET_WM_STATE_FULLSCREEN);
  Atom max_h = ATOM(_NET_WM_STATE_MAXIMIZED_HORZ);
  Atom max_v = ATOM(_NET_WM_STATE_MAXIMIZED_VERT);
  Atom hidden = ATOM(_NET_WM_STATE_HIDDEN);

  if (XGetWindowProperty(display, win, net_wm_state, 0, 1024, False, XA_ATOM,
                         &actual_type, &actual_format, &nitems, &bytes_after,
                         &prop) == Success && prop != NULL) {
    Atom *atoms = (Atom *)prop;
    bool is_fs = false, is_max_h = false, is_max_v = false, is_hidden = false;

    for (unsigned long i = 0; i < nitems; i++) {
      if (atoms[i] == fs) is_fs = true;
      else if (atoms[i] == max_h) is_max_h = true;
      else if (atoms[i] == max_v) is_max_v = true;
      else if (atoms[i] == hidden) is_hidden = true;
    }
    XFree(prop);

    if (is_hidden) return false;
    if (is_fs || (is_max_h && is_max_v)) return true;
  }
  return false;
}
/* Revisa la ventana activa actual y pausa/reanuda mpv vía socket IPC */
static void check_autopause(void) {
  if (autoPauseSocket == NULL || pid <= 0) return;

  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *prop = NULL;
  Window active = None;

  if (XGetWindowProperty(display, window.root, ATOM(_NET_ACTIVE_WINDOW), 0, 1,
                         False, XA_WINDOW, &actual_type, &actual_format,
                         &nitems, &bytes_after, &prop) == Success && prop != NULL) {
    if (nitems > 0) {
      active = *(Window *)prop;
    }
    XFree(prop);
  }

  if (active != currentActiveWin) {
    currentActiveWin = active;
    if (active != None && active != window.root && active != window.window) {
      XSelectInput(display, active, PropertyChangeMask);
    }
  }

  bool should_pause = is_window_maximized_or_fs(active);

  if (should_pause && !isPaused) {
    send_mpv_pause(true);
    isPaused = true;
    if (debug) fprintf(stderr, NAME ": Auto-pausa IPC activada\n");
  } else if (!should_pause && isPaused) {
    send_mpv_pause(false);
    isPaused = false;
    if (debug) fprintf(stderr, NAME ": Auto-pausa IPC desactivada\n");
  }
}

static Window find_subwindow(Window win, int w, int h) {
  unsigned int i, j;
  Window troot, parent, *children;
  unsigned int n;

  /* search subwindows with same size as display or work area */

  for (i = 0; i < 10; i++) {
    XQueryTree(display, win, &troot, &parent, &children, &n);

    for (j = 0; j < n; j++) {
      XWindowAttributes attrs;

      if (XGetWindowAttributes(display, children[j], &attrs)) {
        /* Window must be mapped and same size as display or
         * work space */
        if (attrs.map_state != 0 &&
            ((attrs.width == display_width && attrs.height == display_height) ||
             (attrs.width == w && attrs.height == h))) {
          win = children[j];
          break;
        }
      }
    }

    XFree(children);
    if (j == n) {
      break;
    }
  }

  return win;
}


static Window find_desktop_window(Window *p_root, Window *p_desktop) {
  Atom type;
  int format, i;
  unsigned long nitems, bytes;
  unsigned int n;
  Window root = RootWindow(display, screen);
  Window win = root;
  Window troot, parent, *children;
  unsigned char *buf = NULL;

  if (!p_root || !p_desktop) {
    return 0;
  }

  /* some window managers set __SWM_VROOT to some child of root window */

  XQueryTree(display, root, &troot, &parent, &children, &n);
  for (i = 0; i < (int)n; i++) {
    if (XGetWindowProperty(display, children[i], ATOM(__SWM_VROOT), 0, 1, False,
                           XA_WINDOW, &type, &format, &nitems, &bytes,
                           &buf) == Success &&
        type == XA_WINDOW) {
      win = *(Window *)buf;
      XFree(buf);
      XFree(children);
      if (debug) {
        fprintf(stderr,
                NAME ": desktop window (%lx) found from __SWM_VROOT property\n",
                win);
      }
      fflush(stderr);
      *p_root = win;
      *p_desktop = win;
      return win;
    }

    if (buf) {
      XFree(buf);
      buf = 0;
    }
  }
  XFree(children);

  /* get subwindows from root */
  win = find_subwindow(root, -1, -1);

  display_width = DisplayWidth(display, screen);
  display_height = DisplayHeight(display, screen);

  win = find_subwindow(win, display_width, display_height);

  if (buf) {
    XFree(buf);
    buf = 0;
  }

  if (win != root && debug) {
    fprintf(stderr,
            NAME ": desktop window (%lx) is subwindow of root window (%lx)\n",
            win, root);
  } else if (debug) {
    fprintf(stderr, NAME ": desktop window (%lx) is root window\n", win);
  }

  fflush(stderr);

  *p_root = root;
  *p_desktop = win;

  return win;
}

int main(int argc, char **argv) {
  char widArg[256];
  char *widArgv[] = {widArg};
  char *endArg = NULL;
  int status = 0;
  unsigned int opacity = OPAQUE;

  int i;
  bool have_argb_visual = false;
  bool noInput = false;
  bool argb = false;
  bool set_desktop_type = false;
  bool fullscreen = false;
  bool noFocus = false;
  bool override = false;
  bool overrideroot = false;
  bool undecorated = false;
  bool sticky = false;
  bool below = false;
  bool above = false;
  bool skip_taskbar = false;
  bool skip_pager = false;
  bool daemonize = false;

  win_shape shape = SHAPE_RECT;
  Pixmap mask;
  GC mask_gc;
  XGCValues xgcv;

  window.width = WIDTH;
  window.height = HEIGHT;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-g") == 0) {
      if (++i < argc)
        XParseGeometry(argv[i], &window.x, &window.y, &window.width,
                       &window.height);
    } else if (strcmp(argv[i], "-ni") == 0) {
      noInput = 1;
    } else if (strcmp(argv[i], "-argb") == 0) {
      argb = true;
    } else if (strcmp(argv[i], "-fdt") == 0) {
      set_desktop_type = true;
    } else if (strcmp(argv[i], "-fs") == 0) {
      fullscreen = 1;
    } else if (strcmp(argv[i], "-un") == 0) {
      undecorated = true;
    } else if (strcmp(argv[i], "-s") == 0) {
      sticky = true;
    } else if (strcmp(argv[i], "-st") == 0) {
      skip_taskbar = true;
    } else if (strcmp(argv[i], "-sp") == 0) {
      skip_pager = true;
    } else if (strcmp(argv[i], "-a") == 0) {
      above = true;
    } else if (strcmp(argv[i], "-b") == 0) {
      below = true;
    } else if (strcmp(argv[i], "-nf") == 0) {
      noFocus = 1;
    } else if (strcmp(argv[i], "-o") == 0) {
      if (++i < argc)
        opacity = (unsigned int)(atof(argv[i]) * OPAQUE);
    } else if (strcmp(argv[i], "-sh") == 0) {
      if (++i < argc) {
        if (strcasecmp(argv[i], "circle") == 0) {
          shape = SHAPE_CIRCLE;
        } else if (strcasecmp(argv[i], "triangle") == 0) {
          shape = SHAPE_TRIG;
        }
      }
    } else if (strcmp(argv[i], "-ov") == 0) {
      override = true;
    } else if (strcmp(argv[i], "-ap") == 0) {
      if (++i < argc)
      autoPauseSocket = argv[i];
    } else if (strcmp(argv[i], "-p") == 0) {
      if (++i < argc)
      pidFile = argv[i];
    } else if (strcmp(argv[i], "-m") == 0) {
      if (++i < argc)
      monitorTarget = argv[i];
    } else if (strcmp(argv[i], "-ovr") == 0) {
      overrideroot = true;
    } else if (strcmp(argv[i], "-debug") == 0) {
      debug = true;
    } else if (strcmp(argv[i], "-d") == 0) {
      daemonize = true;
    } else if (strcmp(argv[i], "--") == 0) {
      break;
    } else {
      usage();
      return 1;
    }
  }

  char resolvedPidPath[PATH_MAX];
  if (pidFile != NULL && pidFile[0] != '/') {
    char cwd[PATH_MAX / 2];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
      snprintf(resolvedPidPath, sizeof(resolvedPidPath), "%s/%s", cwd, pidFile);
      pidFile = resolvedPidPath;
    }
  }

  if (daemonize) {
    pid_t process_id = 0;
    pid_t sid = 0;
    process_id = fork();
    if (process_id < 0) {
      fprintf(stderr, "fork failed!\n");
      exit(1);
    }

    if (process_id > 0) {
      fprintf(stderr, "pid of child process %d \n", process_id);
      exit(0);
    }
    umask(0);
    sid = setsid();
    if (sid < 0) {
      exit(1);
    }

    chdir("/");
    if (!debug) {
      close(STDIN_FILENO);
      close(STDOUT_FILENO);
      close(STDERR_FILENO);
    }
  }

  if (pidFile != NULL) {
    FILE *fp = fopen(pidFile, "w");
    if (fp != NULL) {
      fprintf(fp, "%d\n", getpid());
      fclose(fp);
      atexit(cleanupPidFile);
    } else {
      perror("Error al abrir el archivo PID");
    }
  }


  for (i = i + 1; i < argc; i++) {
    if (strcmp(argv[i], "WID") == 0)
      addArguments(widArgv, 1);
    else
      addArguments(&argv[i], 1);
  }

  if (!nChildArgv) {
    fprintf(stderr, "%s: Error: couldn't create command line\n", argv[0]);
    usage();

    return 1;
  }

  addArguments(&endArg, 1);

  init_x11();
  if (!display)
    return 1;

  if (monitorTarget != NULL) {
    if (!set_monitor_geometry(monitorTarget)) {
      XCloseDisplay(display);
      return 1;
    }
  } else if (fullscreen || overrideroot) {
    window.x = 0;
    window.y = 0;
    window.width = DisplayWidth(display, screen);
    window.height = DisplayHeight(display, screen);
  }

  int depth = 0, flags = CWOverrideRedirect | CWBackingStore;
  Visual *visual = NULL;

  if (!find_desktop_window(&window.root, &window.desktop)) {
    fprintf(stderr, NAME ": Error: couldn't find desktop window\n");
    return 1;
  }

  if (argb && get_argb_visual(&visual, &depth)) {
    have_argb_visual = true;
    window.visual = visual;
    window.colourmap = XCreateColormap(display, DefaultRootWindow(display),
                                       window.visual, AllocNone);
  } else {
    window.visual = DefaultVisual(display, screen);
    window.colourmap = DefaultColormap(display, screen);
    depth = CopyFromParent;
    visual = CopyFromParent;
  }

  Atom xa;
  if (overrideroot) {
    /* An override_redirect True window.
     * No WM hints or button processing needed. */
    XSetWindowAttributes attrs = {ParentRelative,
                                  0L,
                                  0,
                                  0L,
                                  0,
                                  0,
                                  Always,
                                  0L,
                                  0L,
                                  False,
                                  StructureNotifyMask | ExposureMask,
                                  0L,
                                  True,
                                  0,
                                  0};

    if (have_argb_visual) {
      attrs.colormap = window.colourmap;
      flags |= CWBorderPixel | CWColormap;
    } else {
      flags |= CWBackPixel;
    }

    window.window = XCreateWindow(display, window.root, window.x, window.y,
                                  window.width, window.height, 0, depth,
                                  InputOutput, visual, flags, &attrs);
    XLowerWindow(display, window.window);
    xa = ATOM(_NET_WM_WINDOW_TYPE);

    Atom prop;
    if (set_desktop_type) {
      prop = ATOM(_NET_WM_WINDOW_TYPE_DESKTOP);
    } else {
      prop = ATOM(_NET_WM_WINDOW_TYPE_NORMAL);
    }

    XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&prop, 1);
    fprintf(stderr, NAME ": window type - override\n");
    fflush(stderr);
  } else if (override) {
    /* An override_redirect True window.
     * No WM hints or button processing needed. */
    XSetWindowAttributes attrs = {ParentRelative,
                                  0L,
                                  0,
                                  0L,
                                  0,
                                  0,
                                  Always,
                                  0L,
                                  0L,
                                  False,
                                  StructureNotifyMask | ExposureMask,
                                  0L,
                                  True,
                                  0,
                                  0};

    if (have_argb_visual) {
      attrs.colormap = window.colourmap;
      flags |= CWBorderPixel | CWColormap;
    } else {
      flags |= CWBackPixel;
    }

    window.window = XCreateWindow(display, window.desktop, window.x, window.y,
                                  window.width, window.height, 0, depth,
                                  InputOutput, visual, flags, &attrs);
    XLowerWindow(display, window.window);

    fprintf(stderr, NAME ": window type - override\n");
    fflush(stderr);
  } else {
    XSetWindowAttributes attrs = {ParentRelative,
                                  0L,
                                  0,
                                  0L,
                                  0,
                                  0,
                                  Always,
                                  0L,
                                  0L,
                                  False,
                                  StructureNotifyMask | ExposureMask |
                                      ButtonPressMask | ButtonReleaseMask,
                                  0L,
                                  False,
                                  0,
                                  0};

    XWMHints wmHint;

    if (have_argb_visual) {
      attrs.colormap = window.colourmap;
      flags |= CWBorderPixel | CWColormap;
    } else {
      flags |= CWBackPixel;
    }

    window.window = XCreateWindow(display, window.root, window.x, window.y,
                                  window.width, window.height, 0, depth,
                                  InputOutput, visual, flags, &attrs);

    wmHint.flags = InputHint | StateHint;
    // wmHint.input = undecorated ? False : True;
    wmHint.input = !noFocus;
    wmHint.initial_state = NormalState;

    XSetWMProperties(display, window.window, NULL, NULL, argv, argc, NULL,
                     &wmHint, NULL);

    xa = ATOM(_NET_WM_WINDOW_TYPE);
    Atom prop;
    if (set_desktop_type) {
      prop = ATOM(_NET_WM_WINDOW_TYPE_DESKTOP);
    } else {
      prop = ATOM(_NET_WM_WINDOW_TYPE_NORMAL);
    }

    XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&prop, 1);

    if (undecorated) {
      xa = ATOM(_MOTIF_WM_HINTS);
      if (xa != None) {
        long prop[5] = {2, 0, 0, 0, 0};
        XChangeProperty(display, window.window, xa, xa, 32, PropModeReplace,
                        (unsigned char *)prop, 5);
      }
    }

    /* Below other windows */
    if (below) {

      xa = ATOM(_WIN_LAYER);
      if (xa != None) {
        long prop = 0;

        XChangeProperty(display, window.window, xa, XA_CARDINAL, 32,
                        PropModeAppend, (unsigned char *)&prop, 1);
      }

      xa = ATOM(_NET_WM_STATE);
      if (xa != None) {
        Atom xa_prop = ATOM(_NET_WM_STATE_BELOW);

        XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeAppend,
                        (unsigned char *)&xa_prop, 1);
      }
    }

    /* Above other windows */
    if (above) {

      xa = ATOM(_WIN_LAYER);
      if (xa != None) {
        long prop = 6;

        XChangeProperty(display, window.window, xa, XA_CARDINAL, 32,
                        PropModeAppend, (unsigned char *)&prop, 1);
      }

      xa = ATOM(_NET_WM_STATE);
      if (xa != None) {
        Atom xa_prop = ATOM(_NET_WM_STATE_ABOVE);

        XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeAppend,
                        (unsigned char *)&xa_prop, 1);
      }
    }

    /* Sticky */
    if (sticky) {

      xa = ATOM(_NET_WM_DESKTOP);
      if (xa != None) {
        CARD32 xa_prop = 0xFFFFFFFF;

        XChangeProperty(display, window.window, xa, XA_CARDINAL, 32,
                        PropModeAppend, (unsigned char *)&xa_prop, 1);
      }

      xa = ATOM(_NET_WM_STATE);
      if (xa != None) {
        Atom xa_prop = ATOM(_NET_WM_STATE_STICKY);

        XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeAppend,
                        (unsigned char *)&xa_prop, 1);
      }
    }

    /* Skip taskbar */
    if (skip_taskbar) {

      xa = ATOM(_NET_WM_STATE);
      if (xa != None) {
        Atom xa_prop = ATOM(_NET_WM_STATE_SKIP_TASKBAR);

        XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeAppend,
                        (unsigned char *)&xa_prop, 1);
      }
    }

    /* Skip pager */
    if (skip_pager) {

      xa = ATOM(_NET_WM_STATE);
      if (xa != None) {
        Atom xa_prop = ATOM(_NET_WM_STATE_SKIP_PAGER);

        XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeAppend,
                        (unsigned char *)&xa_prop, 1);
      }
    }
  }

  if (opacity != OPAQUE)
    setWindowOpacity(opacity);

  if (noInput) {
    Region region;

    region = XCreateRegion();
    if (region) {
      XShapeCombineRegion(display, window.window, ShapeInput, 0, 0, region,
                          ShapeSet);
      XDestroyRegion(region);
    }
  }

  if (shape) {
    mask =
        XCreatePixmap(display, window.window, window.width, window.height, 1);
    mask_gc = XCreateGC(display, mask, 0, &xgcv);

    switch (shape) {
    // Nothing special to be done if it's a rectangle
    case SHAPE_CIRCLE:
      /* fill mask */
      XSetForeground(display, mask_gc, 0);
      XFillRectangle(display, mask, mask_gc, 0, 0, window.width, window.height);

      XSetForeground(display, mask_gc, 1);
      XFillArc(display, mask, mask_gc, 0, 0, window.width, window.height, 0,
               23040);
      break;

    case SHAPE_TRIG: {
      XPoint points[3] = {{0, window.height},
                          {window.width / 2, 0},
                          {window.width, window.height}};

      XSetForeground(display, mask_gc, 0);
      XFillRectangle(display, mask, mask_gc, 0, 0, window.width, window.height);

      XSetForeground(display, mask_gc, 1);
      XFillPolygon(display, mask, mask_gc, points, 3, Complex, CoordModeOrigin);
    }

    break;

    default:
      break;
    }
    /* combine */
    XShapeCombineMask(display, window.window, ShapeBounding, 0, 0, mask,
                      ShapeSet);
  }

  XMapWindow(display, window.window);

  XSync(display, window.window);

  sprintf(widArg, "0x%x", (int)window.window);

  pid = fork();

  switch (pid) {
  case -1:
    perror("fork");
    return 1;
  case 0:
    execvp(childArgv[0], childArgv);
    perror(childArgv[0]);
    exit(2);
    break;
  default:
    break;
  }

  signal(SIGTERM, sigHandler);
  signal(SIGINT, sigHandler);

  if (autoPauseSocket != NULL) {
    XSelectInput(display, window.root, PropertyChangeMask);
    check_autopause();
  }

  int x11_fd = ConnectionNumber(display);
  for (;;) {
    /* Verificar si el proceso hijo (mpv) terminó sin bloquear */
    pid_t res = waitpid(pid, &status, WNOHANG);
    if (res == pid || res == -1) {
      if (res == pid && WIFEXITED(status))
        fprintf(stderr, "%s terminó con estado %d\n", childArgv[0],
                WEXITSTATUS(status));
      break;
    }

    if (autoPauseSocket != NULL) {
      while (XPending(display)) {
        XEvent ev;
        XNextEvent(display, &ev);
        if (ev.type == PropertyNotify) {
          check_autopause();
        }
      }
      /* Esperar eventos de X11 o revisar cada 200ms si mpv cerró */
      fd_set in_fds;
      FD_ZERO(&in_fds);
      FD_SET(x11_fd, &in_fds);
      struct timeval tv = {0, 200000};
      select(x11_fd + 1, &in_fds, NULL, NULL, &tv);
    } else {
      /* Comportamiento original si no se usa -ap */
      if (waitpid(pid, &status, 0) != -1) {
        if (WIFEXITED(status))
          fprintf(stderr, "%s terminó con estado %d\n", childArgv[0],
                  WEXITSTATUS(status));
        break;
      }
    }
  }

  XDestroyWindow(display, window.window);
  XCloseDisplay(display);

  return 0;
}

