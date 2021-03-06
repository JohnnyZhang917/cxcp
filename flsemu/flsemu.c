/*
 * BlueParrot XCP
 *
 * (C) 2007-2019 by Christoph Schueler <github.com/Christoph2,
 *                                      cpu12.gems@googlemail.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * s. FLOSS-EXCEPTION.txt
 */

#include <assert.h>
#include <string.h>

#include "flsemu.h"


/*
**  Local Defines.
*/
#define ERASED_VALUE (0xff) /**< Value of an erased Flash/EEPROM cell. */

/*
**  Local Function-like Macros.
*/
#define FLSEMU_ASSERT_INITIALIZED() assert(FlsEmu_ModuleState == FLSEMU_INIT)
#define VALIDATE_SEGMENT_IDX(idx)    ((idx) < FlsEmu_Config->numSegments)

/*
**  Local Types.
*/

/** @brief State of flash-emulator module.
 *
 *
 */
typedef enum tagFlsEmu_ModuleStateType {
    FLSEMU_UNINIT,
    FLSEMU_INIT
} FlsEmu_ModuleStateType;


/** @brief Information about your system memory.
 *
 *
 */
typedef struct tagFlsEmu_SystemMemoryType {
    DWORD pageSize;
    DWORD allocationGranularity;
} FlsEmu_SystemMemoryType;

/*
**  Local Function Prototypes.
*/
static void FlsEmu_OpenCreate(uint8_t segmentIdx);
static FlsEmu_OpenCreateResultType FlsEmu_OpenCreatePersitentArray(char const * fileName, DWORD size, PersistentArrayType * persistentArray);
static bool FlsEmu_Flush(uint8_t segmentIdx);
static void FlsEmu_Close(uint8_t segmentIdx);
static void FlsEmu_CloseFileView(Xcp_HwFileViewType * fileView);
static void FlsEmu_ClosePersitentArray(PersistentArrayType const * persistentArray);
static bool FlsEmu_MapView(FlsEmu_SegmentType * config, uint32_t offset, uint32_t length);
//static void FlsEmu_SelectBlock(uint8_t segmentIdx, uint8_t block);
static DWORD GetPageSize(void);
static DWORD GetAllocationGranularity(void);
static void MemoryInfo(void * address);
static HANDLE OpenCreateFile(char const * fileName, bool create);
static bool CreateFileView(HANDLE handle, DWORD length, Xcp_HwFileViewType * fileView);
/*
**  Local Variables.
*/
static FlsEmu_ModuleStateType FlsEmu_ModuleState = FLSEMU_UNINIT; /**< Module-state variable. */
static FlsEmu_SystemMemoryType FlsEmu_SystemMemory;     /**< System memory configuration. */
static FlsEmu_ConfigType const * FlsEmu_Config = NULL;  /**< Segment configuration. */

/*
**  Global Functions.
*/

/** @brief Initializes flash-emulator system.
 *
 *
 */
void FlsEmu_Init(FlsEmu_ConfigType const * config)
{
    uint8_t idx;

    FlsEmu_SystemMemory.allocationGranularity = GetAllocationGranularity();
    FlsEmu_SystemMemory.pageSize = GetPageSize();
    FlsEmu_Config = config;
    FlsEmu_ModuleState = FLSEMU_INIT;
    for (idx = 0; idx < FlsEmu_Config->numSegments; ++idx) {
        //printf("FlsEmu_Init-SEG-NAME: %s\n", FlsEmu_Config->segments[idx]->name);
        FlsEmu_OpenCreate(idx);
    }

}

void FlsEmu_DeInit(void)
{
    uint8_t idx;

    for (idx = 0; idx < FlsEmu_Config->numSegments; ++idx) {
        //printf("UNLOAD-SEG-NAME: %s\n", FlsEmu_Config->segments[idx]->name);
        FlsEmu_Close(idx);
    }
    FlsEmu_ModuleState = FLSEMU_UNINIT;
}

static void FlsEmu_OpenCreate(uint8_t segmentIdx)
{
    int length;
    char adm[1024];
    char rom[1024];
    FlsEmu_SegmentType * segment;
    PersistentArrayType temp;
    FlsEmu_OpenCreateResultType result;

    FLSEMU_ASSERT_INITIALIZED();
    if (!VALIDATE_SEGMENT_IDX(segmentIdx)) {
        return;
    }
    segment = FlsEmu_Config->segments[segmentIdx];
    segment->persistentArray = (PersistentArrayType *)malloc(sizeof(PersistentArrayType));
    segment->currentPage = 0x00;
    length = strlen(segment->name);
    strncpy((char *)adm, (char *)segment->name, length);
    adm[length] = '\x00';
    strcat((char *)adm, ".adm");
    strncpy((char *)rom, (char *)segment->name, length);
    rom[length] = '\x00';
    strcat((char *)rom, ".rom");

    result = FlsEmu_OpenCreatePersitentArray(rom, segment->memSize, segment->persistentArray);
    if (result == OPEN_ERROR) {

    } else if (result == NEW_FILE) {
        FillMemory(segment->persistentArray->mappingAddress, segment->memSize, ERASED_VALUE);
    }

    result = FlsEmu_OpenCreatePersitentArray(adm, sizeof(FlsEmu_SegmentType) + (segment->memSize * sizeof(uint32_t)), &temp);
    if (result == OPEN_ERROR) {
    } else if (result == NEW_FILE) {
        CopyMemory(temp.mappingAddress, segment, sizeof(FlsEmu_SegmentType));
        SecureZeroMemory((uint8_t *)temp.mappingAddress + sizeof(FlsEmu_SegmentType), sizeof(uint32_t));
    }
}

/** @brief Flush memory arrray and close file.
 *
 * @param segmentIdx
 *
 */
static void FlsEmu_Close(uint8_t segmentIdx)
{
    FlsEmu_SegmentType const * segment;

    FLSEMU_ASSERT_INITIALIZED();
    if (!VALIDATE_SEGMENT_IDX(segmentIdx)) {
        return;
    }
    segment = FlsEmu_Config->segments[segmentIdx];
    FlsEmu_Flush(segmentIdx);
    FlsEmu_ClosePersitentArray(segment->persistentArray);
    free(segment->persistentArray);
}

/** @brief Flushes, i.e. writes data to disk.
 *
 * @param segmentIdx
 * @return TRUE in successful otherwise FALSE.
 *
 */
static bool FlsEmu_Flush(uint8_t segmentIdx)
{
    MEMORY_BASIC_INFORMATION  info;
    FlsEmu_SegmentType const * segment;

    FLSEMU_ASSERT_INITIALIZED();

    if (!VALIDATE_SEGMENT_IDX(segmentIdx)) {
        return FALSE;
    }
    segment = FlsEmu_Config->segments[segmentIdx];

    if (VirtualQuery(segment->persistentArray->mappingAddress, &info, sizeof(MEMORY_BASIC_INFORMATION)) == 0) {
        Win_ErrorMsg("FlsEmu_Flush::VirtualQuery()", GetLastError());
        return FALSE;
    }

    if (!FlushViewOfFile(segment->persistentArray->mappingAddress, info.RegionSize)) {
        Win_ErrorMsg("FlsEmu_Flush::FlushViewOfFile()", GetLastError());
        return FALSE;
    }

    if (!FlushFileBuffers(segment->persistentArray->fileHandle)) {
        Win_ErrorMsg("FlsEmu_Flush::FlushFileBuffers()", GetLastError());
        return FALSE;
    }

    return TRUE;
}

void * FlsEmu_BasePointer(uint8_t segmentIdx)
{
    FlsEmu_SegmentType const * segment;
    FLSEMU_ASSERT_INITIALIZED();

    if (!VALIDATE_SEGMENT_IDX(segmentIdx)) {
        return (void*)NULL;
    }
    segment = FlsEmu_Config->segments[segmentIdx];

    return segment->persistentArray->mappingAddress;
}

void FlsEmu_SelectPage(uint8_t segmentIdx, uint8_t page)
{
    uint32_t offset;
    FlsEmu_SegmentType * segment;

    FLSEMU_ASSERT_INITIALIZED();
    if (!VALIDATE_SEGMENT_IDX(segmentIdx)) {
        return;
    }
    segment = FlsEmu_Config->segments[segmentIdx];
    if (segment->persistentArray->currentPage == page) {
        return; /* Nothing to do. */
    }
    offset = (segment->pageSize * page);
    if (FlsEmu_MapView(segment, offset, segment->pageSize)) {
        segment->currentPage = page;
    }
}

#if 0
static void FlsEmu_SelectBlock(uint8_t segmentIdx, uint8_t block)
{
    uint32_t offset, blockSize;
    FlsEmu_SegmentType * segment;

    FLSEMU_ASSERT_INITIALIZED();
    if (!VALIDATE_SEGMENT_IDX(segmentIdx)) {
        return;
    }
    segment = FlsEmu_Config->segments[segmentIdx];
    blockSize = (segment->memSize / segment->blockCount);
    offset = (blockSize * block);

     if (FlsEmu_MapView(segment, offset, blockSize)) {

    }
}
#endif

void FlsEmu_EraseSector(uint8_t segmentIdx, uint32_t address)
{
    uint32_t mask;
    uint16_t * ptr;
    FlsEmu_SegmentType const * segment;

    FLSEMU_ASSERT_INITIALIZED();
    if (!VALIDATE_SEGMENT_IDX(segmentIdx)) {
        return;
    }
    segment = FlsEmu_Config->segments[segmentIdx];
    mask = (uint32_t)segment->sectorSize - 1UL;
    ptr = (uint16_t *)segment->persistentArray->mappingAddress;
    if ((address & mask) != 0UL) {
        // TODO: warn misalignment.
        // ("address (%#X) should be aligned to %u-byte sector boundary.", address, segment->sectorSize)
    }

    FillMemory(ptr + (address & ~mask), segment->sectorSize, ERASED_VALUE);
}


void FlsEmu_ErasePage(uint8_t segmentIdx, uint8_t page)
{
    uint8_t * ptr = (uint8_t * )FlsEmu_BasePointer(segmentIdx);
    FlsEmu_SegmentType * segment;

    FLSEMU_ASSERT_INITIALIZED();
    if (!VALIDATE_SEGMENT_IDX(segmentIdx)) {
        return;
    }
    segment = FlsEmu_Config->segments[segmentIdx];
    FillMemory(ptr + (segment->pageSize * page), segment->pageSize, ERASED_VALUE);
    segment->currentPage = page;
}


void FlsEmu_EraseBlock(uint8_t segmentIdx, uint16_t block)
{
    /* TODO: Nur den entsprechenden Block mappen!!! */
    uint8_t * ptr;
    uint32_t offset;
    uint32_t blockSize;
    FlsEmu_SegmentType * segment;

    FLSEMU_ASSERT_INITIALIZED();
    if (!VALIDATE_SEGMENT_IDX(segmentIdx)) {
        return;
    }
    segment = FlsEmu_Config->segments[segmentIdx];
    assert(block < (segment->blockCount));

    blockSize = (segment->memSize / segment->blockCount);
    offset = (blockSize * block);

    ptr = (uint8_t * )FlsEmu_BasePointer(segmentIdx) + offset;

    FillMemory(ptr, blockSize, ERASED_VALUE);
}


#if 0
bool XcpOw_MapFileOpen(char const * fname, Xcp_HwMapFileType * mf)
{
    LARGE_INTEGER size;

    mf->handle = OpenCreateFile(fname, FALSE);
    if (mf->handle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    GetFileSizeEx(mf->handle, &size);

    mf->size = size.QuadPart;

    if (!CreateFileView(mf->handle, size.QuadPart, &mf->view)) {
        return FALSE;
    }
    return TRUE;
}

void XcpOw_MapFileClose(Xcp_HwMapFileType const * mf)
{
    CloseHandle(mf->view.mappingHandle);
    CloseHandle(mf->handle);
}
#endif  // 0


/*
**  Local Functions.
*/
static bool FlsEmu_MapView(FlsEmu_SegmentType * config, uint32_t offset, uint32_t length)
{
    DWORD error;

    FLSEMU_ASSERT_INITIALIZED();
    assert((offset % FlsEmu_SystemMemory.pageSize) == 0);   /*  Offset must be a multiple of the allocation granularity! */

    error = UnmapViewOfFile(config->persistentArray->mappingAddress);
    if (error == 0UL) {
        Win_ErrorMsg("FlsEmu_MapView::MapViewOfFile()", GetLastError());
        CloseHandle(config->persistentArray->mappingHandle);
        return FALSE;
    }

    config->persistentArray->mappingAddress = (void *)MapViewOfFile(config->persistentArray->mappingHandle, FILE_MAP_ALL_ACCESS, 0, offset, length);
    if (config->persistentArray->mappingAddress == NULL) {
        Win_ErrorMsg("FlsEmu_MapView::MapViewOfFile()", GetLastError());
        CloseHandle(config->persistentArray->mappingHandle);
        return FALSE;
    }
    return TRUE;
}

static FlsEmu_OpenCreateResultType FlsEmu_OpenCreatePersitentArray(char const * fileName, DWORD size, PersistentArrayType * persistentArray)
{
    DWORD error;
    Xcp_HwFileViewType fileView;
    FlsEmu_OpenCreateResultType result;
    bool newFile = FALSE;

    FLSEMU_ASSERT_INITIALIZED();
    persistentArray->fileHandle = OpenCreateFile(fileName, FALSE);
    if (persistentArray->fileHandle == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            newFile = TRUE;
            persistentArray->fileHandle = OpenCreateFile(fileName, TRUE);
            if (persistentArray->fileHandle == INVALID_HANDLE_VALUE) {
                return OPEN_ERROR;
            }
        } else {
            return OPEN_ERROR;
        }
    }

    if (!CreateFileView(persistentArray->fileHandle, size, &fileView)) {
        return OPEN_ERROR;
    }

    persistentArray->mappingAddress = fileView.mappingAddress;
    persistentArray->mappingHandle  = fileView.mappingHandle;

    MemoryInfo(persistentArray->mappingAddress);

    if (newFile) {
        result = NEW_FILE;
    } else {
        result = OPEN_EXSISTING;
    }
    return result;
}

static void FlsEmu_ClosePersitentArray(PersistentArrayType const * persistentArray)
{
    FLSEMU_ASSERT_INITIALIZED();
    UnmapViewOfFile(persistentArray->mappingAddress);
    CloseHandle(persistentArray->mappingHandle);
    CloseHandle(persistentArray->fileHandle);
}

/*
**  Wrappers for Windows Functions.
*/
static DWORD GetPageSize(void)
{
    SYSTEM_INFO info;

    GetSystemInfo(&info);
    return info.dwPageSize;
}

static DWORD GetAllocationGranularity(void)
{
    SYSTEM_INFO info;

    GetSystemInfo(&info);
    return info.dwAllocationGranularity;
}

#if 0
static bool FileExits(char * const name)
{
    DWORD attribs = GetFileAttributes(name);

    return (attribs != INVALID_FILE_ATTRIBUTES) && !(attribs & FILE_ATTRIBUTE_DIRECTORY);
}
#endif // 0

/** @brief Open or create a file.
 *
 * @param fileName
 * @param create
 * @return HANDLE of file
 *
 */
static HANDLE OpenCreateFile(char const * fileName, bool create)
{
    HANDLE handle;
    DWORD dispoition = (create == TRUE) ? CREATE_NEW : OPEN_EXISTING;

    handle = CreateFile(fileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        (LPSECURITY_ATTRIBUTES)NULL, dispoition, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, (HANDLE)NULL
    );
    if (handle == INVALID_HANDLE_VALUE) {
        Win_ErrorMsg("OpenCreateFile::CreateFile()", GetLastError());
    }

    return handle;
}

static bool CreateFileView(HANDLE handle, DWORD length, Xcp_HwFileViewType * fileView)
{
    fileView->mappingHandle = CreateFileMapping(handle, (LPSECURITY_ATTRIBUTES)NULL, PAGE_READWRITE, (DWORD)0, (DWORD)length, NULL);
    if (fileView->mappingHandle == NULL) {
        return FALSE;
    }

    /* TODO: Refactor to function; s. FlsEmu_MapView() */
    fileView->mappingAddress = (void *)MapViewOfFile(fileView->mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, length);
    if (fileView->mappingAddress == NULL) {
        Win_ErrorMsg("CreateFileView::MapViewOfFile()", GetLastError());
        CloseHandle(fileView->mappingHandle);
        return FALSE;
    }

    return TRUE;
}

static void MemoryInfo(void * address)
{
    MEMORY_BASIC_INFORMATION info;

    VirtualQuery(address, &info, sizeof(MEMORY_BASIC_INFORMATION));
}


static void FlsEmu_CloseFileView(Xcp_HwFileViewType * fileView)
{
    CloseHandle(fileView->mappingHandle);
}


void FlsEmu_Info(void)
{
    int idx;
    uint8_t * ptr;
    FlsEmu_SegmentType * segment;

    printf("\nFlash-Emulator\n");
    printf("--------------\n");
    printf("Segment              Mapped     Virtual    Size(KB) P-Size(KB) #Pages\n");
    for (idx = 0; idx < FlsEmu_Config->numSegments; ++idx) {
        ptr = FlsEmu_BasePointer(idx);
        segment = FlsEmu_Config->segments[idx];
        printf("%-20.20s 0x%p 0x%p %8d       %4d %6d\n", segment->name, segment->baseAddress, ptr, segment->memSize / 1024, segment->pageSize / 1024, segment->memSize / segment->pageSize);
    }
    printf("\n");

}
