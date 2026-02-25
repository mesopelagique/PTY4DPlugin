/* --------------------------------------------------------------------------------
 #
 #  interactive_pty.cpp
 #  Interactive PTY session — type commands, see read/write results
 #
 #  Build & run:
 #    cd /Users/eric/Downloads/4d-plugin-pty/4d-plugin-pty
 #    c++ -std=c++17 -o interactive_pty interactive_pty.cpp pty_session.cpp
 #    ./interactive_pty
 #
 #  Type commands at the prompt. The program shows:
 #    - what was written (bytes sent)
 #    - what was read back (raw from the PTY)
 #    - a clean version (ANSI stripped)
 #
 #  Special commands:
 #    :quit     — exit
 #    :status   — show session status (pid, running, etc.)
 #    :resize COLSxROWS  — e.g. :resize 120x40
 #
 # --------------------------------------------------------------------------------*/

#include "pty_session.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <termios.h>

// --- ANSI stripping (same as test_pty.cpp) -----------------------------------

static std::string stripAnsi(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\x1b') {
            i++;
            if (i >= s.size()) break;
            if (s[i] == '[') {
                i++;
                while (i < s.size() && s[i] < 0x40) i++;
                if (i < s.size()) i++;
            } else if (s[i] == ']') {
                i++;
                while (i < s.size()) {
                    if (s[i] == '\x07') { i++; break; }
                    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '\\') { i += 2; break; }
                    i++;
                }
            } else if (s[i] == '(' || s[i] == ')') {
                i += 2;
            } else {
                i++;
            }
        } else if (s[i] == '\x08') {
            if (!out.empty()) out.pop_back();
            i++;
        } else if ((unsigned char)s[i] < 0x20 && s[i] != '\n' && s[i] != '\t') {
            i++;
        } else {
            out += s[i];
            i++;
        }
    }
    return out;
}

// --- colors ------------------------------------------------------------------

#define C_RESET   "\033[0m"
#define C_DIM     "\033[2m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_CYAN    "\033[36m"
#define C_RED     "\033[31m"
#define C_BOLD    "\033[1m"

// --- main --------------------------------------------------------------------

int main(int argc, char* argv[]) {
    const char* shell = "/bin/zsh";
    if (argc > 1) shell = argv[1];

    printf(C_BOLD "=== Interactive PTY ===" C_RESET "\n");
    printf("Shell: %s\n", shell);
    printf("Type commands. Special: " C_CYAN ":quit" C_RESET " "
           C_CYAN ":status" C_RESET " " C_CYAN ":resize COLSxROWS" C_RESET "\n\n");

    PtySession pty(1);
    if (!pty.start(shell, 80, 24)) {
        printf(C_RED "Failed to start PTY: %s" C_RESET "\n", pty.lastError().c_str());
        return 1;
    }

    printf(C_DIM "Session started (pid %d)" C_RESET "\n", (int)pty.pid());

    // Read initial shell output (prompt, motd, etc.)
    std::string initial = pty.read(8192, 1000);
    if (!initial.empty()) {
        printf(C_DIM "--- initial output (%zu bytes) ---" C_RESET "\n", initial.size());
        std::string clean = stripAnsi(initial);
        printf("%s\n", clean.c_str());
        printf(C_DIM "--- end initial ---" C_RESET "\n");
    }

    // Main loop
    char line[4096];
    while (true) {
        printf(C_GREEN "pty> " C_RESET);
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == nullptr) {
            printf("\n");
            break;
        }

        // Remove trailing newline from fgets
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }

        std::string input(line, len);

        // --- special commands ---
        if (input == ":quit" || input == ":q") {
            break;
        }

        if (input == ":status" || input == ":s") {
            pty.checkRunning();
            printf(C_CYAN "  pid: %d  running: %s  exitCode: %d" C_RESET "\n",
                   (int)pty.pid(),
                   pty.isRunning() ? "yes" : "no",
                   pty.exitCode());
            continue;
        }

        if (input.rfind(":resize ", 0) == 0) {
            int cols = 0, rows = 0;
            if (sscanf(input.c_str() + 8, "%dx%d", &cols, &rows) == 2 && cols > 0 && rows > 0) {
                bool ok = pty.resize((int16_t)cols, (int16_t)rows);
                printf(C_CYAN "  resize %dx%d: %s" C_RESET "\n", cols, rows, ok ? "ok" : "failed");
            } else {
                printf(C_RED "  usage: :resize COLSxROWS  (e.g. :resize 120x40)" C_RESET "\n");
            }
            continue;
        }

        if (!pty.isRunning()) {
            printf(C_RED "  session is not running" C_RESET "\n");
            break;
        }

        // --- write command to PTY ---
        std::string cmd = input + "\n";
        ssize_t written = pty.write(cmd.c_str(), cmd.size());

        printf(C_YELLOW "  → wrote %zd bytes" C_RESET "\n", written);

        // --- read response ---
        std::string output = pty.read(65536, 1000);

        if (output.empty()) {
            printf(C_DIM "  ← (no output)" C_RESET "\n");
        } else {
            // Show raw (let terminal interpret ANSI)
            printf(C_DIM "  ← read %zu bytes" C_RESET "\n", output.size());
            printf(C_DIM "  --- raw ---" C_RESET "\n");
            fwrite(output.c_str(), 1, output.size(), stdout);
            printf("\n" C_DIM "  --- clean ---" C_RESET "\n");
            std::string clean = stripAnsi(output);
            printf("%s\n", clean.c_str());
            printf(C_DIM "  --- end ---" C_RESET "\n");
        }
    }

    printf(C_DIM "Closing session..." C_RESET "\n");
    pty.close();
    printf("Bye.\n");

    return 0;
}
