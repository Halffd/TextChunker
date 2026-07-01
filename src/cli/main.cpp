#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <set>
#include <sstream>
#include <ctime>
#include <unistd.h>
#include <cstring>

using str = std::string;

class ClipboardManager {
private:
    bool x11_available;
    bool wayland_available;

public:
    ClipboardManager() : x11_available(false), wayland_available(false) {
        // Check for Wayland availability
        const char* wayland_display = getenv("WAYLAND_DISPLAY");
        if (wayland_display && strlen(wayland_display) > 0) {
            wayland_available = true;
        } else {
            // Check for X11 availability
            const char* x11_display = getenv("DISPLAY");
            if (x11_display && strlen(x11_display) > 0) {
                x11_available = true;
            }
        }
        std::cout << "X11 available: " << (x11_available ? "yes" : "no") << std::endl;
        std::cout << "Wayland available: " << (wayland_available ? "yes" : "no") << std::endl;
    }

    ~ClipboardManager() {
    }

    std::string getClipboard() {
        #ifdef __linux__
        if (wayland_available) {
            return getWaylandClipboard();
        } else if (x11_available) {
            // Fallback to external tools
            return getClipboardFallback();
            // return getX11Clipboard();
        }
        #endif

    }

    bool setClipboard(const std::string& text) {
        #ifdef __linux__
        if (wayland_available) {
            return setWaylandClipboard(text);
        } else if (x11_available) {
            // Fallback to external tools
            return setClipboardFallback(text);
            // return setX11Clipboard(text);
        }
        #endif

    }

private:
    #ifdef __linux__
    std::string getX11Clipboard() {
        
    }

    bool setX11Clipboard(const std::string& text) {
        return false;
    }

    std::string getWaylandClipboard() {
        const char* cmd = "timeout 2 wl-paste --primary 2>/dev/null || timeout 2 wl-paste 2>/dev/null";

        FILE* pipe = popen(cmd, "r");
        if (!pipe) return "";

        std::string result;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result += buffer;
        }
        pclose(pipe);
        return result;
    }

    bool setWaylandClipboard(const std::string& text) {
        const char* cmd_primary = "wl-copy --primary";
        const char* cmd_clipboard = "wl-copy";

        // Copy to primary selection
        FILE* pipe = popen(cmd_primary, "w");
        if (!pipe) return false;
        fwrite(text.c_str(), 1, text.size(), pipe);
        int status = pclose(pipe);
        if (status != 0) return false;

        // Copy to clipboard selection
        pipe = popen(cmd_clipboard, "w");
        if (!pipe) return false;
        fwrite(text.c_str(), 1, text.size(), pipe);
        status = pclose(pipe);
        return (status == 0);
    }

    std::string clipboard_text;
    #endif
    
    std::string getClipboardFallback() {
        const char* cmd = "timeout 5 xsel --clipboard --output 2>/dev/null";
        
        FILE* pipe = popen(cmd, "r");
        if (!pipe) return "";
        
        std::string result;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result += buffer;
        }
        pclose(pipe);
        return result;
    }
    
    bool setClipboardFallback(const std::string& text) {
        const char* cmd = "xsel --clipboard --input";
        
        FILE* pipe = popen(cmd, "w");
        if (!pipe) return false;
        
        fwrite(text.c_str(), 1, text.size(), pipe);
        int status = pclose(pipe);
        return (status == 0);
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
    std::set<std::string> used_chunks;
    std::string temp_file_path;
    ClipboardManager clipboard;
    size_t show_preview_size;  // Track how much of the text to show with S command
    
    void recalculateChunks() {
        total_chunks = (text.length() + chunk_size - 1) / chunk_size;
        if (total_chunks == 0) total_chunks = 1;
        
        if (current_chunk > total_chunks) {
            current_chunk = total_chunks;
        }
        if (current_chunk < 1) {
            current_chunk = 1;
        }
        
        updateTempFile();
    }
    
    void updateTempFile() {
        // Just update the existing temp file with current text
        if (!temp_file_path.empty()) {
            std::ofstream temp_file(temp_file_path);
            if (temp_file.is_open()) {
                temp_file << text;
                temp_file.close();
            }
        }
    }
    
    bool isChunkUsed(const std::string& chunk) {
        return used_chunks.find(chunk) != used_chunks.end();
    }
    
    void markChunkAsUsed(const std::string& chunk) {
        used_chunks.insert(chunk);
    }
    
    int findNextUnusedChunk() {
        int start_chunk = current_chunk;
        int attempts = 0;
        int max_attempts = total_chunks;
        
        while (attempts < max_attempts) {
            std::string chunk = getChunkAtPosition(current_chunk);
            if (!isChunkUsed(chunk)) {
                return current_chunk;
            }
            
            if (tail_mode ^ inverted) {
                current_chunk--;
                if (current_chunk < 1) current_chunk = total_chunks;
            } else {
                current_chunk++;
                if (current_chunk > total_chunks) current_chunk = 1;
            }
            
            attempts++;
        }
        
        current_chunk = start_chunk;
        return -1;
    }
    
    std::string getChunkAtPosition(int pos) {
        if (pos < 1 || pos > total_chunks) {
            return "";
        }
        
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
    TextChunker(bool tail, size_t size) :
        chunk_size(size), tail_mode(tail), inverted(false), current_chunk(1), show_preview_size(500) {
        // Create temp file at the start
        time_t now = time(nullptr);
        temp_file_path = "/tmp/textchunker_" + std::to_string(now) + ".txt";
    }
    
    ~TextChunker() {
        if (!temp_file_path.empty()) {
            std::cout << "Temp file preserved at: " << temp_file_path << std::endl;
        }
    }
    
    bool loadText(const std::string& filename) {
        if (filename.empty()) {
            text = clipboard.getClipboard();
            if (text.empty()) {
                std::cerr << "Error: Clipboard is empty or couldn't access clipboard" << std::endl;
                return false;
            }
        } else {
            std::ifstream file(filename);
            if (!file) {
                std::cerr << "Error: Could not open file " << filename << std::endl;
                return false;
            }
            
            text.assign((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
        }
        
        if (text.empty()) {
            std::cerr << "Error: No text loaded" << std::endl;
            return false;
        }
        
        recalculateChunks();
        if (tail_mode) current_chunk = total_chunks;
        
        return true;
    }
    
    void appendTextFromClipboard() {
        std::string clipboard_text = clipboard.getClipboard();
        if (clipboard_text.empty()) {
            std::cout << "⚠ Clipboard is empty" << std::endl;
            return;
        }
        
        text += clipboard_text;
        used_chunks.clear();
        recalculateChunks();
        std::cout << "✓ Added " << clipboard_text.length() << " characters from clipboard." << std::endl;
        std::cout << "✓ Reset usage tracking (now " << total_chunks << " total chunks)" << std::endl;
    }
    
    void replaceTextFromClipboard() {
        std::string clipboard_text = clipboard.getClipboard();
        if (clipboard_text.empty()) {
            std::cout << "⚠ Clipboard is empty" << std::endl;
            return;
        }
        
        text = clipboard_text;
        used_chunks.clear();
        current_chunk = tail_mode ? total_chunks : 1;
        recalculateChunks();
        std::cout << "✓ Replaced text with " << text.length() << " characters from clipboard." << std::endl;
        std::cout << "✓ Reset to chunk " << current_chunk << " of " << total_chunks << std::endl;
    }
    
    void clearText() {
        text.clear();
        used_chunks.clear();
        current_chunk = 1;
        total_chunks = 0;
        std::cout << "✓ Text cleared" << std::endl;
    }
    
    void showCurrentText() {
        if (text.empty()) {
            std::cout << "Text is empty" << std::endl;
            return;
        }

        std::cout << "\n--- Current Text (" << text.length() << " bytes) ---" << std::endl;

        // Show incrementally more content each time S is pressed
        size_t preview_size = std::min(show_preview_size, text.length());
        std::cout << text.substr(0, preview_size);

        if (text.length() > preview_size) {
            std::cout << "\n... (" << (text.length() - preview_size) << " more bytes) ...";
        }

        std::cout << "\n--- End of Preview ---\n" << std::endl;

        // Increment the preview size for next time, but cap it at a reasonable amount
        show_preview_size = std::min(show_preview_size * 2, std::max(text.length(), size_t(5000)));
    }
    
    std::string getCurrentChunk() {
        return getChunkAtPosition(current_chunk);
    }
    
    void copyToClipboard(bool force_current = false) {
        std::string chunk = getCurrentChunk();
        if (chunk.empty()) return;
        
        if (force_current || !isChunkUsed(chunk)) {
            clipboard.setClipboard(chunk);
            markChunkAsUsed(chunk);
            std::cout << "✓ Chunk copied to clipboard" << std::endl;
        } else {
            std::cout << "⚠ Chunk already used - finding next unused chunk..." << std::endl;
            int next_unused = findNextUnusedChunk();
            if (next_unused != -1) {
                current_chunk = next_unused;
                chunk = getCurrentChunk();
                clipboard.setClipboard(chunk);
                markChunkAsUsed(chunk);
                std::cout << "✓ Found unused chunk " << current_chunk << std::endl;
            } else {
                std::cout << "⚠ All chunks have been used" << std::endl;
            }
        }
    }
    
    void showStatus() {
        int used_count = used_chunks.size();
        std::cout << "Chunk " << current_chunk << "/" << total_chunks 
                  << " (" << text.length() << " bytes total, "
                  << chunk_size << " char chunks, "
                  << (tail_mode ? "tail" : "head") << " mode"
                  << (inverted ? ", inverted" : "") 
                  << ", " << used_count << " used)" << std::endl;
    }
    
    bool processCommand(const std::string& cmd) {
        bool manual_navigation = false;
        
        if (cmd.empty()) {
            // Enter: advance to next unused
            int next_unused = findNextUnusedChunk();
            if (next_unused != -1) {
                current_chunk = next_unused;
            } else {
                if (tail_mode ^ inverted) {
                    current_chunk = std::max(1, current_chunk - 1);
                } else {
                    current_chunk = std::min(total_chunks, current_chunk + 1);
                }
            }
        } else if (cmd == "A" || cmd == "a") {
            appendTextFromClipboard();
            return true;
        } else if (cmd == "V" || cmd == "v") {
            replaceTextFromClipboard();
            return true;
        } else if (cmd == "C" || cmd == "c") {
            clearText();
            return true;
        } else if (cmd == "S" || cmd == "s") {
            showCurrentText();
            return true;
        } else if (cmd == "R" || cmd == "r") {
            std::string chunk = getCurrentChunk();
            if (!chunk.empty()) {
                clipboard.setClipboard(chunk);
                std::cout << "✓ Chunk recopied to clipboard" << std::endl;
            }
            return true;
        } else if (cmd == "U" || cmd == "u") {
            std::cout << "Used chunks: " << used_chunks.size() 
                      << "/" << total_chunks << std::endl;
            return true;
        } else if (cmd == "reset") {
            used_chunks.clear();
            std::cout << "✓ Reset all chunks as unused" << std::endl;
            return true;
        } else if (cmd == "P" || cmd == "p") {
            manual_navigation = true;
            if (tail_mode ^ inverted) {
                current_chunk = std::min(total_chunks, current_chunk + 1);  // In tail mode, previous means going back toward start (increasing index)
            } else {
                current_chunk = std::max(1, current_chunk - 1);  // In head mode, previous means going back toward start (decreasing index)
            }
        } else if (cmd == "N" || cmd == "n") {
            manual_navigation = true;
            if (tail_mode ^ inverted) {
                current_chunk = std::max(1, current_chunk - 1);  // In tail mode, next means going toward end (decreasing index)
            } else {
                current_chunk = std::min(total_chunks, current_chunk + 1);  // In head mode, next means going toward end (increasing index)
            }
        } else if (cmd == "F" || cmd == "f") {
            manual_navigation = true;
            current_chunk = (tail_mode ^ inverted) ? total_chunks : 1;
        } else if (cmd == "L" || cmd == "l") {
            manual_navigation = true;
            current_chunk = (tail_mode ^ inverted) ? 1 : total_chunks;
        } else if (cmd == "I" || cmd == "i") {
            inverted = !inverted;
            current_chunk = total_chunks - current_chunk + 1;
            std::cout << "✓ Inverted order" << std::endl;
            return true;
        } else if (cmd[0] == '$' && cmd.length() > 1 && 
                   std::all_of(cmd.begin() + 1, cmd.end(), ::isdigit)) {
            size_t new_size = std::stoul(cmd.substr(1));
            if (new_size > 0 && new_size <= text.length()) {
                std::cout << "Changing chunk size from " << chunk_size 
                          << " to " << new_size << " characters" << std::endl;
                chunk_size = new_size;
                used_chunks.clear();
                recalculateChunks();
            } else {
                std::cout << "Invalid chunk size. Must be > 0 and <= text length (" 
                          << text.length() << ")" << std::endl;
            }
            return true;
        } else if (cmd == "q" || cmd == "Q" || cmd == "quit") {
            return false;
        } else if (std::all_of(cmd.begin(), cmd.end(), ::isdigit)) {
            manual_navigation = true;
            int target = std::stoi(cmd);
            if (target >= 1 && target <= total_chunks) {
                current_chunk = target;
            } else {
                std::cout << "Invalid chunk number. Range: 1-" << total_chunks << std::endl;
                return true;
            }
        } else {
            std::cout << "Commands:" << std::endl;
            std::cout << "  Enter=next unused, R=recopy, P=prev, N=next" << std::endl;
            std::cout << "  F=first, L=last, I=invert" << std::endl;
            std::cout << "  A=append from clipboard, V=replace from clipboard" << std::endl;
            std::cout << "  C=clear text, S=show current text" << std::endl;
            std::cout << "  U=show usage, reset=reset usage, #=goto, $#=resize" << std::endl;
            std::cout << "  Q=quit" << std::endl;
            return true;
        }
        
        // Check bounds
        if (current_chunk < 1) current_chunk = 1;
        if (current_chunk > total_chunks) current_chunk = total_chunks;
        
        // Don't auto-copy for manual navigation commands
        if (manual_navigation) {
            return true;
        }
        
        return true;
    }
    
    bool hasUnusedChunks() {
        return used_chunks.size() < static_cast<size_t>(total_chunks);
    }
    
    bool isAtFinalChunk() {
        if (tail_mode ^ inverted) {
            return current_chunk == 1;
        } else {
            return current_chunk == total_chunks;
        }
    }
    
    void run() {
        std::string input;
        bool first_iteration = true;

        int last_used_count = 0;  // Track how many chunks were used before processing Enter

        while (true) {
            // Auto-copy only on first iteration or after Enter (empty command)
            if (first_iteration || input.empty()) {
                last_used_count = used_chunks.size();  // Remember count before copying
                copyToClipboard();
                first_iteration = false;
            }

            showStatus();

            // Check if we're at the final chunk and should auto-exit
            if (isAtFinalChunk() && !hasUnusedChunks()) {
                std::cout << "✓ All chunks processed. Auto-exiting..." << std::endl;
                std::cout << "Session completed successfully!" << std::endl;
                size_t unique_used = std::min(used_chunks.size(), static_cast<size_t>(total_chunks));
                std::cout << "Used " << unique_used << "/" << total_chunks << " unique chunks" << std::endl;
                break;
            }

            // Check if all chunks are used
            if (!hasUnusedChunks()) {
                std::cout << "⚠ All chunks have been used!" << std::endl;
            }

            std::cout << "Command (Enter=next unused, R=recopy, P=prev, N=next, F=first, L=last, I=invert, A=append, V=replace, C=clear, S=show, U=usage, Q=quit): ";

            std::getline(std::cin, input);

            if (!processCommand(input)) {
                break;
            }

            // Check if text was cleared
            if (text.empty()) {
                std::cout << "Text is empty. Exiting..." << std::endl;
                break;
            }

            // After processing command, if Enter was pressed and no new chunk was used, skip navigation
            if (input.empty() && used_chunks.size() == last_used_count && !hasUnusedChunks()) {
                // User pressed Enter but no new chunk was used (all chunks already used)
                // Skip the default navigation behavior
            } else {
                // After processing command, check for auto-exit condition again
                if (isAtFinalChunk() && getCurrentChunk().empty()) {
                    std::cout << "✓ Reached end of text. Auto-exiting..." << std::endl;
                    break;
                }
            }
        }
    }
};

int main(int argc, char* argv[]) {
    bool tail_mode = false;
    size_t chunk_size = 20000;
    std::string filename;

    std::cout << "Text Chunker with Native Clipboard Support" << std::endl;
    std::cout << "==========================================" << std::endl;

    // Parse arguments
    if (argc > 1) {
        if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
            std::cout << "Usage: " << argv[0] << " [chunk_size] [tail_mode] [filename]" << std::endl;
            std::cout << "  chunk_size: size of each chunk in characters (default: 20000)" << std::endl;
            std::cout << "  tail_mode: 0 for head mode, 1 for tail mode (default: 0)" << std::endl;
            std::cout << "  filename: file to read from (default: clipboard)" << std::endl;
            std::cout << std::endl;
            std::cout << "Features:" << std::endl;
            std::cout << "  - Native X11/Wayland clipboard support" << std::endl;
            std::cout << "  - Prevents duplicate chunks" << std::endl;
            std::cout << "  - Add/Replace text from clipboard (A/V)" << std::endl;
            std::cout << "  - Clear text (C) and show preview (S)" << std::endl;
            std::cout << "  - Auto-saves to /tmp file" << std::endl;
            std::cout << "  - Auto-exits when all chunks processed" << std::endl;
            return 0;
        }

        // First argument is now chunk_size
        chunk_size = std::stoul(argv[1]);
        if (chunk_size == 0) {
            std::cerr << "Error: Chunk size must be > 0" << std::endl;
            return 1;
        }
    }

    if (argc > 2) {
        tail_mode = (std::string(argv[2]) == "1");
    }

    if (argc > 3) {
        filename = argv[3];
    }
    
    TextChunker chunker(tail_mode, chunk_size);
    
    if (!chunker.loadText(filename)) {
        return 1;
    }
    
    std::cout << "Text chunker loaded. Mode: " << (tail_mode ? "tail" : "head") 
              << ", Chunk size: " << chunk_size << " chars" << std::endl;
    std::cout << "Features: Duplicate prevention, Clipboard ops (A/V/C/S), Auto-save to /tmp" << std::endl;
    std::cout << std::endl;
    
    chunker.run();
    
    return 0;
}
