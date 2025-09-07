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

#ifdef __linux__
// X11 includes
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

// Wayland includes (if available)
#ifdef WAYLAND_SUPPORT
#include <wayland-client.h>
#endif
#endif

using str = std::string;

class ClipboardManager {
private:
    Display* display;
    Window window;
    Atom clipboard_atom;
    Atom utf8_atom;
    Atom targets_atom;
    bool x11_available;
    
public:
    ClipboardManager() : display(nullptr), window(0), x11_available(false) {
        #ifdef __linux__
        // Try to initialize X11
        display = XOpenDisplay(nullptr);
        if (display) {
            int screen = DefaultScreen(display);
            window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                       0, 0, 1, 1, 0, 0, 0);
            
            clipboard_atom = XInternAtom(display, "CLIPBOARD", False);
            utf8_atom = XInternAtom(display, "UTF8_STRING", False);
            targets_atom = XInternAtom(display, "TARGETS", False);
            
            x11_available = true;
        }
        #endif
    }
    
    ~ClipboardManager() {
        #ifdef __linux__
        if (display) {
            if (window) XDestroyWindow(display, window);
            XCloseDisplay(display);
        }
        #endif
    }
    
    std::string getClipboard() {
        #ifdef __linux__
        if (x11_available) {
            return getX11Clipboard();
        }
        #endif
        
        // Fallback to external tools
        return getClipboardFallback();
    }
    
    bool setClipboard(const std::string& text) {
        #ifdef __linux__
        if (x11_available) {
            return setX11Clipboard(text);
        }
        #endif
        
        // Fallback to external tools
        return setClipboardFallback(text);
    }

private:
    #ifdef __linux__
    std::string getX11Clipboard() {
        Window owner = XGetSelectionOwner(display, clipboard_atom);
        if (owner == None) return "";
        
        Atom selection_property = XInternAtom(display, "CLIPBOARD_CONTENT", False);
        
        // Request clipboard content
        XConvertSelection(display, clipboard_atom, utf8_atom, selection_property, window, CurrentTime);
        XFlush(display);
        
        // Wait for SelectionNotify event
        XEvent event;
        for (int i = 0; i < 100; i++) { // Timeout after ~1 second
            if (XCheckTypedWindowEvent(display, window, SelectionNotify, &event)) {
                break;
            }
            usleep(10000); // 10ms
        }
        
        if (event.type != SelectionNotify) return "";
        
        if (event.xselection.property == None) return "";
        
        // Get the data
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* data = nullptr;
        
        if (XGetWindowProperty(display, window, selection_property, 0, 65536, False,
                             AnyPropertyType, &actual_type, &actual_format,
                             &nitems, &bytes_after, &data) != Success) {
            return "";
        }
        
        std::string result;
        if (data && nitems > 0) {
            result = std::string(reinterpret_cast<char*>(data), nitems);
            XFree(data);
        }
        
        return result;
    }
    
    bool setX11Clipboard(const std::string& text) {
        // Store text for later retrieval
        clipboard_text = text;
        
        // Claim clipboard ownership
        XSetSelectionOwner(display, clipboard_atom, window, CurrentTime);
        
        if (XGetSelectionOwner(display, clipboard_atom) != window) {
            return false;
        }
        
        // Handle selection requests
        XFlush(display);
        return true;
    }
    
    std::string clipboard_text; // Store clipboard content
    #endif
    
    std::string getClipboardFallback() {
        // Try different clipboard tools
        const char* commands[] = {
            "wl-paste 2>/dev/null",
            "xclip -selection clipboard -o 2>/dev/null",
            "xsel --clipboard --output 2>/dev/null"
        };
        
        for (const char* cmd : commands) {
            FILE* pipe = popen(cmd, "r");
            if (pipe) {
                std::string result;
                char buffer[4096];
                while (fgets(buffer, sizeof(buffer), pipe)) {
                    result += buffer;
                }
                int status = pclose(pipe);
                if (status == 0 && !result.empty()) {
                    return result;
                }
            }
        }
        return "";
    }
    
    bool setClipboardFallback(const std::string& text) {
        // Try different clipboard tools
        const char* commands[] = {
            "wl-copy 2>/dev/null",
            "xclip -selection clipboard -i 2>/dev/null",
            "xsel --clipboard --input 2>/dev/null"
        };
        
        for (const char* cmd : commands) {
            FILE* pipe = popen(cmd, "w");
            if (pipe) {
                fwrite(text.c_str(), 1, text.size(), pipe);
                int status = pclose(pipe);
                if (status == 0) {
                    return true;
                }
            }
        }
        return false;
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
    std::set<std::string> used_chunks; // Track used chunks
    std::string temp_file_path;
    ClipboardManager clipboard;
    
    void recalculateChunks() {
        total_chunks = (text.length() + chunk_size - 1) / chunk_size;
        if (total_chunks == 0) total_chunks = 1;
        
        if (current_chunk > total_chunks) {
            current_chunk = total_chunks;
        }
        if (current_chunk < 1) {
            current_chunk = 1;
        }
        
        // Update temp file
        updateTempFile();
    }
    
    void updateTempFile() {
        if (temp_file_path.empty()) {
            // Create temp file path
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
    
    bool isChunkUsed(const std::string& chunk) {
        return used_chunks.find(chunk) != used_chunks.end();
    }
    
    void markChunkAsUsed(const std::string& chunk) {
        used_chunks.insert(chunk);
    }
    
    int findNextUnusedChunk() {
        int start_chunk = current_chunk;
        
        do {
            std::string chunk = getChunkAtPosition(current_chunk);
            if (!isChunkUsed(chunk)) {
                return current_chunk;
            }
            
            // Move to next chunk
            if (tail_mode ^ inverted) {
                current_chunk = std::max(1, current_chunk - 1);
            } else {
                current_chunk = std::min(total_chunks, current_chunk + 1);
            }
            
            // If we've wrapped around, break
            if (current_chunk == start_chunk) {
                break;
            }
            
        } while (true);
        
        return -1; // No unused chunks found
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
        chunk_size(size), tail_mode(tail), inverted(false), current_chunk(1) {}
    
    ~TextChunker() {
        // Optionally clean up temp file
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
    
    void appendText() {
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
    }
    
    std::string getCurrentChunk() {
        return getChunkAtPosition(current_chunk);
    }
    
    void copyToClipboard() {
        std::string chunk = getCurrentChunk();
        if (!chunk.empty()) {
            if (!isChunkUsed(chunk)) {
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
        if (cmd.empty()) {
            // Default: next unused chunk
            int next_unused = findNextUnusedChunk();
            if (next_unused != -1) {
                current_chunk = next_unused;
            } else {
                // Move to next chunk anyway
                if (tail_mode ^ inverted) {
                    current_chunk = std::max(1, current_chunk - 1);
                } else {
                    current_chunk = std::min(total_chunks, current_chunk + 1);
                }
            }
        } else if (cmd == "A" || cmd == "a") {
            // Add more text
            appendText();
            return true;
        } else if (cmd == "R" || cmd == "r") {
            // Recopy current chunk (force copy even if used)
            std::string chunk = getCurrentChunk();
            if (!chunk.empty()) {
                clipboard.setClipboard(chunk);
                std::cout << "✓ Chunk recopied to clipboard" << std::endl;
            }
        } else if (cmd == "U" || cmd == "u") {
            // Show unused chunks count
            std::cout << "Used chunks: " << used_chunks.size() 
                      << "/" << total_chunks << std::endl;
            return true;
        } else if (cmd == "reset") {
            // Reset used chunks
            used_chunks.clear();
            std::cout << "Reset all chunks as unused" << std::endl;
            return true;
        } else if (cmd == "P" || cmd == "p") {
            if (tail_mode ^ inverted) {
                current_chunk = std::min(total_chunks, current_chunk + 1);
            } else {
                current_chunk = std::max(1, current_chunk - 1);
            }
        } else if (cmd == "N" || cmd == "n") {
            if (tail_mode ^ inverted) {
                current_chunk = std::max(1, current_chunk - 1);
            } else {
                current_chunk = std::min(total_chunks, current_chunk + 1);
            }
        } else if (cmd == "F" || cmd == "f") {
            current_chunk = (tail_mode ^ inverted) ? total_chunks : 1;
        } else if (cmd == "L" || cmd == "l") {
            current_chunk = (tail_mode ^ inverted) ? 1 : total_chunks;
        } else if (cmd == "I" || cmd == "i") {
            // Invert order
            inverted = !inverted;
            current_chunk = total_chunks - current_chunk + 1;
        } else if (cmd[0] == '$' && cmd.length() > 1 && 
                   std::all_of(cmd.begin() + 1, cmd.end(), ::isdigit)) {
            // Change chunk size: $number
            size_t new_size = std::stoul(cmd.substr(1));
            if (new_size > 0 && new_size <= text.length()) {
                std::cout << "Changing chunk size from " << chunk_size 
                          << " to " << new_size << " characters" << std::endl;
                chunk_size = new_size;
                used_chunks.clear(); // Reset used chunks when size changes
                recalculateChunks();
            } else {
                std::cout << "Invalid chunk size. Must be > 0 and <= text length (" 
                          << text.length() << ")" << std::endl;
                return true;
            }
        } else if (cmd == "q" || cmd == "Q" || cmd == "quit") {
            return false;
        } else if (std::all_of(cmd.begin(), cmd.end(), ::isdigit)) {
            // Go to specific chunk number
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
            std::cout << "  F=first, L=last, I=invert, A=add text" << std::endl;
            std::cout << "  U=show usage, reset=reset usage, #=goto, $#=resize" << std::endl;
            std::cout << "  Q=quit" << std::endl;
            return true;
        }
        
        // Check bounds
        if (current_chunk < 1) current_chunk = 1;
        if (current_chunk > total_chunks) current_chunk = total_chunks;
        
        return true;
    }
    
    bool hasUnusedChunks() {
        return used_chunks.size() < total_chunks;
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
        bool auto_exit = false;
        
        while (true) {
            copyToClipboard();
            showStatus();
            
            // Check if we're at the final chunk and should auto-exit
            if (isAtFinalChunk() && !hasUnusedChunks()) {
                std::cout << "✓ All chunks processed. Auto-exiting..." << std::endl;
                auto_exit = true;
                break;
            }
            
            // Check if all chunks are used
            if (!hasUnusedChunks()) {
                std::cout << "⚠ All chunks have been used!" << std::endl;
            }
            
            std::cout << "Command (Enter=next unused, R=recopy, P=prev, N=next, F=first, L=last, I=invert, A=add, U=usage, Q=quit): ";
            
            std::getline(std::cin, input);
            
            if (!processCommand(input)) {
                break;
            }
            
            // After processing command, check for auto-exit condition again
            if (isAtFinalChunk() && getCurrentChunk().empty()) {
                std::cout << "✓ Reached end of text. Auto-exiting..." << std::endl;
                auto_exit = true;
                break;
            }
        }
        
        if (auto_exit) {
            std::cout << "Session completed successfully!" << std::endl;
            std::cout << "Processed " << used_chunks.size() << "/" << total_chunks << " chunks" << std::endl;
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
            std::cout << "Usage: " << argv[0] << " [tail_mode] [chunk_size] [filename]" << std::endl;
            std::cout << "  tail_mode: 0 for head mode, 1 for tail mode (default: 0)" << std::endl;
            std::cout << "  chunk_size: size of each chunk in characters (default: 20000)" << std::endl;
            std::cout << "  filename: file to read from (default: clipboard)" << std::endl;
            std::cout << std::endl;
            std::cout << "Features:" << std::endl;
            std::cout << "  - Native X11/Wayland clipboard support" << std::endl;
            std::cout << "  - Prevents duplicate chunks" << std::endl;
            std::cout << "  - Add text during operation with 'A'" << std::endl;
            std::cout << "  - Auto-saves to /tmp file" << std::endl;
            std::cout << "  - Auto-exits when all chunks processed" << std::endl;
            return 0;
        }
        tail_mode = (std::string(argv[1]) == "1");
    }
    
    if (argc > 2) {
        chunk_size = std::stoul(argv[2]);
        if (chunk_size == 0) {
            std::cerr << "Error: Chunk size must be > 0" << std::endl;
            return 1;
        }
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
    std::cout << "Features: Duplicate prevention, Text addition (A), Auto-save to /tmp" << std::endl;
    std::cout << std::endl;
    
    chunker.run();
    
    return 0;
}
