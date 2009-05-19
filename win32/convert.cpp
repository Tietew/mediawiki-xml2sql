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

unsigned int CALLBACK MainDialog::StderrThreadProc(void *data)
{
	MainDialog *p = (MainDialog *)data;
	char buff[65536];
	DWORD r;
	
	while(ReadFile(p->m_stderr, buff, sizeof(buff), &r, NULL) && r != 0) {
		p->m_csStderr.Lock();
		p->m_errbuff.Append(buff, r);
		p->m_csStderr.Unlock();
	}
	p->m_stderr.Close();
	
	return 0;
}

void MainDialog::OnStart(UINT, int, HWND)
{
	DoDataExchange(true);
	m_errbuff.Empty();
	if(!DoConvert()) {
		ErrorMessage();
		m_fh.Close();
		m_stdin.Close();
		m_stderr.Close();
		m_ph.Close();
	}
}

bool MainDialog::DoConvert()
{
	m_fh.Attach(CreateFile(m_xml, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
	if(m_fh == INVALID_HANDLE_VALUE) return false;
	
	BY_HANDLE_FILE_INFORMATION fi;
	if(!GetFileInformationByHandle(m_fh, &fi)) return false;
	m_fsize = ((ULONGLONG)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
	
	ATL::CString cmdline("xml2sql.exe");
	switch(m_format) {
	case 0:
		cmdline += " --import";
		break;
	case 1:
		cmdline += " --mysql";
		break;
	case 2:
		cmdline += " --postgresql=8.0";
		break;
	case 3:
		cmdline += " --postgresql=8.1";
		break;
	}
	if(m_notext)
		cmdline += " --no-text";
	if(m_compress) {
		if(m_compress_full)
			cmdline += " --compress=full";
		else
			cmdline += " --compress=old";
	}
	
	if(m_outdir.IsEmpty()) {
		ATL::CPath outdir(m_xml);
		outdir.RemoveFileSpec();
		m_outdir = (ATL::CString)outdir;
	}
	
	HANDLE ph = GetCurrentProcess();
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = true;
	
	ATL::CHandle ir, iw;
	if(!CreatePipe(&ir.m_h, &iw.m_h, &sa, 0)) return false;
	if(!DuplicateHandle(ph, iw, ph, &m_stdin.m_h, 0, false,
		DUPLICATE_SAME_ACCESS)) return false;
	iw.Close();
	
	ATL::CHandle er, ew;
	if(!CreatePipe(&er.m_h, &ew.m_h, &sa, 0)) return false;
	if(!DuplicateHandle(ph, er, ph, &m_stderr.m_h, 0, false,
		DUPLICATE_SAME_ACCESS)) return false;
	er.Close();
	
	ATL::CHandle null(CreateFile("NUL", GENERIC_READ | GENERIC_WRITE,
		0, &sa, OPEN_EXISTING, 0, NULL));
	if(null == INVALID_HANDLE_VALUE) return false;
	
	ATL::CHandle inth((HANDLE)_beginthreadex(NULL, 0,
		DecompressThreadProc, this, 0, NULL));
	if(!inth) return false;
	
	ATL::CHandle errth((HANDLE)_beginthreadex(NULL, 0,
		StderrThreadProc, this, 0, NULL));
	if(!errth) return false;
	
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = ir;
	si.hStdOutput = null;
	si.hStdError = ew;
	
	char mpath[MAX_PATH];
	GetModuleFileName(NULL, mpath, MAX_PATH);
	
	ATL::CPath exe(mpath);
	exe.RemoveFileSpec();
	exe.Append("xml2sql.exe");
	
	if(!CreateProcess(exe, cmdline.GetBuffer(), NULL, NULL, true,
		DETACHED_PROCESS, NULL, m_outdir, &si, &pi)) return false;
	m_ph.Attach(pi.hProcess);
	CloseHandle(pi.hThread);
	
	OnStartConvert();
	return true;
}

void MainDialog::SetProgress(LONGLONG pos, LONGLONG total)
{
	RString fmt(IDS_PROGRESS);
	int perc = (int)(pos * 100 / total);
	char title[256];
	wsprintf(title, fmt, perc);
	m_progress.SetPos(perc);
	SetWindowText(title);
}
