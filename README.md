# pract2

## 📌 專案介紹

本專案為 STM32 微控制器的練習專案，使用 STM32CubeMX 進行專案設定，並透過 STM32CubeIDE 進行程式開發與除錯。

## 🛠️ 開發環境

- **MCU：** STM32G474RET6
- **開發工具：** STM32CubeIDE
- **設定工具：** STM32CubeMX
- **程式語言：** C
- **除錯工具：** STM32 Debugger

## 📂 專案結構

```text
pract2/
├── test4/
│   ├── .settings/
│   ├── Core/
│   │   ├── Inc/              # 核心程式標頭檔
│   │   └── Src/              # 核心程式原始碼
│   ├── Debug/                # 編譯與除錯相關檔案
│   ├── Drivers/              # STM32 HAL 與 CMSIS 驅動程式
│   ├── .cproject              # Eclipse C/C++ 專案設定
│   ├── .mxproject             # STM32CubeMX 專案設定
│   ├── .project               # Eclipse 專案設定
│   ├── STM32G474RETX_FLASH.ld # Flash 記憶體配置
│   ├── STM32G474RETX_RAM.ld   # RAM 記憶體配置
│   ├── test4 Debug.launch     # Debug 啟動設定
│   └── test4.ioc              # STM32CubeMX 設定檔
└── .gitignore
```

## 🚀 如何使用

1. Clone 此 GitHub Repository：

   ```bash
   git clone https://github.com/b11413131/pract2.git
   ```

2. 開啟 STM32CubeIDE。

3. 將 `test4` 資料夾匯入為既有的 STM32 專案。

4. 確認 STM32CubeIDE 已安裝對應的 STM32G4 系列支援套件。

5. 根據實際硬體接線與程式需求進行設定、編譯及除錯。

## 📁 主要檔案說明

| 檔案／資料夾 | 說明 |
|---|---|
| `Core/Inc/` | 存放標頭檔 |
| `Core/Src/` | 存放 C 語言原始碼 |
| `Drivers/` | 存放 HAL 與 CMSIS 相關驅動程式 |
| `test4.ioc` | 儲存 STM32CubeMX 的 MCU 與周邊設定 |
| `STM32G474RETX_FLASH.ld` | 定義程式在 Flash 中的記憶體配置 |
| `STM32G474RETX_RAM.ld` | 定義程式在 RAM 中的記憶體配置 |
| `Debug/` | 存放 Debug 組態與相關建置產物 |

## 📝 備註

本專案主要用於 STM32 微控制器的程式開發與實作練習。實際功能請以 `Core/Src/` 中的程式碼及 `test4.ioc` 的設定為準。
