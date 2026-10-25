# Zygisk-Il2CppDumper (Modernized v2.0)
Runtime IL2CPP Dumper using Zygisk. Dumps IL2CPP classes, fields, properties, and methods directly from device memory to bypass protections, encryptions, and obfuscations.

Default target package is configured for **`com.dts.freefireth`**.

---

## 🌟 Key Features
* **Zero OOM Crash / Streaming Output**: Dumps directly into `dump.cs` using buffered streaming, preventing out-of-memory crashes on massive games.
* **Dynamic Target Switching**: Change target games on the fly without recompiling or flashing again:
  ```bash
  adb shell "setprop persist.il2cppdumper.package com.your.game"
  ```
* **Full Constant & Literal Dumping**: Extracts literal values for booleans, integers, floats, doubles, and string constants.
* **Method Tokens & RVA**: Displays RVA, VA, and Token IDs for seamless import into IDA Pro / Ghidra.
* **KernelSU & APatch WebUI**: Includes a built-in interactive WebUI inside KernelSU and APatch manager to switch packages and check dumps with one tap.
* **Root Action Button**: Run `action.sh` from Magisk/KernelSU to automatically copy `dump.cs` to `/sdcard/Download/`.
* **Safe Timing / Anti-Crash**: Automatically waits up to 120s for `libil2cpp.so` and verifies IL2CPP domain initialization before dumping.

---

## 🚀 How to Use

### 1. Requirements
* A rooted device with **Magisk (v24+)**, **KernelSU**, or **APatch** (with Zygisk / ZygiskNext enabled).
* Target Unity IL2CPP game (e.g. `com.dts.freefireth`).

### 2. Installation
1. Download the module zip from GitHub Releases / Actions tab.
2. Flash the zip in your root manager (Magisk, KernelSU, or APatch) and reboot.

### 3. Dumping
1. Launch the game and wait for the splash screen and resources to load.
2. The generated dump will be saved at:
   ```text
   /data/data/com.dts.freefireth/files/dump.cs
   ```
   *(Also automatically copied to `/sdcard/Download/` if accessible, or via the Action button/WebUI).*
