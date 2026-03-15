# Information
This project is created as part of the 2026 CIS 48 Hour Hackathon.

# Disclaimer
This program allows the user to create macros that execute arbitrary terminal commands. The user is responsible for any damage caused by the macros.

# Compatibility
The program is mostly POSIX-compliant (except that it uses the `clear` command, which is still available on most Unix-like systems), meaning it can be used on practically any POSIX-compliant system. However, it cannot be used on Windows.

On macOS, the command-line developer tools need to be installed with the following command:
```command
xcode-select --install
```
This will install the command-line developer tools, which are required for the program's compilation on macOS.

# Usage
The program creates macros of terminal commands to automate repetitive tasks. It can open files and web pages (through the `open` command), and execute terminal commands which can delete and move files and run scripts. The commands are binded to keyboard shortcuts for easy access, which reduces the unnecessary keystrokes required to execute them. Actions outside of the terminal that cannot be done with terminal commands cannot be automated by this program, due to practical and security reasons to prevent any arbitrary action to be done.

The program also has a TUI (Text User Interface) for interacting with the program without using the terminal. The TUI can bind keyboard shortcuts to simple macros, such as opening a file/webpage or moving/removing a file, for users that are unfamiliar with the terminal. The commands the macro is binded to is POSIX-compliant, meaning it can be used on any POSIX-compliant system.

# Installation

To install the program, simply download the Google Drive folder and extract it to a location of your choice. Then, follow the compilation steps below.

<!--
TODO: Update this section **once they give us the link to the repository.**

Clone the repository:
```command
git clone https://github.com/
```
-->

# Compilation

To compile the program, navigate to the project directory and run the following command:

```command
make
```

This will compile the program and produce an executable in the project directory.

# Running
## C Program
Run the compiled executable from the project directory:

```command
./macro
```

You may move the executable to a different directory and run it there, so that the command macros are executed from a different location. You may also move the executable to a different directory and add it to your PATH for easier access.

Once launched, you will be presented with the TUI. From here you can:

- **Create a macro** — assign a terminal command to a keyboard shortcut and name.
- **List macros** — view all currently configured macros and their keybindings and names.
- **Delete a macro** — remove a macro from the list.
- **Run a macro** — press the bound keyboard shortcut or type its name to execute the associated command.
- **Clear all macros** — remove all macros from the list.

Follow the on-screen prompts to navigate the TUI. Enter a number to choose an option and press Enter to confirm a selection.

## Web Page
The web page version (with a GUI) can be viewed by opening the HTML file in a browser. This is usually done by double-clicking the file or using the browser's "Open File" dialog.

Alternatively, you can open the file in the terminal using the `open` command (on macOS) or `xdg-open` (on Linux).

Once open, you can interact with the web page using your browser's GUI.

# Known Limitations

There are several known limitations to be aware of when using this program, specifically regarding keybindings.

Keybindings are captured by reading raw byte sequences from the terminal using the POSIX `termios` API. Because the terminal rather than the OS translates keypresses into byte sequences, some key combinations are indistinguishable from each other. These limitations apply on **all POSIX systems**.

## Ctrl+Shift is indistinguishable from Ctrl

Terminals translate Ctrl+letter into a single control byte (e.g. Ctrl+A → `0x01`). The Shift key is not encoded in this byte, so Ctrl+A and Ctrl+Shift+A produce identical input. The program has no way to tell them apart and will record both as `ctrl+a`.

Some terminal emulators (e.g. xterm with `modifyOtherKeys` enabled) can encode Ctrl+Shift combinations as extended escape sequences, but this is non-standard and not supported by most terminals including macOS Terminal.app and the Linux console.

## Ctrl+Option (macOS) / Ctrl+Alt (Linux) may not be captured correctly

On macOS, holding Option produces a Unicode character (e.g. Option+P → π). The terminal sends the raw UTF-8 bytes for that character with no record of which modifier was held. Ctrl+Option is therefore either not transmitted at all, or transmitted as an unrecognisable sequence, depending on the terminal emulator.

On Linux and other POSIX systems, Alt behaves similarly: it prefixes the character with an ESC byte (`0x1B`). Ctrl+Alt combinations may produce the same byte sequence as regular Ctrl, making them indistinguishable.

## The Command key (macOS) is not supported

The Command (⌘) key is handled entirely by the macOS window server and is never forwarded to terminal applications as a raw byte sequence. It cannot be captured by any terminal program.

## Ctrl+M is indistinguishable from Enter, and other aliased pairs

The ASCII control character for a given letter is produced by subtracting 64 from its uppercase ASCII value. This means some Ctrl+key combinations happen to produce the exact same byte as a named key:

| Keybinding pressed | Byte sent | Captured as | Notes |
|---|---|---|---|
| Ctrl+M **or** Enter | `0x0D` (CR) | `enter` | Labelled `enter` rather than `ctrl+m` because the program uses `"enter"` as the sentinel to skip updating a keybinding in the edit flow. |
| Ctrl+I **or** Tab | `0x09` (HT) | `ctrl+i` | Labelled by the generic Ctrl formula rather than `tab`; no special case was added, making this inconsistent with the `enter` row above. |
| Ctrl+[ **or** Escape | `0x1B` (ESC) | `escape` | Handled in its own branch outside the Ctrl range. |
| Ctrl+J | `0x0A` (LF) | `ctrl+j` | Not aliased with Enter; Enter sends CR (`0x0D`), not LF. |

These pairs are identical at the byte level and cannot be distinguished by any terminal program, regardless of the operating system or terminal emulator. Avoid using these combinations as keybindings if the ambiguity would cause problems.

## The Poe API on the web page does not work
The Poe API on the web page does not work, because the HTML file cannot communicate with the Poe API server due to restrictions. Therefore, the AI explanation and command generation tools are not available if viewed from the browser. The AI features can only be accessed if viewed directly on the Poe website, which is not published.
