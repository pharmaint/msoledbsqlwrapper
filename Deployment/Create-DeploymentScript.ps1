<#
.SYNOPSIS
    Packages the OleDbWrapper.dll into the PowerShell deployment script template.
    This script calculates the DLL's SHA256 hash and Base64 string, then injects
    them into the template file to create a ready-to-use deployment script.
#>

# --- PRE-FLIGHT CHECKS ---

# 1. Confirm the script is running with Administrator privileges.
Write-Host "Checking for Administrator privileges..." -ForegroundColor Yellow
if (-NOT ([System.Security.Principal.WindowsPrincipal][System.Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script must be run as an Administrator. Please re-launch PowerShell with 'Run as Administrator' and try again."
    # Pause to allow user to read the error before the window closes in some environments.
    if ($Host.Name -eq "ConsoleHost") {
        Read-Host -Prompt "Press Enter to exit"
    }
    exit 1
}
Write-Host "Success: Running with Administrator privileges." -ForegroundColor Green

# 2. Set the working directory to the script's location.
$ScriptDirectory = $PSScriptRoot
Set-Location -Path $ScriptDirectory
Write-Host "Working directory set to: $ScriptDirectory" -ForegroundColor Green

# --- VARIABLE DEFINITIONS ---

# Source and output file names.
$SourceDllFile = '..\Release\msoledbsqlwrapper.dll'
$SourcePsFile  = 'Deploy-MsOleDbSqlWrapperTemplate.ps1'
$OutputPsFile  = 'Deploy-MsOleDbSqlWrapper.ps1'

# Build full paths based on the script's location.
$DllFullPath      = Join-Path -Path $ScriptDirectory -ChildPath $SourceDllFile
$TemplateFullPath = Join-Path -Path $ScriptDirectory -ChildPath $SourcePsFile
$OutputFullPath   = Join-Path -Path $ScriptDirectory -ChildPath $OutputPsFile

# Placeholder text to be replaced in the template file.
$HashReplaceText      = 'PASTE_NEW_32_BIT_SHA256_HASH_HERE'
$B64StringReplaceText = 'PASTE THE BASE64 STRING GENERATED FROM THE *32-BIT* msoledbsqlwrapper.dll HERE.'

# 3. Verify that the necessary source files exist before proceeding.
Write-Host "Verifying source files..." -ForegroundColor Yellow
if (-NOT (Test-Path -Path $DllFullPath)) {
    Write-Error "Source file not found: '$DllFullPath'. Please ensure the DLL is in the specified directory."
    if ($Host.Name -eq "ConsoleHost") {
        Read-Host -Prompt "Press Enter to exit"
    }
    exit 1
}
if (-NOT (Test-Path -Path $TemplateFullPath)) {
    Write-Error "Template file not found: '$TemplateFullPath'. Please ensure the template PS1 file is in the same directory."
    if ($Host.Name -eq "ConsoleHost") {
        Read-Host -Prompt "Press Enter to exit"
    }
    exit 1
}
Write-Host "Success: Source DLL and Template PS1 found." -ForegroundColor Green

# --- SCRIPT LOGIC ---

try {
    # 1. Generate the SHA256 hash of the DLL.
    Write-Host "Generating SHA256 hash for '$SourceDllFile'..." -ForegroundColor Yellow
    $FileHash = (Get-FileHash -Path $DllFullPath -Algorithm SHA256).Hash
    Write-Host "Generated Hash: $FileHash"

    # 2. Generate the Base64 string from the DLL file.
    # We use InsertLineBreaks to match the format of the here-string (@"..."@) in the template.
    Write-Host "Generating Base64 string for '$SourceDllFile'..." -ForegroundColor Yellow
    $B64String = [System.Convert]::ToBase64String([System.IO.File]::ReadAllBytes($DllFullPath), [System.Base64FormattingOptions]::InsertLineBreaks)
    Write-Host "Base64 string generated successfully."

    # 3. Read the template file content.
    # -Raw ensures the file is read as a single string, which is crucial for replacement.
    Write-Host "Reading template file '$SourcePsFile'..." -ForegroundColor Yellow
    $templateContent = Get-Content -Path $TemplateFullPath -Raw

    # 4. Perform the replacements.
    Write-Host "Replacing placeholders in template..." -ForegroundColor Yellow
    $newContent = $templateContent.Replace($HashReplaceText, $FileHash)
    $newContent = $newContent.Replace($B64StringReplaceText, $B64String)
    Write-Host "Placeholders replaced."

    # 5. Write the new content to the output file.
    Write-Host "Writing output to '$OutputPsFile'..." -ForegroundColor Yellow
    $newContent | Set-Content -Path $OutputFullPath -Encoding UTF8 -Force
    
    Write-Host "----------------------------------------------------"
    Write-Host "Script completed successfully!" -ForegroundColor Green
    Write-Host "The new deployment script is located at: $OutputFullPath"
    Write-Host "----------------------------------------------------"
}
catch {
    Write-Error "An unexpected error occurred during the process: $_"
    if ($Host.Name -eq "ConsoleHost") {
        Read-Host -Prompt "Press Enter to exit"
    }
    exit 1
}
