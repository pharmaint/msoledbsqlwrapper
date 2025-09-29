# **MSOLEDBSQL Wrapper for MultiSubnetFailover**

## **1\. Problem Statement**

Legacy 32-bit client-server software, such as **Iris ID Iris Access EAC**, often has hardcoded database connection settings. This becomes a problem when migrating the backend SQL database to a modern high-availability (HA) environment, such as a SQL Server Always On Availability Group configured with a multi-subnet failover cluster.

These modern SQL clusters require the client application's database driver to use the MultiSubnetFailover=true parameter in its connection string. This parameter enables the driver to attempt connections to all IP addresses of the availability group listener in parallel, ensuring a fast and transparent failover experience for the end-user.

The **IrisServer.exe** component is a 32-bit application and does not provide a method for administrators to modify its OLE DB connection string to add this required parameter. This project provides a solution by creating a wrapper DLL to inject this parameter transparently, without modifying the original application.

## **2\. The Solution**

This solution is a 32-bit C++ DLL project built with Visual Studio 2022\. It acts as a proxy (or "wrapper") for the original Microsoft OLE DB Driver for SQL Server (msoledbsql.dll).

The wrapper is deployed by renaming the original msoledbsql.dll and placing the compiled wrapper in its place. When the legacy application (IrisServer.exe) attempts to load the OLE DB driver, it loads our wrapper instead. The wrapper then:

1. Loads the original msoledbsql.original.dll into memory.  
2. Intercepts the COM object creation process.  
3. When the application sets the database connection properties, the wrapper intercepts this call.  
4. It intelligently injects the SSPROP\_INIT\_MULTISUBNETFAILOVER property with a boolean value of TRUE.  
5. It passes the modified properties on to the original OLE DB driver.

This process is entirely transparent to the client application, which connects to the database successfully, but now with the critical MultiSubnetFailover functionality enabled.

## **3\. Features**

* **Transparent Injection:** Adds MultiSubnetFailover functionality to any 32-bit application using the MSOLEDBSQL OLE DB driver without any changes to the application itself.  
* **Registry Controlled:** All features are configured via the Windows Registry, allowing for easy changes without recompiling the DLL.  
* **Conditional Logging:** Logging can be enabled or disabled via a registry key for easy debugging.  
* **Conditional Injection:** The MultiSubnetFailover injection can be targeted to specific database servers and/or database names using regular expressions. This provides granular control in environments with multiple databases.  
* **Memory Safe:** Uses deep copying for COM properties to avoid memory corruption or crashes.  
* **Handles Registry Redirection:** Correctly accesses the 64-bit view of the registry from a 32-bit process to read its configuration, avoiding Wow6432Node redirection issues.

## **4\. How to Build the Project**

1. **Open Visual Studio 2022\.**  
2. Create a new C++ **Dynamic-Link Library (DLL)** project. Name it msoledbsqlwrapper.  
3. Add the source files (msoledbsqlwrapper.cpp, framework.h, msoledbsqlwrapper.def, msoledbsqlwrapper.rc) provided in this solution to your project.  
4. **Crucially**, set the build configuration to **Win32** (or x86), as the target application is 32-bit.  
5. In project properties \-\> **Linker** \-\> **Input**, set the **Module Definition File** to msoledbsqlwrapper.def.  
6. In project properties \-\> **C/C++** \-\> **Precompiled Headers**, set **Precompiled Header** to "Not Using Precompiled Headers".  
7. Build the solution. This will generate msoledbsqlwrapper.dll.

## **5\. Deployment Instructions**

1. **Stop the target application/service** (e.g., the IrisServer service).  
2. Open a Command Prompt **as Administrator**.  
3. Navigate to the directory containing the original 32-bit driver: cd C:\\Windows\\SysWOW64\\  
4. **Back up the original driver** by renaming it:  
   ren msoledbsql.dll msoledbsql.original.dll

5. **Copy your compiled msoledbsqlwrapper.dll** into C:\\Windows\\SysWOW64\\.  
6. **Rename the wrapper** to match the original driver's name:  
   ren msoledbsqlwrapper.dll msoledbsql.dll

7. **Start the target application/service.** The wrapper is now active.

## **6\. Configuration via Registry**

The wrapper's behavior is controlled by values in the Windows Registry. Using regedit, create the following key:  
HKEY\_LOCAL\_MACHINE\\SOFTWARE\\msoledbsqlwrapper  
**Important:** Even though this is for a 32-bit application, you must create the key in the standard SOFTWARE hive, not SOFTWARE\\Wow6432Node. The wrapper code is specifically designed to look in the 64-bit registry view.

Within this key, you can create the following values:

| Name | Type | Data | Description |
| :---- | :---- | :---- | :---- |
| LoggingEnabled | REG\_DWORD | 1 to enable, 0 or missing to disable. | Controls whether the wrapper writes to the log files. |
| ServerRegex | REG\_SZ | A regular expression string (e.g., PROD-SQL-AG or .\*-AG). | The wrapper will only inject MultiSubnetFailover if the server name matches this regex. If this value is missing, all server names match. |
| DbaseRegex | REG\_SZ | A regular expression string (e.g., ^IrisDB$ or ^Iris.\*). | The wrapper will only inject MultiSubnetFailover if the database name matches this regex. If this value is missing, all database names match. |

## **7\. Logging**

When LoggingEnabled is set to 1, the wrapper will create two log files in C:\\Users\\Public\\Documents\\:

* dll\_load.log: Records when a process attaches to or detaches from the wrapper DLL.  
* oledb\_wrapper.log: Provides detailed information on the COM interception process, including whether regex conditions were met and if the MultiSubnetFailover property was injected.

These logs are essential for troubleshooting and verifying that the wrapper is functioning as expected.
