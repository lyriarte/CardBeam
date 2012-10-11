
/***********************************************************************
 *
 * FILE : CardBeam.c
 * 
 * DESCRIPTION : CardBeam
 *
 * COPYRIGHT : (C) 2004 Luc Yriarte
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
#define kCardSlots					16
#define kUnknownNameStr				"Unknown"

/***********************************************************************
 *
 *	Global variables
 *
 ***********************************************************************/

static UInt16 sVolRefNum, sCurrentFileIndex, sTopVisibleFileIndex, sCardSlots; 
static Char sFileNameBuf[kFileNameSize];
static Char sFileExtFilter[20];
static FileInfoType sFileInfo;
static MemHandle sCurrentFileNameH;
static Char * sVolNames[kCardSlots];
static UInt16 sVolRefs[kCardSlots];
static Char sTriggerLabel[kFileNameSize];

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
	UInt16 i = 0;

	err = FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &vfsMgrVersion);
	if (err)
		goto Exit;

	sCurrentFileNameH = NULL;
	sCurrentFileIndex = kMaxFileIndex;
	sTopVisibleFileIndex = 0;

	MemSet(sVolNames, kCardSlots * sizeof(Char*), 0);
	MemSet(sVolRefs, kCardSlots * sizeof(UInt16), 0);
	
	while (i < kCardSlots && volIterator != vfsIteratorStop)
	{
		err = VFSVolumeEnumerate(&sVolRefNum, &volIterator);
		if (err == errNone)
		{
			err  = VFSDirCreate(sVolRefNum, kPalmDirPath);
			err |= VFSDirCreate(sVolRefNum, kProgramsDirPath);
			err |= VFSDirCreate(sVolRefNum, kCardBeamDirPath);
			if (err == vfsErrFileAlreadyExists)
				err = errNone;
			sVolNames[i] = MemPtrNew(kFileNameSize);
			err |= VFSVolumeGetLabel(sVolRefNum, sVolNames[i], kFileNameSize);
			if (err != errNone)
				break;
			sVolRefs[i] = sVolRefNum;
			sCardSlots = ++i;
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
	UInt16 i = 0;
	FrmCloseAllForms();
	for (i=0; i<kCardSlots; i++)
		if (sVolNames[i])
			MemPtrFree(sVolNames[i]);
	return err;
}

/***********************************************************************
 *
 * FUNCTION:    CardBeamSendAll
 *
 * DESCRIPTION: Default cleanup code for the CardBeam application.
 *
 * PARAMETERS:  Char * ext
 *
 * RETURNED:    error code
 *
 ***********************************************************************/

static UInt16 CardBeamSendAll (Char * ext)
{
	FileRef fileRef, cardBeamDirRef;
	MemHandle fileDataH, fileNameH;
	Char * filePathP;
	void * fileDataP;
	UInt32 fileIterator, fileSize, bytesSent;
	UInt16 fileIndex, nRows, row;
	UInt16 err = errNone;
	ExgSocketType exgSocket;

	fileIndex = 0;
	fileIterator = vfsIteratorStart;
	err = VFSFileOpen(sVolRefNum, kCardBeamDirPath, vfsModeRead, &cardBeamDirRef);
	while (err == errNone)
	{
		sFileInfo.attributes = 0;
		sFileInfo.nameP = sFileNameBuf;
		sFileInfo.nameBufLen = kFileNameSize;
		err = VFSDirEntryEnumerate(cardBeamDirRef, &fileIterator, &sFileInfo);
		if (err != errNone)
		{
			err = errNone;
			break;
		}
		if (StrStr(sFileInfo.nameP, ext))
		{
			MemSet(&exgSocket, sizeof(exgSocket), 0);
			exgSocket.noGoTo = 1;
			exgSocket.name = MemPtrNew(StrLen(exgLocalPrefix) + StrLen(sFileInfo.nameP));
			StrCopy(exgSocket.name, exgLocalPrefix);
			StrCat(exgSocket.name, sFileInfo.nameP);

			err = ExgPut(&exgSocket);
			if (err != errNone)
				goto Exit;

			filePathP = MemPtrNew(2 + StrLen(kCardBeamDirPath) + StrLen(sFileInfo.nameP));
			StrCopy(filePathP, kCardBeamDirPath);
			StrCat(filePathP, "/");
			StrCat(filePathP, sFileInfo.nameP);
			err = VFSFileOpen(sVolRefNum, filePathP, vfsModeRead, &fileRef);
			MemPtrFree(filePathP);
			if (err != errNone)
				goto CloseDir;

			err = VFSFileSize(fileRef, &fileSize);
			if (err != errNone)
			{
				VFSFileClose(fileRef);
				goto CloseDir;
			}

			fileDataH = MemHandleNew(fileSize);
			if (!fileDataH)
			{
				VFSFileClose(fileRef);
				err = dmErrMemError;
				goto CloseDir;
			}

			fileDataP = MemHandleLock(fileDataH);
			err = VFSFileRead(fileRef, fileSize, fileDataP, NULL);
			if (err != errNone)
			{
				VFSFileClose(fileRef);
				MemHandleUnlock(fileDataH);
				MemHandleFree(fileDataH);
				goto CloseDir;
			}

			do {
				bytesSent = ExgSend(&exgSocket, fileDataP, fileSize, &err);
				fileSize -= bytesSent;
				((UInt8*)fileDataP) += bytesSent;
			} while (fileSize && err == errNone);

			VFSFileClose(fileRef);
			MemHandleUnlock(fileDataH);
			MemHandleFree(fileDataH);

			err = ExgDisconnect(&exgSocket, err);
			MemPtrFree(exgSocket.name);
		}
  	}

CloseDir:
	VFSFileClose(cardBeamDirRef);
Exit:
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
 ***********************************************************************/

static UInt16 CardBeamSendFile (void)
{
	FileRef fileRef;
	MemHandle fileDataH;
	Char * filePathP, * fileNameP;
	void * fileDataP;
	UInt32 fileSize, bytesSent;
	UInt16 err = errNone;
	ExgSocketType exgSocket;

	fileNameP = (Char*) MemHandleLock(sCurrentFileNameH);
	filePathP = MemPtrNew(2 + StrLen(kCardBeamDirPath) + StrLen(fileNameP));
	StrCopy(filePathP, kCardBeamDirPath);
	StrCat(filePathP, "/");
	StrCat(filePathP, fileNameP);
	StrCopy(sFileNameBuf, exgLocalPrefix);
	StrCat(sFileNameBuf, fileNameP);
	MemHandleUnlock(sCurrentFileNameH);

	err = VFSFileOpen(sVolRefNum, filePathP, vfsModeRead, &fileRef);
	if (err != errNone)
		goto CloseFile;

	err = VFSFileSize(fileRef, &fileSize);
	if (err != errNone)
		goto CloseFile;

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
		bytesSent = ExgSend(&exgSocket, fileDataP, fileSize, &err);
		fileSize -= bytesSent;
		((UInt8*)fileDataP) += bytesSent;
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
 * FUNCTION:	ListViewSetTriggerLabel
 *
 * DESCRIPTION: Initialize list view globals, call ListViewLoadTable
 *
 * PARAMETERS:  Pointer to the list view form
 *
 * RETURNED:	nothing
 *
 ***********************************************************************/

static void ListViewSetTriggerLabel(FormPtr frmP, Int16 selection)
{
	ListPtr lstP;
	ControlPtr	ctlP;

	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardSelectList));
	LstSetSelection(lstP, selection);
	StrCopy(sTriggerLabel, (selection != noListSelection && sVolNames[selection] && *(sVolNames[selection])) ? sVolNames[selection] : kUnknownNameStr);
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardSelectTrigger));
	CtlSetLabel(ctlP, sTriggerLabel);
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
	ListPtr lstP;
	TablePtr tblP;
	UInt16 nRows, row;

	tblP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardTable));
	nRows = TblGetNumberOfRows(tblP);
	sFileExtFilter[0] = '\0';

	if (sTopVisibleFileIndex > sCurrentFileIndex)
		sTopVisibleFileIndex = sCurrentFileIndex;

	for (row = 0; row < nRows; row++)
	{
		TblSetRowID(tblP, row, kMaxFileIndex);
		TblSetRowData(tblP, row, NULL);
		TblSetItemStyle(tblP, row, 0, customTableItem);
	}

	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardSelectList));
	LstSetListChoices(lstP, sVolNames, sCardSlots);
	ListViewSetTriggerLabel(frmP, (Int16) sCardSlots-1);

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

		case popSelectEvent:
			if (evtP->data.popSelect.selection != noListSelection)
				sVolRefNum = sVolRefs[evtP->data.popSelect.selection];
			frmP = FrmGetActiveForm();
			ListViewLoadTable(frmP);
			ListViewSetTriggerLabel(frmP, evtP->data.popSelect.selection);
			handled = true;
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

				case BeamAllButton:
					handled = true;
					frmP = FrmGetActiveForm();
					tblP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CardTable));
					sFileExtFilter[0] = '\0';
					frmP = FrmInitForm(BeamAllDialog);
					FrmSetControlGroupSelection(frmP, 1, PushButtonTXT);
					if (FrmDoDialog(frmP) != BeamAllOkButton)
						break;
					switch (FrmGetObjectId(frmP,FrmGetControlGroupSelection(frmP, 1)))
					{
						case PushButtonTXT:
							StrCopy(sFileExtFilter, ".txt");
						break;
						case PushButtonVCS:
							StrCopy(sFileExtFilter, ".vcs");
						break;
						case PushButtonVCF:
							StrCopy(sFileExtFilter, ".vcf");
						break;
						case PushButtonPRC:
							StrCopy(sFileExtFilter, ".prc");
						break;
						case PushButtonOther:						
							StrCopy(sFileExtFilter, FldGetTextPtr((FieldType *)FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, BeamAllExtField))));
						break;
					}

					FrmDeleteForm(frmP);
					if (sFileExtFilter[0])
						CardBeamSendAll(sFileExtFilter);
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
					FrmDeleteForm(frmP);
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

