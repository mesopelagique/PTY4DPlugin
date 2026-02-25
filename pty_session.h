/* --------------------------------------------------------------------------------
 #
 #  pty_session.h
 #  4d-plugin-pty
 #
 #  PTY session management for macOS/Unix
 #
 # --------------------------------------------------------------------------------*/

#ifndef PTY_SESSION_H
#define PTY_SESSION_H

#include <string>
#include <sys/types.h>

class PtySession {

private:
    int m_id;
    int m_masterFd;
    int m_slaveFd;
    pid_t m_pid;
    int16_t m_cols;
    int16_t m_rows;
    bool m_running;
    int m_exitCode;
    std::string m_lastError;
    int m_interruptPipe[2];

    bool configurePty();
    void setupChildProcess();

public:
    PtySession(int id);
    ~PtySession();

    bool start(const char* shellPath, int16_t cols, int16_t rows, const char* cwd = nullptr);
    ssize_t write(const char* data, size_t len);
    std::string read(size_t maxBytes, int timeoutMs);
    bool resize(int16_t cols, int16_t rows);
    bool sendSignal(int signum);
    bool close();
    bool checkRunning();

    int id() const { return m_id; }
    pid_t pid() const { return m_pid; }
    bool isRunning() const { return m_running; }
    int exitCode() const { return m_exitCode; }
    const std::string& lastError() const { return m_lastError; }
};

#endif /* PTY_SESSION_H */
