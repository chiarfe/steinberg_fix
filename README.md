# Winetricks for Running Steinberg Software under Wine

This repository contains the necessary winetricks to run some of Steinberg's software under Wine.

---

## ✅ Supported Software

| Software       | Status         | Notes |
|----------------|----------------|-------|
| **Dorico 6**   | ✅ Edit, engrave, print features working | ❌ Playback not working |
| **WaveLab 14** | ✅ Everything works with no issues | |
| **Cubase 15**  | ❌ Does not install nor work | |

---

## 📝 Steps to Use

1. **Set up a Wine prefix** as usual.
2. **Install the verb** (or the general fixsteinberg for all apps):
   ```bash
   winetricks https://raw.githubusercontent.com/chiarfe/steinberg_fix/main/fixsteinberg.verb
   ```
3. **Install Steinberg Download Assistant**.
4. **Perform authentication** and download your app of interest along with its dependencies.
5. **Start the app** and authenticate.

---

## 🛠️ Troubleshooting

### Dorico
- **App is stuck**:  
  As of now, enabling playback causes the app to freeze.  
  ✅ **Solution**: Disable project playback before opening any file and avoid using playback for now.

- **Dropdowns appear blank**:  
  As of now, dropdowns may appear blank after their first use.  
  ✅ **Solution**: Try to guess the position of the entries you want to press, since the click action works even if you see nothing.


### Wavelab
- No know issues.

### Cubase
- ❌ Not working at all as of now

---

# Tehnical information

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
