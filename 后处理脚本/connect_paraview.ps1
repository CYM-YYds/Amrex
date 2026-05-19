# ParaView remote connection launcher

# Configuration
$PARAVIEW_PATH = "D:\Software\ParaView 5.11.2\bin\paraview.exe"
# $PARAVIEW_PATH = "D:\Software\ParaView-5.11.1-Windows-Python3.9-msvc2017-AMD64\bin\paraview.exe"
$LOCAL_PORT = "11115"        # Local port
$REMOTE_PORT = "11115"       # Remote start port
$MPI_PROCESSES = "32"        # MPI process count
$MAX_PORT_ATTEMPTS = 10      # Max port attempts

# Check whether remote port is available
function Test-RemotePort {
    param(
        [string]$RemoteHost = "27.18.114.25",
        [int]$Port = 6022,
        [string]$Username = "lijing",
        [int]$RemotePort
    )
    
    Write-Host "Checking remote port $RemotePort..." -ForegroundColor Yellow
    
    # Test remote port and return quickly
    $testCommand = "ssh -o ConnectTimeout=5 -o BatchMode=yes -p $Port ${Username}@${RemoteHost} `"netstat -ln | grep :${RemotePort} || echo 'PORT_AVAILABLE'`""
    
    try {
        $result = Invoke-Expression $testCommand 2>$null
        
        # If occupied, netstat returns listener info; otherwise PORT_AVAILABLE.
        if ($result -match "PORT_AVAILABLE" -or $result -eq $null -or $result.Trim() -eq "") {
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
#$sshCommand = "ssh -gCL ${LOCAL_PORT}:localhost:${REMOTE_PORT} lijing@27.18.114.25 -p6022 -t `"bash -l -c 'module use /home/HPCBase/modulefiles && module purge && module load tools/Paraview/5.11.1 && module load mpi/openmpi/4.1.2_gcc9.3.0 && mpirun -np ${MPI_PROCESSES} -mca btl vader,self --bind-to core --oversubscribe pvserver --force-offscreen-rendering --disable-xdisplay-test --multi-clients --server-port=${REMOTE_PORT}'`""

# Disable NVIDIA index related plugins
$sshCommand = "ssh -gCL ${LOCAL_PORT}:localhost:${REMOTE_PORT} lijing@27.18.114.25 -p6022 -t `"bash -l -c 'module use /home/HPCBase/modulefiles && module purge && module load tools/Paraview/5.11.1 && module load mpi/openmpi/4.1.2_gcc9.3.0 && export PV_DISABLE_NVIDIA_INDEX=1 && export NVIDIA_INDEX_DISABLE=1 && mkdir -p /tmp/pv-no-plugins && export PV_PLUGIN_PATH=/tmp/pv-no-plugins && mpirun -np ${MPI_PROCESSES} pvserver --force-offscreen-rendering --disable-xdisplay-test --server-port=${REMOTE_PORT}'`""

Write-Host "`n=== SSH Debug Info ===" -ForegroundColor Cyan
Write-Host "REMOTE_PORT=$REMOTE_PORT" -ForegroundColor Yellow
Write-Host "Full SSH command:" -ForegroundColor Yellow
Write-Host $sshCommand -ForegroundColor Gray

# 精简版的命令
#$sshCommand = "ssh -gCL ${LOCAL_PORT}:localhost:${REMOTE_PORT} lijing@27.18.114.25 -p6022 -t `"bash -l -c 'module use /home/HPCBase/modulefiles && module purge && module load tools/Paraview/5.11.1 && module load mpi/openmpi/4.1.2_gcc9.3.0 && mpirun -np ${MPI_PROCESSES} pvserver --force-offscreen-rendering --disable-xdisplay-test --server-port=${REMOTE_PORT}'`""

# Create server startup window
$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
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
$batchContent | Out-File -FilePath $batchFile -Encoding UTF8

# Start server and client
Write-Host "`nStarting ParaView server..." -ForegroundColor Cyan
Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", "`"$batchFile`"") -WorkingDirectory $logDir -WindowStyle Normal
Write-Host "Server startup script: $batchFile" -ForegroundColor Cyan

Start-Sleep -Seconds 1

Write-Host "Starting ParaView client..." -ForegroundColor Cyan
$resolvedParaViewPath = Find-ParaViewExecutable -ConfiguredPath $PARAVIEW_PATH
if ($resolvedParaViewPath) {
    Start-Process -FilePath $resolvedParaViewPath
    Write-Host "ParaView client started: $resolvedParaViewPath" -ForegroundColor Green
} else {
    Write-Host "ERR: ParaView executable not found." -ForegroundColor Red
    Write-Host "Configured path: $PARAVIEW_PATH" -ForegroundColor Red
    Write-Host "Please set PARAVIEW_PATH to your local installation." -ForegroundColor Yellow
}

# Show connection info
Write-Host "`n=== Connection Settings ===" -ForegroundColor Yellow
Write-Host "Connect in ParaView with:" -ForegroundColor White
Write-Host "  Server Name: SuperComputer" -ForegroundColor Gray
Write-Host "  Host: localhost" -ForegroundColor Gray
Write-Host "  Port: $LOCAL_PORT" -ForegroundColor Gray
Write-Host "  Remote Port: $REMOTE_PORT (auto-selected)" -ForegroundColor Gray

Write-Host "`n=== Usage Notes ===" -ForegroundColor Green
Write-Host "OK: Server window should show 'Waiting for client...'" -ForegroundColor White
Write-Host "OK: In ParaView use File -> Connect -> Add Server" -ForegroundColor White
Write-Host "OK: Keep server window open, closing it will disconnect" -ForegroundColor White
Write-Host "OK: Auto-selected remote port is $REMOTE_PORT" -ForegroundColor Cyan

# Keep the server startup script under paraview_logs for inspection.



