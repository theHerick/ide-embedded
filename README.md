<table>
<tr>
<td width="180">

<img src="readme_foto.png" width="160" alt="IDE Embedded Logo"/>

</td>
<td>

<pre>
    ██╗██████╗ ███████╗    ███████╗███╗   ███╗██████╗ ███████╗██████╗ ██████╗ ███████╗██████╗ 
    ██║██╔══██╗██╔════╝    ██╔════╝████╗ ████║██╔══██╗██╔════╝██╔══██╗██╔══██╗██╔════╝██╔══██╗
    ██║██║  ██║█████╗      █████╗  ██╔████╔██║██████╔╝█████╗  ██║  ██║██║  ██║█████╗  ██║  ██║
    ██║██║  ██║██╔══╝      ██╔══╝  ██║╚██╔╝██║██╔══██╗██╔══╝  ██║  ██║██║  ██║██╔══╝  ██║  ██║
    ██║██████╔╝███████╗    ███████╗██║ ╚═╝ ██║██████╔╝███████╗██████╔╝██████╔╝███████╗██████╔╝
    ╚═╝╚═════╝ ╚══════╝    ╚══════╝╚═╝     ╚═╝╚═════╝ ╚══════╝╚═════╝ ╚═════╝ ╚══════╝╚═════╝ 
</pre> 

<p><b>Event-Driven IDE · Physical Routing · Powered by Qt6 & C++17 · Instant Code Gen</b></p>

<img src="https://img.shields.io/badge/Status-Stable-brightgreen?logo=qt" alt="Status" />
<img src="https://img.shields.io/badge/Language-C%2B%2B17-blue?logo=c%2B%2B" alt="C++" />
<img src="https://img.shields.io/badge/Framework-Qt6--Widgets-mediumblue?logo=qt" alt="Qt6" />
<img src="https://img.shields.io/badge/Build-CMake--Ninja-orange?logo=cmake" alt="CMake" />
<img src="https://img.shields.io/badge/license-Academic--Non--Commercial-red" alt="License" />

</td>
</tr>
</table>

**IDE Embedded** is a high-performance, native desktop platform for modeling, simulating, and generating code for event-driven embedded systems.

Stop wrestling with low-level boilerplate, debounce logic, and hardcoded state machines. Model your hardware workspace visually, design robust logic with an intuitive block-based engine, and compile high-quality C++ code in seconds.

```text
Visual Workspace (Components + Blocks) → IDE Embedded → Production C++ Code (.ino)
```

Designed for speed, visual excellence, and a premium developer experience.

---

## ⚡ High-Performance Core Features (Recently Optimized)

To deliver a state-of-the-art developer experience, the IDE architecture has been engineered with critical performance and safety optimizations:

*   **O(1) Component Lookup Cache**: The simulator uses a high-performance hash mapping (`QHash`) to resolve and interact with component states instantly, bypassing legacy linear $O(n)$ search sweeps. This guarantees smooth, CPU-efficient simulation ticks even in dense workspaces.
*   **Thread-Safe Code Generation**: Utilizes C++17 thread-local memory structures (`thread_local`) during compile passes, making compilation routines fully concurrent and safe from data races.
*   **Collision-Free EEPROM Mapping**: Implements a deterministic, alphabetical pre-scan sequencer for visual state restoration variables, ensuring zero address collisions and clean memory layouts inside the microcontroler's EEPROM.
*   **Zero-Boilerplate Event Sandwich**: Abstracted hardware monitoring (debouncing, noise filtering) from visual block logic so that user loops remain clean and lightweight.

---

## 🎨 Pioneering Architecture

**IDE Embedded** is powered by a groundbreaking, event-oriented programming model for embedded components pioneered by **Herick B. Tiburski**. This methodology abstracts raw physical hardware signals and interrupts into clean, component-level logical events, establishing a pioneering paradigm shift in how embedded software is visually designed and modeled.

### How it Works (The Event Sandwich)

IDE Embedded separates raw physical hardware signals from your core business logic:

```text
                     Hardware Workspace (ESP32)
                                 │
                                 ▼
          Physical Monitor (Automatic Debouncing & Hysteresis)
                                 │
                                 ▼
          User Event Logic (High-Level Block functions)
```

1. **Hardware Monitors** directly track state, manage debouncing, and handle noise isolation in the background.
2. **User Logic Functions** are triggered only when clean, confirmed events occur.
3. The **Main Loop** remains ultra-lean, acting strictly as a scheduler to keep timing-critical systems highly responsive.

---

## 🌟 Key Features

*   **Visual Block Editor** — drag-and-drop Sketchware-inspired interface. Create math expressions, loops, serial formatting, and manage persistent memory visually.
*   **Custom Component Creator** — Design and define complex custom hardware parts with dedicated properties, pins, simulation behaviors, and semantic roles.
*   **Automatic Pin Tracing & Routing** — Automatically maps GPIO signals and validates GND/VCC rail connectivity even when chained through complex passive networks (resistors, switches, tracks).
*   **EEPROM State Persist Engine** — High-level persistent saving and restoring blocks that handle low-level memory offset management and C++ EEPROM commitments transparently.
*   **Live Simulation Canvas** — Interactive QGraphicsView workspace displaying active motor rotations, buzzer audio ripples, glowing LEDs, and analog sensor readings in real-time.
*   **Zero Boilerplate Generation** — Generates complete, compile-ready, and human-readable Arduino/ESP32 C++ sketches instantly.
*   **Physical PCB Exporter** — Instantly export layout traces to Laser-cut PNG, Drill guide mapping, or schematic KiCad formats.

---

## 🛠️ Tech Stack

| Component | Technology |
|---|---|
| **Core Architecture** | C++17 |
| **UI & Graphics** | Qt6 (QtWidgets, QtGui, QtCore), QGraphicsScene, QGraphicsView |
| **Build System** | CMake, Ninja compiler |
| **Serialization** | Qt JSON Library (QJsonDocument, QJsonObject, QJsonArray) |

---

## 🚀 Getting Started

### Prerequisites

You need a C++ compiler supporting C++17 (e.g. GCC/MinGW) and the Qt6 SDK.
For the in-app PDF preview, install the optional **Qt6Pdf** module; without it the PDF tab shows a placeholder.

### Build and Launch (Windows)

```powershell
# Set compiler paths (e.g. MSYS2 MinGW-w64)
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH

# Configure the build files
cmake -S . -B build -G Ninja

# Compile the target executable
cmake --build build

# Launch the executable
./build/apps/ide/ide-embedded.exe
```

---

## 📦 Build Automation, Commit, and Version Restoring

To prevent any loss of progress, the IDE features an automated compilation, backup, and version-control system powered by Git.

### 1. Build, Auto-Commit, and Push
Whenever you want to build the project and securely push your changes to GitHub, run the `compilar.bat` script located in the root of the project:

```powershell
# Compiles the project and pushes an incremental backup to GitHub
.\compilar.bat
```

This script:
1. Rebuilds the IDE with maximum release optimizations (`-O3` and Link-Time Optimization).
2. Detects modifications in your codebase if the compilation is 100% successful.
3. Automatically generates an incremental version tag titled **`ID:<number>`** (e.g., `ID:227`...).
4. Pushes the commit directly to your remote repository on GitHub.

### 2. How to Restore an Older Version
If you ever want to revert or examine a previous version, simply use Git's text-matching feature:

```powershell
# Rollback the workspace state to version ID:2
git checkout ":/ID:2"
```

*   **To return to the latest code (continue developing)**:
    ```powershell
    git checkout main
    ```

---

## 📂 Project Structure

```text
IDE-embedded/
├── apps/
│   └── ide/              # Main executable entry point & UI (MainWindow, TutorialOverlay)
├── packages/
│   ├── block-engine/     # Sketchware-style visual programming engine & variables
│   ├── compiler/         # C++ Code Generator & physical PCB schematics exporter (PcbExporter)
│   ├── simulator/        # Live interactive hardware simulation framework (HardwareSimulator)
│   └── workspace/        # Component canvas, routing cables, custom components Registry
├── assets/               # Artwork, icons, and logo assets
├── examples/             # Sample components and workspace presets
├── CMakeLists.txt        # Root build orchestration
└── EVENT_LOGIC_ARCHITECTURE.md
```

<div align="center">
Made by Tiburski, Herick B.
</div>
