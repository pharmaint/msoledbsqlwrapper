<#
.SYNOPSIS
    Installs, uninstalls, or configures the 32-bit OLE DB Wrapper for the legacy SQLOLEDB provider
    using a direct file replacement method.
#>
[CmdletBinding(DefaultParameterSetName = 'Install')]
param(
    [Parameter(ParameterSetName = 'Install', HelpMessage = "Installs the wrapper. This is the default action.")]
    [Alias('i')]
    [Switch]$Install,

    [Parameter(Mandatory = $true, ParameterSetName = 'Uninstall', HelpMessage = "Uninstalls the wrapper.")]
    [Alias('u')]
    [Switch]$Uninstall,

    [Parameter(Mandatory = $true, ParameterSetName = 'Change', HelpMessage = "Changes the Regex configuration.")]
    [Alias('c')]
    [Switch]$Change,

    [Parameter(Mandatory = $false, ParameterSetName = 'Change')]
    [Parameter(Mandatory = $false, ParameterSetName = 'Install')]

    [Parameter(HelpMessage = "Specify a specific SQL Server.")]
    [string]$ServerRegex = ".*",
    
    [Parameter(HelpMessage = "Specify a specific SQL Database.")]
    [string]$DbaseRegex = ".*",

    [Parameter(HelpMessage = "Suppresses all informational output.")]
    [Switch]$Quiet
)

function Invoke-WrapperConfiguration {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Mode
    )
    
    if ($Quiet) { $VerbosePreference = 'SilentlyContinue' } else { $VerbosePreference = 'Continue' }
    
    # --- Script Variables ---
    $wrapperDllName = "msoledbsqlwrapper.dll" # The name of our compiled DLL
    $targetDllName  = "msoledbsql.dll" # The original MS DLL we are replacing
    $backupDllName  = "msoledbsql.original.dll" # The backup name for original MS DLL we are replacing
    
    $sysWow64Path  = Join-Path -Path $env:SystemRoot -ChildPath "SysWOW64"
    $targetDllPath = Join-Path -Path $sysWow64Path -ChildPath $targetDllName
    $backupDllPath = Join-Path -Path $sysWow64Path -ChildPath $backupDllName

    $configRegistryPath = "HKLM:\SOFTWARE\msoledbsqlwrapper"
    $expectedHash = "PASTE_NEW_32_BIT_SHA256_HASH_HERE"

    try {
        if ($Mode -eq 'Install') {
            Write-Verbose "Starting file-based hijack installation for $targetDllName..."

            # Step 1: Dependency Check
            Write-Verbose "Step 1: Dependency Check..."
            $vcRuntimePath = Join-Path -Path $sysWow64Path -ChildPath "msvcp140.dll"
            if (-not (Test-Path -Path $vcRuntimePath)) {
                throw "Dependency check failed. Please install the Microsoft Visual C++ Redistributable (x86)."
            }
            Write-Verbose "--> Dependencies verified."

            # Step 2: Decode & Verify DLL
            Write-Verbose "Step 2: Decode & Verify DLL..."
            $dllBytes = [System.Convert]::FromBase64String($embeddedDllBase64)
            $calculatedHash = (Get-FileHash -InputStream ([System.IO.MemoryStream]::new($dllBytes)) -Algorithm SHA256).Hash
            if (-not ($calculatedHash -eq $expectedHash)) {
                throw "File hash mismatch! Aborting installation."
            }
            Write-Verbose "--> File hash verified successfully."

            # Step 3: Rename the original DLL
            Write-Verbose "Step 3: Renaming up original DLL..."
            if (-not (Test-Path -Path $targetDllPath)) {
                throw "Original DLL not found at '$targetDllPath'. Cannot proceed."
            }
            if (Test-Path -Path $backupDllPath) {
                Write-Warning "Renamed file '$backupDllPath' already exists. Previous installation detected."
                throw "First uninstall previous installation using: .\Deploy-MsOleDbSqlWrapper.ps1 -Uninstall"
            }
            Rename-Item -Path $targetDllPath -NewName "$($targetDllName).original" -Force
            Write-Verbose "--> Original file renamed to '$($targetDllName).original'."

            # Step 4: Write our wrapper DLL as the original
            Write-Verbose "Step 4: Writing wrapper DLL to '$targetDllPath'..."
            [System.IO.File]::WriteAllBytes($targetDllPath, $dllBytes)
            Write-Verbose "--> Wrapper DLL successfully deployed."

            # Step 5: Set Configuration
            Write-Verbose "Step 5: Set Configuration..."
            if (-not (Test-Path -Path $configRegistryPath)) {
                New-Item -Path $configRegistryPath -Force | Out-Null
                Write-Verbose "--> Created registry key [$configRegistryPath]."
            }
            New-ItemProperty -Path $configRegistryPath -Name "DbaseRegex"     -Value $DbaseRegex  -PropertyType String -Force | Out-Null
            New-ItemProperty -Path $configRegistryPath -Name "ServerRegex"    -Value $ServerRegex -PropertyType String -Force | Out-Null
            New-ItemProperty -Path $configRegistryPath -Name "LoggingEnabled" -Value 0 -PropertyType DWord -Force | Out-Null
            Write-Verbose "--> Created registry key properties."

            Write-Verbose "Installation complete."
        }
        elseif ($Mode -eq 'Uninstall') {
            Write-Verbose "Starting uninstallation of file-based hijack..."
            
            # Step 1: Verify registry information exists
            Write-Verbose "Step 1: Verify registry information exists..."
            if (-not (Test-Path -Path $configRegistryPath)) {
                throw "Can not continue. Missing registry key [$configRegistryPath]."
            } else {
                Write-Verbose "--> found registry key [$configRegistryPath]."
            }
            $OriginalFileHash = Get-ItemPropertyValue -Path $configRegistryPath -Name 'OriginalFileHash'
            if ([string]::IsNullOrEmpty($OriginalFileHash)) {
                throw "Can not continue. Missing original file hash."
            } else {
                Write-Verbose "--> found original file hash: $OriginalFileHash"
            }

            # Step 2: Remove our wrapper DLL
            Write-Verbose "Step 2: Remove our wrapper DLL..."
            if (Test-Path -Path $targetDllPath) {
                # Verify it's not the original dll file before deleting
                $currentHash = (Get-FileHash -Path $targetDllPath -Algorithm SHA256).Hash
                if ($currentHash -ne $OriginalFileHash) {
                    Remove-Item -Path $targetDllPath -Force
                    Write-Verbose "--> Wrapper DLL removed."
                } else {
                    throw "Can not continue. The file at '$targetDllPath' is the original file."
                }
            }
            
            # Step 3: Rename the DLL
            Write-Verbose "Step 3: Rename the DLL..."
            if (Test-Path -Path $backupDllPath) {
                Rename-Item -Path $backupDllPath -NewName $targetDllName -Force
                Write-Verbose "--> Original DLL name restored."
            } else {
                throw "Original file '$backupDllPath' not found. Cannot rename original DLL."
            }
            
            # Step 4: Remove registry configuration
            Write-Verbose "Step 4: Remove registry configuration..."
            if (Test-Path -Path $configRegistryPath) {
                Remove-Item -Path $configRegistryPath -Recurse -Force
                Write-Verbose "--> Configuration registry key removed."
            }
            
            Write-Verbose "Uninstallation complete."
        }
        elseif ($Mode -eq 'Change') {
             if (-not (Test-Path -Path $configRegistryPath)) {
                throw "Registry key not found. Please install the wrapper first."
            }
            New-ItemProperty -Path $configRegistryPath -Name "DbaseRegex"     -Value $DbaseRegex  -PropertyType String -Force | Out-Null
            New-ItemProperty -Path $configRegistryPath -Name "ServerRegex"    -Value $ServerRegex -PropertyType String -Force | Out-Null
            New-ItemProperty -Path $configRegistryPath -Name "LoggingEnabled" -Value 0 -PropertyType DWord -Force | Out-Null
            Write-Verbose "--> Registry key properties updated successfully."
        }
    }
    catch {
        Write-Error "An error occurred: $($_.Exception.Message)"
    }
}

# --- Instructions for generating the embedded content ---
# To generate the string, use these PowerShell commands on the machine where you compiled the DLL:
#
# 1. Define the source file path:
#    $SourceFile = 'C:\path\to\your\repos\MsOleDbSqlWrapper\Win32\Release\msoledbsqlwrapper.dll'
#
# 2. Get the file hash and display it (copy this into the $expectedHash variable):
#    (Get-FileHash -Path $SourceFile -Algorithm SHA256).Hash | Set-Clipboard
#
# 3. Generate the Base64 string and copy it to the clipboard:
#    [System.Convert]::ToBase64String([System.IO.File]::ReadAllBytes($SourceFile), [System.Base64FormattingOptions]::InsertLineBreaks) | Set-Clipboard
#
# 4. Paste the clipboard content between the @" and "@ below.

$embeddedDllBase64 = @"
PASTE THE BASE64 STRING GENERATED FROM THE *32-BIT* msoledbsqlwrapper.dll HERE.
"@

# --- Execute the Main Function ---
if ($Uninstall) { Invoke-WrapperConfiguration -Mode 'Uninstall' }
elseif ($Change) { Invoke-WrapperConfiguration -Mode 'Change' }
else { Invoke-WrapperConfiguration -Mode 'Install' }
