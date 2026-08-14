# Arquitectura y Capa de Personalización — Aperture Reality (Wolvic 3DoF VR)

Documento maestro de referencia que detalla la evolución completa del desarrollo, los componentes propietarios creados, las correcciones matemáticas y espaciales en C++/Java/JNI, y la **Guía de Portabilidad y Actualización** para integrar esta capa en futuras versiones de *Wolvic Main*.

---

## 1. Arquitectura de la Capa "Aperture Reality"

Aperture Reality transforma el navegador VR Wolvic (concebido para visores 6DoF como Meta Quest o Pico) en una experiencia completa, inmersiva y calibrada para smartphones Android dentro de gafas tipo **Google Cardboard / VRBox / visores 3DoF**.

```
[Smartphone Android + Giroscopio]
       │
       ▼
[DeviceDelegate3DOF.cpp] ──► Cámara (0, 1.48, 1.45) + Frustum Asimétrico Per-Ojo
       │
       ▼
[Aperture Controls 3D] ──► IPD (54-74mm) + Recentrado Yaw + Zoom Distancia Z
       │
       ▼
[Scene Graph Wolvic] ──► Diálogos (+0.08m) + Teclado (+0.15m, -0.12m) + Dragging sin saltos
```

---

## 2. Componentes Propietarios y Modificaciones Clave

### A. Sabor y Backend 3DoF (`app/src/universal3dof/`)
- **`DeviceDelegate3DOF.cpp` / `DeviceDelegate3DOF.h`**:
  - Manejo del sensor de orientación (giroscopio/acelerómetro) sin drift.
  - Cámara virtual situada en `(0.0f, 1.48f, 1.45f)`.
  - Frustum de proyección estéreo asimétrico configurable por ojo (separación IPD y alineación vertical `shiftY`).
  - Retícula de puntero apuntada por la mirada (`Gaze pointer`) con prioridad visual absoluta (`glDisable(GL_DEPTH_TEST)`).
- **`PlatformActivity.java`**:
  - Actividad principal Android que gestiona la orientación horizontal, pantalla completa inmersiva y puente JNI hacia C++.
  - Integración del escáner ZXing para perfiles QR de visores.

### B. Controles Espaciales 3D en el Navegador (`app/src/common/shared/.../widgets/`)
- **`ApertureSideControlsWidget.java` & `aperture_side_controls.xml` (Lado Derecho)**:
  - Panel desplegable 3D anclado al lateral de la ventana principal.
  - Permite ajustar en tiempo real el **IPD (Distancia Interpupilar)** entre $54\text{ mm}$ y $74\text{ mm}$ mediante botones `+` / `−` o slider.
  - Botón de **Recentrado Instantáneo (`btn_recenter`)**.
- **`ApertureLeftControlsWidget.java` & `aperture_left_controls.xml` (Lado Izquierdo)**:
  - Widget lateral izquierdo con iconos vectoriales de lupa/zoom (`ic_aperture_zoom_in` y `ic_aperture_zoom_out`).
  - Control de distancia de la ventana en tiempo real hacia adelante y hacia atrás.

### C. Correcciones Matemáticas y de Espacio 3D

1. **Corrección del Arrastre de Ventanas (`WidgetMover.cpp` & `BrowserWorld.cpp`)**:
   - Se eliminó el bucle de retroalimentación de matrices compuestas en `ProjectPoint` y la aceleración indebida de $Z$ (`ThrottledWindowDistanceComputation`), permitiendo tomar la ventana desde su anclaje exacto sin saltos ni desfasajes.
2. **Profundidad Relativa de Diálogos (`PromptDialogWidget.java`, etc.)**:
   - Reemplazo de las fórmulas de coordenadas absolutas por un offset relativo constante (`translationZ = +0.08m`). Los carteles de bienvenida, búsqueda por voz y login flotan siempre $8\text{ cm}$ por delante de la pantalla sin ser tapados por la retícula.
3. **Ergonomía del Teclado Flotante (`KeyboardWidget.java`)**:
   - Anclaje superior (`anchorY = 1.0f`) con separación de $12\text{ cm}$ bajo la barra de direcciones (`translationY = -0.12m`), inclinación a $-30^\circ$, centrado horizontal (`translationX = 0.0f`) y profundidad libre (`translationZ = +0.15m`).
4. **Restauración de Distancia al Salir del Modo Inmersivo (`Windows.java`)**:
   - Al salir de pantalla completa o videos VR, se preserva y restaura la distancia $Z$ configurada por el usuario sin ser sobreescrita por `placeWindow`.
5. **Calibración de Distancia Óptima (`build.gradle` & `dimen.xml`)**:
   - `DEFAULT_WINDOW_DISTANCE = 0.35f` (distancia por defecto natural a $-3.0\text{m}$).
   - Margen de zoom $Z$ expandido entre $-1.5\text{m}$ y $-6.0\text{m}$.

---

## 3. Mapa de Archivos de la Capa Aperture Reality

Para portar o actualizar este proyecto sobre una nueva versión de Wolvic, los siguientes archivos contienen todas las innovaciones de Aperture Reality:

| Archivo | Tipo | Propósito |
| :--- | :--- | :--- |
| `app/src/universal3dof/*` | **NUEVO / EXCLUSIVO** | Código fuente del flavor Universal 3DoF (C++, Java, CMake, recursos). |
| `app/src/common/shared/.../ui/widgets/ApertureSideControlsWidget.java` | **NUEVO** | Lógica del widget 3D de IPD y recentrado. |
| `app/src/common/shared/.../ui/widgets/ApertureLeftControlsWidget.java` | **NUEVO** | Lógica del widget 3D de acercar/alejar pantalla. |
| `app/src/common/shared/.../utils/CardboardQrDecoder.java` | **NUEVO** | Decodificador Protobuf para códigos QR de Google Cardboard. |
| `app/src/main/res/layout/aperture_side_controls.xml` | **NUEVO** | Layout visual del panel 3D derecho. |
| `app/src/main/res/layout/aperture_left_controls.xml` | **NUEVO** | Layout visual del panel 3D izquierdo. |
| `app/src/main/res/drawable/ic_aperture_*.xml` | **NUEVO** | Iconos vectoriales modernos de zoom y flechas. |
| `app/src/main/cpp/BrowserWorld.cpp` | **MODIFICADO** | Renderizado de retícula, recentrado UI, ciclo de vida inmersivo. |
| `app/src/main/cpp/WidgetMover.cpp` | **MODIFICADO** | Corrección de agarre y movimiento de ventanas sin saltos. |
| `app/src/common/shared/.../ui/widgets/KeyboardWidget.java` | **MODIFICADO** | Posicionamiento ergonómico del teclado flotante. |
| `app/src/common/shared/.../ui/widgets/Windows.java` | **MODIFICADO** | Restauración fiel de distancia al salir de Fullscreen. |
| `app/src/common/shared/.../ui/widgets/dialogs/*.java` | **MODIFICADO** | Offset $Z$ relativo constante para cuadros de diálogo. |
| `app/src/common/shared/.../VRBrowserActivity.java` | **MODIFICADO** | Registro de widgets Aperture y eventos de recentrado. |
| `app/build.gradle` | **MODIFICADO** | Configuración de distancias por defecto y dependencias ZXing. |

---

## 4. Guía para Actualizar a una Nueva Versión de Wolvic Main

Cuando descargues una nueva versión upstream de Wolvic, el proceso de actualización se puede realizar en pocos pasos limpios:

### Método 1: Mediante Git Rebase / Merge (Recomendado)
1. Agregar el repositorio upstream oficial de Wolvic como un remote secundario:
   ```bash
   git remote add upstream https://github.com/Igalia/wolvic.git
   git fetch upstream
   ```
2. Crear una rama de actualización sobre la nueva versión:
   ```bash
   git checkout -b update-wolvic-main upstream/main
   git merge main --allow-unrelated-histories
   # o bien: git rebase upstream/main
   ```
3. Como el 90% de nuestro código vive en su propia carpeta aislada (`app/src/universal3dof/`, widgets `Aperture*.java` y layouts dedicados), los conflictos serán mínimos y se resolverán casi de inmediato.

### Método 2: Aplicación de Parches / Overlay
Si prefieres partir de una carpeta limpia con el nuevo código de Wolvic:
1. Copiar la carpeta completa `app/src/universal3dof/`.
2. Copiar los widgets `ApertureSideControlsWidget.java`, `ApertureLeftControlsWidget.java`, `CardboardQrDecoder.java` y los recursos XML asociados.
3. Aplicar las modificaciones puntuales documentadas en la Sección 2 en `VRBrowserActivity.java`, `BrowserWorld.cpp`, `WidgetMover.cpp`, `Windows.java` y `KeyboardWidget.java`.
4. Compilar con JDK 17:
   ```powershell
   $env:JAVA_HOME="C:\Users\PC\Downloads\jdk17\jdk-17.0.19+10"; .\gradlew.bat assembleUniversal3dofArm64GeckoGenericDebug
   ```
