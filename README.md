# Winetricks for Running Steinberg Software under Wine

This repository contains the necessary winetricks to run some of Steinberg's software under Wine.  
If you find any issues in programs marked as "working", please let me know and I'll do my best to fix them.

---

## ✅ Supported Software

| Software       | Status         | Notes |
|----------------|----------------|-------|
| **Dorico 6**   | ✅ Everything works with no issues | |
| **WaveLab 14** | ✅ Everything works with no issues | |
| **Cubase 15**  | ❌ Does not install nor work | (WIP) |

---

## 📝 Steps to Use

1. **Set up a Wine prefix** as usual.
2. **Install the verb**:
   ```bash
   wget https://raw.githubusercontent.com/chiarfe/steinberg_fix/refs/heads/main/fixsteinberg.verb
   winetricks fixsteinberg.verb
   ```
3. **Install Steinberg Download Assistant** and authenticate.
4. **Perform authentication** and download your app of interest along with its dependencies.
5. **Start the app** and authenticate.

---

## 🛠️ Issues

### Dorico
- ~~App gets stuck in play view~~
- ~~Dropdowns appear blank~~
- No know issues.


### Wavelab
- No know issues.

### Cubase
- ❌ Not working at all as of now (WIP)

---

# 🛠️ Tehnical information

- **COMMON**: all verbs install some common tricks for commonly used components, such as fonts, VC++, .NET
- **DCOMP**: this is a simple stub direct composition implementation, which is required to prevent programs crashes
- **PWRSHSIP**: this fixes a security problem with execution of powershell scripts during programs installations

---

## Credits
Thanks to Zhiyi Zhang for providing dcomp stub implementation from https://gitlab.winehq.org/zhiyi/wine/-/tree/bug-23698-react-native

## License
Copyright (C) 2026 chiarfe.
Copyright (C) 1993-2026 the Wine project authors (`dcomp/`, ported from https://gitlab.winehq.org/zhiyi/wine/-/tree/bug-23698-react-native — see [dcomp/README.md](dcomp/README.md)).

This project is licensed under the GNU Lesser General Public License v2.1 (or later) — see [LICENSE](LICENSE). The `dcomp/` component is a derivative of Wine source code and remains under the same license.
