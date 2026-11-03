# ParaView remote connection launcher

# 已连接窗口可只检查状态或更新客户端，不创建新隧道、服务或GUI。
param(
    [switch]$CollectDiagnostics,
    [string]$DiagnosticsRoot = "",
    [switch]$BridgeStatus,
    [switch]$UpdateBridge,
    [switch]$NoPauseOnError,
    [ValidateRange(1,256)][int]$ServerProcesses = 1,
    [ValidatePattern('^(/[A-Za-z0-9_./-]+)?$')][string]$RemoteParaViewPrefix = ""
)


# 打包仍在写入的文件时允许共享读取；归档失败不能掩盖原始故障。
function Export-LaunchDiagnostics {
    param([string]$Root, [string]$LaunchId, [string]$Label = 'startup')
    if (-not $Root -or -not $LaunchId) { return }
    $staging = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString('N'))
    try {
        [IO.Directory]::CreateDirectory($staging) | Out-Null
        foreach ($file in Get-ChildItem -LiteralPath $Root -File | Where-Object { $_.Name.Contains($LaunchId) -and $_.Extension -ne '.zip' }) {
            $inputStream = $null
            $outputStream = $null
            try {
                $inputStream = [IO.File]::Open($file.FullName, 'Open', 'Read', 'ReadWrite')
                $outputStream = [IO.File]::Create((Join-Path $staging $file.Name))
                $inputStream.CopyTo($outputStream)
            } catch {
                Add-Content -LiteralPath (Join-Path $staging 'collection-errors.txt') -Value "$($file.Name): $_"
            } finally {
                if ($outputStream) { $outputStream.Dispose() }
                if ($inputStream) { $inputStream.Dispose() }
            }
        }
        # 原生GUI崩溃未必进入stdout/stderr，补充Windows应用错误与WER记录。
        if ($env:OS -eq 'Windows_NT') {
            try {
                $events = Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddDays(-1); Id=1000,1001} -ErrorAction Stop |
                    Where-Object { $_.Message -match '(?i)paraview|pvpython|pvserver' } |
                    Select-Object -Property @('TimeCreated', 'Id', 'ProviderName', 'Message')
                $events | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $staging 'windows-crash-events.json') -Encoding utf8
            } catch {
                "Windows event query: $_" | Set-Content -LiteralPath (Join-Path $staging 'windows-events-query.txt')
            }
        }
        $archive = Join-Path $Root "diagnostics_${LaunchId}_${Label}.zip"
        Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $archive -Force
        Write-Host "Send this diagnostic bundle: $archive" -ForegroundColor Yellow
    } catch {
        Write-Host "Diagnostic archive failed: $_; original logs remain in $Root" -ForegroundColor Yellow
    } finally {
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# 崩溃后可随时重打包最新启动；不连接远端、不启动或关闭任何窗口。
if ($CollectDiagnostics) {
    $root = if ($DiagnosticsRoot) { $DiagnosticsRoot } else { Join-Path $PSScriptRoot 'paraview_logs' }
    $latest = Get-ChildItem -LiteralPath $root -Filter 'launcher_*.log' -File |
        Where-Object { $_.Name -match '^launcher_\d{8}_\d{6}_[a-f0-9]{32}\.log$' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw 'No launcher transcript found.' }
    $id = [IO.Path]::GetFileNameWithoutExtension($latest.Name).Split('_')[-1]
    Export-LaunchDiagnostics -Root $root -LaunchId $id -Label 'collected'
    return
}

# 启动失败必须留存错误；从资源管理器运行时避免窗口立即消失。
$ErrorActionPreference = 'Stop'
trap {
    $startupError = $_
    Write-Host "ParaView startup failed: $($startupError.Exception.Message)" -ForegroundColor Red
    $errorName = "launcher_error_$(Get-Date -Format 'yyyyMMdd_HHmmss')_$BRIDGE_LAUNCH_ID.log"
    $errorText = ($startupError | Format-List * -Force | Out-String) +
        [Environment]::NewLine + $startupError.ScriptStackTrace
    $savedError = $false
    # 桌面或 OneDrive 不可写时退回临时目录，保留最初错误。
    foreach ($errorRoot in @((Join-Path $PSScriptRoot 'paraview_logs'), [IO.Path]::GetTempPath())) {
        try {
            [IO.Directory]::CreateDirectory($errorRoot) | Out-Null
            $errorPath = Join-Path $errorRoot $errorName
            [IO.File]::WriteAllText($errorPath, $errorText, [Text.UTF8Encoding]::new($false))
            Write-Host "Failure details: $errorPath" -ForegroundColor Yellow
            $savedError = $true
            break
        } catch {
            # 日志写入失败不能覆盖原始启动错误。
        }
    }
    if (-not $savedError) { Write-Host $errorText -ForegroundColor Red }
    if ($transcriptStarted) { Stop-Transcript -ErrorAction SilentlyContinue | Out-Null; $transcriptStarted = $false }
    Export-LaunchDiagnostics -Root $transcriptRoot -LaunchId $BRIDGE_LAUNCH_ID -Label 'failure'
    if (-not $NoPauseOnError -and $Host.Name -eq 'ConsoleHost' -and
        [Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
        try { Read-Host 'Press Enter to close this failed launch' | Out-Null } catch { }
    }
    throw $startupError
}

# 在首次版本检查、SSH 或端口检查之前开启记录，finally 保证不记录调用者后续操作。
$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$BRIDGE_LAUNCH_ID = [Guid]::NewGuid().ToString('N')
$transcriptStarted = $false
$transcriptPath = $null
try {
    foreach ($transcriptRoot in @((Join-Path $scriptRoot 'paraview_logs'), [IO.Path]::GetTempPath())) {
        try {
            [IO.Directory]::CreateDirectory($transcriptRoot) | Out-Null
            $transcriptPath = Join-Path $transcriptRoot "launcher_$(Get-Date -Format 'yyyyMMdd_HHmmss')_$BRIDGE_LAUNCH_ID.log"
            Start-Transcript -LiteralPath $transcriptPath -ErrorAction Stop | Out-Null
            $transcriptStarted = $true
            break
        } catch { }
    }
    if (-not $transcriptStarted) { throw 'Cannot create launcher transcript in script or temporary directory.' }
    Write-Host "Launcher transcript: $transcriptPath" -ForegroundColor Cyan
    $collector = Join-Path $transcriptRoot 'collect_latest.cmd'
    $collectorText = "@echo off`r`npowershell.exe -NoLogo -NoProfile -File `"$PSCommandPath`" -CollectDiagnostics -DiagnosticsRoot `"$transcriptRoot`"`r`npause`r`n"
    [IO.File]::WriteAllText($collector, $collectorText, [Text.UTF8Encoding]::new($false))
# Configuration
$PARAVIEW_PATH = "D:\Software\ParaView 6.2.0\bin\paraview.exe"
$LOCAL_PORT = "11115"        # Local port
$REMOTE_PORT = "11115"       # Remote start port
$SERVER_HOST = "whshare-agent-1" # 服务固定在编译节点，进程数由ServerProcesses指定
$PARAVIEW_VERSION = "6.2.0"
$MAX_PORT_ATTEMPTS = 10      # Max port attempts
$REMOTE_USERNAME = "Liuzhaohui" # Remote SSH account
# 本机控制桥固定放在启动脚本旁，每次启动从仓库同步最新版。
$BRIDGE_SCRIPT = Join-Path $scriptRoot "paraview_bridge_client.py"
$REMOTE_HOST = "27.18.114.25"
$SSH_PORT = 6022
# 每次启动使用独立编号，自检不会把旧会话当作本次启动成功。
$BRIDGE_STARTUP_SECONDS = 180
$REMOTE_REPO_ROOT = "/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex"
$REMOTE_SERVER_SCRIPT = "$REMOTE_REPO_ROOT/.agents/skills/paraview/scripts/run_paraview_server.sh"
$REMOTE_PYTHON = "$REMOTE_REPO_ROOT/.venv/bin/python"
$REMOTE_SENDER = "$REMOTE_REPO_ROOT/.agents/skills/paraview/scripts/paraview_bridge_send.py"

# 明确的认证前限流可重试；普通断线仅允许重试已核对为只读的操作。
function Invoke-StartupNative {
    param([string]$Program, [string[]]$NativeArguments, [switch]$ReadOnly)
    for ($attempt = 0; $attempt -lt 6; $attempt++) {
        $stderrPath = [IO.Path]::GetTempFileName()
        try {
            # Windows PowerShell 5.1会把原生命令stderr包装为错误；先保留诊断再分类。
            $savedErrorPreference = $ErrorActionPreference
            try {
                $ErrorActionPreference = 'Continue'
                $output = & $Program @NativeArguments 2> $stderrPath
                $nativeExit = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $savedErrorPreference
            }
            $diagnostic = [IO.File]::ReadAllText($stderrPath)
        } finally {
            Remove-Item -LiteralPath $stderrPath -ErrorAction SilentlyContinue
        }
        if ($transcriptPath) {
            $nativeLog = "$transcriptPath.native.log"
            Add-Content -LiteralPath $nativeLog -Value ("[{0}] {1} attempt={2} exit={3}`nSTDOUT:`n{4}`nSTDERR:`n{5}" -f [DateTimeOffset]::Now.ToString('o'), $Program, ($attempt + 1), $nativeExit, ($output -join "`n"), $diagnostic)
        }
        $authenticationFailure = $diagnostic -match 'Permission denied|Host key verification failed|REMOTE HOST IDENTIFICATION HAS CHANGED'
        $preAuthBusy = $diagnostic -match 'Exceeded MaxStartups'
        $readOnlyDisconnect = $ReadOnly -and $diagnostic -match 'Connection closed|Connection reset|Connection timed out|Operation timed out'
        if ($nativeExit -eq 255 -and -not $authenticationFailure -and ($preAuthBusy -or $readOnlyDisconnect) -and $attempt -lt 5) {
            $delay = [Math]::Min(60, [Math]::Pow(2, $attempt + 2))
            Write-Host "SSH connection interrupted or entry busy; retry $($attempt + 1)/5 in $delay seconds." -ForegroundColor Yellow
            Start-Sleep -Seconds $delay
            continue
        }
        if ($diagnostic) { [Console]::Error.WriteLine($diagnostic.TrimEnd()) }
        $global:LASTEXITCODE = $nativeExit
        $output
        return
    }
}
function Invoke-StartupSsh { Invoke-StartupNative -Program 'ssh' -NativeArguments $args }
function Invoke-StartupReadOnlySsh { Invoke-StartupNative -Program 'ssh' -ReadOnly -NativeArguments $args }

if ($BridgeStatus -and $UpdateBridge) { throw "Choose either -BridgeStatus or -UpdateBridge." }
if ($BridgeStatus -or $UpdateBridge) {
    $action = if ($UpdateBridge) { "reload_client" } else { "client_status" }
    $replyOutput = Invoke-StartupSsh -T -o BatchMode=yes -o ConnectTimeout=10 -p $SSH_PORT "${REMOTE_USERNAME}@${REMOTE_HOST}" "'$REMOTE_PYTHON' '$REMOTE_SENDER' current $action --timeout 180"
    $replyExit = $LASTEXITCODE
    try { $reply = ($replyOutput -join "`n") | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "Bridge status/update failed. Check SSH and the existing ParaView window; no new connection was started." }
    $status = if ($reply.verification) { $reply.verification } else { $reply }
    Write-Host "Bridge state: $($status.status)" -ForegroundColor Cyan
    Write-Host "Connection: $($status.connection_uri)" -ForegroundColor Cyan
    if ($status.current) {
        Write-Host "Client version: $($status.current.version); repository: $($status.expected.version)"
        Write-Host "Client source SHA256: $($status.current.source_sha256)"
    }
    if ($replyExit -ne 0) { throw "Bridge operation failed: $($reply.error). Existing window and server were retained." }
    if ($status.status -eq "client_update_required") {
        if ($status.can_hot_update) {
            Write-Host "Connection is live; update the client with -UpdateBridge." -ForegroundColor Yellow
        } else {
            Write-Host "Connection is live; this legacy client needs one local reload, or use this launcher at the next normal startup." -ForegroundColor Yellow
        }
    } else {
        Write-Host "Client is current; no data reconnection was performed." -ForegroundColor Green
    }
    return
}

# 所有服务命令通过已有 SSH 入口转到编译节点；编码避免双层 Shell 引号歧义。
function Get-ServerCommand {
    param([string]$Command)
    $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Command))
    return "if hostname -s | grep -Fxq '$SERVER_HOST'; then echo $encoded | base64 -d | bash; else ssh -T -o BatchMode=yes -o ConnectTimeout=10 ${SERVER_HOST} 'echo $encoded | base64 -d | bash'; fi"
}

# Locate ParaView executable using configured path first, then common folders.
function Find-ParaViewExecutable {
    param(
        [string]$ConfiguredPath
    )

    if ($ConfiguredPath -and (Test-Path -LiteralPath $ConfiguredPath)) {
        return $ConfiguredPath
    }

    $searchPatterns = @(
        "D:\Software\ParaView $PARAVIEW_VERSION\bin\paraview.exe",
        "$env:ProgramFiles\ParaView $PARAVIEW_VERSION*\bin\paraview.exe",
        "${env:ProgramFiles(x86)}\ParaView $PARAVIEW_VERSION*\bin\paraview.exe"
    )

    $candidates = @()
    foreach ($pattern in $searchPatterns) {
        $candidates += Get-ChildItem -Path $pattern -File -ErrorAction SilentlyContinue
    }

    if ($candidates.Count -gt 0) {
        return ($candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
    }

    return $null
}

# 先确认客户端与控制桥可用，再启动远端服务，避免遗留无客户端的服务进程。
$resolvedParaViewPath = Find-ParaViewExecutable -ConfiguredPath $PARAVIEW_PATH
if (-not $resolvedParaViewPath) {
    throw "ParaView executable not found: $PARAVIEW_PATH"
}
# Windows GUI 程序可能不向管道输出 --version；优先读取文件版本资源。
$versionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($resolvedParaViewPath)
$versionPattern = '(?<![0-9])([0-9]+\.[0-9]+\.[0-9]+)(?:\.[0-9]+)?(?![0-9])'
$clientVersion = $null
foreach ($candidate in @($versionInfo.ProductVersion, $versionInfo.FileVersion)) {
    if ($candidate -match $versionPattern -and $Matches[1] -ne "0.0.0") {
        $clientVersion = $Matches[1]
        break
    }
}
if (-not $clientVersion) {
    # 无版本资源时使用同一安装目录的控制台程序，不再启动 GUI 做版本探测。
    $localPvpython = Join-Path (Split-Path -Parent $resolvedParaViewPath) 'pvpython.exe'
    if (Test-Path -LiteralPath $localPvpython) {
        $consoleVersion = (& $localPvpython --version 2>&1 | Out-String)
        if ($LASTEXITCODE -eq 0 -and $consoleVersion -match $versionPattern) {
            $clientVersion = $Matches[1]
        }
    }
}
if (-not $clientVersion) {
    throw "Cannot identify local ParaView version from file metadata or pvpython.exe: $resolvedParaViewPath"
}
if ($clientVersion -ne $PARAVIEW_VERSION) {
    throw "Expected local ParaView $PARAVIEW_VERSION; got: $clientVersion ($resolvedParaViewPath)"
}
Write-Host "Local ParaView version: $clientVersion" -ForegroundColor Cyan
# 固定本机端口已被占用时停止，不连接到旧窗口对应的远端服务。
$localListener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, [int]$LOCAL_PORT)
try {
    $localListener.Start()
} catch {
    throw "Local port $LOCAL_PORT is occupied. Close the previous SSH tunnel before starting a new session."
} finally {
    $localListener.Stop()
}
# 五项只读预检在同一次SSH中完成，减少入口认证和每个端口单独查询。
$serverOptions = "--processes $ServerProcesses"
if ($RemoteParaViewPrefix) { $serverOptions += " --prefix '$RemoteParaViewPrefix'" }
$firstRemotePort = [int]$REMOTE_PORT
$bundleConfig = @{
    version_command = (Get-ServerCommand "bash '$REMOTE_SERVER_SCRIPT' $serverOptions --version")
    ports_command = (Get-ServerCommand 'netstat -lnt')
    sender = $REMOTE_SENDER
    start_port = $firstRemotePort
    max_ports = $MAX_PORT_ATTEMPTS
} | ConvertTo-Json -Compress
$config64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($bundleConfig))
$bundlePython = @'
import base64, hashlib, json, pathlib, re, socket, subprocess, sys
config = json.loads(base64.b64decode('__CONFIG64__'))
def query(command):
    result = subprocess.run(command, shell=True, capture_output=True, text=True, timeout=60)
    if result.stderr:
        print(result.stderr, file=sys.stderr, end='')
    if result.returncode:
        raise RuntimeError('Read-only preflight failed: ' + command)
    return result.stdout
version = query(config['version_command'])
if not re.search(r'(?<![0-9.])6\.2\.0(?![0-9.])', version):
    raise RuntimeError('ParaView version preflight failed: ' + version)
manifest = json.loads(subprocess.check_output([sys.executable, config['sender'], 'current', 'client_manifest'], text=True))
client = pathlib.Path(manifest['client_source']).read_bytes()
if hashlib.sha256(client).hexdigest() != manifest['source_sha256']:
    raise RuntimeError('Repository bridge changed during preflight; retry')
listeners = query(config['ports_command'])
busy = {int(port) for port in re.findall(r'[:.](\d+)\s', listeners)}
port = next((p for p in range(config['start_port'], config['start_port'] + config['max_ports']) if p not in busy), None)
if port is None:
    raise RuntimeError('No available remote port in configured range')
print(json.dumps(dict(gateway=socket.gethostname().split('.')[0], server_version=version, manifest=manifest,
                     client_base64=base64.b64encode(client).decode(), available_port=port)))
'@
$bundlePython = $bundlePython.Replace('__CONFIG64__', $config64)
$bundle64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($bundlePython))
Write-Host "Checking server, bridge and ports in one SSH connection..." -ForegroundColor Cyan
$bundleOutput = Invoke-StartupReadOnlySsh -T -o BatchMode=yes -o ConnectTimeout=10 -p $SSH_PORT "${REMOTE_USERNAME}@${REMOTE_HOST}" "echo $bundle64 | base64 -d | '$REMOTE_PYTHON'"
if ($LASTEXITCODE -ne 0) { throw "Combined read-only preflight failed after retries; see preceding diagnostic. Existing bridge was retained." }
$bundle = ($bundleOutput -join "`n") | ConvertFrom-Json -ErrorAction Stop
$TUNNEL_TARGET = if ($bundle.gateway -eq $SERVER_HOST) { "localhost" } else { $SERVER_HOST }
$bridgeManifest = $bundle.manifest
$REMOTE_BRIDGE_ROOT = $bridgeManifest.bridge_root
$REMOTE_BRIDGE_SCRIPT = $bridgeManifest.client_source
if (-not $REMOTE_BRIDGE_ROOT -or -not $REMOTE_BRIDGE_SCRIPT) { throw "Repository manifest is missing bridge paths." }
$BRIDGE_EXPECTED_VERSION = $bridgeManifest.version
$BRIDGE_REQUIRED_CAPABILITIES = @($bridgeManifest.capabilities)
$bridgeDownload = "$BRIDGE_SCRIPT.download"
[IO.File]::WriteAllBytes($bridgeDownload, [Convert]::FromBase64String($bundle.client_base64))
$bridgeHash = (Get-FileHash -LiteralPath $bridgeDownload -Algorithm SHA256).Hash.ToLowerInvariant()
if ($bridgeHash -ne $bridgeManifest.source_sha256) {
    Remove-Item -LiteralPath $bridgeDownload -ErrorAction SilentlyContinue
    throw "Downloaded client does not match repository manifest; existing bridge was retained."
}
Move-Item -LiteralPath $bridgeDownload -Destination $BRIDGE_SCRIPT -Force
$REMOTE_PORT = [int]$bundle.available_port
if ($REMOTE_PORT -lt $firstRemotePort -or $REMOTE_PORT -ge ($firstRemotePort + $MAX_PORT_ATTEMPTS)) { throw "Invalid remote port in preflight reply." }
Write-Host "Bridge script: $BRIDGE_SCRIPT" -ForegroundColor Cyan
Write-Host "Remote bridge entry: $REMOTE_BRIDGE_ROOT/current.json" -ForegroundColor Cyan
Write-Host "ParaView $PARAVIEW_VERSION; server: $SERVER_HOST; processes: $ServerProcesses; remote port: $REMOTE_PORT" -ForegroundColor Cyan

# 隧道目的地与实际服务节点一致；不在 SSH 登录节点启动 pvserver。
# 每次启动使用独立服务日志目录，与本机启动记录通过同一编号关联。
$remoteLogDir = "$REMOTE_REPO_ROOT/.tools/paraview-mpi/logs/launch-$BRIDGE_LAUNCH_ID"
$serverCommand = Get-ServerCommand "PARAVIEW_SERVER_LOG_DIR='$remoteLogDir' bash '$REMOTE_SERVER_SCRIPT' $serverOptions --server-port=$REMOTE_PORT"
$sshCommand = "ssh -o ExitOnForwardFailure=yes -o BatchMode=yes -o ServerAliveInterval=30 -CL 127.0.0.1:${LOCAL_PORT}:${TUNNEL_TARGET}:${REMOTE_PORT} ${REMOTE_USERNAME}@${REMOTE_HOST} -p${SSH_PORT} -T `"$serverCommand`""
Write-Host "Server node: $SERVER_HOST, remote port: $REMOTE_PORT" -ForegroundColor Cyan

# Create server startup window
$logDir = Join-Path $scriptRoot "paraview_logs"
if (-not (Test-Path -LiteralPath $logDir)) {
    New-Item -ItemType Directory -Path $logDir | Out-Null
}
$launchStem = "pvserver_$(Get-Date -Format 'yyyyMMdd_HHmmss')_port${REMOTE_PORT}_$BRIDGE_LAUNCH_ID"
$batchFile = Join-Path $logDir "$launchStem.cmd"
$launchRecord = Join-Path $logDir "$launchStem.json"
$clientStdout = Join-Path $logDir "$launchStem.client.stdout.log"
$clientStderr = Join-Path $logDir "$launchStem.client.stderr.log"
$serverRunner = Join-Path $logDir "$launchStem.server.ps1"
$serverOutput = Join-Path $logDir "$launchStem.server.log"
$serverExit = Join-Path $logDir "$launchStem.server-exit.json"
$receiptFile = Join-Path $logDir "$launchStem.bridge.json"
$launchInfo = [ordered]@{
    launch_id = $BRIDGE_LAUNCH_ID
    launcher_transcript = $transcriptPath
    started_at = [DateTimeOffset]::Now.ToString('o')
    server_host = $SERVER_HOST
    server_processes = $ServerProcesses
    local_port = $LOCAL_PORT
    remote_port = $REMOTE_PORT
    paraview_version = $PARAVIEW_VERSION
    remote_log_directory = $remoteLogDir
    startup_script = $batchFile
    server_output = $serverOutput
    server_exit = $serverExit
    client_stdout = $clientStdout
    client_stderr = $clientStderr
    bridge_receipt = $receiptFile
}
[IO.File]::WriteAllText($launchRecord, ($launchInfo | ConvertTo-Json), [Text.UTF8Encoding]::new($false))

# 独立服务窗口先落本地SSH输出，认证、跳板和远端脚本之前的错误也能保存。
$runnerTemplate = @'
$ErrorActionPreference = 'Continue'
__EXPORT_FUNCTION__
$root = $PSScriptRoot
$id = '__LAUNCH_ID__'
$stem = '__STEM__'
$started = [DateTimeOffset]::Now.ToString('o')
$code = 1
try {
    __SSH_COMMAND__ 2>&1 | ForEach-Object {
        $line = "[{0}] {1}" -f [DateTimeOffset]::Now.ToString('o'), $_
        $line | Out-File -LiteralPath (Join-Path $root "$stem.server.log") -Append -Encoding utf8
        Write-Host $line
    }
    $code = $LASTEXITCODE
} catch {
    $_ | Out-String | Out-File -LiteralPath (Join-Path $root "$stem.server.log") -Append -Encoding utf8
} finally {
    @{started_at=$started; ended_at=[DateTimeOffset]::Now.ToString('o'); exit_code=$code} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root "$stem.server-exit.json") -Encoding utf8
    Export-LaunchDiagnostics -Root $root -LaunchId $id -Label 'server-exit'
}
Write-Host "Server exited with code $code. Logs: $root"
exit $code
'@
$runnerText = $runnerTemplate.Replace('__EXPORT_FUNCTION__', "function Export-LaunchDiagnostics {`n${function:Export-LaunchDiagnostics}`n}").Replace('__LAUNCH_ID__', $BRIDGE_LAUNCH_ID).Replace('__STEM__', $launchStem).Replace('__SSH_COMMAND__', $sshCommand)
[IO.File]::WriteAllText($serverRunner, $runnerText, [Text.UTF8Encoding]::new($true))
$batchLines = @(
    '@echo off'
    'chcp 65001 >nul'
    "title ParaView-${PARAVIEW_VERSION}-port${REMOTE_PORT}"
    'cd /d "%~dp0"'
    "powershell.exe -NoLogo -NoProfile -File `"$serverRunner`""
    'echo Server window retained. Logs and diagnostic ZIP are beside this script.'
)

$batchContent = $batchLines -join [Environment]::NewLine
[IO.File]::WriteAllText($batchFile, $batchContent, [Text.UTF8Encoding]::new($false))

# Start server and client
Write-Host "`nStarting ParaView server..." -ForegroundColor Cyan
Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", "`"$batchFile`"") -WorkingDirectory $logDir -WindowStyle Normal
Write-Host "Server startup script: $batchFile" -ForegroundColor Cyan
Write-Host "Launch record: $launchRecord" -ForegroundColor Cyan
Write-Host "Remote server logs: $remoteLogDir" -ForegroundColor Cyan

# 检查监听状态，不建立 TCP 探测连接，避免消耗 pvserver 的客户端连接。
Write-Host "Waiting for remote pvserver listener..." -ForegroundColor Cyan
$attempts = (1..30) -join ' '
$readyCommand = Get-ServerCommand "for i in $attempts; do if netstat -lnt 2>/dev/null | grep -Eq '[:.]${REMOTE_PORT}[[:space:]]'; then exit 0; fi; sleep 1; done; exit 1"
Invoke-StartupReadOnlySsh -T -o BatchMode=yes -o ConnectTimeout=5 -p $SSH_PORT "${REMOTE_USERNAME}@${REMOTE_HOST}" $readyCommand
if ($LASTEXITCODE -ne 0) {
    throw "pvserver listener did not become ready. Read the server startup window; it is kept open."
}

Write-Host "Starting ParaView client..." -ForegroundColor Cyan
$resolvedParaViewPath = Find-ParaViewExecutable -ConfiguredPath $PARAVIEW_PATH
if ($resolvedParaViewPath) {
    # 原生参数需要保留脚本路径两侧引号，兼容含空格的 Windows 路径。
    $bridgeEnvironment = @{
        PV_BRIDGE_ROOT = $REMOTE_BRIDGE_ROOT
        PV_BRIDGE_CLIENT_SOURCE = $REMOTE_BRIDGE_SCRIPT
        PV_BRIDGE_SCRIPT = $BRIDGE_SCRIPT
        PV_BRIDGE_LAUNCH_ID = $BRIDGE_LAUNCH_ID
        PV_BRIDGE_SSH_PORT = "$SSH_PORT"
        PV_BRIDGE_SSH_TARGET = "${REMOTE_USERNAME}@${REMOTE_HOST}"
    }
    $savedEnvironment = @{}
    try {
        foreach ($name in $bridgeEnvironment.Keys) {
            $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
            [Environment]::SetEnvironmentVariable($name, $bridgeEnvironment[$name], "Process")
        }
        $paraViewProcess = Start-Process -PassThru -FilePath $resolvedParaViewPath -RedirectStandardOutput $clientStdout -RedirectStandardError $clientStderr -ArgumentList @("--url=cs://localhost:${LOCAL_PORT}", "--script=`"$BRIDGE_SCRIPT`"")
    } finally {
        foreach ($name in $savedEnvironment.Keys) {
            [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], "Process")
        }
    }
    Write-Host "ParaView client started: $resolvedParaViewPath" -ForegroundColor Green
} else {
    Write-Host "ERR: ParaView executable not found." -ForegroundColor Red
    Write-Host "Configured path: $PARAVIEW_PATH" -ForegroundColor Red
    Write-Host "Please set PARAVIEW_PATH to your local installation." -ForegroundColor Yellow
}

# 等待 GUI 主线程真实执行查询并经 SSH 回传，不能只凭进程启动或旧 current.json 判成功。
Write-Host "Checking bridge GUI response (up to $BRIDGE_STARTUP_SECONDS seconds)..." -ForegroundColor Cyan
$receiptPath = "$REMOTE_BRIDGE_ROOT/launch-$BRIDGE_LAUNCH_ID.json"
$checkAttempts = (1..$BRIDGE_STARTUP_SECONDS) -join ' '
$checkCommand = "for i in $checkAttempts; do if test -s '$receiptPath'; then cat '$receiptPath'; exit 0; fi; sleep 1; done; exit 1"
$receiptOutput = Invoke-StartupReadOnlySsh -T -o BatchMode=yes -o ConnectTimeout=5 -p $SSH_PORT "${REMOTE_USERNAME}@${REMOTE_HOST}" $checkCommand
if ($LASTEXITCODE -ne 0) {
    throw "Bridge GUI check timed out. ParaView may be connected but the bridge is not ready. Check ParaView Output Messages and $BRIDGE_SCRIPT. Server window is retained."
}
try {
    $receipt = ($receiptOutput -join "`n") | ConvertFrom-Json -ErrorAction Stop
} catch {
    throw "Bridge returned invalid JSON. Check remote shell output. Receipt: $receiptPath"
}
if ($receipt.launch_id -ne $BRIDGE_LAUNCH_ID -or $receipt.id -ne "startup-$BRIDGE_LAUNCH_ID" -or -not $receipt.ok) {
    throw "Bridge GUI check failed or launch ID does not match: $($receipt.error)"
}
if ($receipt.version -ne $BRIDGE_EXPECTED_VERSION -or $receipt.source_sha256 -ne $bridgeHash) {
    throw "Bridge version or source hash mismatch. The running bridge does not match the downloaded script."
}
foreach ($capability in $BRIDGE_REQUIRED_CAPABILITIES) {
    if ($receipt.value.capabilities -notcontains $capability) {
        throw "Bridge is missing required capability: $capability"
    }
}
$expectedUri = "cs://localhost:${LOCAL_PORT}"
if (-not $receipt.value.connection_uri -or $receipt.value.connection_uri.TrimEnd('/') -ne $expectedUri) {
    throw "Bridge is running but ParaView is not connected to $expectedUri (actual: $($receipt.value.connection_uri))."
}
$paraViewProcess.Refresh()
if ($paraViewProcess.HasExited) {
    throw "ParaView exited during bridge startup."
}
Write-Host "Bridge ready: version $($receipt.version), GUI response OK, source hash verified." -ForegroundColor Green
[IO.File]::WriteAllText($receiptFile, ($receipt | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
Write-Host "Session: $($receipt.directory)" -ForegroundColor Cyan
Write-Host "Client stdout/stderr: $clientStdout / $clientStderr" -ForegroundColor Cyan

# Show connection info
Write-Host "`n=== Connection Settings ===" -ForegroundColor Yellow
Write-Host "Automatic connection (manual fallback settings):" -ForegroundColor White
Write-Host "  Server Name: SuperComputer" -ForegroundColor Gray
Write-Host "  Host: localhost" -ForegroundColor Gray
Write-Host "  Port: $LOCAL_PORT" -ForegroundColor Gray
Write-Host "  Remote Port: $REMOTE_PORT (auto-selected)" -ForegroundColor Gray

Write-Host "`n=== Usage Notes ===" -ForegroundColor Green
Write-Host "OK: Server window should show 'Waiting for client...'" -ForegroundColor White
Write-Host "OK: ParaView connects automatically; File -> Connect is a fallback." -ForegroundColor White
Write-Host "OK: Bridge loads automatically; no Python Shell command is needed." -ForegroundColor White
Write-Host "OK: Agent discovers the latest session from the fixed remote entry." -ForegroundColor White
Write-Host "OK: Use -BridgeStatus or -UpdateBridge for an existing window; these modes do not reconnect data." -ForegroundColor White
Write-Host "OK: Keep server window open, closing it will disconnect" -ForegroundColor White
Write-Host "OK: Auto-selected remote port is $REMOTE_PORT" -ForegroundColor Cyan

# Keep the server startup script under paraview_logs for inspection.
} finally {
    # 成功、异常、状态查询提前 return 均须结束本次记录。
    if ($transcriptStarted) { Stop-Transcript -ErrorAction SilentlyContinue | Out-Null; $transcriptStarted = $false }
    Export-LaunchDiagnostics -Root $transcriptRoot -LaunchId $BRIDGE_LAUNCH_ID
}
