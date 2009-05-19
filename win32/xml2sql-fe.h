/*
 * xml2sql, MediaWiki XML to SQL converter.
 * Copyright (C) Tietew.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * tietew@tietew.net
 */
#pragma once
#include "resource.h"

class RString : public ATL::CString
{
public:
	RString(int id) { LoadString(id); }
};

class SevenZipHelper;

class MainDialog :
	public ATL::CDialogImpl<MainDialog>,
	public WTL::CMessageFilter,
	public WTL::CWinDataExchange<MainDialog>
{
public:
	MainDialog();
	~MainDialog();
	enum { IDD = IDD_DIALOG1 };
	friend class SevenZipHelper;

private:
	// DDX
	WTL::CProgressBarCtrl m_progress;
	ATL::CString m_xml;
	ATL::CString m_outdir;
	int m_format;
	bool m_notext;
	bool m_compress;
	bool m_compress_full;
	
	// converter
	ATL::CComAutoCriticalSection m_csStderr;
	ATL::CString m_errbuff;
	ATL::CHandle m_fh;
	ATL::CHandle m_stdin;
	ATL::CHandle m_stderr;
	ATL::CHandle m_ph;
	ULONGLONG m_fsize;
	bool m_converting;
	volatile bool m_abort;
	WTL::CCursorHandle m_waitcursor;

public:
	BEGIN_DDX_MAP(MainDialog)
		DDX_CONTROL_HANDLE(IDC_PROGRESS, m_progress)
		DDX_TEXT(IDC_XML, m_xml)
		DDX_TEXT(IDC_OUTDIR, m_outdir)
		DDX_RADIO(IDC_OUT_IMPORT, m_format)
		DDX_CHECK(IDC_OPT_NOTEXT, m_notext)
		DDX_CHECK(IDC_OPT_COMPRESS, m_compress)
		DDX_CHECK(IDC_OPT_COMPRESS_FULL, m_compress_full)
	END_DDX_MAP()
	
	BEGIN_MSG_MAP(MainDialog)
		MSG_WM_SETCURSOR(OnSetCursor)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_ID_HANDLER_EX(IDC_OUT_IMPORT, OnUpdateOption)
		COMMAND_ID_HANDLER_EX(IDC_OUT_MYSQL, OnUpdateOption)
		COMMAND_ID_HANDLER_EX(IDC_OUT_PSQL7, OnUpdateOption)
		COMMAND_ID_HANDLER_EX(IDC_OUT_PSQL8, OnUpdateOption)
		COMMAND_ID_HANDLER_EX(IDC_OPT_COMPRESS, OnUpdateOption)
		COMMAND_ID_HANDLER_EX(IDC_XML, OnXML)
		COMMAND_ID_HANDLER_EX(IDC_XML_BROWSE, OnXMLBrowse)
		COMMAND_ID_HANDLER_EX(IDC_OUTDIR_BROWSE, OnOutdirBrowse)
		COMMAND_ID_HANDLER_EX(IDC_START, OnStart)
		MESSAGE_HANDLER_EX(WM_APP, OnFinishConvert)
		MSG_WM_DROPFILES(OnDropFiles)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()
	
	virtual BOOL PreTranslateMessage(MSG *msg);
	void ErrorMessage(HRESULT hr = GetLastError(), UINT flag = MB_ICONEXCLAMATION);
	int MessageBox(LPCSTR text, UINT type = MB_OK) {
		return ATL::CWindow::MessageBox(text, RString(IDS_APP_TITLE), type);
	}
	void EnableDlgItem(int id, bool enable = true) {
		::EnableWindow(GetDlgItem(id), enable);
	}
	
	// message handlers
	LRESULT OnInitDialog(HWND hwnd, LPARAM lp);
	void OnUpdateOption(UINT notify, int id, HWND ctrl);
	void OnXML(UINT notify, int id, HWND ctrl);
	void OnXMLBrowse(UINT notify, int id, HWND ctrl);
	void OnDropFiles(HDROP hdrop);
	void OnOutdirBrowse(UINT notify, int id, HWND ctrl);
	void OnStart(UINT notify, int id, HWND ctrl);
	LRESULT OnFinishConvert(UINT msg, WPARAM wp, LPARAM lp);
	LRESULT OnSetCursor(HWND hwnd, UINT hit, UINT mesg);
	void OnClose();
	void OnDestroy();
	
	// decompresser
	static unsigned int CALLBACK DecompressThreadProc(void *data);
	static unsigned int CALLBACK StderrThreadProc(void *data);
	unsigned int DecompressThread();
	void OnStartConvert();
	void SetProgress(LONGLONG pos, LONGLONG total);
	
	bool DoConvert();
	void DecompressNone();
	void DecompressGzip();
	void DecompressBzip2();
	void Decompress7zip();
	void AbortConvert(const char *mesg, DWORD err = -1);
};
