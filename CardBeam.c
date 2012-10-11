
/***********************************************************************
 *
 * FILE : CardBeam.c
 * 
 * DESCRIPTION : CardBeam
 *
 * COPYRIGHT : (C) 2003 Luc Yriarte
 * 
 *
 ***********************************************************************/

#define TRACE_OUTPUT TRACE_OUTPUT_ON

#include <PalmOS.h>
#include <VFSMgr.h>
#include <ExgLocalLib.h>
#include <TraceMgr.h>

#include "CardBeam.h"


/***********************************************************************
 *
 *	Defines, Macros and Constants
 *
 ***********************************************************************/

#define sysFileCCardBeam			'CaBe'
#define kPalmDirPath				"/Palm"
#define kProgramsDirPath			"/Palm/Programs"
#define kCardBeamDirPath			"/Palm/Programs/CardBeam"
#define kFileNameSize				256
#define kMaxFileIndex				0xffff

/***********************************************************************
 *
 *	Global variables
 *
 ***********************************************************************/

static UInt16 sVolRefNum, sCurrentFileIndex, sTopVisibleFileIndex; 
static Char sFileNameBuf[kFileNameSize];
static FileInfoType sFileInfo;
static MemHandle sCurrentFileNameH;


/***********************************************************************
 *
 *	Internal Functions
 *
 ***********************************************************************/
 /***********************************************************************
 *
 * FUNCTION:    StartApplication
 *
 * DESCRIPTION: Default initialization for the CardBeam application.
 *
 * PARAMETERS:  none
 *
 * RETURNED:    error code
 *
 ***********************************************************************/

static UInt16 StartApplication (void)
{
	UInt32 vfsMgrVersion;
	UInt32 volIterator = vfsIteratorStart;
	UInt16 err = errNone;

	err = FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &vfsMgrVersion);
	if (err)
		goto Exit;

	sCurrentFileNameH = NULL;
	sCurrentFileIndex = kMaxFileIndex;
	sTopVisibleFileIndex = 0;

	while (volIterator != vfsIteratorStop)
	{
		err = VFSVolumeEnumerate(&sVolRefNum, &volIterator);
		if (err == errNone)
		{
			err = VFSDirCreate(sVolRefNum, kPalmDirPath);
			err = VFSDirCreate(sVolRefNum, kProgramsDirPath);
			err = VFSDirCreate(sVolRefNum, kCardBeamDirPath);
			if (err == vfsErrFileAlreadyExists)
				err = errNone;
			break;
		}
	}

Exit:
	return err;
}


/***********************************************************************
 *
 * FUNCTION:    StopApplication
 *
 * DESCRIPTION: Default cleanup code for the CardBeam application.
 *
 * PARAMETERS:  none
 *
 * RETURNED:    error code
 *
 ***********************************************************************/

static UInt16 StopApplication (void)
{
	UInt16 err = errNone;
	FrmCloseAllForms();
	return err;
}


/***********************************************************************
 *
 * FUNCTION:    CardBeamSendFile
 *
 * DESCRIPTION: Default cleanup code for the CardBeam application.
 *
 * PARAMETERS:  none
 *
 * RETURNED:    error code
 *

typedef struct ExgSocketType {
	UInt16	libraryRef;	// identifies the Exg library in use
	UInt32 	socketRef;	// used by Exg library to identify this connection
	UInt32 	target;		// Creator ID of application this is sent to
	UInt32	count;		// # of objects in this connection (usually 1)
	UInt32	length;		// # total byte count for all objects being sent (optional)
	UInt32	time;		// last modified time of object (optional)
	UInt32	appData;	// application specific info
	UInt32 	goToCreator; // creator ID of app to launch with goto after receive
	ExgGoToType goToParams;	// If launchCreator then this contains goto find info
	UInt16	localMode:1; // Exchange with local machine only mode 
	UInt16	packetMode:1;// Use connectionless packet mode (Ultra)
	UInt16	noGoTo:1; 	// Do not go to app (local mode only)
	UInt16 	noStatus:1; // Do not display status dialogs
	UInt16 	preview:1;	// Preview in progress: don't throw away data as it's read
	UInt16	reserved:11;// reserved system flags
	Char *description;	// text description of object (for user)
	Char *type;		// Mime type of object (optional)
	Char *name;		// name of object, generally a file name (optional)
} ExgSocketType;

 ***********************************************************************/

static UInt16 CardBeamSendFile (void)
{
	FileRef fileRef;
	MemHandle fileDataH;
	Char * filePathP, * fileNameP;
	void * fileDataP;
	UInt32 fileSize;
	UInt16 err = errNone;
	ExgSocketType exgSocket;

TraceOutput(TL(appErrorClass, "CardBeamSendFile"));

	fileNameP = (Char*) MemHandleLock(sCurrentFileNameH);
	filePathP = MemPtrNew(2 + StrLen(kCardBeamDirPath) + StrLen(fileNameP));
	StrCopy(filePathP, kCardBeamDirPath);
	StrCat(filePathP, "/");
	StrCat(filePathP, fileNameP);
	StrCopy(sFileNameBuf, exgLocalPrefix);
	StrCat(sFileNameBuf, fileNameP);
	MemHandleUnlock(sCurrentFileNameH);

TraceOutput(TL(appErrorClass, "CardBeamSendFile opening %s", filePathP));

	err = VFSFileOpen(sVolRefNum, filePathP, vfsModeRead, &fileRef);
	if (err != errNone)
		goto CloseFile;

TraceOutput(TL(appErrorClass, "CardBeamSendFile %s opened", filePathP));

	err = VFSFileSize(fileRef, &fileSize);
	if (err != errNone)
		goto CloseFile;

TraceOutput(TL(appErrorClass, "CardBeamSendFile %s size %lu", filePathP, fileSize));

	fileDataH = MemHandleNew(fileSize);
	if (!fileDataH)
	{
		err = dmErrMemError;
		goto CloseFile;
	}

	fileDataP = MemHandleLock(fileDataH);
	err = VFSFileRead(fileRef, fileSize, fileDataP, NULL);
	if (err != errNone)
		goto FreeHandle;

	MemSet(&exgSocket, sizeof(exgSocket), 0);
	exgSocket.name = sFileNameBuf;

	err = ExgPut(&exgSocket);
	if (err != errNone)
		goto FreeHandle;

	do {
		fileSize -= ExgSend(&exgSocket, fileDataP, fileSize, &err);
		((UInt8*)fileDataP) += fileSize;
	} while (fileSize && err == errNone);

	err = ExgDisconnect(&exgSocket, err);

FreeHandle:
	MemHandleUnlock(fileDataH);
	MemHandleFree(fileDataH);
CloseFile:
	MemPtrFree(filePathP);
	VFSFileClose(fileRef);
Exit:
	return err;
}

/***********************************************************************
 *
 * FUNCTION:	ListViewDrawRecord
 *
 * DESCRIPTION: Table callback to draw records in the list view
 *
 * PARAMETERS:  tblP, row, col, rec¨P
 *
 * RETURNED:	nothing
 *
 ***********************************************************************/

static void ListViewDrawRecord (void * tblP, Int16 row, Int16 col, RectanglePtr recP)
{
	MemHandle fileNameH;
	Char * fileNameP;
	UInt16 err = errNone;
	UInt16 nameLen;

TraceOutput(TL(appErrorClass, "ListViewDrawRecord"));

	fileNameH = (MemHandle) TblGetRowData(tblP, row);
	if (!fileNameH)
		return;
	
	fileNameP = (Char*) MemHandleLock(fileNameH);
	nameLen = FntWordWrap(fileNameP, recP->extent.x);
	WinDrawChars(fileNameP, nameLen, recP->topLeft.x, recP->topLeft.y);
	MemHandleUnlock(fileNameH);
}


/***********************************************************************
 *
 * FUNCTION:	ListViewUpdateScrollers
 *
 * DESCRIPTION: Update the scroll arrows
 *
 * PARAMETERS:  Pointer to the list view form, top and bottom indexes
 *
 * RETURNED:	nothing
 *
 ***********************************************************************/

static void ListViewUpdateScrollers (FormPtr frmP, UInt16 topIndex, UInt16 bottomIndex)
{
	TablePtr tblP;
	Boolean scrollableUp = false;
	Boolean scrollableDown = false;

	tblP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardTable));

	scrollableUp = (topIndex > 0);
	scrollableDown = (1 + bottomIndex - topIndex == TblGetNumberOfRows(tblP));

	FrmUpdateScrollers(frmP, FrmGetObjectIndex(frmP, UpButton), FrmGetObjectIndex(frmP, DownButton), scrollableUp, scrollableDown);
}


/***********************************************************************
 *
 * FUNCTION:	ListViewLoadTable
 *
 * DESCRIPTION: Load MemoDB records into the list view form
 *
 * PARAMETERS:  Pointer to the list view form
 *
 * RETURNED:	nothing
 *
 ***********************************************************************/

static void ListViewLoadTable (FormPtr frmP)
{
	TablePtr tblP;
	FileRef cardBeamDirRef;
	MemHandle fileNameH;
	UInt32 fileIterator;
	UInt16 fileIndex, nRows, row;
	UInt16 err = errNone;

TraceOutput(TL(appErrorClass, "ListViewLoadTable"));

	tblP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardTable));

	fileIndex = 0;
	fileIterator = vfsIteratorStart;
	err = VFSFileOpen(sVolRefNum, kCardBeamDirPath, vfsModeRead, &cardBeamDirRef);

	while (fileIndex < sTopVisibleFileIndex)
	{
		sFileInfo.attributes = 0;
		sFileInfo.nameP = sFileNameBuf;
		sFileInfo.nameBufLen = kFileNameSize;
		err = VFSDirEntryEnumerate(cardBeamDirRef, &fileIterator, &sFileInfo);
		if (err == errNone)
			fileIndex++;
		else
			fileIndex = sTopVisibleFileIndex = 0;
	}

	nRows = TblGetNumberOfRows(tblP);
	for (row = 0; row < nRows; row++)
	{
		sFileInfo.attributes = 0;
		sFileInfo.nameP = sFileNameBuf;
		sFileInfo.nameBufLen = kFileNameSize;
		err = VFSDirEntryEnumerate(cardBeamDirRef, &fileIterator, &sFileInfo);
		if (err == errNone)
		{
			// Initialize row and force redraw only if this row has changed
			if ((fileNameH = (MemHandle) TblGetRowData(tblP, row)) != NULL)
				MemHandleFree(fileNameH);

			fileNameH = MemHandleNew(sFileInfo.nameBufLen);
			StrCopy((Char*) MemHandleLock(fileNameH), sFileInfo.nameP);
			MemHandleUnlock(fileNameH);
			TblSetRowID(tblP, row, fileIndex);
			TblSetRowData(tblP, row, (UInt32) fileNameH);
			TblSetRowUsable(tblP, row, true);
			TblMarkRowInvalid(tblP, row);
			fileIndex++;
		}
		else
		{
			TblSetRowData(tblP, row, NULL);
			TblSetRowUsable(tblP, row, false);
		}
	}

	VFSFileClose(cardBeamDirRef);
	TblDrawTable(tblP);
	ListViewUpdateScrollers(frmP, sTopVisibleFileIndex, fileIndex-1);
}


/***********************************************************************
 *
 * FUNCTION:	ListViewScroll
 *
 * DESCRIPTION: Scrolls list view
 *
 * PARAMETERS:  Form pointer, direction
 *
 * RETURNED:	nothing
 *
 ***********************************************************************/

static void ListViewScroll (FormPtr frmP, Int16 direction)
{
	TablePtr tblP;
	UInt16 offset;

	tblP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardTable));
	offset = TblGetNumberOfRows(tblP) - 1;
	sCurrentFileIndex = kMaxFileIndex;

	if (direction == dmSeekBackward)
	{
		if (offset > sTopVisibleFileIndex)
			sTopVisibleFileIndex = 0;
		else
			sTopVisibleFileIndex -= offset;
	}
	else
		sTopVisibleFileIndex += offset;

	ListViewLoadTable(frmP);
}


/***********************************************************************
 *
 * FUNCTION:	ListViewSave
 *
 * DESCRIPTION: Frees the fileNames handles
 *
 * PARAMETERS:  Pointer to the list view form
 *
 * RETURNED:	nothing
 *
 ***********************************************************************/

static void ListViewSave (FormPtr frmP)
{
	TablePtr tblP;
	MemHandle fileNameH;
	UInt16 nRows, row;

	tblP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardTable));
	nRows = TblGetNumberOfRows(tblP);

	for (row = 0; row < nRows; row++)
	{
		if ((fileNameH = (MemHandle) TblGetRowData(tblP, row)) != NULL)
			MemHandleFree(fileNameH);
	}
}


/***********************************************************************
 *
 * FUNCTION:	ListViewInit
 *
 * DESCRIPTION: Initialize list view globals, call ListViewLoadTable
 *
 * PARAMETERS:  Pointer to the list view form
 *
 * RETURNED:	nothing
 *
 ***********************************************************************/

static void ListViewInit (FormPtr frmP)
{
	TablePtr tblP;
	UInt16 nRows, row;

	tblP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardTable));
	nRows = TblGetNumberOfRows(tblP);

	if (sTopVisibleFileIndex > sCurrentFileIndex)
		sTopVisibleFileIndex = sCurrentFileIndex;

	for (row = 0; row < nRows; row++)
	{
		TblSetRowID(tblP, row, kMaxFileIndex);
		TblSetRowData(tblP, row, NULL);
		TblSetItemStyle(tblP, row, 0, customTableItem);
	}

	ListViewLoadTable(frmP);
	TblSetCustomDrawProcedure (tblP, 0, ListViewDrawRecord);
	TblSetColumnUsable(tblP, 0, true);
}


/***********************************************************************
 *
 * FUNCTION:	ListViewHandleEvent
 *
 * DESCRIPTION: List view form event handler
 *
 * PARAMETERS:  event pointer
 *
 * RETURNED:	true if the event was handled 
 *
 ***********************************************************************/

static Boolean ListViewHandleEvent (EventType * evtP)
{
	FormPtr frmP;
	TablePtr tblP;
	UInt16 err = errNone;
	Boolean handled = false;

	switch (evtP->eType)
	{
		case frmOpenEvent:
			frmP = FrmGetActiveForm();
			ListViewInit(frmP);
			FrmDrawForm(frmP);
			handled = true;
		break;

		case frmCloseEvent:
			frmP = FrmGetActiveForm();
			ListViewSave(frmP);
		break;

		case tblSelectEvent:
			frmP = FrmGetActiveForm();
			tblP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardTable));
			sCurrentFileIndex = TblGetRowID(tblP, evtP->data.tblSelect.row);
			sCurrentFileNameH = (MemHandle) TblGetRowData(tblP, evtP->data.tblSelect.row);
			handled = true;
		break;

		case ctlSelectEvent:
			switch (evtP->data.ctlSelect.controlID)
			{
				case BeamButton:
					if (sCurrentFileIndex == kMaxFileIndex)
						break;
					err = CardBeamSendFile();
					handled = true;
				break;
			}
		break;

		case ctlRepeatEvent:
			switch (evtP->data.ctlRepeat.controlID)
			{
				case UpButton:
					frmP = FrmGetActiveForm();
					ListViewScroll(frmP, dmSeekBackward);
				break;

				case DownButton:
					frmP = FrmGetActiveForm();
					ListViewScroll(frmP, dmSeekForward);
				break;
			}
		break;

		case menuEvent:
			switch (evtP->data.menu.itemID)
			{
				case ListViewOptionsAboutMenu:
					MenuEraseStatus(NULL);
					frmP = FrmInitForm(AboutDialog);
					FrmDoDialog(frmP);
					handled = true;
				break;
			}
		break;
	}

	return handled;
}






/***********************************************************************
 *
 * FUNCTION:    ApplicationHandleEvent
 *
 * DESCRIPTION: Load form resources and set the form event handler
 *
 * PARAMETERS:  event pointer
 *
 * RETURNED:    true if the event was handled 
 *
 ***********************************************************************/

static Boolean ApplicationHandleEvent (EventType * evtP)
{
	FormPtr frmP;
	UInt16 formID;

	if (evtP->eType != frmLoadEvent)
		return false;

	formID = evtP->data.frmLoad.formID;
	frmP = FrmInitForm(formID);
	FrmSetActiveForm(frmP);

	switch (formID)
	{
	case ListView:
		FrmSetEventHandler (frmP, ListViewHandleEvent);
		break;

	}
	return true;
}


/***********************************************************************
 *
 * FUNCTION:    EventLoop
 *
 * DESCRIPTION: Main event loop for the CardBeam application.
 *
 * PARAMETERS:  none
 *
 * RETURNED:    nothing
 *
 ***********************************************************************/

static void EventLoop (void)
{
	EventType evt;
	UInt16 err;

	do
	{
		EvtGetEvent(&evt, evtWaitForever);
		if (! SysHandleEvent(&evt))
			if (! MenuHandleEvent (NULL, &evt, &err))
				if (! ApplicationHandleEvent(&evt))
					FrmDispatchEvent (&evt);
	}
	while (evt.eType != appStopEvent);
}


/***********************************************************************
 *
 * FUNCTION:    PilotMain
 *
 * DESCRIPTION: Main entry point for the CardBeam application.
 *
 * PARAMETERS:  launch code, command block pointer, launch flags
 *
 * RETURNED:    error code
 *
 ***********************************************************************/

UInt32 PilotMain (UInt16 launchCode, MemPtr cmdPBP, UInt16 launchFlags)
{
	UInt16 err = errNone;

	switch (launchCode)
	{
		case sysAppLaunchCmdNormalLaunch:
			err = StartApplication();
			if (err != errNone)
				break;
			FrmGotoForm(ListView);
			EventLoop();
			err = StopApplication();
		break;
	}

	return (UInt32) err;
}

