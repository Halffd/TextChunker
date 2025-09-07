#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <ctime>

#include <QApplication>
#include <QClipboard>
#include <future>
#include <chrono>

using str = std::string;

class ClipboardManager {
private:
    QClipboard* clipboard;

public:
    ClipboardManager(QClipboard* cb) : clipboard(cb) {}

    std::string getClipboard(int timeout_ms = 800) {
        // Run QClipboard::text() in a separate thread
        auto fut = std::async(std::launch::async, [this]() {
            return clipboard->text().toStdString();
        });

        if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::ready) {
            return fut.get();
        } else {
            std::cerr << "⚠ Clipboard read timed out (Chrome/Electron app locked it)" << std::endl;
            return ""; // gracefully fail instead of freezing
        }
    }

    bool setClipboard(const std::string& text) {
        clipboard->setText(QString::fromStdString(text));
        return true;
    }
};

class TextChunker {
private:
    str text;
    size_t chunk_size;
    bool tail_mode;
    bool inverted;
    int current_chunk;
    int total_chunks;
    std::string temp_file_path;
    ClipboardManager& clipboard;

    void recalculateChunks() {
        total_chunks = (text.length() + chunk_size - 1) / chunk_size;
        if (total_chunks == 0) total_chunks = 1;

        if (current_chunk > total_chunks) current_chunk = total_chunks;
        if (current_chunk < 1) current_chunk = 1;

        updateTempFile();
    }

    void updateTempFile() {
        if (temp_file_path.empty()) {
            time_t now = time(nullptr);
            temp_file_path = "/tmp/textchunker_" + std::to_string(now) + ".txt";
        }

        std::ofstream temp_file(temp_file_path);
        if (temp_file.is_open()) {
            temp_file << text;
            temp_file.close();
            std::cout << "Text saved to: " << temp_file_path << std::endl;
        }
    }

    std::string getChunkAtPosition(int pos) {
        if (pos < 1 || pos > total_chunks) return "";

        size_t start_pos, end_pos;
        if (tail_mode ^ inverted) {
            end_pos = text.length() - (total_chunks - pos) * chunk_size;
            start_pos = (end_pos > chunk_size) ? end_pos - chunk_size : 0;
        } else {
            start_pos = (pos - 1) * chunk_size;
            end_pos = std::min(start_pos + chunk_size, text.length());
        }
        return text.substr(start_pos, end_pos - start_pos);
    }

public:
    TextChunker(bool tail, size_t size, ClipboardManager& cb)
        : chunk_size(size), tail_mode(tail), inverted(false), current_chunk(1), clipboard(cb) {}

    ~TextChunker() {
        if (!temp_file_path.empty()) {
            std::cout << "Temp file preserved at: " << temp_file_path << std::endl;
        }
    }

    bool loadText(const std::string& filename) {
        if (filename.empty()) {
            text = clipboard.getClipboard();
            if (text.empty()) {
                std::cerr << "Error: Clipboard is empty" << std::endl;
                return false;
            }
        } else {
            std::ifstream file(filename);
            if (!file) {
                std::cerr << "Error: Could not open file " << filename << std::endl;
                return false;
            }
            text.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        if (text.empty()) {
            std::cerr << "Error: No text loaded" << std::endl;
            return false;
        }

        recalculateChunks();
        if (tail_mode) current_chunk = total_chunks;
        return true;
    }

    std::string getCurrentChunk() { return getChunkAtPosition(current_chunk); }

    void copyToClipboard() {
        std::string chunk = getCurrentChunk();
        if (!chunk.empty()) {
            clipboard.setClipboard(chunk);
            std::cout << "✓ Chunk copied to clipboard" << std::endl;
        }
    }

    void showStatus() {
        std::cout << "Chunk " << current_chunk << "/" << total_chunks
                  << " (" << text.length() << " bytes total, "
                  << chunk_size << " char chunks, "
                  << (tail_mode ? "tail" : "head")
                  << (inverted ? ", inverted" : ")") << std::endl;
    }

    bool processCommand(const std::string& cmd) {
        if (cmd.empty()) {
            if (tail_mode ^ inverted)
                current_chunk = std::max(1, current_chunk - 1);
            else
                current_chunk = std::min(total_chunks, current_chunk + 1);
        } else if (cmd == "A" || cmd == "a") {
            std::cout << "Enter additional text (end with Ctrl+D or empty line):" << std::endl;
            std::string line, additional_text;
            while (std::getline(std::cin, line)) {
                if (line.empty()) break;
                additional_text += line + "\n";
            }
            if (!additional_text.empty()) {
                text += additional_text;
                recalculateChunks();
                std::cout << "Added " << additional_text.length() << " characters." << std::endl;
            }
        } else if (cmd == "R" || cmd == "r") {
            // Force recopy
            std::string chunk = getCurrentChunk();
            clipboard.setClipboard(chunk);
            std::cout << "✓ Chunk recopied to clipboard" << std::endl;
        } else if (cmd == "P" || cmd == "p") {
            if (tail_mode ^ inverted)
                current_chunk = std::min(total_chunks, current_chunk + 1);
            else
                current_chunk = std::max(1, current_chunk - 1);
        } else if (cmd == "N" || cmd == "n") {
            if (tail_mode ^ inverted)
                current_chunk = std::max(1, current_chunk - 1);
            else
                current_chunk = std::min(total_chunks, current_chunk + 1);
        } else if (cmd == "F" || cmd == "f") {
            current_chunk = (tail_mode ^ inverted) ? total_chunks : 1;
        } else if (cmd == "L" || cmd == "l") {
            current_chunk = (tail_mode ^ inverted) ? 1 : total_chunks;
        } else if (cmd == "I" || cmd == "i") {
            inverted = !inverted;
            current_chunk = total_chunks - current_chunk + 1;
        } else if (cmd[0] == '$' && cmd.length() > 1 && std::all_of(cmd.begin() + 1, cmd.end(), ::isdigit)) {
            size_t new_size = std::stoul(cmd.substr(1));
            if (new_size > 0 && new_size <= text.length()) {
                std::cout << "Changing chunk size from " << chunk_size << " to " << new_size << std::endl;
                chunk_size = new_size;
                recalculateChunks();
            } else {
                std::cout << "Invalid chunk size." << std::endl;
            }
        } else if (cmd == "q" || cmd == "Q" || cmd == "quit") {
            return false;
        } else if (std::all_of(cmd.begin(), cmd.end(), ::isdigit)) {
            int target = std::stoi(cmd);
            if (target >= 1 && target <= total_chunks)
                current_chunk = target;
            else
                std::cout << "Invalid chunk number." << std::endl;
        } else {
            std::cout << "Commands: Enter=next, R=recopy, P=prev, N=next, F=first, L=last, I=invert, A=add, Q=quit" << std::endl;
        }

        if (current_chunk < 1) current_chunk = 1;
        if (current_chunk > total_chunks) current_chunk = total_chunks;
        return true;
    }

    void run() {
        std::string input;
        while (true) {
            copyToClipboard();
            showStatus();
            QCoreApplication::processEvents();

            std::cout << "Command: ";
            std::getline(std::cin, input);
            if (!processCommand(input)) break;
        }
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    bool tail_mode = false;
    size_t chunk_size = 20000;
    std::string filename;

    std::cout << "Qt Text Chunker with Clipboard" << std::endl;

    if (argc > 1) tail_mode = (std::string(argv[1]) == "1");
    if (argc > 2) {
        chunk_size = std::stoul(argv[2]);
        if (chunk_size == 0) {
            std::cerr << "Error: Chunk size must be > 0" << std::endl;
            return 1;
        }
    }
    if (argc > 3) filename = argv[3];

    ClipboardManager cb(QApplication::clipboard());
    TextChunker chunker(tail_mode, chunk_size, cb);

    if (!chunker.loadText(filename)) return 1;

    chunker.run();
    return 0;
}
