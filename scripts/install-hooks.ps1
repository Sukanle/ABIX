#Requires -Version 5.1

$ErrorActionPreference = "Stop"

Write-Host " Installing Git Hooks"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error "Git was not found. Please install Git first."
    exit 1
}

try {
    git rev-parse --is-inside-work-tree *> $null
}
catch {
    Write-Error "Current directory is not inside a Git repository."
    exit 1
}

git config core.hooksPath .githooks

$hooksPath = git config --get core.hooksPath

if ($hooksPath -ne ".githooks") {
    Write-Error "Failed to configure Git hooks."
    exit 1
}

Write-Host "Git Hooks installed successfully."
Write-Host "Hooks Path : $hooksPath"

if (-not (Get-Command clang-format -ErrorAction SilentlyContinue)) {

    Write-Warning ""
    Write-Warning "clang-format was not found in PATH."
    Write-Warning "Please install LLVM/Clang before committing."
    Write-Warning ""

} else {

    $version = (clang-format --version)

    Write-Host ""
    Write-Host "Detected:"
    Write-Host "    $version"
}

Write-Host ""
Write-Host "Done."
