# 🥽 Aperture Reality — Next-Gen 3DOF VR Browser for Mobile Headsets

[![Android 3DOF](https://img.shields.io/badge/Platform-Android%203DOF-success.svg?style=for-the-badge&logo=android)](https://developer.android.com/)
[![OpenGL ES 3.0](https://img.shields.io/badge/Graphics-OpenGL%20ES%203.0-orange.svg?style=for-the-badge&logo=opengl)](https://www.khronos.org/opengles/)
[![Gecko Engine](https://img.shields.io/badge/Engine-GeckoView-blue.svg?style=for-the-badge&logo=firefox)](https://mozilla.org)
[![Build Status](https://img.shields.io/badge/Build-Gradle%20%7C%20Java%2017-brightgreen.svg?style=for-the-badge)](https://gradle.org)

**Aperture Reality** es un navegador WebXR/VR 3DOF de alto rendimiento optimizado para visores móviles (Google Cardboard y derivados). Diseñado sobre el motor nativo de **Wolvic**, reconstruye el pipeline de visión óptica estéreo, agrega controles ergonómicos 3D vinculados dinámicamente y permite depuración y sintonización de la interfaz en tiempo real por ADB.

---

## 🌟 Características Destacadas

### 🎯 1. Pipeline Óptico Cardboard de Alta Precisión
- **Corrección de Lentes Estéreo**: Renderizado FBO nativo en C++ (`DeviceDelegate3DOF.cpp`) con soporte para los coeficientes de distorsión radial $k_1$ y $k_2$.
- **FOV y Mapeo Pupilar**: Ajuste estricto del campo de visión y centrado óptico según el perfil oficial de Google Cardboard.
- **Calibración IPD**: Cambio de distancia interpupilar en tiempo real con valor por defecto de **60.7 mm**.

### 🎮 2. Controles Ergonómicos y Paneles 3D Anclados
- **Panel 3D / IPD (Derecho)** (`ApertureSideControlsWidget`):
  - Botón desplegable `⚙️ 3D / IPD` para minimizar la distracción visual.
  - Controles integrados de IPD, recentrado de vista y lector de QR de Cardboard.
  - Instancia única anclada automáticamente a la pestaña de más a la derecha.
- **Control de Distancia / Zoom (Izquierdo)** (`ApertureLeftControlsWidget`):
  - Botones ergonómicos ⬆️ (**Acercar**) y ⬇️ (**Alejar**).
  - Permite aproximar la pantalla gigante a solo **2.5 metros** en el espacio 3D para una experiencia inmersiva profunda.
  - Anclado automáticamente a la pestaña de más a la izquierda.
- **Bandeja Inferior Sincronizada 1:1** (`TrayWidget`):
  - La bandeja de gestión de ventanas y modo incógnito se mueve y escala de forma 100% rígida junto con la pantalla principal.

### ⚡ 3. Sistema de Depuración y Ajuste Visual en Vivo por ADB
- Modifica la posición $X, Y, Z$, inclinación (rotación) y escala de cualquier widget flotante en vivo mientras usas las gafas, **sin necesidad de reiniciar o reinstalar la app**.

---

## 📐 Glosario de Elementos

Para conocer los nombres en código y coloquiales de todos los componentes, consulta nuestro [GLOSSARY.md](GLOSSARY.md).

---

## 🛠️ Instalación y Compilación

### Requisitos Previos
- **JDK 17** instalado (`JAVA_HOME`).
- **Android SDK** con `adb` y `platform-tools`.
- Teléfono móvil Android conectado vía USB / ADB.

### 1. Compilar el APK Debug
```powershell
$env:JAVA_HOME="C:\Users\PC\Downloads\jdk17\jdk-17.0.19+10"
.\gradlew.bat assembleUniversal3dofArm64GeckoGenericDebug
```

### 2. Instalar y Ejecutar en el Celular
```powershell
& "C:\Users\PC\AppData\Local\Android\Sdk\platform-tools\adb.exe" -s R58T31LQ6HE install -r "app\build\outputs\apk\universal3dofArm64GeckoGeneric\debug\Wolvic-universal3dof-arm64-gecko-generic-debug.apk"
& "C:\Users\PC\AppData\Local\Android\Sdk\platform-tools\adb.exe" -s R58T31LQ6HE shell "am force-stop com.igalia.wolvic.u3dof; am start -n com.igalia.wolvic.u3dof/com.igalia.wolvic.VRBrowserActivity"
```

---

## 💻 Cheatsheet de Depuración ADB en Tiempo Real

Ejecuta estos comandos en tu consola mientras tienes puestas las gafas VR:

| Acción | Comando ADB |
| :--- | :--- |
| **Mover Bandeja Inferior** | `adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --es target tray --ef x 0.0 --ef y -0.35 --ef z 0.05` |
| **Inclinar Bandeja** | `adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --es target tray --ef rot -30` |
| **Escalar Bandeja** | `adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --es target tray --ef scale 1.2` |
| **Ajustar IPD (mm)** | `adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --ef ipd 60.7` |
| **Ajustar Distancia Z** | `adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --ef dist 0.3` |
| **Restablecer Todo** | `adb shell am broadcast -a com.igalia.wolvic.DEBUG_UI --ez reset true` |

---

## 📁 Estructura del Código

```text
app/src/
 ├── universal3dof/cpp/
 │    └── DeviceDelegate3DOF.cpp       # Pipeline nativo C++ de distorsión óptica y sensores 3DOF
 └── common/shared/com/igalia/wolvic/
      ├── VRBrowserActivity.java        # Actividad principal, receptor DEBUG_UI y gestión de widgets
      └── ui/widgets/
           ├── ApertureSideControlsWidget.java  # Panel 3D / IPD derecho
           ├── ApertureLeftControlsWidget.java  # Control de distancia/zoom izquierdo
           ├── TrayWidget.java                  # Bandeja flotante inferior 1:1
           └── Windows.java                     # Administrador de ventanas 3D
```

---

## 📜 Licencia

Basado en el motor de código abierto **Wolvic** por [Igalia](https://igalia.com) bajo licencia MPL 2.0. Personalizaciones y pipeline óptico de **Aperture Reality**.
