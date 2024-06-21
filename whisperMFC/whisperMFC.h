
// whisperMFC.h : main header file for the PROJECT_NAME application
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'pch.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols


// CwhisperMFCApp:
// See whisperMFC.cpp for the implementation of this class
//

class CwhisperMFCApp : public CWinApp
{
public:
	CwhisperMFCApp();

// Overrides
public:
	virtual BOOL InitInstance();

// Implementation

	DECLARE_MESSAGE_MAP()
};

extern CwhisperMFCApp theApp;
