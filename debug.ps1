[CmdletBinding()]
param(
    [string]$UcrtBin = 'C:\msys64\ucrt64\bin',
    [switch]$NoRun
)

$ErrorActionPreference = 'Stop'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit $LASTEXITCODE): $Program $($Arguments -join ' ')"
    }
}

# Restore the calling terminal's environment even when a build fails.
$savedPath = $env:PATH

Push-Location $PSScriptRoot
try {
    foreach ($tool in @('cmake.exe', 'ninja.exe', 'gcc.exe', 'pkg-config.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $UcrtBin $tool))) {
            throw "MSYS2 UCRT64 tool missing: $UcrtBin\$tool"
        }
    }
    # Use UCRT tools and DLLs for the desktop build and the launched application.
    $env:PATH = "$UcrtBin;$savedPath"
    $uiCmake = Join-Path $UcrtBin 'cmake.exe'
    Write-Host 'Building GTK UI...' -ForegroundColor Cyan
    Invoke-Checked $uiCmake @('-S', 'debug_ui', '-B', 'debug_ui/build', '-G', 'Ninja')
    Invoke-Checked $uiCmake @('--build', 'debug_ui/build')

    if (-not $NoRun) {
        Write-Host 'Starting UI...' -ForegroundColor Green
        Start-Process -FilePath (Join-Path $PSScriptRoot 'debug_ui\build\minitablesaw_debug.exe') `
            -WorkingDirectory $PSScriptRoot -WindowStyle Hidden | Out-Null
    }
    Write-Host 'Done.' -ForegroundColor Green
} finally {
    Pop-Location
    $env:PATH = $savedPath
}
