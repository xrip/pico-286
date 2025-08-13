# Network Redirector Documentation

Based on RBIL6 (Ralph Brown's Interrupt List) specifications and implementation analysis of Pico-286 network redirector functions.

## Overview

The network redirector implements DOS INT 2Fh network file system calls, providing filesystem passthrough functionality for DOS applications. It enables DOS programs to access files on the host filesystem as if they were on a network drive.

**Supported Platforms:**
- **Host builds** (`network-redirector.c.inl`): Uses standard C library (Windows/Linux)
- **RP2350 builds** (`network-redirector-rp2350.c.inl`): Uses FatFS library for SD card access

**Base Directory Mapping:**
- Host builds: `C:\FASM` 
- RP2350 builds: `\XT\` (on SD card)

## Architecture

### DOS Integration
- **INT 2Fh Handler**: Intercepts DOS kernel network redirector calls
- **SDA Access**: Uses Swappable Data Area for filename and DTA pointers
- **SFT Management**: Maintains System File Table entries for open files
- **Error Handling**: Converts filesystem errors to standard DOS error codes

### Data Structures

#### System File Table (SFT) Entry
```c
typedef struct __attribute__((packed)) {
    uint16_t total_handles;
    uint16_t open_mode;        // File access mode
    uint8_t attribute;         // File attributes
    uint16_t device_info;      // Device information (0x8040 | 'H')
    uint32_t unk0;
    uint16_t file_handle;      // Internal file handle
    uint16_t file_time;        // File time stamp
    uint16_t file_date;        // File date stamp
    uint32_t file_size;        // File size in bytes
    uint32_t file_position;    // Current file position
    uint16_t unk1, unk2, unk3;
    uint8_t unk4;
    char file_name[11];        // DOS 8.3 format filename
} sftstruct;
```

#### Search Data Block (SDB)
```c
typedef struct __attribute__((packed)) {
    unsigned char drive_letter;     // Drive letter with bit 7 set for network
    unsigned char srch_tmpl[11];    // Search template (8.3 format)
    unsigned char srch_attr;        // Search attributes
    unsigned short dir_entry;       // Directory entry number
    unsigned short par_clstr;       // Parent cluster
    unsigned char f1[4];            // Reserved
    foundfilestruct foundfile;      // Found file information
} sdbstruct;
```

#### Found File Structure
```c
typedef struct {
    unsigned char fname[11];        // Filename in 8.3 format
    unsigned char fattr;            // File attributes (RO=1, HID=2, SYS=4, VOL=8, DIR=16, ARCH=32)
    unsigned char f1[10];           // Reserved
    unsigned short time_lstupd;     // Last update time (hhhhhmmm mmmsssss)
    unsigned short date_lstupd;     // Last update date (YYYYYYYM MMMDDDDD)
    unsigned short start_clstr;     // Starting cluster
    unsigned long fsize;            // File size
} foundfilestruct;
```

## Implemented Functions

### AX=1100h - Installation Check
**Purpose**: Check if network redirector is installed  
**Input**: AX=1100h  
**Output**: 
- AL=FFh if installed
- AH=50h (Product identifier 'P' for Pico-286)

**RBIL6 Compliance**: ✅ Correctly sets product identifier

---

### AX=1101h - Remove Remote Directory
**Purpose**: Delete a directory  
**Input**: 
- AX=1101h
- SDA+9Eh → Directory path

**Output**:
- CF=0 if success
- CF=1 if error, AX=DOS error code

**Implementation**: Converts FatFS/system errors to DOS error codes

---

### AX=1103h - Create Remote Directory
**Purpose**: Create a new directory  
**Input**:
- AX=1103h
- SDA+9Eh → Directory path

**Output**:
- CF=0 if success  
- CF=1 if error, AX=DOS error code

---

### AX=1105h - Change Remote Directory
**Purpose**: Change current working directory  
**Input**:
- AX=1105h
- SDA+9Eh → New directory path

**Output**:
- CF=0 if success
- Sets internal current_remote_dir variable

**Path Handling**:
- `\` → Root directory
- `\path` → Absolute path from root
- `path` → Relative path

---

### AX=1106h - Close Remote File
**Purpose**: Close an open file handle  
**Input**:
- AX=1106h
- ES:DI → SFT entry

**Output**:
- CF=0 if success
- CF=1 if error, AX=6 (Invalid handle)

**Implementation**: Frees file handle and closes underlying file

---

### AX=1107h - Commit Remote File
**Purpose**: Flush file buffers to disk  
**Input**:
- AX=1107h
- ES:DI → SFT entry

**Output**:
- CF=0 if success
- CF=1 if error, AX=6 (Invalid handle)

---

### AX=1108h - Read Remote File
**Purpose**: Read data from file  
**Input**:
- AX=1108h
- ES:DI → SFT entry
- CX = bytes to read
- DTA → user buffer

**Output**:
- CF=0 if success, CX=bytes actually read
- CF=1 if error, AX=error code
- Updates SFT file_position

**Implementation**: 
- Seeks to SFT position before reading
- Uses chunked reading for large transfers (RP2350)
- Updates file position after read

---

### AX=1109h - Write Remote File
**Purpose**: Write data to file  
**Input**:
- AX=1109h
- ES:DI → SFT entry  
- CX = bytes to write
- DTA → user buffer

**Output**:
- CF=0 if success, AX=bytes written
- CF=1 if error, AX=error code
- Updates SFT file_position

**Implementation**:
- Seeks to SFT position before writing
- Uses chunked writing for large transfers (RP2350)
- Auto-flushes after write

---

### AX=110Ah - Lock/Unlock File Region
**Purpose**: File locking (stub implementation)  
**Input**: AX=110Ah  
**Output**: CF=0 (always success)

**Note**: No-op implementation - file locking not supported

---

### AX=110Ch - Get Remote Disk Information
**Purpose**: Return disk space information  
**Output**:
- AX, BX, CX, DX = 512 (stub values)
- CF=0

**Note**: Returns fixed values - not actual disk information

---

### AX=110Eh - Set Remote File Attributes
**Purpose**: Change file attributes (stub)  
**Output**: CF=0 (always success)

**Note**: No-op implementation

---

### AX=110Fh - Get Remote File Attributes
**Purpose**: Get file attributes and size  
**Input**:
- AX=110Fh
- SDA+9Eh → filename

**Output** (CF=0 if success):
- AX = file attributes
- BX:DI = file size (BX=high word, DI=low word)
- CX = file time
- DX = file date

**RBIL6 Compliance**: ✅ Correct register layout

**DOS Attributes**:
- 01h = Read-only
- 02h = Hidden  
- 04h = System
- 10h = Directory
- 20h = Archive

---

### AX=1111h - Rename Remote File
**Purpose**: Rename/move a file  
**Input**:
- AX=1111h
- SDA+9Eh → old filename
- SDA+16Ah → new filename

**Output**:
- CF=0 if success
- CF=1 if error, AX=DOS error code

---

### AX=1113h - Delete Remote File
**Purpose**: Delete a file  
**Input**:
- AX=1113h
- SDA+9Eh → filename

**Output**:
- CF=0 if success
- CF=1 if error, AX=DOS error code

---

### AX=1116h - Open Remote File
**Purpose**: Open existing file for read/write  
**Input**:
- AX=1116h
- ES:DI → uninitialized SFT
- SDA+9Eh → filename

**Output**:
- CF=0 if success, SFT filled
- CF=1 if error, AX=error code

**Implementation**:
- Tries read/write mode first, falls back to read-only
- Fills SFT with file information
- Converts filename to DOS 8.3 format

---

### AX=1117h - Create Remote File
**Purpose**: Create new file or truncate existing  
**Input**:
- AX=1117h
- ES:DI → uninitialized SFT
- SDA+9Eh → filename

**Output**:
- CF=0 if success, SFT filled
- CF=1 if error, AX=error code

**Implementation**: Creates file in write mode with truncation

---

### AX=111Bh - Find First File
**Purpose**: Start file search operation  
**Input**:
- AX=111Bh
- SDA+9Eh → search pattern
- DTA → 21-byte search data block

**Output**:
- CF=0 if file found, DTA updated
- CF=1 if no files, AX=18

**RBIL6 Compliance**: ✅ Sets bit 7 in DTA first byte

**Implementation**:
- Parses search pattern and directory
- Initializes static DIR structure
- Updates SDB in DTA with found file info

---

### AX=111Ch - Find Next File
**Purpose**: Continue file search operation  
**Input**:
- AX=111Ch
- DTA → search data from Find First

**Output**:
- CF=0 if file found, DTA updated  
- CF=1 if no more files, AX=18

**RBIL6 Compliance**: ✅ Preserves bit 7 in DTA first byte

**Implementation**: Uses static DIR structure from Find First

---

### AX=1120h - Flush All Remote Disk Buffers
**Purpose**: Flush all open file buffers  
**Input**: AX=1120h  
**Output**: CF=0

**Implementation**: Flushes all open file handles

---

### AX=1121h - Seek from End of File
**Purpose**: Seek to position relative to file end  
**Input**:
- AX=1121h
- ES:DI → SFT entry
- CX:DX = signed offset from end

**Output**:
- CF=0 if success, DX:AX = new position
- CF=1 if error, AX=6

**Implementation**: 
- Gets file size, adds offset
- Clamps position to >= 0
- Updates SFT file_position

---

## Error Handling

### FatFS to DOS Error Code Mapping (RP2350)
```c
FR_OK                 → 0   (Success)
FR_NO_FILE           → 2   (File not found)  
FR_NO_PATH           → 3   (Path not found)
FR_TOO_MANY_OPEN_FILES → 4   (Too many open files)
FR_DENIED            → 5   (Access denied)
FR_INVALID_OBJECT    → 6   (Invalid handle)
FR_NOT_ENOUGH_CORE   → 8   (Insufficient memory)
FR_INVALID_DRIVE     → 15  (Invalid drive)
FR_WRITE_PROTECTED   → 19  (Write protect error)
FR_NOT_READY         → 21  (Drive not ready)
FR_DISK_ERR          → 29  (General failure)
FR_LOCKED            → 33  (Lock violation)
FR_EXIST             → 80  (File exists)
```

### Standard DOS Error Codes
- 0 = Success
- 2 = File not found
- 3 = Path not found  
- 4 = Too many open files
- 5 = Access denied
- 6 = Invalid handle
- 8 = Insufficient memory
- 18 = No more files
- Others = Various system errors

## Platform Differences

| Feature | Host Build | RP2350 Build |
|---------|------------|--------------|
| Filesystem | C stdlib (fopen, etc.) | FatFS library |
| Base Path | `C:\FASM` | `\XT\` (SD card) |
| Path Separator | `\` (Windows) | `/` (converted) |
| Memory Access | Direct RAM access | read86/write86 functions |
| File Finding | `_findfirst/_findnext` (Windows) | `f_findfirst/f_findnext` |
| Error Codes | errno → DOS codes | FatFS → DOS codes |

## Usage Notes

1. **SDA Address**: Set automatically on first Installation Check call
2. **File Handles**: Limited to 32 concurrent open files
3. **Path Conversion**: Automatic conversion between DOS and host path formats
4. **8.3 Filenames**: Long filenames converted to DOS 8.3 format in SFT
5. **Network Drive**: Appears as drive 'H:' with network attribute
6. **Search Patterns**: Supports DOS wildcards (* and ?) in file searches
7. **Case Handling**: Filenames converted to uppercase for DOS compatibility

## Compliance Status

✅ **Fully Compliant Functions**: Installation Check, Find First/Next, Get File Attributes  
⚠️ **Stub Implementations**: Lock/Unlock, Set Attributes, Get Disk Info  
✅ **Error Handling**: Proper DOS error codes and carry flag usage  
✅ **Data Structures**: SFT, SDB, and found file structures match DOS layout

The implementation provides a functional network redirector that enables DOS applications to seamlessly access host filesystem resources through the standard DOS file API.