# 📁 Upstream Releases (Nuevas Versiones Base de Wolvic)

Esta carpeta está destinada a recibir las nuevas versiones oficiales del código base de **Wolvic** (de Igalia) a medida que vayan saliendo.

---

## 🛠️ Cómo funciona el proceso de conversión a Aperture Reality

1. **Colocar la nueva versión de Wolvic**:
   Descarga o coloca la carpeta/archivo comprimido de la nueva versión de Wolvic dentro de esta carpeta (ej. `upstream_releases/wolvic-v1.6.0` o `upstream_releases/wolvic-main-new`).

2. **Indicar la conversión**:
   Pídeme: *"Toma la nueva versión en `upstream_releases/` y conviértela a Aperture Reality"*.

3. **Flujo de Trasplante de Capas**:
   Procesaré automáticamente la nueva versión aplicando toda la **capa de personalización de Aperture Reality**:
   - 🎯 **Pipeline Nativo C++ (`DeviceDelegate3DOF.cpp`)**: Matemática de distorsión de lentes Cardboard $k_1, k_2$, FOV y centrado óptico.
   - ⚙️ **Panel 3D / IPD Derecho (`ApertureSideControlsWidget.java` & XML)**: IPD por defecto en 60.7mm, recentrado 3D y escáner QR.
   - 🔍 **Control de Distancia Izquierdo (`ApertureLeftControlsWidget.java` & XML)**: Botones ⬆️ / ⬇️ con margen hasta 2.5m.
   - 📌 **Bandeja Inferior 1:1 (`TrayWidget.java`)**: Movimiento y escalado 100% rígido sincronizado con la ventana principal.
   - ⚡ **Sistema ADB en Vivo (`com.igalia.wolvic.DEBUG_UI`)**: Receptor para depuración e inclinación de elementos en vivo.

4. **Compilación y Despliegue**:
   Se compilará la nueva versión convertida en el APK de Aperture Reality, se instalará en el celular y se subirá el commit actualizado a GitHub.
