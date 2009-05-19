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

unsigned int CALLBACK MainDialog::DecompressThreadProc(void *data)
{
	MainDialog *p = (MainDialog *)data;
	return p->DecompressThread();
}

unsigned int MainDialog::DecompressThread()
{
	ATL::CString ext = ATL::CPath(m_xml).GetExtension();
	
	if(ext.CompareNoCase(".gz") == 0) {
		DecompressGzip();
	} else if(ext.CompareNoCase(".bz2") == 0) {
		DecompressBzip2();
	} else if(ext.CompareNoCase(".7z") == 0) {
		Decompress7zip();
	} else {
		DecompressNone();
	}
	DWORD err = GetLastError();
	m_stdin.Close();
	m_fh.Close();
	
	if(!(err == ERROR_SUCCESS || err == ERROR_NO_DATA))
		ErrorMessage(err);
	if(m_ph) {
		WaitForSingleObject(m_ph, INFINITE);
		GetExitCodeProcess(m_ph, &err);
		PostMessage(WM_APP, (WPARAM)err);
	}
	m_ph.Close();
	
	return 0;
}

void MainDialog::AbortConvert(const char *mesg, DWORD err)
{
	if(mesg)
		MessageBox(mesg, MB_ICONEXCLAMATION);
	else if(err != -1)
		ErrorMessage(err ? err : GetLastError());
	m_abort = true;
}

void MainDialog::DecompressNone()
{
	char buff[65536];
	ULONGLONG rsize = 0;
	DWORD r;
	
	while(!m_abort) {
		if(!ReadFile(m_fh, buff, sizeof(buff), &r, NULL)) {
			AbortConvert(NULL, 0);
			break;
		}
		if(r == 0) break;
		if(!WriteFile(m_stdin, buff, r, &r, NULL)) {
			r = GetLastError();
			if(r != ERROR_NO_DATA)
				AbortConvert(NULL, r);
			break;
		}
		rsize += r;
		SetProgress(rsize, m_fsize);
	}
}

void MainDialog::DecompressGzip()
{
	char buff[65536];
	int fd = _open_osfhandle((intptr_t)(HANDLE)m_fh, _O_RDONLY|_O_BINARY);
	gzFile gz = gzdopen(fd, "rb");
	DWORD lo, wr;
	LONG hi;
	ULONGLONG rsize;
	int r;
	
	while(!m_abort) {
		r = gzread(gz, buff, sizeof(buff));
		if(r == 0) break;
		if(r == -1) {
			const char *mesg = gzerror(gz, &r);
			if(r == Z_ERRNO) mesg = strerror(errno);
			AbortConvert(mesg);
			break;
		}
		if(!WriteFile(m_stdin, buff, r, &wr, NULL)) {
			wr = GetLastError();
			if(wr != ERROR_NO_DATA)
				AbortConvert(NULL, wr);
			break;
		}
		hi = 0;
		lo = SetFilePointer((HANDLE)m_fh, 0, &hi, FILE_CURRENT);
		rsize = ((ULONGLONG)hi << 32) | lo;
		SetProgress(rsize, m_fsize);
	}
	gzclose(gz);
	m_fh.Detach();
}

void MainDialog::DecompressBzip2()
{
	char buff[65536];
	int fd = _open_osfhandle((intptr_t)(HANDLE)m_fh, _O_RDONLY|_O_BINARY);
	BZFILE *bz2 = BZ2_bzdopen(fd, "rb");
	DWORD lo, wr;
	LONG hi;
	ULONGLONG rsize;
	int r;
	
	while(!m_abort) {
		r = BZ2_bzread(bz2, buff, sizeof(buff));
		if(r == 0) break;
		if(r == -1) {
			const char *mesg = BZ2_bzerror(bz2, &r);
			if(r == BZ_IO_ERROR) mesg = strerror(errno);
			AbortConvert(mesg);
			break;
		}
		if(!WriteFile(m_stdin, buff, r, &wr, NULL)) {
			wr = GetLastError();
			if(wr != ERROR_NO_DATA)
				AbortConvert(NULL, wr);
			break;
		}
		hi = 0;
		lo = SetFilePointer(m_fh, 0, &hi, FILE_CURRENT);
		rsize = ((ULONGLONG)hi << 32) | lo;
		SetProgress(rsize, m_fsize);
	}
	BZ2_bzclose(bz2);
	m_fh.Detach();
}
