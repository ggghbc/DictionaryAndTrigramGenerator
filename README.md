# Dictionary and Trigram Generator 

A high-performance, standalone console utility written in **C++17** for Windows designed to automatically download, clean, merge comprehensive wordlists (Russian and English), and compute logarithmic probabilities of character n-grams (trigrams).

Ideal for text classification tasks, language detection, and lightweight spell-checking engines.

---

## Features

* **Zero External Dependencies:** Built entirely with native Win32/WinHTTP APIs; requires no external packages
* **Multi-Encoding Auto-Detection:** Seamlessly handles UTF-8, Windows-1251 (CP1251), and KOI8-R to prevent Cyrillic character loss from legacy sources.
* **Text Normalization:** Strips combining acute accents (`\u0301`), normalizes case, removes punctuation, and filters non-alphabetical tokens.
* **Automatic Merging:** Detects preexisting `words_*.json` files in the root or `output/` directory and merges them with fresh datasets without duplicates.
* **Workspace Cleanliness:** Saves all processed outputs to a designated `output/` directory and automatically cleans up intermediate temporary files upon completion.

---

## Output Files

After running the executable, four files will be generated in the `output/` directory:

| File | Description | Approximate Size |
| :--- | :--- | :--- |
| `words_en.json` | Array of unique, valid lowercase English words `[a-z]` | 370,000+ words (~6 MB) |
| `trigrams_en.json` | Key-value mapping of log-probability trigram weights (EN) | 10,000+ trigrams (~300 KB) |
| `words_ru.json` | Array of unique, valid lowercase Russian words `[а-я]` (inflections, surnames, nouns) | 500,000+ words (~55 MB) |
| `trigrams_ru.json` | Key-value mapping of log-probability trigram weights (RU) | 15,000+ trigrams (~500 KB) |

---

# Mathematical Model

For each trigram `t`, the probability is computed based on its frequency across the tokenized vocabulary corpus:

    ln P(t) = ln( count(t) / N )

Where:

- `N` is the total count of trigrams generated across the dataset.
- Each word is padded with boundary delimiters: two leading spaces and one trailing space (`"  " + word + " "`). This captures word-initial (`"  a"`, `"  ab"`), medial, and word-final (`"ba "`) character transitions.

---

## Prerequisites

* **Operating System:** Windows 10 / 11 (x64)
* **C++17 Compatible Compiler:**
  * MinGW-w64 (`g++` 8.0+)
  * Microsoft Visual C++ (`cl.exe` 2017+)
  * LLVM / Clang (`clang++`)

---

## Build & Run

Clone the repository and navigate to the project directory:

```powershell
git clone https://github.com/ggghbc/DictionaryAndTrigramGenerator.git
cd DictionaryAndTrigramGenerator
```
Option 1: **Just run the pre-builded binary from releases!**

Option 2: MinGW (GCC)
```powershell
g++ -O3 -std=c++17 build_dictionaries.cpp -lwinhttp -o build_dictionaries.exe
.\build_dictionaries.exe
```

Option 3: Clang
```powershell
clang++ -O3 -std=c++17 build_dictionaries.cpp -lwinhttp -o build_dictionaries.exe
.\build_dictionaries.exe
```

## Offline Mode

If network access is unavailable or restricted by an enterprise firewall:
1. Download raw wordlists via an alternative machine or browser.
2. Place them in the project root:
   - `words_ru.txt` (or `words_ru.json`)
   - `words_en.txt` (or `words_en.json`)
3. Execute the binary — it will automatically detect local sources, normalize the data, merge existing lists, and write the final output files to the output/ folder.

## License

[MIT License](./LICENSE). Free to use, modify, and distribute.
