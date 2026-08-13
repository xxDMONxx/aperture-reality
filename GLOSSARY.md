# Glosario de Elementos - Aperture Reality / Wolvic

Este documento sirve como referencia rápida de los elementos de la interfaz de usuario (UI), el motor de renderizado 3DOF y la arquitectura del proyecto para facilitar la comunicación entre el usuario y el asistente de desarrollo.

---

## 📌 1. Elementos Visuales e Interfaz de Usuario (UI)

| Nombre Real en el Código | Nombre Coloquial | Ubicación y Función Visual |
| :--- | :--- | :--- |
| **`TrayWidget`** | **Bandeja Inferior** / *Barra Flotante Inferior* | Panel flotante horizontal inclinado justo **debajo** de la ventana web principal. Contiene el botón `+` (nueva ventana), botón de **Modo Incógnito**, Marcadores y Descargas. |
| **`WindowWidget`** (o `Windows`) | **Ventana Principal** / *Pantalla Web* | La pantalla 3D curva donde se renderizan las páginas web (ej. YouTube, Google). |
| **`ApertureSideControlsWidget`** | **Panel 3D / IPD** (Derecho) | Panel adosado al **borde derecho** de la ventana web. Inicia replegado como `⚙️ 3D / IPD` y al pulsar se despliega el slider de distancia pupilar (IPD), recentrado y escáner QR. |
| **`ApertureLeftControlsWidget`** | **Control de Distancia** (Izquierdo) | Widget adosado al **borde izquierdo** de la ventana web. Contiene los botones ⬆️ (**Acercar**) y ⬇️ (**Alejar**) para desplazar la ventana en el espacio 3D. |
| **`NavigationBar`** (o `URLBar`) | **Barra de Dirección / URL** | Barra ubicada en la **parte superior** de la ventana principal con botones de navegación (atrás, adelante, recargar), dirección URL (`https://...`) y menú. |
| **`HorizontalTabsBar`** / **`VerticalTabsBar`** | **Barra de Pestañas** | Lista de pestañas abiertas que aparece en el borde superior o lateral cuando hay múltiples ventanas abiertas. |
| **`KeyboardWidget`** | **Teclado Flotante 3D** | Teclado virtual 3D que se despliega al interactuar con campos de texto o la barra de dirección. |
| **`SettingsWidget`** | **Menú de Configuración** | Ventana emergente con las opciones globales de Wolvic (puntero, passthrough, motor de búsqueda, etc.). |

---

## ⚙️ 2. Motor Óptico y Sistema Nivel Código (Backend C++)

| Nombre Real en el Código | Nombre Coloquial | Función en el Código |
| :--- | :--- | :--- |
| **`DeviceDelegate3DOF.cpp`** | **Motor de Render / Pipeline 3DOF** | Código nativo C++ que procesa la orientación del giroscopio, el posicionamiento de las cámaras estéreo y el viewport para cada ojo. |
| **`CardboardDistortionPass`** | **FBO / Shader de Distorsión** | Shader OpenGL que aplica la distorsión de lentes (parámetros $k_1, k_2$) ajustada al perfil Cardboard. |
| **`SettingsStore`** | **Almacén de Preferencias** | Guarda y gestiona los parámetros globales (IPD en mm, distancia de ventana, etc.). |
| **`DEBUG_UI`** | **Comandos ADB en Vivo** | Receptor de eventos `BroadcastReceiver` (`com.igalia.wolvic.DEBUG_UI`) que permite ajustar posiciones y escalas en tiempo real mediante ADB. |

---

## 🛠️ 3. Comandos de Depuración ADB en Vivo

Para ajustar elementos en tiempo real desde la PC mientras usas la aplicación en tu celular:

```powershell
# Ajustar la Bandeja Inferior (TrayWidget)
adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --es target tray --ef x 0.0 --ef y -0.35 --ef z 0.05 --ef rot -45 --ef scale 1.0

# Ajustar el Panel 3D Derecho (ApertureSideControlsWidget)
adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --es target right --ef x 0.06 --ef y 0.0 --ef z 0.0

# Ajustar el Control de Distancia Izquierdo (ApertureLeftControlsWidget)
adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --es target left --ef x -0.06 --ef y 0.0 --ef z 0.0

# Cambiar la IPD en mm
adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --ef ipd 60.7

# Restablecer posiciones originales
adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --ez reset true
```
