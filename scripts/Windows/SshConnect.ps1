<#
Auto-login SSH helper. Supports plain single-hop login and multi-hop jump
chains (e.g. login.icm.edu.pl -> okeanos). Passwords are auto-typed from a
local, gitignored profile file; anything that looks like an OTP/2FA prompt
stops and asks the user interactively.

Profiles: copy ssh-profiles.example.json to ssh-profiles.local.json next to
this script and fill in real host/user/password per hop. That file is
gitignored (see .gitignore) - passwords stay plaintext on disk, never in git.

Usage:
  .\SshConnect.ps1              # picks a profile from a menu
  .\SshConnect.ps1 -Profile icm-okeanos
#>

param(
    [string]$Profile,
    [string]$ProfilesPath = "$PSScriptRoot\ssh-profiles.local.json"
)

$ErrorActionPreference = 'Stop'

# ponytail: prompt matching is substring-based, not a full SSH protocol
# client. Good enough for standard OpenSSH password/keyboard-interactive
# prompts; a server with unusual prompt text needs a regex tweak below.
$PasswordPromptPattern = '(?i)password\s*:\s*$'
$OtpPromptPattern = '(?i)(verification code|one-?time|passcode|otp|authentication code|duo push|token)\D*:\s*$'

function Get-Profiles {
    if (-not (Test-Path $ProfilesPath)) {
        throw "No profiles file at '$ProfilesPath'. Copy ssh-profiles.example.json to ssh-profiles.local.json and fill in real credentials."
    }
    (Get-Content $ProfilesPath -Raw | ConvertFrom-Json).profiles
}

function Select-Profile {
    param($Profiles)
    if ($Profile) {
        $p = $Profiles | Where-Object { $_.name -eq $Profile }
        if (-not $p) { throw "Profile '$Profile' not found in $ProfilesPath" }
        return $p
    }
    Write-Host "Profiles:"
    for ($i = 0; $i -lt $Profiles.Count; $i++) { Write-Host "  [$i] $($Profiles[$i].name)" }
    $idx = Read-Host "Pick a profile number"
    return $Profiles[[int]$idx]
}

function Connect-Ssh {
    param($Hops)

    $ssh = Get-Command ssh -ErrorAction SilentlyContinue
    if (-not $ssh) { throw "ssh.exe not found on PATH. Windows OpenSSH client is required." }

    $first = $Hops[0]
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ssh.Source
    # -tt forces a remote pty even though our stdio is redirected, which is
    # what makes the server actually emit password/OTP prompts on the
    # channel instead of refusing non-interactive auth.
    $psi.Arguments = "-tt $($first.user)@$($first.host)"
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false

    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdin = $proc.StandardInput
    $stdout = $proc.StandardOutput
    $stderr = $proc.StandardError

    $hopIndex = 0
    $passwordSent = $false
    $buffer = ''

    function Send-Line([string]$text) {
        $stdin.Write($text + "`n")
        $stdin.Flush()
    }

    function Read-Available([System.IO.StreamReader]$reader) {
        $text = ''
        while ($reader.Peek() -ge 0) { $text += [char]$reader.Read() }
        return $text
    }

    Write-Host "--- connecting: $($first.user)@$($first.host) ---"

    # Auth phase: react to password/OTP prompts per hop, chain to the next
    # hop once a hop's credentials are sent. Handoff to raw passthrough
    # after the last hop's credentials go out.
    while ($hopIndex -lt $Hops.Count) {
        Start-Sleep -Milliseconds 50
        $chunk = (Read-Available $stdout) + (Read-Available $stderr)
        if ($chunk) {
            Write-Host -NoNewline $chunk
            $buffer += $chunk
        }
        if ($proc.HasExited) { break }

        $hop = $Hops[$hopIndex]

        if (-not $passwordSent -and $hop.password -and ($buffer -match $PasswordPromptPattern)) {
            Send-Line $hop.password
            $passwordSent = $true
            $buffer = ''
            continue
        }

        if ($hop.otp -and ($buffer -match $OtpPromptPattern)) {
            $code = Read-Host "OTP for $($hop.host)"
            Send-Line $code
            $buffer = ''
            # ponytail: fixed delay instead of detecting the remote shell
            # prompt (prompts vary too much to regex reliably). Bump this
            # if a hop needs more time to land before the next `ssh` fires.
            Start-Sleep -Seconds 2
            $hopIndex++
            $passwordSent = $false
            if ($hopIndex -lt $Hops.Count) {
                $next = $Hops[$hopIndex]
                Send-Line "ssh $($next.user)@$($next.host)"
                Write-Host "--- hopping to: $($next.user)@$($next.host) ---"
            }
            continue
        }

        if ($passwordSent -and -not $hop.otp) {
            Start-Sleep -Seconds 2
            $hopIndex++
            $passwordSent = $false
            $buffer = ''
            if ($hopIndex -lt $Hops.Count) {
                $next = $Hops[$hopIndex]
                Send-Line "ssh $($next.user)@$($next.host)"
                Write-Host "--- hopping to: $($next.user)@$($next.host) ---"
            }
            continue
        }
    }

    Write-Host "--- authenticated, handing off to interactive session (Ctrl+C to detach) ---"

    # Raw passthrough: forward console keys to remote stdin, remote
    # stdout/stderr to console. No real local PTY, so full-screen curses
    # apps (vim, top) may render roughly; fine for normal shell use.
    while (-not $proc.HasExited) {
        $chunk = (Read-Available $stdout) + (Read-Available $stderr)
        if ($chunk) { Write-Host -NoNewline $chunk }
        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.Key -eq 'Enter') { $stdin.Write("`n") }
            else { $stdin.Write($key.KeyChar) }
            $stdin.Flush()
        }
        Start-Sleep -Milliseconds 10
    }

    Write-Host "--- session ended (exit code $($proc.ExitCode)) ---"
}

$profiles = Get-Profiles
$chosen = Select-Profile $profiles
Connect-Ssh $chosen.hops
