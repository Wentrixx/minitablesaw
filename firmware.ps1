[CmdletBinding()]
param(
    [ValidateSet('build', 'flash')][string]$Action = 'build',
    [string]$Port = 'COM3',
    [string]$IdfPath = 'C:\esp\v6.1\esp-idf',
    [string]$ToolsPath = 'C:\Espressif\tools'
)
$ErrorActionPreference = 'Stop'
function Find-Tool([string]$Pattern) {
    $tool = Get-ChildItem -Path $Pattern -File -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $tool) { throw "Required tool not found: $Pattern" }
    $tool.FullName
}
try {
    $python = Find-Tool "$ToolsPath\python\v6.1\venv\Scripts\python.exe"
    $env:PYTHONUTF8 = '1'
    $env:PYTHONUNBUFFERED = '1'
    if ($Action -eq 'build') {
        $cmake = Find-Tool "$ToolsPath\cmake\*\bin\cmake.exe"
        $ninja = Find-Tool "$ToolsPath\ninja\*\ninja.exe"
        $compiler = Find-Tool "$ToolsPath\xtensa-esp-elf\*\xtensa-esp-elf\bin\xtensa-esp32-elf-gcc.exe"
        $rom = Find-Tool "$ToolsPath\esp-rom-elfs\*\esp32_rev0_rom.elf"
        $env:IDF_PATH = $IdfPath
        $env:ESP_IDF_VERSION = '6.1.0'
        $env:IDF_TOOLS_PATH = $ToolsPath
        $env:IDF_PYTHON_ENV_PATH = Split-Path (Split-Path $python)
        $env:ESP_ROM_ELF_DIR = Split-Path $rom
        $env:PATH = "$(Split-Path $python);$(Split-Path $cmake);$(Split-Path $ninja);$(Split-Path $compiler);$env:PATH"
        Set-Location -LiteralPath $PSScriptRoot
        # Native stderr is diagnostic output, not a terminating PowerShell error.
        $ErrorActionPreference = 'Continue'
        & $python -u (Join-Path $IdfPath 'tools\idf.py') build 2>&1 | ForEach-Object { $_.ToString() }
    } else {
        Set-Location -LiteralPath (Join-Path $PSScriptRoot 'build')
        if (-not (Test-Path -LiteralPath 'flash_args')) { throw 'Build firmware before Run.' }
        $ErrorActionPreference = 'Continue'
        & $python -u -m esptool --chip esp32 --port $Port -b 460800 --before default-reset --after hard-reset write-flash '@flash_args' 2>&1 | ForEach-Object { $_.ToString() }
    }
    exit $LASTEXITCODE
} catch {
    [Console]::Error.WriteLine($_.ToString())
    exit 1
}
