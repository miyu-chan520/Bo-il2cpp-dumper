# Zygisk-Il2CppDumper (Dynamic Edition)

Automated runtime IL2CPP and Native ELF dumper module for **Magisk (Zygisk)**, **KernelSU (ZygiskNext)**, and **APatch (ZygiskNext)**.
Extracts metadata structures, C# dummy classes, decrypted string literals, dynamic function RVAs, field offsets, native ELF exports, constructors (`DT_INIT`/`DT_INIT_ARRAY`), and security signatures directly from memory at runtime. Includes a modern built-in WebUI for KernelSU, APatch, and MMRL.

---

## 🌟 Key Features

1. **Modern Built-in WebUI (KernelSU / APatch / MMRL)**:
   - Full-featured cyberpunk dashboard directly in the manager app.
   - Master Switch with real-time toggle between ENABLED / DISABLED.
   - Target package editor with instant chip presets (`com.dts.freefireth`, `com.dts.freefiremax`, `*`, etc.).
   - Integrated file explorer & offset inspector: browse dump folders, view files, and filter offsets live in the UI.
   - One-tap Action execution and dump cleanup.
2. **100% Dynamic Runtime Offset & RVA Extraction**:
   - Automatically resolves all class method RVAs (`Class$$Method = 0x<RVA>`) and field offsets (`Class::Field = 0x<Offset>`) in real-time.
   - **Game Update Immune**: No hardcoded addresses. When a game updates to a new version, launching the game automatically extracts the updated offsets.
3. **Advanced Native ELF & Anogs Extraction (`native_exports.txt`)**:
   - Parses `DT_HASH` / `DT_GNU_HASH` symbol tables of loaded ELF libraries (`libanogs.so`, `libil2cpp.so`, `libunity.so`, etc.).
   - Resolves ELF constructors (`DT_INIT` and `DT_INIT_ARRAY`) with relative offsets (RVA) and virtual addresses (VA).
   - Scans readable memory segments for security signatures (`ptrace`, `tracer`, `/proc/`, `frida`, `root`, `socket`, `connect`, etc.) and outputs their offsets.
   - Format:
     ```text
     ANOGS_DT_INIT = 0x<Offset>; // Offset: 0x<Offset> | VA: 0x<VA> (Constructor Entry)
     ANOGS_INIT_ARRAY_0 = 0x<Offset>; // Offset: 0x<Offset> | VA: 0x<VA> (Constructor #0)
     ANOGS_<SymbolName> = 0x<Offset>; // Offset: 0x<Offset> | VA: 0x<VA>
     ANOGS_STR_ptrace = 0x<Offset>; // Offset: 0x<Offset> | VA: 0x<VA> ("...")
     [libunity.so] <SymbolName> = 0x<Offset>; // Offset: 0x<Offset> | VA: 0x<VA>
     ```
4. **Per-Package Clean Directory Isolation (`/sdcard/Bo_dump/<package>/`)**:
   - Dumps each game into its own dedicated subfolder to avoid clutter when dumping multiple games.
   - **Auto-wipe on launch**: Every time a target game starts, any previous dump folder for that package is automatically cleaned, ensuring each session produces a 100% fresh dump.
5. **Action Button Toggle (Magisk / KernelSU / APatch)**:
   - Includes a built-in root manager Action button (`action.sh`).
   - Tap once: Turn ON dumper.
   - Tap again: Turn OFF dumper.
   - State is cleanly stored inside `/data/adb/modules/zygisk_il2cppdumper/disable`, persisting across reboots without polluting temporary directories or requiring SELinux property overrides.
6. **Zero OOM / 64KB Buffered Streaming**:
   - Flushes output in 64KB I/O chunks (`pubsetbuf`) directly to storage, preventing Out-Of-Memory crashes on massive games.

---

## 📂 Output Files (`/sdcard/Bo_dump/<package>/`)

When dumping completes, all files are saved to `/sdcard/Bo_dump/<package_name>/`:

| File | Description |
|---|---|
| `offset.txt` | Flat searchable list of all dynamically scanned method RVAs (`Class$$Method = 0x<RVA>`) and field offsets (`Class::Field = 0x<Offset>`). |
| `native_exports.txt` | Complete native dynamic symbol exports with relative offsets (RVA) and virtual addresses (VA) from `libanogs.so` (`ANOGS_...`), `libil2cpp.so`, `libunity.so`. |
| `dump.cs` | Full C# dummy header declarations with types, attributes, fields, and method signatures. |
| `script.json` | Symbol script mapping file ready for **IDA Pro** and **Ghidra** via Il2CppDumper plugins. |
| `stringliteral.json` | Decrypted metadata string literals extracted directly from memory. |

---

## 🎯 Target Package Configuration (`target.txt`)

By default, the module targets:
```text
com.dts.freefireth
```

### Adding or Changing Target Packages:
To dump other games or multiple games, edit or create:
```text
/sdcard/Bo_dump/target.txt
```
- Each package name on a new line.
- You can add multiple packages at once. Any matching package will be dumped when launched.
- Use `*` to dump **any** Unity game that starts.

#### Example `/sdcard/Bo_dump/target.txt`:
```text
# Target packages to dump (one package per line)
com.dts.freefireth
com.dts.freefiremax
com.miHoYo.GenshinImpact
com.proximabeta.nikke
```

#### Quick setup via Terminal (ADB / Termux):
```bash
echo "com.your.game" > /sdcard/Bo_dump/target.txt
```

---

## 🔘 Action Button & Toggle Switch

### Method 1: Root Manager Action Button (Recommended)
1. Open **Magisk**, **KernelSU**, or **APatch**.
2. Go to the **Modules** tab.
3. Locate **Il2CppDumper** and tap the **Action** button:
   - Tap once: Toggles **ON** (`ENABLED`).
   - Tap again: Toggles **OFF** (`DISABLED`).

### Method 2: ADB / Shell Command
You can also toggle or inspect the state via ADB or Termux:
```bash
# Check current status
adb shell su -c "[ -f /data/adb/modules/zygisk_il2cppdumper/disable ] && echo 'DISABLED' || echo 'ENABLED'"

# Enable dumper
adb shell su -c "rm -f /data/adb/modules/zygisk_il2cppdumper/disable"

# Disable dumper
adb shell su -c "touch /data/adb/modules/zygisk_il2cppdumper/disable"

# Or execute action.sh directly
adb shell su -c "/data/adb/modules/zygisk_il2cppdumper/action.sh"
```

---

## 🚀 How to Use

### 1. Requirements
- Rooted Android device with **Magisk (v24+)**, **KernelSU**, or **APatch**.
- Zygisk enabled (Magisk) or **ZygiskNext** module installed (KernelSU / APatch).

### 2. Installation
1. Download the latest `zygisk-il2cppdumper-v3.0.0-release.zip` from [Releases](https://github.com/miyu-chan520/Bo-il2cpp-dumper/releases).
2. Install the zip inside your root manager (Install from storage).
3. Reboot your device.

### 3. Running a Dump
1. Make sure your target package is in `/sdcard/Bo_dump/target.txt` (default is `com.dts.freefireth`).
2. Launch the target game.
3. Wait for the splash screen and initial loading. An on-screen Toast will notify you:
   ```text
   [Il2CppDumper] Dumping <package>, please wait...
   ```
4. Once completed, a confirmation Toast appears:
   ```text
   [Il2CppDumper] SUCCESS! Saved to /sdcard/Bo_dump/<package>/
   ```
5. All dumped files are ready in `/sdcard/Bo_dump/<package>/`.
