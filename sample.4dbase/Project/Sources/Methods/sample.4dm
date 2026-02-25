//%attributes = {}
// ------------------------------------------------------------
// PTY Plugin — Usage Examples
// ------------------------------------------------------------

// ============================================================
// 1. Basic: run a command and get its output
// ============================================================

var $id : Integer
var $output : Text

$id:=PTY Create("/bin/zsh"; 80; 24; "")
If ($id#0)
	
	PTY Write($id; "echo Hello from PTY\n")
	
	// wait up to 2s for output
	$output:=PTY Read($id; 65536; 2000)
	ALERT($output)
	
	PTY Close($id)
	
End if 


// ============================================================
// 2. Run a script and collect all output
// ============================================================

var $session : Integer
var $result : Text
var $chunk : Text
var $status : Object

$session:=PTY Create("/bin/zsh"; 120; 40)
If ($session#0)
	
	// run multiple commands
	PTY Write($session; "cd /tmp\n")
	PTY Write($session; "ls -la\n")
	PTY Write($session; "pwd\n")
	
	// collect output in a loop
	$result:=""
	Repeat 
		$chunk:=PTY Read($session; 65536; 500)
		$result:=$result+$chunk
		$status:=PTY Get status($session)
	Until (($chunk="") || (Not($status.running)))
	
	// display collected output
	ALERT($result)
	
	PTY Close($session)
	
End if 


// ============================================================
// 3. Interactive session with process status check
// ============================================================

var $pty : Integer
var $line : Text
var $info : Object

$pty:=PTY Create("/bin/bash"; 80; 24)
If ($pty#0)
	
	// check status
	$info:=PTY Get status($pty)
	// $info.pid = 12345
	// $info.running = True
	// $info.exitCode = -1
	
	// run a long command followed by exit so the shell quits when done
	// without exit the shell stays alive and Not($info.running) never becomes true
	PTY Write($pty; "for i in 1 2 3; do echo \"Step $i\"; sleep 1; done; exit\n")
	
	// poll for output while running — safe because the shell will exit
	Repeat 
		$line:=PTY Read($pty; 4096; 1000)
		If ($line#"")
			// process each chunk of output
		End if 
		$info:=PTY Get status($pty)
	Until (Not($info.running))
	
	// read any remaining output
	$line:=PTY Read($pty; 65536; 500)
	
	PTY Close($pty)
	
End if 


// ============================================================
// 4. Resize terminal during session
// ============================================================

var $ok : Integer

$id:=PTY Create("/bin/zsh"; 80; 24)
If ($id#0)
	
	// start with 80x24
	PTY Write($id; "tput cols; tput lines\n")
	ALERT(PTY Read($id; 4096; 1000))  // should show 80 / 24
	
	// resize to 120x40
	$ok:=PTY Set window size($id; 120; 40)
	
	PTY Write($id; "tput cols; tput lines\n")
	ALERT(PTY Read($id; 4096; 1000))  // should show 120 / 40
	
	PTY Close($id)
	
End if 


// ============================================================
// 5. Run git commands
// ============================================================


$pty:=PTY Create("/bin/zsh"; 120; 30)
If ($pty#0)
	
	PTY Write($pty; "cd "+"/path/to/my/repo"+"\n")
	PTY Write($pty; "git status\n")
	
	$output:=PTY Read($pty; 65536; 3000)
	
	// parse git output
	If ($output="@nothing to commit@")
		// working tree clean
	End if 
	
	PTY Close($pty)
	
End if 


// ============================================================
// 6. Non-blocking read pattern (polling)
// ============================================================

var $data : Text
var $all : Text

$pty:=PTY Create("/bin/zsh"; 80; 24)
If ($pty#0)
	
	PTY Write($pty; "find / -name '*.log' 2>/dev/null\n")
	
	$all:=""
	var $attempts : Integer
	$attempts:=0
	
	Repeat 
		// non-blocking: timeout = 0
		$data:=PTY Read($pty; 8192; 0)
		If ($data#"")
			$all:=$all+$data
			$attempts:=0  // reset when we get data
		Else 
			$attempts:=$attempts+1
			DELAY PROCESS(Current process; 10)  // wait 10 ticks (~166ms)
		End if 
	Until ($attempts>20)  // stop after ~3s of no data
	
	PTY Close($pty)
	
End if 


// ============================================================
// 7. Multiple concurrent sessions
// ============================================================

var $pty1 : Integer
var $pty2 : Integer

$pty1:=PTY Create("/bin/zsh"; 80; 24)
$pty2:=PTY Create("/bin/zsh"; 80; 24)

If (($pty1#0) & ($pty2#0))
	
	// run different commands on each
	PTY Write($pty1; "uptime\n")
	PTY Write($pty2; "df -h\n")
	
	var $out1 : Text
	var $out2 : Text
	$out1:=PTY Read($pty1; 65536; 2000)
	$out2:=PTY Read($pty2; 65536; 2000)
	
	PTY Close($pty1)
	PTY Close($pty2)
	
End if 


// ============================================================
// 9. List all active sessions
// ============================================================

var $sessionId : Integer
var $sessionInfo : Object
var $summary : Text

// open a couple of sessions
$pty1:=PTY Create("/bin/zsh"; 80; 24)
$pty2:=PTY Create("/bin/zsh"; 80; 24)

// get all active session IDs
var $sessions:=PTY List sessions()
// $sessions is a Collection of Longint, e.g. [1, 2]

$summary:="Active sessions: "+String($sessions.length)+"\n"
For each ($sessionId; $sessions)
	$sessionInfo:=PTY Get status($sessionId)
	$summary:=$summary+"  id="+String($sessionId)\
		+" pid="+String($sessionInfo.pid)\
		+" running="+String($sessionInfo.running)+"\n"
End for each 

ALERT($summary)

// clean up all sessions using the list
For each ($sessionId; $sessions)
	PTY Close($sessionId)
End for each 


// ============================================================
// 10. Send signal to interrupt a running command
// ============================================================

$pty:=PTY Create("/bin/zsh"; 80; 24)
If ($pty#0)
	
	// start a long-running command
	PTY Write($pty; "sleep 60\n")
	
	// wait a moment then send SIGINT (Ctrl+C) to the entire process group
	// this interrupts sleep even though it is a child of the shell
	DELAY PROCESS(Current process; 30)  // ~500ms
	$ok:=PTY Send signal($pty; 2)  // 2 = SIGINT
	
	$output:=PTY Read($pty; 4096; 1000)
	// shell should print ^C and return to prompt
	
	PTY Close($pty)
	
End if 


// ============================================================
// 8. Parsing ANSI output
// ============================================================

$pty:=PTY Create("/bin/zsh"; 80; 24)
If ($pty#0)
	
	// Run a command that outputs ANSI colors (e.g. ls -laG on mac or ls --color)
	PTY Write($pty; "ls -laG\n")
	
	var $rawOutput : Text
	$rawOutput:=PTY Read($pty; 65536; 2000)
	
	
	
	// Or we can get HTML output to display in a Web Area
	var $htmlText : Text
	$htmlText:=$parser.toHTML($rawOutput)
	// For instance, you could load $htmlText into a Web Area component
	// ALERT("HTML text summary:\n"+Substring($htmlText; 1; 200)+"...")
	
	PTY Close($pty)
	
End if 
