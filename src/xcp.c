/*
 * pySART - Simplified AUTOSAR-Toolkit for Python.
 *
 * (C) 2007-2018 by Christoph Schueler <github.com/Christoph2,
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

#include "xcp.h"

#include <stdio.h>
//#include <stdlib.h>

/*
** Private Options.
*/
#define XCP_ENABLE_STD_COMMANDS         XCP_ON
#define XCP_ENABLE_INTERLEAVED_MODE     XCP_OFF

#define XCP_DRIVER_VERSION              (10)


/*
** Local Types.
*/
typedef struct tagXcp_StateType {
    bool connected;
    bool busy;
#if XCP_ENABLE_DAQ_COMMANDS == XCP_ON
    Xcp_DaqProcessorType daqProcessor;
    Xcp_DaqPointerType daqPointer;
#endif // XCP_ENABLE_DAQ_COMMANDS
#if XCP_TRANSPORT_LAYER_COUNTER_SIZE != 0
    uint16_t counter;
#endif // XCP_TRANSPORT_LAYER_COUNTER_SIZE
    bool programming;
    uint8_t mode;
    uint8_t protection;
    Xcp_MtaType mta;
} Xcp_StateType;


/*
**  Global Variables.
*/
Xcp_PDUType Xcp_PduIn;
Xcp_PDUType Xcp_PduOut;


/*
** Local Variables.
*/
static Xcp_ConnectionStateType Xcp_ConnectionState = XCP_DISCONNECTED;
static Xcp_StateType Xcp_State;

static Xcp_SendCalloutType Xcp_SendCallout = (Xcp_SendCalloutType)NULL;
static const Xcp_StationIDType Xcp_StationID = { (uint16_t)sizeof(XCP_STATION_ID), (uint8_t const *)XCP_STATION_ID };

void Xcp_WriteMemory(void * dest, void * src, uint16_t count);
void Xcp_ReadMemory(void * dest, void * src, uint16_t count);


#define XCP_POSITIVE_RESPONSE() Xcp_Send8(UINT8(1), UINT8(0xff), UINT8(0), UINT8(0), UINT8(0), UINT8(0), UINT8(0), UINT8(0), UINT8(0))
#define XCP_ERROR_RESPONSE(ec) Xcp_Send8(UINT8(2), UINT8(0xfe), UINT8(ec), UINT8(0), UINT8(0), UINT8(0), UINT8(0), UINT8(0), UINT8(0))

#define XCP_COMMAND     (cmoIn->data[0])

#define DATA_IN(idx)    (cmoIn->data[(idx)])
#define DATA_OUT(idx)   (cmoOut->data[(idx)])

#define COUNTER_IN      (cmoIn->data[1])
#define COUNTER_OUT     (cmoOut->data[2])


/*
** Local Function Prototypes.
*/

static void Xcp_SendResult(Xcp_ReturnType result);
static void Xcp_CommandNotImplemented_Res(Xcp_PDUType const * const pdu);
static void Xcp_Connect_Res(Xcp_PDUType const * const pdu);
static void Xcp_Disconnect_Res(Xcp_PDUType const * const pdu);
static void Xcp_GetStatus_Res(Xcp_PDUType const * const pdu);
static void Xcp_Synch_Res(Xcp_PDUType const * const pdu);
#if XCP_ENABLE_GET_COMM_MODE_INFO == XCP_ON
static void Xcp_GetCommModeInfo_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_COMM_MODE_INFO
#if XCP_ENABLE_GET_ID == XCP_ON
static void Xcp_GetId_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_ID
#if XCP_ENABLE_SET_REQUEST == XCP_ON
static void Xcp_SetRequest_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_SET_REQUEST
#if XCP_ENABLE_GET_SEED == XCP_ON
static void Xcp_GetSeed_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_SEED
#if XCP_ENABLE_UNLOCK == XCP_ON
static void Xcp_Unlock_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_UNLOCK
#if XCP_ENABLE_SET_MTA == XCP_ON
static void Xcp_SetMta_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_SET_MTA
#if XCP_ENABLE_UPLOAD == XCP_ON
static void Xcp_Upload_Res(Xcp_PDUType const * const pdu);
#endif
#if XCP_ENABLE_SHORT_UPLOAD == XCP_ON
static void Xcp_ShortUpload_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_SHORT_UPLOAD
#if XCP_ENABLE_BUILD_CHECKSUM == XCP_ON
static void Xcp_BuildChecksum_Res(Xcp_PDUType const * const pdu);
#endif
#if XCP_ENABLE_TRANSPORT_LAYER_CMD == XCP_ON
static void Xcp_TransportLayerCmd_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_TRANSPORT_LAYER_CMD
#if XCP_ENABLE_USER_CMD == XCP_ON
static void Xcp_UserCmd_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_USER_CMD

#if XCP_ENABLE_CAL_COMMANDS == XCP_ON
static void Xcp_Download_Res(Xcp_PDUType const * const pdu);
#if XCP_ENABLE_DOWNLOAD_NEXT == XCP_ON
static void Xcp_DownloadNext_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_DOWNLOAD_NEXT
#if XCP_ENABLE_DOWNLOAD_MAX == XCP_ON
static void Xcp_DownloadMax_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_DOWNLOAD_MAX
#if XCP_ENABLE_SHORT_DOWNLOAD == XCP_ON
static void Xcp_ShortDownload_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_SHORT_DOWNLOAD
#if XCP_ENABLE_MODIFY_BITS == XCP_ON
static void Xcp_ModifyBits_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_MODIFY_BITS

static void Xcp_SetCalPage_Res(Xcp_PDUType const * const pdu);
static void Xcp_GetCalPage_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_CAL_COMMANDS

#if XCP_ENABLE_PAG_COMMANDS == XCP_ON
#if XCP_ENABLE_GET_PAG_PROCESSOR_INFO == XCP_ON
static void Xcp_GetPagProcessorInfo_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_PAG_PROCESSOR_INFO
#if XCP_ENABLE_GET_SEGMENT_INFO == XCP_ON
static void Xcp_GetSegmentInfo_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_SEGMENT_INFO
#if XCP_ENABLE_GET_PAGE_INFO == XCP_ON
static void Xcp_GetPageInfo_Res(Xcp_PDUType const * const pdu);
#endif
#if XCP_ENABLE_SET_SEGMENT_MODE == XCP_ON
static void Xcp_SetSegmentMode_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_SET_SEGMENT_MODE
#if XCP_ENABLE_GET_SEGMENT_MODE == XCP_ON
static void Xcp_GetSegmentMode_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_SEGMENT_MODE
#if XCP_ENABLE_COPY_CAL_PAGE
static void Xcp_CopyCalPage_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_COPY_CAL_PAGE
#endif // XCP_ENABLE_PAG_COMMANDS

#if XCP_ENABLE_DAQ_COMMANDS == XCP_ON
static void Xcp_ClearDaqList_Res(Xcp_PDUType const * const pdu);
static void Xcp_SetDaqPtr_Res(Xcp_PDUType const * const pdu);
static void Xcp_WriteDaq_Res(Xcp_PDUType const * const pdu);
static void Xcp_SetDaqListMode_Res(Xcp_PDUType const * const pdu);
static void Xcp_GetDaqListMode_Res(Xcp_PDUType const * const pdu);
static void Xcp_StartStopDaqList_Res(Xcp_PDUType const * const pdu);
static void Xcp_StartStopSynch_Res(Xcp_PDUType const * const pdu);
#if XCP_ENABLE_GET_DAQ_CLOCK == XCP_ON
static void Xcp_GetDaqClock_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_DAQ_CLOCK
#if XCP_ENABLE_READ_DAQ == XCP_ON
static void Xcp_ReadDaq_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_READ_DAQ
#if XCP_ENABLE_GET_DAQ_PROCESSOR_INFO == XCP_ON
static void Xcp_GetDaqProcessorInfo_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_DAQ_PROCESSOR_INFO
#if XCP_ENABLE_GET_DAQ_RESOLUTION_INFO == XCP_ON
static void Xcp_GetDaqResolutionInfo_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_DAQ_RESOLUTION_INFO
#if XCP_ENABLE_GET_DAQ_LIST_INFO == XCP_ON
static void Xcp_GetDaqListInfo_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_DAQ_LIST_INFO
#if XCP_ENABLE_GET_DAQ_EVENT_INFO == XCP_ON
static void Xcp_GetDaqEventInfo_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_DAQ_EVENT_INFO
#if XCP_ENABLE_FREE_DAQ == XCP_ON
static void Xcp_FreeDaq_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_FREE_DAQ
#if XCP_ENABLE_ALLOC_DAQ == XCP_ON
static void Xcp_AllocDaq_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_ALLOC_DAQ
#if XCP_ENABLE_ALLOC_ODT == XCP_ON
static void Xcp_AllocOdt_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_ALLOC_ODT
#if XCP_ENABLE_ALLOC_ODT_ENTRY == XCP_ON
static void Xcp_AllocOdtEntry_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_ALLOC_ODT_ENTRY
#if XCP_ENABLE_WRITE_DAQ_MULTIPLE == XCP_ON
static void Xcp_WriteDaqMultiple_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_WRITE_DAQ_MULTIPLE
#endif // XCP_ENABLE_DAQ_COMMANDS

#if XCP_ENABLE_PGM_COMMANDS == XCP_ON
static void Xcp_ProgramStart_Res(Xcp_PDUType const * const pdu);
static void Xcp_ProgramClear_Res(Xcp_PDUType const * const pdu);
static void Xcp_Program_Res(Xcp_PDUType const * const pdu);
static void Xcp_ProgramReset_Res(Xcp_PDUType const * const pdu);
#if XCP_ENABLE_GET_PGM_PROCESSOR_INFO == XCP_ON
static void Xcp_GetPgmProcessorInfo_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_PGM_PROCESSOR_INFO
#if XCP_ENABLE_GET_SECTOR_INFO == XCP_ON
static void Xcp_GetSectorInfo_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_GET_SECTOR_INFO
#if XCP_ENABLE_PROGRAM_PREPARE == XCP_ON
static void Xcp_ProgramPrepare_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_PROGRAM_PREPARE
#if XCP_ENABLE_PROGRAM_FORMAT == XCP_ON
static void Xcp_ProgramFormat_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_PROGRAM_FORMAT
#if XCP_ENABLE_PROGRAM_NEXT == XCP_ON
static void Xcp_ProgramNext_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_PROGRAM_NEXT
#if XCP_ENABLE_PROGRAM_MAX == XCP_ON
static void Xcp_ProgramMax_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_PROGRAM_MAX
#if XCP_ENABLE_PROGRAM_VERIFY == XCP_ON
static void Xcp_ProgramVerify_Res(Xcp_PDUType const * const pdu);
#endif // XCP_ENABLE_PROGRAM_VERIFY
#endif // XCP_ENABLE_PGM_COMMANDS


/*
** Big, fat jump table.
*/

static const Xcp_ServerCommandType Xcp_ServerCommands[] = {
    //lint -e632      Assignment to strong type 'Xcp_ServerCommandType' considered harmless in this context.
#if XCP_ENABLE_STD_COMMANDS == XCP_ON
    Xcp_Connect_Res,
    Xcp_Disconnect_Res,
    Xcp_GetStatus_Res,
    Xcp_Synch_Res,
#if XCP_ENABLE_GET_COMM_MODE_INFO == XCP_ON
    Xcp_GetCommModeInfo_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_GET_ID == XCP_ON
    Xcp_GetId_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_SET_REQUEST == XCP_ON
    Xcp_SetRequest_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_GET_SEED == XCP_ON
    Xcp_GetSeed_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_UNLOCK == XCP_ON
    Xcp_Unlock_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_SET_MTA == XCP_ON
    Xcp_SetMta_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_UPLOAD == XCP_ON
    Xcp_Upload_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_SHORT_UPLOAD == XCP_ON
    Xcp_ShortUpload_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_BUILD_CHECKSUM == XCP_ON
    Xcp_BuildChecksum_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_TRANSPORT_LAYER_CMD == XCP_ON
    Xcp_TransportLayerCmd_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_USER_CMD == XCP_ON
    Xcp_UserCmd_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#else
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
#endif

#if XCP_ENABLE_CAL_COMMANDS == XCP_ON
    Xcp_Download_Res,
#if XCP_ENABLE_DOWNLOAD_NEXT == XCP_ON
    Xcp_DownloadNext_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_DOWNLOAD_MAX == XCP_ON
    Xcp_DownloadMax_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_SHORT_DOWNLOAD == XCP_ON
    Xcp_ShortDownload_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_MODIFY_BITS == XCP_ON
    Xcp_ModifyBits_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#else
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
#endif

#if XCP_ENABLE_PAG_COMMANDS == XCP_ON
    Xcp_SetCalPage_Res,
    Xcp_GetCalPage_Res,
#if XCP_ENABLE_GET_PAG_PROCESSOR_INFO == XCP_ON
    Xcp_GetPagProcessorInfo_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_GET_SEGMENT_INFO == XCP_ON
    Xcp_GetSegmentInfo_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_GET_PAGE_INFO == XCP_ON
    Xcp_GetPageInfo_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_SET_SEGMENT_MODE == XCP_ON
    Xcp_SetSegmentMode_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_GET_SEGMENT_MODE == XCP_ON
    Xcp_GetSegmentMode_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_COPY_CAL_PAGE == XCP_ON
    Xcp_CopyCalPage_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#else
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
#endif

#if XCP_ENABLE_DAQ_COMMANDS == XCP_ON
    Xcp_ClearDaqList_Res,
    Xcp_SetDaqPtr_Res,
    Xcp_WriteDaq_Res,
    Xcp_SetDaqListMode_Res,
    Xcp_GetDaqListMode_Res,
    Xcp_StartStopDaqList_Res,
    Xcp_StartStopSynch_Res,
#if XCP_ENABLE_GET_DAQ_CLOCK == XCP_ON
    Xcp_GetDaqClock_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_READ_DAQ == XCP_ON
    Xcp_ReadDaq_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_GET_DAQ_PROCESSOR_INFO == XCP_ON
    Xcp_GetDaqProcessorInfo_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_GET_DAQ_RESOLUTION_INFO == XCP_ON
    Xcp_GetDaqResolutionInfo_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_GET_DAQ_LIST_INFO == XCP_ON
    Xcp_GetDaqListInfo_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_GET_DAQ_EVENT_INFO == XCP_ON
    Xcp_GetDaqEventInfo_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_FREE_DAQ == XCP_ON
    Xcp_FreeDaq_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_ALLOC_DAQ == XCP_ON
    Xcp_AllocDaq_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_ALLOC_ODT == XCP_ON
    Xcp_AllocOdt_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_ALLOC_ODT_ENTRY == XCP_ON
    Xcp_AllocOdtEntry_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#else
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
#endif

#if XCP_ENABLE_PGM_COMMANDS == XCP_ON
    Xcp_ProgramStart_Res,
    Xcp_ProgramClear_Res,
    Xcp_Program_Res,
    Xcp_ProgramReset_Res,
#if XCP_ENABLE_GET_PGM_PROCESSOR_INFO == XCP_ON
    Xcp_GetPgmProcessorInfo_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_GET_SECTOR_INFO == XCP_ON
    Xcp_GetSectorInfo_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_PROGRAM_PREPARE == XCP_ON
    Xcp_ProgramPrepare_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_PROGRAM_FORMAT == XCP_ON
    Xcp_ProgramFormat_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_PROGRAM_NEXT == XCP_ON
    Xcp_ProgramNext_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_PROGRAM_MAX == XCP_ON
    Xcp_ProgramMax_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#if XCP_ENABLE_PROGRAM_VERIFY == XCP_ON
    Xcp_ProgramVerify_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
#else
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
    Xcp_CommandNotImplemented_Res,
#endif
#if (XCP_ENABLE_DAQ_COMMANDS == XCP_ON) && (XCP_ENABLE_WRITE_DAQ_MULTIPLE == XCP_ON)
    Xcp_WriteDaqMultiple_Res,
#else
    Xcp_CommandNotImplemented_Res,
#endif
    //lint +e632
};

/*
**  Global Functions.
*/

void Xcp_Init(void)
{
    DBG_PRINT1("Xcp_Init()\n");
    Xcp_ConnectionState = XCP_DISCONNECTED;

    Xcp_MemSet(&Xcp_State, UINT8(0), (uint32_t)sizeof(Xcp_StateType));

#if (XCP_PROTECT_CAL == XCP_ON) || (XCP_PROTECT_PAG == XCP_ON)
    Xcp_State.protection |= XCP_RESOURCE_CAL_PAG;
#endif // XCP_PROTECT_CAL
#if XCP_PROTECT_DAQ == XCP_ON
    Xcp_State.protection |= XCP_RESOURCE_DAQ;
#endif // XCP_PROTECT_DAQ
#if XCP_PROTECT_STIM == XCP_ON
    Xcp_State.protection |= XCP_RESOURCE_STIM;
#endif // XCP_PROTECT_STIM
#if XCP_PROTECT_PGM == XCP_ON
    Xcp_State.protection |= XCP_RESOURCE_PGM;
#endif // XCP_PROTECT_PGM

#if XCP_ENABLE_DAQ_COMMANDS == XCP_ON
    Xcp_InitDaq();
    Xcp_State.daqProcessor.running = (bool)FALSE;
    Xcp_State.daqPointer.daqList = (uint16_t)0u;
    Xcp_State.daqPointer.odt = (uint8_t)0;
    Xcp_State.daqPointer.odtEntry = (uint8_t)0;
    Xcp_State.daqPointer.daqEntityNumber = (uint16_t)0;
#endif // XCP_ENABLE_DAQ_COMMANDS
#if XCP_TRANSPORT_LAYER_COUNTER_SIZE != 0
    Xcp_State.counter = 0;
#endif // XCP_TRANSPORT_LAYER_COUNTER_SIZE
    XcpHw_Init();
    XcpTl_Init();
}


void Xcp_MainFunction(void)
{

}

void Xcp_SetMta(Xcp_MtaType mta)
{
    Xcp_State.mta = mta;
}

Xcp_MtaType Xcp_GetNonPagedAddress(void const * const ptr)
{
    Xcp_MtaType mta;

    mta.ext = (uint8_t)0;
    mta.address = (uint32_t)ptr;
    return mta;
}

void Xcp_SendPdu(void)
{
    uint16_t len = Xcp_PduOut.len;

    Xcp_PduOut.data[0] = LOBYTE(len);
    Xcp_PduOut.data[1] = HIBYTE(len);
#if XCP_TRANSPORT_LAYER_COUNTER_SIZE != 0
    Xcp_PduOut.data[2] = LOBYTE(Xcp_State.counter);
    Xcp_PduOut.data[3] = HIBYTE(Xcp_State.counter);
    Xcp_State.counter++;
#endif // XCP_TRANSPORT_LAYER_COUNTER_SIZE

    //DBG_PRINT1("Sending PDU: ");
    XcpTl_Send(Xcp_PduOut.data, Xcp_PduOut.len + (uint16_t)4);
}


uint8_t * Xcp_GetOutPduPtr(void)
{
    return &(Xcp_PduOut.data[4]);
}

void Xcp_SetPduOutLen(uint16_t len)
{
    Xcp_PduOut.len = len;
}

void Xcp_Send8(uint8_t len, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5, uint8_t b6, uint8_t b7)
{

    uint8_t * dataOut = Xcp_GetOutPduPtr();

    Xcp_SetPduOutLen((uint16_t)len);

    /* Controlled fall-through (copy optimization) */
    /* MISRA 2004 violation Rule 15.2*/
    switch(len) {
        case 8:
            dataOut[7] = b7;
            //lint -fallthrough
        case 7:
            dataOut[6] = b6;
            //lint -fallthrough
        case 6:
            dataOut[5] = b5;
            //lint -fallthrough
        case 5:
            dataOut[4] = b4;
            //lint -fallthrough
        case 4:
            dataOut[3] = b3;
            //lint -fallthrough
        case 3:
            dataOut[2] = b2;
            //lint -fallthrough
        case 2:
            dataOut[1] = b1;
            //lint -fallthrough
        case 1:
            dataOut[0] = b0;
            break;
    }

    Xcp_SendPdu();
}

static void Xcp_Upload(uint8_t len)
{
    //uint8_t len = pdu->data[1];
    uint8_t * dataOut = Xcp_GetOutPduPtr();
    Xcp_MtaType dst;

// TODO: RangeCheck / Blockmode!!!
    dataOut[0] = (uint8_t)0xff;

    dst.address = (uint32_t)(dataOut + 1);  // FIX ME!!!
    dst.ext = (uint8_t)0;

    Xcp_CopyMemory(dst, Xcp_State.mta, (uint32_t)len);

    Xcp_State.mta.address += UINT32(len);   // Advance MTA.

    Xcp_SetPduOutLen(UINT16(len));
    Xcp_SendPdu();
}

/**
 * Entry point, needs to be "wired" to CAN-Rx interrupt.
 *
 * @param pdu
 */
void Xcp_DispatchCommand(Xcp_PDUType const * const pdu)
{
    uint8_t cmd = pdu->data[0];

    DBG_PRINT1("Req: ");
    //Xcp_DumpMessageObject(pdu);

    if (Xcp_State.connected == (bool)TRUE) {
        DBG_PRINT2("CMD: [%02X]\n", cmd);
        Xcp_ServerCommands[UINT8(0xff) - cmd](pdu);   // TODO: range check!!!
    } else {    // not connected.
        if (pdu->data[0] == UINT8(XCP_CONNECT)) {
            Xcp_Connect_Res(pdu);
        } else {

        }
    }
#if defined(_MSC_VER)
    fflush(stdout);
#endif
}


void Xcp_SetSendCallout(Xcp_SendCalloutType callout)
{
    Xcp_SendCallout = callout;
}


/*
**
** Global Helper Functions.
**
** Note: These functions are only useful for unit-testing and debugging.
**
*/
Xcp_ConnectionStateType Xcp_GetConnectionState(void)
{
    return Xcp_ConnectionState;
}


/*
**
** Local Functions.
**
*/
static void Xcp_CommandNotImplemented_Res(Xcp_PDUType const * const pdu)
{
    DBG_PRINT2("Command not implemented [%02X].\n", pdu->data[0]);
    XCP_ERROR_RESPONSE(UINT8(ERR_CMD_UNKNOWN));
}


static void Xcp_Connect_Res(Xcp_PDUType const * const pdu)
{
    uint8_t resource = UINT8(0x00);
    uint8_t commModeBasic = UINT8(0x00);

    DBG_PRINT1("CONNECT: \n");

    if (Xcp_State.connected == (bool)FALSE) {
        Xcp_State.connected = (bool)TRUE;
        // TODO: Init stuff
    }

#if XCP_ENABLE_PGM_COMMANDS == XCP_ON
    resource |= XCP_RESOURCE_PGM;
#endif  // XCP_ENABLE_PGM_COMMANDS
#if XCP_ENABLE_DAQ_COMMANDS == XCP_ON
    resource |= XCP_RESOURCE_DAQ;
#endif  // XCP_ENABLE_DAQ_COMMANDS
#if (XCP_ENABLE_CAL_COMMANDS == XCP_ON) || (XCP_ENABLE_PAG_COMMANDS == XCP_ON)
    resource |= XCP_RESOURCE_CAL_PAG;
#endif
#if XCP_ENABLE_STIM == XCP_ON
    resource |= XCP_RESOURCE_STIM;
#endif  // XCP_ENABLE_STIM

    commModeBasic |= XCP_BYTE_ORDER;
    commModeBasic |= XCP_ADDRESS_GRANULARITY;
#if XCP_ENABLE_SLAVE_BLOCKMODE == XCP_ON
    commModeBasic |= XCP_SLAVE_BLOCK_MODE;
#endif // XCP_ENABLE_SLAVE_BLOCKMODE
#if XCP_ENABLE_GET_COMM_MODE_INFO
    commModeBasic |= XCP_OPTIONAL_COMM_MODE;
#endif // XCP_ENABLE_GET_COMM_MODE_INFO


    XcpTl_SaveConnection();

    Xcp_Send8(UINT8(8), UINT8(0xff), UINT8(resource), UINT8(commModeBasic), UINT8(XCP_MAX_CTO),
              LOBYTE(XCP_MAX_DTO), HIBYTE(XCP_MAX_DTO), UINT8(XCP_PROTOCOL_VERSION_MAJOR), UINT8(XCP_TRANSPORT_LAYER_VERSION_MAJOR)
    );
    //DBG_PRINT("MAX-DTO: %04X H: %02X L: %02X\n", XCP_MAX_DTO, HIBYTE(XCP_MAX_DTO), LOBYTE(XCP_MAX_DTO));
}


static void Xcp_Disconnect_Res(Xcp_PDUType const * const pdu)
{
    DBG_PRINT1("DISCONNECT: \n");

    XCP_POSITIVE_RESPONSE();
    XcpTl_ReleaseConnection();
}


static void Xcp_GetStatus_Res(Xcp_PDUType const * const pdu)   // TODO: Implement!!!
{
    DBG_PRINT1("GET_STATUS: \n");

    Xcp_Send8(UINT8(6), UINT8(0xff),
        UINT8(0),     // Current session status
        Xcp_State.protection,  // Current resource protection status
        UINT8(0x00),  // Reserved
        UINT8(0),     // Session configuration id
        UINT8(0),     // "                      "
        UINT8(0), UINT8(0)
    );
}


static void Xcp_Synch_Res(Xcp_PDUType const * const pdu)
{
    DBG_PRINT1("SYNCH: \n");

    //Xcp_Send8(UINT8(2), UINT8(0xfe), UINT8(ERR_CMD_SYNCH), UINT8(0), UINT8(0), UINT8(0), UINT8(0), UINT8(0), UINT8(0));
    XCP_ERROR_RESPONSE(ERR_CMD_SYNCH);
}


#if XCP_ENABLE_GET_COMM_MODE_INFO == XCP_ON
static void Xcp_GetCommModeInfo_Res(Xcp_PDUType const * const pdu)
{
    uint8_t commModeOptional = UINT8(0);

    DBG_PRINT1("GET_COMM_MODE_INFO: \n");

#if XCP_ENABLE_MASTER_BLOCKMODE == XCP_ON
    commModeOptional |= XCP_MASTER_BLOCK_MODE;
#endif // XCP_ENABLE_MASTER_BLOCKMODE

#if XCP_ENABLE_INTERLEAVED_MODE == XCP_ON
    commModeOptional |= XCP_INTERLEAVED_MODE;
#endif // XCP_ENABLE_INTERLEAVED_MODE

    Xcp_Send8(UINT8(8), UINT8(0xff),
        UINT8(0),     // Reserved
        commModeOptional,
        UINT8(0),     // Reserved
        UINT8(XCP_MAX_BS),
        UINT8(XCP_MIN_ST),
        UINT8(XCP_QUEUE_SIZE),
        UINT8(XCP_DRIVER_VERSION)
    );
}
#endif // XCP_ENABLE_GET_COMM_MODE_INFO


#if XCP_ENABLE_GET_ID == XCP_ON
static void Xcp_GetId_Res(Xcp_PDUType const * const pdu)
{
    uint8_t idType = Xcp_GetByte(pdu, UINT8(1));

#if 0
0 BYTE Packet ID: 0xFF
1 BYTE Mode
2 WORD Reserved
4 DWORD Length [BYTE]
---------------------
0           ASCII text
1           ASAM-MC2 filename without path and extension
2           ASAM-MC2 filename with path and extension
3           URL where the ASAM-MC2 file can be found
4           ASAM-MC2 file to upload
128..255    User defined
#endif

    DBG_PRINT2("GET_ID [%u]: \n", idType);

    if (idType == UINT8(1)) {
        Xcp_SetMta(Xcp_GetNonPagedAddress(Xcp_StationID.name));
        Xcp_Send8(UINT8(8), UINT8(0xff), UINT8(0), UINT8(0), UINT8(0), UINT8(Xcp_StationID.len), UINT8(0), UINT8(0), UINT8(0));
    }
#if XCP_ENABLE_GET_ID_HOOK == XCP_ON
    else {
        Xcp_HookFunction_GetId(idType);
    }
#endif // XCP_ENABLE_GET_ID_HOOK


}
#endif // XCP_ENABLE_GET_ID


#if XCP_ENABLE_UPLOAD == XCP_ON
static void Xcp_Upload_Res(Xcp_PDUType const * const pdu)
{
    uint8_t len = Xcp_GetByte(pdu, UINT8(1));

// TODO: RangeCheck / Blockmode!!!
    DBG_PRINT2("UPLOAD [%u]\n", len);

    Xcp_Upload(len);
}
#endif // XCP_ENABLE_UPLOAD


#if XCP_ENABLE_SHORT_UPLOAD == XCP_ON
static void Xcp_ShortUpload_Res(Xcp_PDUType const * const pdu)
{
    uint8_t len = Xcp_GetByte(pdu, UINT8(1));

// TODO: RangeCheck / Blockmode!!!
    DBG_PRINT2("SHORT-UPLOAD [%u]\n", len);
    Xcp_State.mta.ext = Xcp_GetByte(pdu, UINT8(3));
    Xcp_State.mta.address = Xcp_GetDWord(pdu, UINT8(4));
    Xcp_Upload(len);
}
#endif // XCP_ENABLE_SHORT_UPLOAD


#if XCP_ENABLE_SET_MTA == XCP_ON
static void Xcp_SetMta_Res(Xcp_PDUType const * const pdu)
{
#if 0
0 BYTE Command Code = 0xF6
1 WORD Reserved
3 BYTE Address extension
4 DWORD Address
#endif // 0
    Xcp_State.mta.ext = Xcp_GetByte(pdu, UINT8(3));
    Xcp_State.mta.address = Xcp_GetDWord(pdu, UINT8(4));

    DBG_PRINT3("SET_MTA %x::%x\n", Xcp_State.mta.ext, Xcp_State.mta.address);

    XCP_POSITIVE_RESPONSE();
}
#endif // XCP_ENABLE_SET_MTA


#if XCP_ENABLE_BUILD_CHECKSUM == XCP_ON
static void Xcp_BuildChecksum_Res(Xcp_PDUType const * const pdu)
{
    uint32_t blockSize = Xcp_GetDWord(pdu, UINT8(4));

    DBG_PRINT2("BUILD_CHECKSUM [%lu]\n", blockSize);

    //Xcp_CalculateCRC()

}
#endif // XCP_ENABLE_BUILD_CHECKSUM


#if 0
    XCP_VALIDATE_ADRESS
        Callout der die gültigkeit eines Speicherzugriffs überprüft [addr;length]
#endif


#if XCP_ENABLE_TRANSPORT_LAYER_CMD
static void Xcp_TransportLayerCmd_Res(Xcp_PDUType const * const pdu)
{
    XcpTl_TransportLayerCmd_Res(pdu);
}
#endif // XCP_ENABLE_TRANSPORT_LAYER_CMD


#if XCP_ENABLE_USER_CMD
static void Xcp_UserCmd_Res(Xcp_PDUType const * const pdu)
{

}
#endif // XCP_ENABLE_USER_CMD

/*
**
**  DAQ Commands.
**
*/
#if XCP_ENABLE_DAQ_COMMANDS == XCP_ON
static void Xcp_ClearDaqList_Res(Xcp_PDUType const * const pdu)
{
    uint16_t daqListNumber;

    daqListNumber = Xcp_GetWord(pdu, UINT8(2));

    DBG_PRINT2("CLEAR_DAQ_LIST [%u] \n", daqListNumber);
#if 0
0  BYTE  Command Code = 0xE3
1  BYTE  reserved
2,3  WORD  DAQ_LIST_NUMBER [0,1..MAX_DAQ-1]

    This command can be used for PREDEFINED and for configurable DAQ lists, so the range
for DAQ_LIST_NUMBER is [0,1,..MAX_DAQ-1].
If the specified list is not available, ERR_OUT_OF_RANGE will  be returned.
CLEAR_DAQ_LIST  clears  the  specified  DAQ  list.  For  a  configurable  DAQ  list,  all  ODT
entries will be reset to address=0, extension=0 and size=0 (if valid : bit_offset = 0xFF). For
PREDEFINED and configurable DAQ lists, the running Data Transmission on this list will be
stopped and all DAQ list states are reset.
#endif // 0

}


static void Xcp_SetDaqPtr_Res(Xcp_PDUType const * const pdu)
{
    Xcp_State.daqPointer.daqList = Xcp_GetWord(pdu, UINT8(2));
    Xcp_State.daqPointer.odt = Xcp_GetByte(pdu, UINT8(4));
    Xcp_State.daqPointer.odtEntry = Xcp_GetByte(pdu, UINT8(5));
    // TODO: Calculate DAQ Entity Number.
    //Xcp_State.daqPointer.daqEntityNumber = ???();

    // TODO: If the specified list is not available, ERR_OUT_OF_RANGE will be returned.

    DBG_PRINT4("SET_DAQ_PTR [%u:%u:%u]\n", Xcp_State.daqPointer.daqList, Xcp_State.daqPointer.odt, Xcp_State.daqPointer.odtEntry);

    XCP_POSITIVE_RESPONSE();
}

static void Xcp_WriteDaq_Res(Xcp_PDUType const * const pdu)
{
    Xcp_ODTEntryType * entry;
    uint8_t bitOffset = Xcp_GetByte(pdu, 1);
    uint8_t elemSize  = Xcp_GetByte(pdu, 2);
    uint8_t adddrExt  = Xcp_GetByte(pdu, 3);
    uint32_t address  = Xcp_GetDWord(pdu, 4);

    entry = Daq_GetOdtEntry(Xcp_State.daqPointer.daqList, Xcp_State.daqPointer.odt, Xcp_State.daqPointer.odtEntry);

    entry->length = elemSize;
    entry->mta.address = address;
    entry->mta.ext = adddrExt;

}

static void Xcp_SetDaqListMode_Res(Xcp_PDUType const * const pdu)
{
    Xcp_DaqListType * entry;
    uint8_t mode = Xcp_GetByte(pdu, 1);
    uint16_t daqListNumber = Xcp_GetWord(pdu, 2);
    uint16_t eventChannelNumber = Xcp_GetWord(pdu, 4);
    uint8_t prescaler = Xcp_GetByte(pdu, 6);
    uint8_t priority = Xcp_GetByte(pdu, 7);

    entry = Daq_GetList(daqListNumber);
}

static void Xcp_StartStopDaqList_Res(Xcp_PDUType const * const pdu)
{

}

static void Xcp_StartStopSynch_Res(Xcp_PDUType const * const pdu)
{

}

static void Xcp_GetDaqListMode_Res(Xcp_PDUType const * const pdu)
{

}


///
/// MANY MISSING FUNCTIONS
///

#if XCP_ENABLE_FREE_DAQ == XCP_ON
static void Xcp_FreeDaq_Res(Xcp_PDUType const * const pdu)
{
    DBG_PRINT1("FREE_DAQ: \n");
    Xcp_SendResult(Xcp_FreeDaq());
}
#endif  // XCP_ENABLE_FREE_DAQ

#if XCP_ENABLE_ALLOC_DAQ == XCP_ON
static void Xcp_AllocDaq_Res(Xcp_PDUType const * const pdu)
{
    uint16_t daqCount;

    daqCount = Xcp_GetWord(pdu, UINT8(2));
    DBG_PRINT2("ALLOC_DAQ [%u] \n", daqCount);
    Xcp_SendResult(Xcp_AllocDaq(daqCount));
}
#endif

#if XCP_ENABLE_ALLOC_ODT == XCP_ON
static void Xcp_AllocOdt_Res(Xcp_PDUType const * const pdu)
{
    uint16_t daqListNumber;
    uint8_t odtCount;

    daqListNumber = Xcp_GetWord(pdu, UINT8(2));
    odtCount = Xcp_GetByte(pdu, UINT8(4));
    DBG_PRINT3("ALLOC_ODT [#%u::%u] \n", daqListNumber, odtCount);
    Xcp_SendResult(Xcp_AllocOdt(daqListNumber, odtCount));
}
#endif  // XCP_ENABLE_ALLOC_ODT

#if XCP_ENABLE_ALLOC_ODT_ENTRY == XCP_ON
static void Xcp_AllocOdtEntry_Res(Xcp_PDUType const * const pdu)
{
    uint16_t daqListNumber;
    uint8_t odtNumber;
    uint8_t odtEntriesCount;

    daqListNumber = Xcp_GetWord(pdu, UINT8(2));
    odtNumber = Xcp_GetByte(pdu, UINT8(4));
    odtEntriesCount = Xcp_GetByte(pdu, UINT8(5));
    DBG_PRINT4("ALLOC_ODT_ENTRY: [#%u:#%u::%u]\n", daqListNumber, odtNumber, odtEntriesCount);
    Xcp_SendResult(Xcp_AllocOdtEntry(daqListNumber, odtNumber, odtEntriesCount));
}
#endif  // XCP_ENABLE_ALLOC_ODT_ENTRY


#endif // XCP_ENABLE_DAQ_COMMANDS

#if XCP_ENABLE_GET_DAQ_CLOCK == XCP_ON
static void Xcp_GetDaqClock_Res(Xcp_PDUType const * const pdu)
{
    uint32_t timestamp;
    //uint8_t * dataOut = Xcp_GetOutPduPtr();

    DBG_PRINT1("GET_DAQ_CLOCK: \n");

    timestamp = XcpHw_GetTimerCounter();

    Xcp_Send8(UINT8(8), UINT8(0xff),
    UINT8(0), UINT8(0), UINT8(0),
    LOBYTE(LOWORD(timestamp)), HIBYTE(LOWORD(timestamp)), LOBYTE(HIWORD(timestamp)), HIBYTE(HIWORD(timestamp))
    );
#if 0
0  BYTE  Packet ID: 0xFF
1  BYTE  reserved
2  WORD  reserved
4  DWORD  Receive Timestamp
#endif // 0
}
#endif // XCP_ENABLE_GET_DAQ_CLOCK


#if XCP_ENABLE_GET_DAQ_RESOLUTION_INFO == XCP_ON
static void Xcp_GetDaqResolutionInfo_Res(Xcp_PDUType const * const pdu)
{
    DBG_PRINT1("GET_DAQ_RESOLUTION_INFO: \n");

    Xcp_Send8(UINT8(8), UINT8(0xff),
      UINT8(1),    // Granularity for size of ODT entry (DIRECTION = DAQ)
      UINT8(0),    // Maximum size of ODT entry (DIRECTION = DAQ)
      UINT8(1),    // Granularity for size of ODT entry (DIRECTION = STIM)
      UINT8(0),    // Maximum size of ODT entry (DIRECTION = STIM)
      UINT8(0x34), // Timestamp unit and size
      UINT8(1),    // Timestamp ticks per unit (WORD)
      UINT8(0)
    );
#if 0
0  BYTE                                     Packet ID: 0xFF
1  BYTE  GRANULARITY_ODT_ENTRY_SIZE_DAQ
2  BYTE  MAX_ODT_ENTRY_SIZE_DAQ
3  BYTE  GRANULARITY_ODT_ENTRY_SIZE_STIM
4  BYTE  MAX_ODT_ENTRY_SIZE_STIM
5  BYTE  TIMESTAMP_MODE
6  WORD  TIMESTAMP_TICKS
#endif
}
#endif // XCP_ENABLE_GET_DAQ_RESOLUTION_INFO


/*
**  Helpers.
*/
static void Xcp_SendResult(Xcp_ReturnType result)
{
    if (result == ERR_SUCCESS) {
        XCP_POSITIVE_RESPONSE();
    } else {
        XCP_ERROR_RESPONSE(UINT8(result));
    }
}

#if 0
void Xcp_DumpMessageObject(Xcp_PDUType const * pdu)
{
    //DBG_PRINT3("LEN: %d CMD: %02X\n", pdu->len, pdu->data[0]);
    //DBG_PRINT2("PTR: %p\n", pdu->data);
#if 0
    DBG_PRINT("%08X  %u  [%02X %02X %02X %02X %02X %02X %02X %02X]\n", cmo->canID, cmo->dlc,
           cmo->data[0], cmo->data[1], cmo->data[2], cmo->data[3], cmo->data[4], cmo->data[5], cmo->data[6], cmo->data[7]
    );
#endif
}
#endif

void Xcp_WriteMemory(void * dest, void * src, uint16_t count)
{
    Xcp_MemCopy(dest, src, UINT32(count));
}

void Xcp_CopyMemory(Xcp_MtaType dst, Xcp_MtaType src, uint32_t len)
{
    if ((dst.ext == (uint8_t)0) && (src.ext == (uint8_t)0)) {
//        DBG_PRINT2("LEN: %u\n", len);
//        DBG_PRINT3("dst: %08X src: %08x\n", dst.address, src.address);
        Xcp_MemCopy((void*)dst.address, (void*)src.address, len);
    } else {
        // We need assistance...
    }
}

INLINE uint8_t Xcp_GetByte(Xcp_PDUType const * const pdu, uint8_t offs)
{
  return (*(pdu->data + offs));
}

INLINE uint16_t Xcp_GetWord(Xcp_PDUType const * const pdu, uint8_t offs)
{
  return (*(pdu->data + offs))        |
    ((*(pdu->data + 1 + offs)) << 8);
}

INLINE uint32_t Xcp_GetDWord(Xcp_PDUType const * const pdu, uint8_t offs)
{
    uint16_t h;
    uint16_t l;

    l = (*(pdu->data + offs)) | ((*(pdu->data + 1 + offs)) << 8);
    h = (*(pdu->data + 2 + offs)) | ((*(pdu->data + 3 + offs)) << 8);
    //l = Xcp_GetWord(pdu, 0);
    //h = Xcp_GetWord(pdu, 2);

    return (uint32_t)(h * 0x10000 ) + l;
}

INLINE void Xcp_SetByte(Xcp_PDUType const * const pdu, uint8_t offs, uint8_t value)
{
    (*(pdu->data + offs)) = value;
}

INLINE void Xcp_SetWord(Xcp_PDUType const * const pdu, uint8_t offs, uint16_t value)
{
    (*(pdu->data + offs)) = value & 0xff;
    (*(pdu->data + 1 + offs)) = (value & 0xff00) >> 8;
}

INLINE void Xcp_SetDWord(Xcp_PDUType const * const pdu, uint8_t offs, uint32_t value)
{
    (*(pdu->data + offs)) = value & 0xff;
    (*(pdu->data + 1 + offs)) = (value & 0xff00) >> 8;
    (*(pdu->data + 2 + offs)) = (value & 0xff0000) >> 16;
    (*(pdu->data + 3 + offs)) = (value & 0xff000000) >> 24;
}

void Xcp_MemCopy(void * dst, void * src, uint32_t len)
{
    uint8_t * pd = (uint8_t *)dst;
    uint8_t * ps = (uint8_t *)src;

//    ASSERT(dst != (void *)NULL);
//    ASSERT(pd >= ps + len || ps >= pd + len);
//    ASSERT(len != (uint16_t)0);

    while (len--) {
        *pd++ = *ps++;
    }

}


void Xcp_MemSet(void * dest, uint8_t fill_char, uint32_t len)
{
    uint8_t * p = (uint8_t *)dest;

//    ASSERT(dest != (void *)NULL);

    while (len--) {
        *p++ = fill_char;
    }
}
