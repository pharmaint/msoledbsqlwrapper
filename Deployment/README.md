# **MSOLEDBSQL Wrapper for MultiSubnetFailover Deployment**
## **1\. Deployment Instructions**

1. **Stop the target application/service** (e.g., the IrisServer service).  
2. Open a Command Prompt **as Administrator**.  
3. Navigate to the directory containing the original 32-bit driver: cd C:\\Windows\\SysWOW64\\  
4. **Back up the original driver** by renaming it:  
   ren msoledbsql.dll msoledbsql.original.dll

5. **Copy your compiled msoledbsqlwrapper.dll** into C:\\Windows\\SysWOW64\\.  
6. **Rename the wrapper** to match the original driver's name:  
   ren msoledbsqlwrapper.dll msoledbsql.dll

7. **Start the target application/service.** The wrapper is now active.

## **2\. Configuration via Registry**

The wrapper's behavior is controlled by values in the Windows Registry. Using regedit, create the following key:  
HKEY\_LOCAL\_MACHINE\\SOFTWARE\\msoledbsqlwrapper  
**Important:** Even though this is for a 32-bit application, you must create the key in the standard SOFTWARE hive, not SOFTWARE\\Wow6432Node. The wrapper code is specifically designed to look in the 64-bit registry view.

Within this key, you can create the following values:

| Name | Type | Data | Description |
| :---- | :---- | :---- | :---- |
| LoggingEnabled | REG\_DWORD | 1 to enable, 0 or missing to disable. | Controls whether the wrapper writes to the log files. |
| ServerRegex | REG\_SZ | A regular expression string (e.g., PROD-SQL-AG or .\*-AG). | The wrapper will only inject MultiSubnetFailover if the server name matches this regex. If this value is missing, all server names match. |
| DbaseRegex | REG\_SZ | A regular expression string (e.g., ^IrisDB$ or ^Iris.\*). | The wrapper will only inject MultiSubnetFailover if the database name matches this regex. If this value is missing, all database names match. |

## **3\. Logging**

When LoggingEnabled is set to 1, the wrapper will create two log files in C:\\Users\\Public\\Documents\\:

* dll\_load.log: Records when a process attaches to or detaches from the wrapper DLL.  
* oledb\_wrapper.log: Provides detailed information on the COM interception process, including whether regex conditions were met and if the MultiSubnetFailover property was injected.

These logs are essential for troubleshooting and verifying that the wrapper is functioning as expected.
