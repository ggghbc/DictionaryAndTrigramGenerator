#include <windows.h>
#include <winhttp.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <cmath>
#include <filesystem>
#include <cwctype>
#include <iomanip>

#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;

// Output folder and temporary files
const std::string OUTPUT_DIR = "output";
const std::vector<std::string> TEMP_FILES = { "raw_en_utf8.txt", "raw_ru_utf8.txt" };

// Verified remote endpoints
const std::vector<std::wstring> EN_SOURCES = {
    L"https://cdn.jsdelivr.net/gh/dwyl/english-words@master/words_alpha.txt",
    L"https://cdn.jsdelivr.net/gh/redbo/scrabble@master/dictionary.txt"
};

const std::vector<std::wstring> RU_SOURCES = {
    L"https://cdn.jsdelivr.net/gh/danakt/russian-words@master/russian.txt",
    L"https://cdn.jsdelivr.net/gh/Harrix/Russian-Nouns@main/dist/russian_nouns.txt",
    L"https://cdn.jsdelivr.net/gh/danakt/russian-words@master/russian_surnames.txt",
    L"https://cdn.jsdelivr.net/gh/danakt/russian-words@master/russian_names.txt"
};

/**
 * Downloads binary payload directly via WinHTTP, explicitly bypassing any dead local proxies.
 */
bool download_file(const std::wstring& url, std::string& out_data) {
    URL_COMPONENTS url_comp = { 0 };
    url_comp.dwStructSize = sizeof(url_comp);

    wchar_t host_name[256] = { 0 };
    wchar_t url_path[2048] = { 0 };
    url_comp.lpszHostName = host_name;
    url_comp.dwHostNameLength = ARRAYSIZE(host_name);
    url_comp.lpszUrlPath = url_path;
    url_comp.dwUrlPathLength = ARRAYSIZE(url_path);

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &url_comp)) {
        return false;
    }

    // Force direct connection bypassing any dead proxy (127.0.0.1:port)
    HINTERNET h_session = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!h_session) return false;

    HINTERNET h_connect = WinHttpConnect(h_session, host_name, url_comp.nPort, 0);
    if (!h_connect) {
        WinHttpCloseHandle(h_session);
        return false;
    }

    DWORD flags = (url_comp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET h_request = WinHttpOpenRequest(
        h_connect,
        L"GET",
        url_path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    bool success = false;
    if (h_request) {
        if (WinHttpSendRequest(h_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(h_request, NULL)) {

            DWORD status_code = 0;
            DWORD size = sizeof(status_code);
            WinHttpQueryHeaders(
                h_request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &status_code,
                &size,
                WINHTTP_NO_HEADER_INDEX
            );

            if (status_code == 200) {
                out_data.clear();
                DWORD bytes_available = 0;
                while (WinHttpQueryDataAvailable(h_request, &bytes_available) && bytes_available > 0) {
                    std::vector<char> buffer(bytes_available);
                    DWORD bytes_read = 0;
                    if (WinHttpReadData(h_request, buffer.data(), bytes_available, &bytes_read) && bytes_read > 0) {
                        out_data.append(buffer.data(), bytes_read);
                    }
                }
                success = !out_data.empty();
            }
        }
        WinHttpCloseHandle(h_request);
    }

    WinHttpCloseHandle(h_connect);
    WinHttpCloseHandle(h_session);
    return success;
}

/**
 * Counts valid alphabetical characters matching the specified language.
 */
size_t count_valid_chars(const std::wstring& wstr, const std::string& lang) {
    size_t count = 0;
    for (wchar_t ch : wstr) {
        wchar_t lower = towlower(ch);
        if (lang == "ru") {
            if ((lower >= L'а' && lower <= L'я') || lower == L'ё') {
                count++;
            }
        } else {
            if (lower >= L'a' && lower <= L'z') {
                count++;
            }
        }
    }
    return count;
}

/**
 * Autodetects encoding (UTF-8, Windows-1251, KOI8-R) and decodes bytes into UTF-16 wide string.
 */
std::wstring decode_to_wide(const std::string& bytes, const std::string& lang, std::string& out_enc) {
    if (bytes.empty()) {
        out_enc = "Empty";
        return L"";
    }

    struct EncodingCandidate {
        UINT code_page;
        DWORD flags;
        std::string name;
    };

    std::vector<EncodingCandidate> candidates = {
        { CP_UTF8, MB_ERR_INVALID_CHARS, "UTF-8" },
        { 1251, 0, "Windows-1251" },
        { 20866, 0, "KOI8-R" }
    };

    std::wstring best_wstr;
    size_t max_valid_chars = 0;
    out_enc = "Unknown";

    for (const auto& candidate : candidates) {
        int wlen = MultiByteToWideChar(candidate.code_page, candidate.flags, bytes.data(), (int)bytes.size(), NULL, 0);
        if (wlen > 0) {
            std::wstring wstr(wlen, 0);
            MultiByteToWideChar(candidate.code_page, candidate.flags, bytes.data(), (int)bytes.size(), &wstr[0], wlen);
            size_t valid_chars = count_valid_chars(wstr, lang);
            if (valid_chars > max_valid_chars) {
                max_valid_chars = valid_chars;
                best_wstr = wstr;
                out_enc = candidate.name;
            }
        }
    }

    if (max_valid_chars == 0) {
        // Fallback: UTF-8 ignoring invalid sequences
        int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), NULL, 0);
        std::wstring wstr(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &wstr[0], wlen);
        out_enc = "UTF-8 (fallback)";
        return wstr;
    }

    return best_wstr;
}

/**
 * Encodes UTF-16 wide string to UTF-8 std::string.
 */
std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], len, NULL, NULL);
    return str;
}

/**
 * Tokenizes words, filters by allowed characters, and strips acute accent marks (U+0301).
 */
std::set<std::wstring> extract_words(const std::wstring& text, const std::string& lang) {
    std::set<std::wstring> words;
    std::wstring current;

    for (wchar_t ch : text) {
        // Skip unicode combining acute accent mark
        if (ch == 0x0301) continue;

        wchar_t lower = towlower(ch);
        bool valid = false;

        if (lang == "ru") {
            valid = (lower >= L'а' && lower <= L'я') || lower == L'ё';
        } else {
            valid = (lower >= L'a' && lower <= L'z');
        }

        if (valid) {
            current.push_back(lower);
        } else {
            if (!current.empty()) {
                words.insert(current);
                current.clear();
            }
        }
    }

    if (!current.empty()) {
        words.insert(current);
    }
    return words;
}

/**
 * Reads and merges preexisting words from disk.
 */
std::set<std::wstring> load_existing_words(const std::string& filepath, const std::string& lang) {
    std::set<std::wstring> words;
    if (!fs::exists(filepath)) return words;

    std::ifstream file(filepath, std::ios::binary);
    if (!file) return words;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string enc;
    std::wstring wcontent = decode_to_wide(content, lang, enc);
    return extract_words(wcontent, lang);
}

/**
 * Calculates trigram probabilities.
 * Pattern: "  " + word + " ".
 * Formula: ln P(t) = ln(count / total_tokens).
 */
std::map<std::string, double> calculate_trigrams(const std::vector<std::wstring>& word_list) {
    std::unordered_map<std::string, uint64_t> counts;
    uint64_t total_trigrams = 0;

    for (const auto& w : word_list) {
        std::wstring padded = L"  " + w + L" ";
        if (padded.size() < 3) continue;

        for (size_t i = 0; i <= padded.size() - 3; ++i) {
            std::wstring tri_w = padded.substr(i, 3);
            std::string tri_utf8 = wide_to_utf8(tri_w);
            counts[tri_utf8]++;
            total_trigrams++;
        }
    }

    std::map<std::string, double> sorted_trigrams;
    double total_f64 = static_cast<double>(total_trigrams);

    for (const auto& pair : counts) {
        sorted_trigrams[pair.first] = std::log(static_cast<double>(pair.second) / total_f64);
    }

    return sorted_trigrams;
}

/**
 * Pipeline executor for language processing.
 */
bool process_language(const std::string& lang) {
    std::cout << "\n==================== [ " << lang << " ] ====================\n";

    std::string words_path = OUTPUT_DIR + "/words_" + lang + ".json";
    std::string trigrams_path = OUTPUT_DIR + "/trigrams_" + lang + ".json";
    std::string raw_temp_path = "raw_" + lang + "_utf8.txt";

    // 1. Load preexisting words from root or output folder
    std::set<std::wstring> words = load_existing_words("words_" + lang + ".json", lang);
    if (words.empty()) {
        words = load_existing_words(words_path, lang);
    }
    if (!words.empty()) {
        std::cout << "[" << lang << "] Loaded existing words: " << words.size() << "\n";
    }

    std::ofstream raw_out(raw_temp_path, std::ios::binary);
    const auto& sources = (lang == "ru") ? RU_SOURCES : EN_SOURCES;

    // 2. Fetch and merge all remote dictionary endpoints
    for (const auto& url : sources) {
        size_t slash_pos = url.find_last_of(L'/');
        std::wstring filename = (slash_pos != std::wstring::npos) ? url.substr(slash_pos + 1) : url;
        std::wcout << L"[" << lang.c_str() << L"] Downloading: " << filename << L"...\n";

        std::string raw_data;
        if (download_file(url, raw_data)) {
            std::string detected_enc;
            std::wstring decoded = decode_to_wide(raw_data, lang, detected_enc);
            std::set<std::wstring> extracted = extract_words(decoded, lang);

            std::cout << "  -> Size: " << std::fixed << std::setprecision(1) 
                      << (raw_data.size() / 1024.0) << " KB | Encoding: " << detected_enc << "\n";
            std::cout << "  [+] Extracted words: " << extracted.size() << "\n";
            words.insert(extracted.begin(), extracted.end());

            if (raw_out.is_open()) {
                std::string utf8_content = wide_to_utf8(decoded);
                raw_out.write(utf8_content.data(), utf8_content.size());
                raw_out << "\n";
            }
        } else {
            std::wcout << L"  [-] Failed to download: " << filename << L"\n";
        }
    }
    raw_out.close();

    if (words.empty()) {
        std::cerr << "[" << lang << "] Error: Vocabulary is empty. Skipping output generation.\n";
        return false;
    }

    std::vector<std::wstring> sorted_words(words.begin(), words.end());
    std::cout << "[" << lang << "] Total vocabulary size: " << sorted_words.size() << " words.\n";

    // 3. Write words JSON file
    std::cout << "[" << lang << "] Saving " << words_path << "...\n";
    {
        std::ofstream out(words_path, std::ios::binary);
        if (!out) return false;
        out << "[\n";
        for (size_t i = 0; i < sorted_words.size(); ++i) {
            out << "  \"" << wide_to_utf8(sorted_words[i]) << "\"";
            if (i + 1 < sorted_words.size()) out << ",";
            out << "\n";
        }
        out << "]\n";
    }

    // 4. Compute and write trigrams JSON file
    std::cout << "[" << lang << "] Calculating trigrams...\n";
    auto trigrams = calculate_trigrams(sorted_words);
    std::cout << "[" << lang << "] Generated trigrams: " << trigrams.size() << "\n";

    std::cout << "[" << lang << "] Saving " << trigrams_path << "...\n";
    {
        std::ofstream out(trigrams_path, std::ios::binary);
        if (!out) return false;
        out << std::setprecision(15);
        out << "{\n";
        size_t idx = 0;
        for (const auto& pair : trigrams) {
            out << "  \"";
            for (char c : pair.first) {
                if (c == '"') out << "\\\"";
                else if (c == '\\') out << "\\\\";
                else out << c;
            }
            out << "\": " << pair.second;
            if (++idx < trigrams.size()) out << ",";
            out << "\n";
        }
        out << "}\n";
    }

    return true;
}

int main() {
    std::cout << "=== Starting Dictionary & Trigram Generation (C++17) ===\n";

    // Ensure output directory exists
    std::error_code ec;
    fs::create_directories(OUTPUT_DIR, ec);
    if (ec) {
        std::cerr << "Failed to create directory: " << OUTPUT_DIR << "\n";
        return 1;
    }

    bool en_ok = process_language("en");
    bool ru_ok = process_language("ru");

    // Remove intermediate raw files
    std::cout << "\nCleaning up temporary files...\n";
    for (const auto& temp_file : TEMP_FILES) {
        if (fs::exists(temp_file)) {
            fs::remove(temp_file, ec);
            std::cout << "  [x] Deleted: " << temp_file << "\n";
        }
    }

    // Final file verification on disk
    std::cout << "\n==================== [ VERIFICATION ] ====================\n";
    std::vector<std::string> checks = {
        OUTPUT_DIR + "/words_en.json",
        OUTPUT_DIR + "/trigrams_en.json",
        OUTPUT_DIR + "/words_ru.json",
        OUTPUT_DIR + "/trigrams_ru.json"
    };

    bool all_exist = true;
    for (const auto& path : checks) {
        if (fs::exists(path) && fs::file_size(path) > 0) {
            double size_mb = static_cast<double>(fs::file_size(path)) / (1024.0 * 1024.0);
            std::cout << "  [OK] " << std::left << std::setw(24) << path 
                      << " : " << std::fixed << std::setprecision(2) << size_mb << " MB\n";
        } else {
            std::cout << "  [FAIL] " << std::left << std::setw(24) << path << " : MISSING/EMPTY\n";
            all_exist = false;
        }
    }

    if (all_exist && en_ok && ru_ok) {
        std::cout << "\n[SUCCESS] All 4 files generated and verified in './" << OUTPUT_DIR << "' folder.\n";
    } else {
        std::cerr << "\n[WARNING] Errors occurred during dictionary generation.\n";
    }

    return all_exist ? 0 : 1;
}