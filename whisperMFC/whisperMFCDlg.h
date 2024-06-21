
// whisperMFCDlg.h : header file
//

#pragma once

#include <vector>
#include "ctranslate2/models/whisper.h"


// CwhisperMFCDlg dialog
class CwhisperMFCDlg : public CDialogEx
{
// Construction
public:
	CwhisperMFCDlg(CWnd* pParent = nullptr);	// standard constructor

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_WHISPERMFC_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


	ctranslate2::Device m_device;
	ctranslate2::models::Whisper *m_whisper_pool;
	bool m_log_profiling;

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	// audio file path
	CString m_strPath;
	// Asr result string
	CString m_strResult;
	afx_msg void OnBnClickedBtnExit();
	afx_msg void OnBnClickedBtnOpen();

	std::string transcribeAudio(std::vector<float> float_samples);
	bool initModel();
	afx_msg void OnStnClickedStaticResult();
	CString m_strStatus;
};
