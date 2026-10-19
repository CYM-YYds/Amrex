# ParaView remote connection launcher

# Configuration
$PARAVIEW_PATH = "D:\Software\ParaView 5.11.2\bin\paraview.exe"
# $PARAVIEW_PATH = "D:\Software\ParaView-5.11.1-Windows-Python3.9-msvc2017-AMD64\bin\paraview.exe"
$LOCAL_PORT = "11115"        # Local port
$REMOTE_PORT = "11115"       # Remote start port
$MPI_PROCESSES = "32"        # MPI process count
$MAX_PORT_ATTEMPTS = 10      # Max port attempts
$REMOTE_USERNAME = "Liuzhaohui" # Remote SSH account
# 本机控制桥固定放在启动脚本旁，每次启动从仓库同步最新版。
$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$BRIDGE_SCRIPT = Join-Path $scriptRoot "paraview_bridge_client.py"
$REMOTE_HOST = "27.18.114.25"
$SSH_PORT = 6022
$REMOTE_BRIDGE_SCRIPT = "/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/后处理脚本/paraview_bridge_client.py"
$REMOTE_BRIDGE_ROOT = "/home/share/iosoeqkp/home/Liuzhaohui/.cache/paraview-bridge"
# 每次启动使用独立编号，自检不会把旧会话当作本次启动成功。
$BRIDGE_LAUNCH_ID = [Guid]::NewGuid().ToString("N")
$BRIDGE_STARTUP_SECONDS = 180
$BRIDGE_EXPECTED_VERSION = "2"
$BRIDGE_REQUIRED_CAPABILITIES = @("inspect", "background", "restore_background", "export_centerline")

# Check whether remote port is available
function Test-RemotePort {
    param(
        [string]$RemoteHost = $REMOTE_HOST,
        [int]$Port = $SSH_PORT,
        [string]$Username = $REMOTE_USERNAME,
        [int]$RemotePort
    )
    
    Write-Host "Checking remote port $RemotePort..." -ForegroundColor Yellow
    
    # Test remote port and return quickly
    $testCommand = "ssh -o ConnectTimeout=5 -o BatchMode=yes -p $Port ${Username}@${RemoteHost} `"netstat -ln | grep :${RemotePort} || echo 'PORT_AVAILABLE'`""
    
    try {
        $result = Invoke-Expression $testCommand 2>$null
        
        # If occupied, netstat returns listener info; otherwise PORT_AVAILABLE.
        if ($LASTEXITCODE -ne 0) {
            throw "SSH port check failed (exit $LASTEXITCODE)"
        }
        if ($result -match "PORT_AVAILABLE") {
            Write-Host "OK: Port $RemotePort is available" -ForegroundColor Green
            return $true
        } else {
            Write-Host "ERR: Port $RemotePort is in use" -ForegroundColor Red
            return $false
        }
    } catch {
        Write-Host "ERR: Port test failed for ${RemotePort}: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

# Find available remote port
function Find-AvailablePort {
    param(
        [int]$StartPort = 11115,
        [int]$MaxAttempts = 10
    )
    
    Write-Host "`n=== Search Available Ports ===" -ForegroundColor Cyan
    
    for ($i = 0; $i -lt $MaxAttempts; $i++) {
        $testPort = $StartPort + $i
        Write-Host "[$($i + 1)/$MaxAttempts] " -NoNewline -ForegroundColor Gray
        
        if (Test-RemotePort -RemotePort $testPort) {
            Write-Host "Selected available port: $testPort" -ForegroundColor Green
            return $testPort
        }
        
        Start-Sleep -Seconds 1
    }
    
    Write-Host "ERR: No available port found after $MaxAttempts attempts" -ForegroundColor Red
    return $null
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
        "D:\Software\ParaView*\bin\paraview.exe",
        "$env:ProgramFiles\ParaView*\bin\paraview.exe",
        "$env:ProgramFiles(x86)\ParaView*\bin\paraview.exe"
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
# 固定本机端口已被占用时停止，不连接到旧窗口对应的远端服务。
$localListener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, [int]$LOCAL_PORT)
try {
    $localListener.Start()
} catch {
    throw "Local port $LOCAL_PORT is occupied. Close the previous SSH tunnel before starting a new session."
} finally {
    $localListener.Stop()
}
$bridgeDownload = "$BRIDGE_SCRIPT.download"
& scp -B -o ConnectTimeout=5 -P $SSH_PORT "${REMOTE_USERNAME}@${REMOTE_HOST}:${REMOTE_BRIDGE_SCRIPT}" $bridgeDownload
if ($LASTEXITCODE -ne 0) {
    throw "Bridge download failed. Check SSH key authentication; existing bridge was retained."
}
Move-Item -LiteralPath $bridgeDownload -Destination $BRIDGE_SCRIPT -Force
$bridgeHash = (Get-FileHash -LiteralPath $BRIDGE_SCRIPT -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Bridge script: $BRIDGE_SCRIPT" -ForegroundColor Cyan
Write-Host "Remote bridge entry: $REMOTE_BRIDGE_ROOT/current.json" -ForegroundColor Cyan

Write-Host "=== ParaView Remote HPC Launcher ===" -ForegroundColor Green
Write-Host "MPI processes: $MPI_PROCESSES" -ForegroundColor Yellow

# Find available remote port
$availablePort = Find-AvailablePort -StartPort $REMOTE_PORT -MaxAttempts $MAX_PORT_ATTEMPTS

if ($availablePort -eq $null) {
    Write-Host "ERR: No available remote port found. Connection aborted." -ForegroundColor Red
    Write-Host "Retry later or check server status with admin." -ForegroundColor Yellow
    pause
    exit 1
}

# Update remote port
$REMOTE_PORT = $availablePort
Write-Host "Using remote port: $REMOTE_PORT" -ForegroundColor Green

# Build SSH command
#$sshCommand = "ssh -o ExitOnForwardFailure=yes -CL 127.0.0.1:${LOCAL_PORT}:localhost:${REMOTE_PORT} ${REMOTE_USERNAME}@${REMOTE_HOST} -p${SSH_PORT} -t `"bash -l -c 'module use /home/HPCBase/modulefiles && module purge && module load tools/Paraview/5.11.1 && module load mpi/openmpi/4.1.2_gcc9.3.0 && mpirun -np ${MPI_PROCESSES} -mca btl vader,self --bind-to core --oversubscribe pvserver --force-offscreen-rendering --disable-xdisplay-test --multi-clients --server-port=${REMOTE_PORT}'`""

# Disable NVIDIA index related plugins
$sshCommand = "ssh -o ExitOnForwardFailure=yes -CL 127.0.0.1:${LOCAL_PORT}:localhost:${REMOTE_PORT} ${REMOTE_USERNAME}@${REMOTE_HOST} -p${SSH_PORT} -t `"bash -l -c 'module use /home/HPCBase/modulefiles && module purge && module load tools/Paraview/5.11.1 && module load mpi/openmpi/4.1.2_gcc9.3.0 && export PV_DISABLE_NVIDIA_INDEX=1 && export NVIDIA_INDEX_DISABLE=1 && mkdir -p /tmp/pv-no-plugins && export PV_PLUGIN_PATH=/tmp/pv-no-plugins && mpirun -np ${MPI_PROCESSES} pvserver --force-offscreen-rendering --disable-xdisplay-test --server-port=${REMOTE_PORT}'`""

Write-Host "`n=== SSH Debug Info ===" -ForegroundColor Cyan
Write-Host "REMOTE_PORT=$REMOTE_PORT" -ForegroundColor Yellow
Write-Host "Full SSH command:" -ForegroundColor Yellow
Write-Host $sshCommand -ForegroundColor Gray

# 精简版的命令
#$sshCommand = "ssh -o ExitOnForwardFailure=yes -CL 127.0.0.1:${LOCAL_PORT}:localhost:${REMOTE_PORT} ${REMOTE_USERNAME}@${REMOTE_HOST} -p${SSH_PORT} -t `"bash -l -c 'module use /home/HPCBase/modulefiles && module purge && module load tools/Paraview/5.11.1 && module load mpi/openmpi/4.1.2_gcc9.3.0 && mpirun -np ${MPI_PROCESSES} pvserver --force-offscreen-rendering --disable-xdisplay-test --server-port=${REMOTE_PORT}'`""

# Create server startup window
$logDir = Join-Path $scriptRoot "paraview_logs"
if (-not (Test-Path -LiteralPath $logDir)) {
    New-Item -ItemType Directory -Path $logDir | Out-Null
}
$batchFile = Join-Path $logDir "pvserver_start_port${REMOTE_PORT}.cmd"

$batchLines = @(
    '@echo off'
    'chcp 65001 >nul'
    "title ParaView-${MPI_PROCESSES}proc-port${REMOTE_PORT}"
    'cd /d "%~dp0"'
    'echo ================================================'
    "echo ParaView server start ($MPI_PROCESSES CPU cores)"
    "echo Remote port: ${REMOTE_PORT}"
    'echo If successful, output should include "Waiting for client..."'
    'echo Keep this window open.'
    'echo This debug window is launched with cmd /k, so it should stay open after errors.'
    "echo Startup script: $batchFile"
    'echo ================================================'
    $sshCommand
    'set PVSERVER_EXIT_CODE=%ERRORLEVEL%'
    'echo ================================================'
    'echo Server exited with code %PVSERVER_EXIT_CODE%'
    'echo If ParaView crashed while loading a .py file, read the last red/error lines above.'
    'echo This window is intentionally kept open. Close it manually when done.'
)

$batchContent = $batchLines -join [Environment]::NewLine
[IO.File]::WriteAllText($batchFile, $batchContent, [Text.UTF8Encoding]::new($false))

# Start server and client
Write-Host "`nStarting ParaView server..." -ForegroundColor Cyan
Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", "`"$batchFile`"") -WorkingDirectory $logDir -WindowStyle Normal
Write-Host "Server startup script: $batchFile" -ForegroundColor Cyan

# 检查监听状态，不建立 TCP 探测连接，避免消耗 pvserver 的客户端连接。
Write-Host "Waiting for remote pvserver listener..." -ForegroundColor Cyan
$attempts = (1..30) -join ' '
$readyCommand = "for i in $attempts; do if netstat -lnt 2>/dev/null | grep -Eq '[:.]${REMOTE_PORT}[[:space:]]'; then exit 0; fi; sleep 1; done; exit 1"
& ssh -T -o BatchMode=yes -o ConnectTimeout=5 -p $SSH_PORT "${REMOTE_USERNAME}@${REMOTE_HOST}" $readyCommand
if ($LASTEXITCODE -ne 0) {
    throw "pvserver listener did not become ready. Read the server startup window; it is kept open."
}

Write-Host "Starting ParaView client..." -ForegroundColor Cyan
$resolvedParaViewPath = Find-ParaViewExecutable -ConfiguredPath $PARAVIEW_PATH
if ($resolvedParaViewPath) {
    # 原生参数需要保留脚本路径两侧引号，兼容含空格的 Windows 路径。
    $bridgeEnvironment = @{
        PV_BRIDGE_ROOT = $REMOTE_BRIDGE_ROOT
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
        $paraViewProcess = Start-Process -PassThru -FilePath $resolvedParaViewPath -ArgumentList @("--url=cs://localhost:${LOCAL_PORT}", "--script=`"$BRIDGE_SCRIPT`"")
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
$receiptOutput = & ssh -T -o BatchMode=yes -o ConnectTimeout=5 -p $SSH_PORT "${REMOTE_USERNAME}@${REMOTE_HOST}" $checkCommand
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
Write-Host "Session: $($receipt.directory)" -ForegroundColor Cyan

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
Write-Host "OK: Keep server window open, closing it will disconnect" -ForegroundColor White
Write-Host "OK: Auto-selected remote port is $REMOTE_PORT" -ForegroundColor Cyan

# Keep the server startup script under paraview_logs for inspection.



