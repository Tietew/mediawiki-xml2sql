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
#include "pch.h"
#include "xml2sql-fe.h"

WTL::CAppModule _Module;

inline void MakeFilterString(ATL::CString& str)
{
	char *pch = str.GetBuffer();
	for(int i = 0; i < str.GetLength(); ++i)
		if(pch[i] == '|') pch[i] = '\0';
}

MainDialog::MainDialog()
{
	m_format = 0;
	m_notext = false;
	m_compress = false;
	m_compress_full = false;
	m_fsize = 0;
	m_converting = false;
	m_abort = false;
}

MainDialog::~MainDialog()
{
}

BOOL MainDialog::PreTranslateMessage(MSG *msg)
{
	return IsDialogMessage(msg);
}

void MainDialog::ErrorMessage(HRESULT hr, UINT flag)
{
	char *mesg = NULL;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_IGNORE_INSERTS |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPTSTR>(&mesg), 0, NULL);
	MessageBox(mesg, flag);
	LocalFree(mesg);
}

LRESULT MainDialog::OnInitDialog(HWND hwnd, LPARAM lp)
{
	WTL::CMessageLoop *loop = _Module.GetMessageLoop();
	loop->AddMessageFilter(this);
	
	DragAcceptFiles(*this, true);
	SetIcon(WTL::AtlLoadIconImage(IDI_MEDIAWIKI, 0,
		GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)));
	SetIcon(WTL::AtlLoadIconImage(IDI_MEDIAWIKI, 0,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)), false);
	
	EnableDlgItem(IDC_OPT_COMPRESS_FULL, false);
	EnableDlgItem(IDC_START, false);
	
	DoDataExchange();
	m_progress.SetRange(0, 100);
	
	m_waitcursor.LoadOEMCursor(IDC_WAIT);
	
	return false;
}

void MainDialog::OnXMLBrowse(UINT, int, HWND)
{
	DoDataExchange(true, IDC_XML);
	RString filter(IDS_XML_FILTER);
	MakeFilterString(filter);
	
	WTL::CFileDialog dlg(true, 0, m_xml,
		OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY |
		OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST,
		filter);
	if(dlg.DoModal(*this) == IDOK) {
		m_xml = dlg.m_szFileName;
		DoDataExchange(false, IDC_XML);
		EnableDlgItem(IDC_START);
	}
}

void MainDialog::OnDropFiles(HDROP hdrop)
{
	ATL::CString path;
	UINT len = DragQueryFile(hdrop, 0, NULL, 0);
	char *pch = path.GetBufferSetLength(len);
	if(DragQueryFile(hdrop, 0, pch, len + 1)) {
		m_xml = pch;
		DoDataExchange(false, IDC_XML);
		EnableDlgItem(IDC_START);
	}
	DragFinish(hdrop);
}

void MainDialog::OnOutdirBrowse(UINT, int, HWND)
{
	WTL::CFolderDialog dlg;
	if(!WTL::AtlIsOldWindows())
		dlg.m_bi.ulFlags |= BIF_USENEWUI;
	
	DoDataExchange(true);
	if(m_outdir.IsEmpty() && !m_xml.IsEmpty()) {
		ATL::CPath path(m_xml);
		path.RemoveFileSpec();
		m_outdir = (ATL::CString)path;
	}
	dlg.SetInitialFolder(m_outdir);
	
	if(dlg.DoModal(*this) == IDOK) {
		m_outdir = dlg.m_szFolderPath;
		DoDataExchange(false, IDC_OUTDIR);
	}
}

void MainDialog::OnUpdateOption(UINT, int, HWND)
{
	DoDataExchange(true);
	bool my = (m_format == 0 || m_format == 1);
	EnableDlgItem(IDC_OPT_COMPRESS, my);
	EnableDlgItem(IDC_OPT_COMPRESS_FULL, my && m_compress);
}

void MainDialog::OnXML(UINT, int, HWND)
{
	DoDataExchange(true, IDC_XML);
	EnableDlgItem(IDC_START, !m_xml.IsEmpty());
}

void MainDialog::OnStartConvert()
{
	EnableDlgItem(IDC_XML, false);
	EnableDlgItem(IDC_XML_BROWSE, false);
	EnableDlgItem(IDC_OUT_IMPORT, false);
	EnableDlgItem(IDC_OUT_MYSQL, false);
	EnableDlgItem(IDC_OUT_PSQL7, false);
	EnableDlgItem(IDC_OUT_PSQL8, false);
	EnableDlgItem(IDC_OPT_NOTEXT, false);
	EnableDlgItem(IDC_OPT_COMPRESS, false);
	EnableDlgItem(IDC_OPT_COMPRESS_FULL, false);
	EnableDlgItem(IDC_OUTDIR, false);
	EnableDlgItem(IDC_OUTDIR_BROWSE, false);
	EnableDlgItem(IDC_START, false);
	m_converting = true;
}

LRESULT MainDialog::OnFinishConvert(UINT, WPARAM wp, LPARAM)
{
	EnableDlgItem(IDC_XML, true);
	EnableDlgItem(IDC_XML_BROWSE, true);
	EnableDlgItem(IDC_OUT_IMPORT, true);
	EnableDlgItem(IDC_OUT_MYSQL, true);
	EnableDlgItem(IDC_OUT_PSQL7, true);
	EnableDlgItem(IDC_OUT_PSQL8, true);
	EnableDlgItem(IDC_OPT_NOTEXT, true);
	EnableDlgItem(IDC_OUTDIR, true);
	EnableDlgItem(IDC_OUTDIR_BROWSE, true);
	EnableDlgItem(IDC_START, true);
	OnUpdateOption(0, 0, 0);
	m_converting = false;
	
	SetWindowText(RString(IDS_APP_TITLE));
	if(!m_abort) {
		m_csStderr.Lock();
		if(wp == 0) {
			if(m_errbuff.IsEmpty()) {
				MessageBox(RString(IDS_COMPLETE), MB_ICONINFORMATION);
			} else {
				RString mesg(IDS_COMPLETE_WARNING);
				MessageBox(mesg + m_errbuff, MB_ICONINFORMATION);
			}
		} else {
			MessageBox(m_errbuff, MB_ICONEXCLAMATION);
		}
		m_csStderr.Unlock();
	}
	m_abort = false;
	return 0;
}

LRESULT MainDialog::OnSetCursor(HWND hwnd, UINT hit, UINT mesg)
{
	if(m_converting && hit == HTCLIENT) {
		SetCursor(m_waitcursor);
		return true;
	}
	SetMsgHandled(false);
	return false;
}

void MainDialog::OnClose()
{
	if(m_converting) {
		int r = MessageBox(RString(IDS_QUERY_ABORT),  MB_ICONQUESTION | MB_YESNO);
		if(r == IDYES)
			m_abort = true;
		return;
	}
	DestroyWindow();
}

void MainDialog::OnDestroy()
{
	PostQuitMessage(0);
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int show)
{
	MainDialog dlg;
	WTL::CMessageLoop loop;
	int ret = 1;
	
	CoInitialize(NULL);
	_Module.Init(NULL, hinst);
	_Module.AddMessageLoop(&loop);
	if(dlg.Create(NULL)) {
		dlg.ShowWindow(show);
		ret = loop.Run();
	}
	_Module.RemoveMessageLoop();
	_Module.Term();
	CoUninitialize();
	
	return ret;
}
