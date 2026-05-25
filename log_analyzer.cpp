#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <regex>
#include <cctype>

struct LogEntry {
    std::string datetime;
    int pid;
    int tid;
    std::string module;
    std::string level;
    std::string message;
};

int countLines(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << "\n";
        return 0;
    }
    int count = 0;
    std::string line;
    while (std::getline(file, line)) {
        count++;
    }
    return count;
}

bool startsWithDate(const std::string& line) {
    if (line.length() < 10) return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            if (line[i] != '-') return false;
        } else {
            if (!isdigit(line[i])) return false;
        }
    }
    return true;
}

int parseHex(const std::string& s) {
    int val = 0;
    for (char c : s) {
        val *= 16;
        if (c >= '0' && c <= '9') val += c - '0';
        else if (c >= 'a' && c <= 'f') val += c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val += c - 'A' + 10;
    }
    return val;
}

bool tryParseStandardLine(const std::string& line, LogEntry& entry, std::string& fullDatetime) {
    if (line.length() < 26) return false;

    std::string date = line.substr(0, 10);
    if (line[10] != ' ') return false;
    size_t timeEnd = line.find(' ', 11);
    if (timeEnd == std::string::npos) return false;
    std::string time = line.substr(11, timeEnd - 11);

    fullDatetime = date + " " + time;

    size_t pos = timeEnd + 1;
    size_t colonPos = line.find(':', pos);
    if (colonPos == std::string::npos) return false;
    std::string pidStr = line.substr(pos, colonPos - pos);
    if (pidStr.empty() || !std::all_of(pidStr.begin(), pidStr.end(), ::isdigit)) return false;
    size_t tidEnd = line.find(' ', colonPos + 1);
    if (tidEnd == std::string::npos) return false;
    std::string tidStr = line.substr(colonPos + 1, tidEnd - colonPos - 1);
    if (tidStr.empty()) return false;

    try {
        entry.pid = std::stoi(pidStr);
        entry.tid = parseHex(tidStr);
    } catch (...) {
        return false;
    }

    pos = tidEnd + 1;
    if (pos >= line.length() || line[pos] != '(') return false;
    size_t closeParen = line.find(')', pos);
    if (closeParen == std::string::npos) return false;
    entry.module = line.substr(pos + 1, closeParen - pos - 1);

    pos = closeParen + 2;
    if (pos >= line.length()) return false;

    size_t levelEnd = line.find(": ", pos);
    if (levelEnd == std::string::npos) {
        size_t altLevelEnd = line.find(':', pos);
        if (altLevelEnd == std::string::npos) {
            entry.level = "OTHER";
            entry.message = line.substr(pos);
        } else {
            std::string possibleLevel = line.substr(pos, altLevelEnd - pos);
            if (possibleLevel == "INFO" || possibleLevel == "WARNING" || possibleLevel == "ERROR") {
                entry.level = possibleLevel;
                entry.message = line.substr(altLevelEnd + 2);
            } else {
                entry.level = "OTHER";
                entry.message = line.substr(pos);
            }
        }
    } else {
        std::string possibleLevel = line.substr(pos, levelEnd - pos);
        if (possibleLevel == "INFO" || possibleLevel == "WARNING" || possibleLevel == "ERROR") {
            entry.level = possibleLevel;
        } else {
            entry.level = "OTHER";
        }
        entry.message = line.substr(levelEnd + 2);
    }

    return true;
}

bool tryParseBridgeLine(const std::string& line, LogEntry& entry, std::string& fullDatetime) {
    if (line.length() < 27) return false;

    std::string date = line.substr(0, 10);
    if (line[10] != ' ') return false;
    size_t timeEnd = line.find('+', 11);
    if (timeEnd == std::string::npos) return false;
    std::string time = line.substr(11, timeEnd - 11);
    fullDatetime = date + " " + time;

    size_t pos = timeEnd + 5;
    pos = line.find_first_not_of(' ', pos);
    if (pos == std::string::npos) return false;
    size_t pidEnd = line.find(' ', pos);
    if (pidEnd == std::string::npos) return false;

    try {
        entry.pid = std::stoi(line.substr(pos, pidEnd - pos));
    } catch (...) {
        return false;
    }

    pos = line.find_first_not_of(' ', pidEnd + 1);
    if (pos == std::string::npos) return false;
    size_t tidEnd = line.find(' ', pos);
    if (tidEnd == std::string::npos) return false;

    try {
        entry.tid = std::stoi(line.substr(pos, tidEnd - pos));
    } catch (...) {
        return false;
    }

    pos = line.find_first_not_of(' ', tidEnd + 1);
    if (pos == std::string::npos) return false;
    size_t moduleEnd = line.find(' ', pos);
    if (moduleEnd == std::string::npos) return false;
    entry.module = line.substr(pos, moduleEnd - pos);

    pos = line.find_first_not_of(' ', moduleEnd + 1);
    if (pos == std::string::npos) return false;
    size_t levelEnd = line.find(' ', pos);
    if (levelEnd == std::string::npos) return false;

    pos = levelEnd + 1;
    size_t msgStart = line.find(' ', pos);
    if (msgStart == std::string::npos) {
        entry.level = "OTHER";
        entry.message = line.substr(pos);
    } else {
        std::string levelStr = line.substr(pos, msgStart - pos);
        if (levelStr == "I:") entry.level = "INFO";
        else if (levelStr == "E:") entry.level = "ERROR";
        else if (levelStr == "W:") entry.level = "WARNING";
        else entry.level = "OTHER";
        entry.message = line.substr(msgStart + 1);
    }

    return true;
}

int parseLine(const std::string& line, LogEntry& entry) {
    static int prevPid = 0;
    static int prevTid = 0;
    static std::string prevModule;
    static std::string prevDatetime;

    if (!startsWithDate(line)) {
        return -1;
    }

    std::string fullDatetime;
    if (tryParseStandardLine(line, entry, fullDatetime)) {
        entry.datetime = fullDatetime;
        prevPid = entry.pid;
        prevTid = entry.tid;
        prevModule = entry.module;
        prevDatetime = fullDatetime;
        return 1;
    }

    if (tryParseBridgeLine(line, entry, fullDatetime)) {
        entry.datetime = fullDatetime;
        prevPid = entry.pid;
        prevTid = entry.tid;
        prevModule = entry.module;
        prevDatetime = fullDatetime;
        return 1;
    }

    return 0;
}

void parseFile(const std::string& filename, LogEntry*& entries, int& count) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << "\n";
        count = 0;
        entries = nullptr;
        return;
    }

    std::vector<LogEntry> parsedEntries;
    std::string line;

    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        LogEntry entry;
        int result = 0;
        try {
            result = parseLine(line, entry);
        } catch (const std::exception& e) {
            std::cerr << "Error on line " << lineNum << ": " << e.what() << "\n  -> " << line.substr(0, 80) << "\n";
            continue;
        }
        if (result == 1) {
            parsedEntries.push_back(entry);
        }
    }

    count = parsedEntries.size();
    entries = new LogEntry[count];
    for (int i = 0; i < count; i++) {
        entries[i] = parsedEntries[i];
    }
}

void printStatistics(const LogEntry* entries, int count) {
    if (count == 0) {
        std::cout << "No entries parsed.\n";
        return;
    }

    int infoCount = 0, warningCount = 0, errorCount = 0, otherCount = 0;
    std::map<std::string, int> moduleCount;
    std::map<int, int> pidCount;
    long long totalMsgLen = 0;
    std::string firstTimestamp = entries[0].datetime;
    std::string lastTimestamp = entries[0].datetime;

    for (int i = 0; i < count; i++) {
        const auto& e = entries[i];

        if (e.level == "INFO") infoCount++;
        else if (e.level == "WARNING") warningCount++;
        else if (e.level == "ERROR") errorCount++;
        else otherCount++;

        moduleCount[e.module]++;
        pidCount[e.pid]++;
        totalMsgLen += e.message.length();

        if (e.datetime < firstTimestamp) firstTimestamp = e.datetime;
        if (e.datetime > lastTimestamp) lastTimestamp = e.datetime;
    }

    std::cout << "\n========== BlueStacks Log Analysis ==========\n\n";

    std::cout << "1. Total lines in file: " << count << "\n\n";

    std::cout << "2. Distribution by level:\n";
    std::cout << "   INFO:    " << infoCount << " (" << std::fixed << std::setprecision(1)
              << (100.0 * infoCount / count) << "%)\n";
    std::cout << "   WARNING: " << warningCount << " (" << std::fixed << std::setprecision(1)
              << (100.0 * warningCount / count) << "%)\n";
    std::cout << "   ERROR:   " << errorCount << " (" << std::fixed << std::setprecision(1)
              << (100.0 * errorCount / count) << "%)\n";
    std::cout << "   OTHER:   " << otherCount << " (" << std::fixed << std::setprecision(1)
              << (100.0 * otherCount / count) << "%)\n\n";

    std::cout << "3. Unique modules: " << moduleCount.size() << "\n\n";

    std::cout << "4. Top-3 modules by message count:\n";
    std::vector<std::pair<std::string, int>> moduleVec(moduleCount.begin(), moduleCount.end());
    std::sort(moduleVec.begin(), moduleVec.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    for (int i = 0; i < std::min(3, (int)moduleVec.size()); i++) {
        std::cout << "   " << (i+1) << ". " << moduleVec[i].first
                  << ": " << moduleVec[i].second << "\n";
    }
    std::cout << "\n";

    std::cout << "5. Messages per PID:\n";
    for (const auto& [pid, pcount] : pidCount) {
        std::cout << "   PID " << pid << ": " << pcount << "\n";
    }
    std::cout << "\n";

    std::cout << "6. Time range:\n";
    std::cout << "   First:  " << firstTimestamp << "\n";
    std::cout << "   Last:   " << lastTimestamp << "\n\n";

    double errorRate = 100.0 * errorCount / count;
    std::cout << "7. Error rate: " << std::fixed << std::setprecision(2)
              << errorRate << "%\n\n";

    double avgMsgLen = (double)totalMsgLen / count;
    std::cout << "8. Average message length: " << std::fixed << std::setprecision(2)
              << avgMsgLen << " chars\n\n";

    std::cout << "============================================\n";
}

void freeMemory(LogEntry*& entries) {
    delete[] entries;
    entries = nullptr;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <logfile>\n";
        return 1;
    }

    std::string filename = argv[1];
    int totalLines = countLines(filename);
    LogEntry* entries = nullptr;
    int parsedCount = 0;

    std::cout << "Total lines in file: " << totalLines << "\n";

    parseFile(filename, entries, parsedCount);
    printStatistics(entries, parsedCount);
    freeMemory(entries);

    return 0;
}
