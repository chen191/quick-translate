$ErrorActionPreference = 'Stop'

$exe = Join-Path $PSScriptRoot 'quick-translate.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "quick-translate.exe was not found next to this script."
}

$exe = [IO.Path]::GetFullPath($exe)
$startup = [Environment]::GetFolderPath('Startup')
$linkPath = Join-Path $startup 'Quick Translate.lnk'
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($linkPath)
$link.TargetPath = $exe
$link.WorkingDirectory = [IO.Path]::GetDirectoryName($exe)
$link.IconLocation = $exe + ',0'
$link.Description = 'Quick Translate - Ctrl+E selected English to Chinese'
$link.Save()

Write-Output $linkPath
