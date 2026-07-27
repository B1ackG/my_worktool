# OpenSSL 1.1 for Qt 5 (Windows MinGW)

Qt 5.15 MinGW builds load OpenSSL **at runtime**. Without these DLLs next to
the executable, HTTPS calls (e.g. DeepSeek) fail with `TLS initialization failed`.

Bundled files (OpenSSL 1.1.1w, Win64):

- `libssl-1_1-x64.dll`
- `libcrypto-1_1-x64.dll`

`ModbusTCPAssistant.pro` copies them into the build `debug/` / `release/` folder
after each link on Windows.

Source: FireDaemon OpenSSL 1.1.1w package. See `LICENSE` for terms.
