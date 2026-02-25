# PTY 4D Plugin

A 4D plugin that provides interactive pseudoterminal (PTY) session management.

> [!WARNING]
> This is an experimental project. Use at your own risk.

This plugin allows you to spawn terminal sessions (like `bash`, `zsh`, or other CLI tools), send input to them, and read their output asynchronously. It correctly handles ANSI escape sequences and terminal sizing, making it ideal for interacting with complex, interactive command-line applications directly from 4D.

## Features

- **Spawn PTY Sessions**: Start interactive shell sessions with a specified window size and working directory.
- **Read & Write**: Send text directly to the process's standard input and read standard output.
- **Base64 Encoded Output**: To prevent 4D's UTF-16 conversion from corrupting binary stream data or raw ANSI formatting codes, all output from the PTY session is base64-encoded.
- **Window Resizing**: Dynamically resize the terminal rows and columns, just like a real terminal emulator.
- **Process Management**: Check if the session is running, get its exit code, and send UNIX signals (e.g., SIGINT, SIGKILL) to the underlying process.
- **Session Tracking**: Manage multiple interactive sessions concurrently.

## Commands

### `PTY Create`
Creates a new PTY session and spawns the specified command.

```4d
$sessionId := PTY Create($shellPath; $cols; $rows; $cwd)
```
- **$shellPath** (*Text*): The absolute path to the executable (e.g., `"/bin/zsh"` or `"/usr/bin/python3"`).
- **$cols** (*Longint*): Initial number of terminal columns (e.g., 80). Pass 0 for default.
- **$rows** (*Longint*): Initial number of terminal rows (e.g., 24). Pass 0 for default.
- **$cwd** (*Text*): The current working directory for the spawned process. Keep empty string `""` to use the default working directory.
- **Returns** (*Longint*): A unique session ID. Returns `0` if initialization fails.

### `PTY Write`
Writes text input to the active PTY session.

```4d
$writtenBytes := PTY Write($sessionId; $data)
```
- **$sessionId** (*Longint*): The session ID returned by `PTY Create`.
- **$data** (*Text*): The text to send to the process. Note that to simulate a user pressing "Enter", you typically need to append a line feed character (`Char(10)` or `\n`).
- **Returns** (*Longint*): The number of bytes successfully written to the session, or `-1` if the session was not found.

### `PTY Read`
Reads output from the PTY session. The output is Base64 encoded to preserve structural integrity of raw byte sequences (such as ANSI colors and partial multibyte characters).

```4d
$base64Output := PTY Read($sessionId; $maxBytes; $timeoutMs)
```
- **$sessionId** (*Longint*): The session ID.
- **$maxBytes** (*Longint*): Maximum number of bytes to read. Pass `0` to use the default `65536` bytes.
- **$timeoutMs** (*Longint*): How long to wait in milliseconds for data to become available before returning.
- **Returns** (*Text*): A Base64-encoded string representing the raw terminal output. Use `BASE64 DECODE` component or 4D command to decode the content before displaying it.

### `PTY Set window size`
Updates the terminal dimensions, sending a `SIGWINCH` signal to the underlying process.

```4d
$success := PTY Set window size($sessionId; $cols; $rows)
```
- **$sessionId** (*Longint*): The session ID.
- **$cols** (*Longint*): New number of columns.
- **$rows** (*Longint*): New number of rows.
- **Returns** (*Longint*): `1` if the resize was successful, `0` otherwise.

### `PTY Get status`
Fetches the current status of the running PTY session.

```4d
$statusObj := PTY Get status($sessionId)
```
- **$sessionId** (*Longint*): The session ID.
- **Returns** (*Object*): An object containing:
  - `pid` (*Longint*): The OS process ID of the spawned task.
  - `running` (*Boolean*): `True` if the process is currently running, `False` otherwise.
  - `exitCode` (*Longint*): The exit code of the process if it has stopped running.

### `PTY Send signal`
Sends a UNIX signal to the PTY session process.

```4d
$success := PTY Send signal($sessionId; $signalId)
```
- **$sessionId** (*Longint*): The session ID.
- **$signalId** (*Longint*): The integer UNIX signal to send (e.g., `9` for SIGKILL, `2` for SIGINT).
- **Returns** (*Longint*): `1` if the signal was successfully dispatched, `0` otherwise.

### `PTY Close`
Closes and cleans up a PTY session. This will force close the pseudo-terminal and clean up the memory.

```4d
$success := PTY Close($sessionId)
```
- **$sessionId** (*Longint*): The session ID.
- **Returns** (*Longint*): `1` if the session was closed successfully, `0` otherwise.

### `PTY List sessions`
Retrieves a collection of all currently managed active PTY sessions.

```4d
$sessions := PTY List sessions
```
- **Returns** (*Collection*): A collection of `Longint` values representing the active session IDs.

## Usage Example

```4d
// 1. Create a session running ZSH
$sessionId:=PTY Create("/bin/zsh"; 80; 24; "")

// 2. See if it's running
$status:=PTY Get status($sessionId)
If ($status.running)
    // 3. Write a command
    $cmd:="ls -la"+Char(10)
    PTY Write($sessionId; $cmd)
    
    // 4. Read the output (decode from Base64)
    $outputBase64:=PTY Read($sessionId; 4096; 100)
    // Use an appropriate Base64 decode string command here...
End if

// 5. Cleanup
PTY Close($sessionId)
```
