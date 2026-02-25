/* --------------------------------------------------------------------------------
 #
 #  pty_session.cpp
 #  4d-plugin-pty
 #
 #  PTY session management for macOS/Unix
 #
 # --------------------------------------------------------------------------------*/

#include "pty_session.h"

#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>

#if defined(__APPLE__)
#include <util.h>
#endif

PtySession::PtySession(int id)
    : m_id(id)
    , m_masterFd(-1)
    , m_slaveFd(-1)
    , m_pid(-1)
    , m_cols(80)
    , m_rows(24)
    , m_running(false)
    , m_exitCode(-1)
{
    if (pipe(m_interruptPipe) == -1) {
        m_interruptPipe[0] = -1;
        m_interruptPipe[1] = -1;
    } else {
        // Set pipe ends to non-blocking
        fcntl(m_interruptPipe[0], F_SETFL, O_NONBLOCK);
        fcntl(m_interruptPipe[1], F_SETFL, O_NONBLOCK);
    }
}

PtySession::~PtySession()
{
    close();
}

bool PtySession::configurePty()
{
    struct termios ttmode;
    int rc = tcgetattr(m_masterFd, &ttmode);
    if (rc != 0) {
        m_lastError = std::string("tcgetattr failed: ") + strerror(errno);
        return false;
    }

    ttmode.c_iflag = ICRNL | IXON | IXANY | IMAXBEL | BRKINT;
#if defined(IUTF8)
    ttmode.c_iflag |= IUTF8;
#endif

    ttmode.c_oflag = OPOST | ONLCR;
    ttmode.c_cflag = CREAD | CS8 | HUPCL;
    ttmode.c_lflag = ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL;

    ttmode.c_cc[VEOF]     = 4;      // Ctrl-D
    ttmode.c_cc[VEOL]     = 255;
    ttmode.c_cc[VEOL2]    = 255;
    ttmode.c_cc[VERASE]   = 0x7f;   // DEL
    ttmode.c_cc[VWERASE]  = 23;     // Ctrl-W
    ttmode.c_cc[VKILL]    = 21;     // Ctrl-U
    ttmode.c_cc[VREPRINT] = 18;     // Ctrl-R
    ttmode.c_cc[VINTR]    = 3;      // Ctrl-C
    ttmode.c_cc[VQUIT]    = 0x1c;   // Ctrl-backslash
    ttmode.c_cc[VSUSP]    = 26;     // Ctrl-Z
    ttmode.c_cc[VSTART]   = 17;     // Ctrl-Q
    ttmode.c_cc[VSTOP]    = 19;     // Ctrl-S
    ttmode.c_cc[VLNEXT]   = 22;     // Ctrl-V
    ttmode.c_cc[VDISCARD] = 15;     // Ctrl-O
    ttmode.c_cc[VMIN]     = 1;
    ttmode.c_cc[VTIME]    = 0;

#if defined(__APPLE__)
    ttmode.c_cc[VDSUSP]   = 25;     // Ctrl-Y
    ttmode.c_cc[VSTATUS]  = 20;     // Ctrl-T
#endif

    cfsetispeed(&ttmode, B38400);
    cfsetospeed(&ttmode, B38400);

    rc = tcsetattr(m_masterFd, TCSANOW, &ttmode);
    if (rc != 0) {
        m_lastError = std::string("tcsetattr failed: ") + strerror(errno);
        return false;
    }

    return true;
}

bool PtySession::start(const char* shellPath, int16_t cols, int16_t rows, const char* cwd)
{
    m_cols = cols;
    m_rows = rows;

    // Open master PTY
    m_masterFd = posix_openpt(O_RDWR | O_NOCTTY);
    if (m_masterFd < 0) {
        m_lastError = std::string("posix_openpt failed: ") + strerror(errno);
        return false;
    }

    // Grant and unlock slave
    if (grantpt(m_masterFd) != 0) {
        m_lastError = std::string("grantpt failed: ") + strerror(errno);
        close();
        return false;
    }

    if (unlockpt(m_masterFd) != 0) {
        m_lastError = std::string("unlockpt failed: ") + strerror(errno);
        close();
        return false;
    }

    // Get slave name
    const char* slaveName = ptsname(m_masterFd);
    if (slaveName == nullptr) {
        m_lastError = std::string("ptsname failed: ") + strerror(errno);
        close();
        return false;
    }

    // Open slave
    m_slaveFd = ::open(slaveName, O_RDWR | O_NOCTTY);
    if (m_slaveFd < 0) {
        m_lastError = std::string("open slave failed: ") + strerror(errno);
        close();
        return false;
    }

    // Set close-on-exec
    fcntl(m_masterFd, F_SETFD, FD_CLOEXEC);
    fcntl(m_slaveFd, F_SETFD, FD_CLOEXEC);

    // Configure terminal
    if (!configurePty()) {
        close();
        return false;
    }

    // Fork
    pid_t pid = fork();
    if (pid < 0) {
        m_lastError = std::string("fork failed: ") + strerror(errno);
        close();
        return false;
    }

    if (pid == 0) {
        // === Child process ===

        // Close master in child
        ::close(m_masterFd);

        // Create new session
        setsid();

        // Set controlling terminal
        ioctl(m_slaveFd, TIOCSCTTY, 0);

        // Redirect stdio to slave
        dup2(m_slaveFd, STDIN_FILENO);
        dup2(m_slaveFd, STDOUT_FILENO);
        dup2(m_slaveFd, STDERR_FILENO);

        // Close slave fd (already duped)
        if (m_slaveFd > STDERR_FILENO) {
            ::close(m_slaveFd);
        }

        // Set foreground process group
        pid_t sid = getpid();
        tcsetpgrp(STDIN_FILENO, sid);

        // Set environment
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("LANG", "en_US.UTF-8", 1);
        setenv("LC_ALL", "en_US.UTF-8", 1);
        setenv("LC_CTYPE", "UTF-8", 1);
        setenv("COMMAND_MODE", "unix2003", 1);

        // Change directory if provided
        if (cwd != nullptr && cwd[0] != '\0') {
            chdir(cwd);
        }

        // Execute shell
        execl(shellPath, shellPath, (char*)nullptr);

        // If exec fails
        _exit(127);
    }

    // === Parent process ===

    // Close slave in parent
    ::close(m_slaveFd);
    m_slaveFd = -1;

    m_pid = pid;
    m_running = true;

    // Set window size
    resize(cols, rows);

    return true;
}

ssize_t PtySession::write(const char* data, size_t len)
{
    if (m_masterFd < 0 || !m_running) {
        return -1;
    }
    return ::write(m_masterFd, data, len);
}

std::string PtySession::read(size_t maxBytes, int timeoutMs)
{
    if (m_masterFd < 0) {
        return "";
    }

    if (maxBytes > 65536) maxBytes = 65536;
    
    // Use a pre-sized string to avoid heap allocation per read and redundant appends
    std::string result;
    result.resize(maxBytes);
    size_t totalRead = 0;

    // Calculate deadline
    struct timeval deadline;
    gettimeofday(&deadline, nullptr);
    deadline.tv_sec  += timeoutMs / 1000;
    deadline.tv_usec += (timeoutMs % 1000) * 1000;
    if (deadline.tv_usec >= 1000000) {
        deadline.tv_sec  += 1;
        deadline.tv_usec -= 1000000;
    }

    bool firstIteration = true;

    while (totalRead < maxBytes) {

        struct timeval tv;
        struct timeval* pTv = &tv;

        if (firstIteration && timeoutMs < 0) {
            // First iteration: wait infinitely
            // Because the command is threadSafe: true, this fully suspends the 
            // preemptive OS worker thread without taking any CPU time gracefully.
            pTv = nullptr;
            firstIteration = false;
        } else if (firstIteration) {
            // First iteration: use the full timeout directly
            tv.tv_sec  = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            firstIteration = false;
        } else {
            // Subsequent iterations: once we have read some data, we should not block 
            // waiting for more. Only check if more is IMMEDIATELY available in the buffer.
            tv.tv_sec  = 0;
            tv.tv_usec = 0;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_masterFd, &readfds);
        
        int maxFd = m_masterFd;
        if (m_interruptPipe[0] >= 0) {
            FD_SET(m_interruptPipe[0], &readfds);
            if (m_interruptPipe[0] > maxFd) {
                maxFd = m_interruptPipe[0];
            }
        }

        int rc = select(maxFd + 1, &readfds, nullptr, nullptr, pTv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;   // real error
        }
        
        if (m_interruptPipe[0] >= 0 && FD_ISSET(m_interruptPipe[0], &readfds)) {
            // We were interrupted via the self-pipe (e.g., session closed)
            break;
        }
        
        if (rc == 0) break;  // timeout — no more data immediately available

        size_t toRead = maxBytes - totalRead;
        ssize_t n = ::read(m_masterFd, &result[totalRead], toRead);
        if (n <= 0) break;   // EOF or error

        totalRead += n;
        
        // If read() gave us less than we asked for, the kernel buffer is empty.
        // We can safely return what we have without doing another select() loop.
        if (n < toRead) {
            break;
        }
    }

    // Shrink down to actual bytes read
    result.resize(totalRead);
    return result;
}

bool PtySession::resize(int16_t cols, int16_t rows)
{
    if (m_masterFd < 0) {
        return false;
    }

    struct winsize ws;
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    if (ioctl(m_masterFd, TIOCSWINSZ, &ws) != -1) {
        m_cols = cols;
        m_rows = rows;
        return true;
    }
    return false;
}

bool PtySession::checkRunning()
{
    if (!m_running || m_pid <= 0) {
        return false;
    }

    int status;
    pid_t result = waitpid(m_pid, &status, WNOHANG);

    if (result == 0) {
        // Still running
        return true;
    }

    if (result == m_pid) {
        m_running = false;
        if (WIFEXITED(status)) {
            m_exitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            m_exitCode = -WTERMSIG(status);
        } else {
            m_exitCode = -1;
        }
    }

    return false;
}

bool PtySession::sendSignal(int signum)
{
    if (m_pid <= 0 || !m_running) {
        return false;
    }

    // Send to the process group so the signal reaches all foreground processes
    // (e.g. a subprocess spawned by the shell), not just the shell itself.
    if (::kill(-m_pid, signum) == 0) {
        return true;
    }

    // Process group may not exist yet; fall back to the shell pid directly.
    return (::kill(m_pid, signum) == 0);
}

bool PtySession::close()
{
    // Wake up any thread stuck in a blocking read
    if (m_interruptPipe[1] >= 0) {
        char dummy = 'x';
        ::write(m_interruptPipe[1], &dummy, 1);
    }

    // Close master fd
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }

    // Close slave fd
    if (m_slaveFd >= 0) {
        ::close(m_slaveFd);
        m_slaveFd = -1;
    }

    // Terminate child process
    if (m_pid > 0 && m_running) {
        // Try SIGHUP first (shell standard)
        ::kill(m_pid, SIGHUP);

        // Give it a moment
        int status;
        pid_t result = waitpid(m_pid, &status, WNOHANG);

        if (result == 0) {
            // Still running, try SIGTERM
            ::kill(m_pid, SIGTERM);
            usleep(100000); // 100ms

            result = waitpid(m_pid, &status, WNOHANG);
            if (result == 0) {
                // Force kill
                ::kill(m_pid, SIGKILL);
                waitpid(m_pid, &status, 0);
            }
        }

        if (WIFEXITED(status)) {
            m_exitCode = WEXITSTATUS(status);
        }

        m_running = false;
        m_pid = -1;
    }

    if (m_interruptPipe[0] >= 0) {
        ::close(m_interruptPipe[0]);
        m_interruptPipe[0] = -1;
    }
    if (m_interruptPipe[1] >= 0) {
        ::close(m_interruptPipe[1]);
        m_interruptPipe[1] = -1;
    }

    return true;
}
