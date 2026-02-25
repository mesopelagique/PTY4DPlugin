/* --------------------------------------------------------------------------------
 #
 #  test_pty.cpp
 #  Standalone test for PtySession
 #
 #  Build & run:
 #    cd /Users/eric/Downloads/4d-plugin-pty/4d-plugin-pty
 #    c++ -std=c++17 -o test_pty test_pty.cpp pty_session.cpp && ./test_pty
 #
 # --------------------------------------------------------------------------------*/

#include "pty_session.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <signal.h>

// ---- helpers ----------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* label) {
    if (cond) {
        printf("  \033[32mPASS\033[0m  %s\n", label);
        g_pass++;
    } else {
        printf("  \033[31mFAIL\033[0m  %s\n", label);
        g_fail++;
    }
}

// Strip ANSI escape sequences (CSI, OSC, etc.) and control chars for clean output
static std::string stripAnsi(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\x1b') {
            i++;
            if (i >= s.size()) break;
            if (s[i] == '[') {
                // CSI: ESC [ ... final-byte (0x40-0x7E)
                i++;
                while (i < s.size() && s[i] < 0x40) i++;
                if (i < s.size()) i++;
            } else if (s[i] == ']') {
                // OSC: ESC ] ... terminated by BEL (\x07) or ST (ESC \)
                i++;
                while (i < s.size()) {
                    if (s[i] == '\x07') { i++; break; }
                    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '\\') { i += 2; break; }
                    i++;
                }
            } else if (s[i] == '(' || s[i] == ')') {
                // Designate character set: ESC ( X  or  ESC ) X
                i += 2;
            } else {
                // Other two-byte escape (ESC =, ESC >, etc.)
                i++;
            }
        } else if (s[i] == '\x08') {
            // Backspace: erase previous character
            if (!out.empty()) out.pop_back();
            i++;
        } else if ((unsigned char)s[i] < 0x20 && s[i] != '\n' && s[i] != '\t') {
            // Skip control chars except newline and tab
            i++;
        } else {
            out += s[i];
            i++;
        }
    }
    return out;
}

static void printEscaped(const char* label, const std::string& data) {
    printf("  [%s escaped] (%zu bytes): \"", label, data.size());
    for (unsigned char c : data) {
        if (c == '\n')      printf("\\n");
        else if (c == '\r') printf("\\r");
        else if (c == '\t') printf("\\t");
        else if (c == '\\') printf("\\\\");
        else if (c == '"')  printf("\\\"");
        else if (c < 0x20 || c == 0x7f) printf("\\x%02x", c);
        else                putchar(c);
    }
    printf("\"\n");
}

static void printRaw(const char* label, const std::string& data) {
    printf("  [%s raw] (%zu bytes): \"", label, data.size());
    fwrite(data.c_str(), 1, data.size(), stdout);
    printf("\"\n");
}

static void printClean(const char* label, const std::string& data) {
    std::string clean = stripAnsi(data);
    printf("  [%s] \"", label);
    for (unsigned char c : clean) {
        if (c == '\r') continue;           // skip CR
        else if (c == '\n') printf("\n         "); // indent continuation lines
        else if (c < 0x20 || c == 0x7f) continue; // skip other control chars
        else putchar(c);
    }
    printf("\"\n");
}

// ---- tests ------------------------------------------------------------------

static void test_start_and_status() {
    printf("\n--- test_start_and_status ---\n");

    PtySession pty(1);

    bool ok = pty.start("/bin/zsh", 80, 24);
    check(ok, "start() returns true");
    check(pty.isRunning(), "isRunning() after start");
    check(pty.pid() > 0, "pid() > 0");

    // Read the initial shell prompt (give it a moment)
    std::string initial = pty.read(4096, 1000);
    printEscaped("raw", initial);
    printRaw("raw", initial);
    printClean("clean", initial);
    check(!initial.empty(), "got initial output from shell");

    pty.close();
    check(!pty.isRunning(), "not running after close");
    printf("\n");
}

static void test_echo_command() {
    printf("\n--- test_echo_command ---\n");

    PtySession pty(2);
    pty.start("/bin/zsh", 80, 24);

    // Drain the initial prompt
    pty.read(4096, 1000);

    // Write a simple echo command
    const char* cmd = "echo hello_pty_test\n";
    ssize_t written = pty.write(cmd, strlen(cmd));
    check(written == (ssize_t)strlen(cmd), "write() returned correct byte count");

    // Read response (echo + output + next prompt)
    std::string output = pty.read(4096, 2000);
    printEscaped("raw", output);
    printRaw("raw", output);
    printClean("clean", output);

    std::string clean = stripAnsi(output);
    check(clean.find("hello_pty_test") != std::string::npos,
          "output contains 'hello_pty_test'");

    pty.close();
    printf("\n");
}

static void test_ls_command() {
    printf("\n--- test_ls_command ---\n");

    PtySession pty(3);
    pty.start("/bin/zsh", 80, 24);

    // Drain initial prompt
    pty.read(4096, 1000);

    // Run ls on a known directory
    const char* cmd = "ls /tmp\n";
    pty.write(cmd, strlen(cmd));

    std::string output = pty.read(8192, 2000);
    printEscaped("raw", output);
    printRaw("raw", output);
    printClean("clean", output);

    check(!output.empty(), "ls /tmp produced output");

    pty.close();
    printf("\n");
}

static void test_multiline_output() {
    printf("\n--- test_multiline_output ---\n");

    PtySession pty(4);
    pty.start("/bin/zsh", 80, 24);
    pty.read(4096, 1000); // drain prompt

    // Command that produces exactly 5 lines
    const char* cmd = "for i in 1 2 3 4 5; do echo \"line_$i\"; done\n";
    pty.write(cmd, strlen(cmd));

    std::string output = pty.read(8192, 2000);
    printEscaped("raw", output);
    printRaw("raw", output);
    printClean("clean", output);

    std::string clean = stripAnsi(output);
    check(clean.find("line_1") != std::string::npos, "contains line_1");
    check(clean.find("line_5") != std::string::npos, "contains line_5");

    pty.close();
    printf("\n");
}

static void test_read_timeout_zero() {
    printf("\n--- test_read_timeout_zero (poll mode) ---\n");

    PtySession pty(5);
    pty.start("/bin/zsh", 80, 24);

    // Small sleep so shell has time to print prompt
    usleep(500000);

    // timeout=0 should still grab what's already buffered
    std::string output = pty.read(4096, 0);
    printEscaped("raw", output);
    printRaw("raw", output);
    printClean("clean", output);
    check(!output.empty(), "timeout=0 still reads buffered data");

    pty.close();
    printf("\n");
}

static void test_read_timeout_empty() {
    printf("\n--- test_read_timeout_empty (nothing to read) ---\n");

    PtySession pty(6);
    pty.start("/bin/zsh", 80, 24);
    pty.read(4096, 1000); // drain prompt

    // Nothing was written — read with short timeout should return empty
    std::string output = pty.read(4096, 200);
    printEscaped("raw", output);
    printRaw("raw", output);
    check(output.empty(), "read returns empty when no new data");

    pty.close();
    printf("\n");
}

static void test_write_then_sequential_reads() {
    printf("\n--- test_write_then_sequential_reads ---\n");

    PtySession pty(7);
    pty.start("/bin/zsh", 80, 24);
    pty.read(4096, 1000); // drain prompt

    // Write command
    const char* cmd = "echo FIRST && sleep 0.3 && echo SECOND\n";
    pty.write(cmd, strlen(cmd));

    // First read — should get FIRST
    std::string r1 = pty.read(4096, 500);
    printEscaped("raw 1", r1);
    printRaw("raw 1", r1);
    printClean("clean 1", r1);

    // Second read — should get SECOND
    std::string r2 = pty.read(4096, 1000);
    printEscaped("raw 2", r2);
    printRaw("raw 2", r2);
    printClean("clean 2", r2);

    std::string all = stripAnsi(r1 + r2);
    check(all.find("FIRST") != std::string::npos, "got FIRST");
    check(all.find("SECOND") != std::string::npos, "got SECOND");

    pty.close();
    printf("\n");
}

static void test_resize() {
    printf("\n--- test_resize ---\n");

    PtySession pty(8);
    pty.start("/bin/zsh", 80, 24);
    pty.read(4096, 1000); // drain

    bool ok = pty.resize(120, 40);
    check(ok, "resize() returns true");

    // Verify via tput
    const char* cmd = "echo cols=$(tput cols) rows=$(tput lines)\n";
    pty.write(cmd, strlen(cmd));
    std::string output = pty.read(4096, 1000);
    printEscaped("raw", output);
    printRaw("raw", output);
    printClean("clean", output);

    std::string clean = stripAnsi(output);
    check(clean.find("cols=120") != std::string::npos, "cols=120");
    check(clean.find("rows=40") != std::string::npos, "rows=40");

    pty.close();
    printf("\n");
}

static void test_close_kills_child() {
    printf("\n--- test_close_kills_child ---\n");

    PtySession pty(9);
    pty.start("/bin/zsh", 80, 24);
    pid_t pid = pty.pid();
    check(pid > 0, "have a child pid");

    pty.close();

    // Give OS a moment to reap
    usleep(200000);

    // kill(pid, 0) should fail if process is gone
    int rc = kill(pid, 0);
    check(rc != 0, "child process no longer exists after close");

    printf("\n");
}

static void test_bad_shell_path() {
    printf("\n--- test_bad_shell_path ---\n");

    PtySession pty(10);

    // The fork + exec still succeeds at fork level but child exits 127
    bool ok = pty.start("/nonexistent/shell", 80, 24);

    if (ok) {
        // Child was forked but exec failed — it exited 127
        usleep(200000);
        pty.checkRunning();
        check(!pty.isRunning(), "not running after bad exec");
        check(pty.exitCode() == 127, "exit code 127 for bad exec");
    } else {
        check(true, "start() returned false for bad shell (acceptable)");
    }

    pty.close();
    printf("\n");
}

// ---- main -------------------------------------------------------------------

int main() {
    printf("=== PtySession standalone tests ===\n");

    test_start_and_status();
    test_echo_command();
    test_ls_command();
    test_multiline_output();
    test_read_timeout_zero();
    test_read_timeout_empty();
    test_write_then_sequential_reads();
    test_resize();
    test_close_kills_child();
    test_bad_shell_path();

    printf("===================================\n");
    if (g_fail == 0)
        printf("\033[32mResults: %d passed, %d failed\033[0m\n", g_pass, g_fail);
    else
        printf("\033[31mResults: %d passed, %d failed\033[0m\n", g_pass, g_fail);
    printf("===================================\n");

    return g_fail > 0 ? 1 : 0;
}
