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

#include <initguid.h>
#include <7zip/IStream.h>
#include <7zip/Archive/IArchive.h>

// {23170F69-40C1-278A-1000-000110070000}
DEFINE_GUID(CLSID_CFormat7z, 0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x07, 0x00, 0x00);

static HMODULE szdll;
static UINT32 (WINAPI *pfnCreateObject)(const GUID *clsID, const GUID *interfaceID, void **outObject);

class ATL_NO_VTABLE SevenZipHelper :
	public ATL::CComObjectRoot,
	public IInStream,
	public IStreamGetSize,
	public ISequentialOutStream,
	public IArchiveExtractCallback
{
	MainDialog *m_dlg;
	UInt64 m_total;
	Int32 m_result;

public:
	BEGIN_COM_MAP(SevenZipHelper)
		COM_INTERFACE_ENTRY_IID(IID_IInStream, IInStream)
		COM_INTERFACE_ENTRY_IID(IID_ISequentialInStream, ISequentialInStream)
		COM_INTERFACE_ENTRY_IID(IID_IStreamGetSize, IStreamGetSize)
		COM_INTERFACE_ENTRY_IID(IID_ISequentialOutStream, ISequentialOutStream)
		COM_INTERFACE_ENTRY_IID(IID_IArchiveExtractCallback, IArchiveExtractCallback)
		COM_INTERFACE_ENTRY_IID(IID_IProgress, IProgress)
	END_COM_MAP()
	
	SevenZipHelper() : m_result(0) { }
	
	bool Init(MainDialog *dlg)
	{
		m_dlg = dlg;
		if(!szdll && !pfnCreateObject) {
			szdll = LoadLibrary("7zxa.dll");
			if(!szdll) szdll = LoadLibrary("7za.dll");
			if(!szdll) return false;
			(FARPROC&)pfnCreateObject = GetProcAddress(szdll, "CreateObject");
			if(!pfnCreateObject) return false;
		}
		return true;
	}
	HRESULT CreateObject(REFCLSID rclsid, REFIID riid, void **ppv)
	{
		return (*pfnCreateObject)(&rclsid, &riid, ppv);
	}
	int GetResult() const
	{
		return m_result;
	}
	
	// ISequentialInStream
	STDMETHOD(Read)(void *data, UInt32 size, UInt32 *processedSize)
	{
		DWORD r;
		if(m_dlg->m_abort)
			return HRESULT_FROM_WIN32(ERROR_NO_DATA);
		BOOL b = ReadFile(m_dlg->m_fh, data, size, &r, NULL);
		if(processedSize) *processedSize = r;
		if(!b) {
			DWORD err = GetLastError();
			return HRESULT_FROM_WIN32(err);
		}
		return S_OK;
	}
	// IInStream
	STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition)
	{
		LONG hi = (LONG)(offset >> 32);
		SetLastError(0);
		DWORD lo = SetFilePointer(m_dlg->m_fh, (LONG)offset, &hi, seekOrigin);
		if(lo == INVALID_SET_FILE_POINTER) {
			DWORD err = GetLastError();
			if(err != 0) return HRESULT_FROM_WIN32(err);
		}
		if(newPosition) *newPosition = ((Int64)hi << 32) | lo;
		return S_OK;
	}
	// IStreamGetSize
	STDMETHOD(GetSize)(UInt64 *size)
	{
		if(size) *size = m_dlg->m_fsize;
		return S_OK;
	}
	// ISequentialOutStream
	STDMETHOD(Write)(const void *data, UInt32 size, UInt32 *processedSize)
	{
		DWORD w;
		if(m_dlg->m_abort)
			return HRESULT_FROM_WIN32(ERROR_NO_DATA);
		BOOL b = WriteFile(m_dlg->m_stdin, data, size, &w, NULL);
		if(processedSize) *processedSize = w;
		if(!b) {
			DWORD err = GetLastError();
			return HRESULT_FROM_WIN32(err);
		}
		return S_OK;
	}
	// IArchiveExtractCallback
	// GetStream OUT: S_OK - OK, S_FALSE - skeep this file
	STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream **outStream, 
		Int32 askExtractMode)
	{
		if(askExtractMode == NArchive::NExtract::NAskMode::kExtract)
			return QueryInterface(IID_ISequentialOutStream, (void**)outStream);
		return S_FALSE;
	}
	STDMETHOD(PrepareOperation)(Int32 askExtractMode)
	{
		return S_OK;
	}
	STDMETHOD(SetOperationResult)(Int32 resultEOperationResult)
	{
		m_result = resultEOperationResult;
		return S_OK;
	}
	// IProgress
	STDMETHOD(SetTotal)(UInt64 total)
	{
		m_total = total;
		return S_OK;
	}
	STDMETHOD(SetCompleted)(const UInt64 *completeValue)
	{
		m_dlg->SetProgress(*completeValue, m_total);
		return S_OK;
	}
};

void MainDialog::Decompress7zip()
{
	ATL::CComObject<SevenZipHelper> *sz;
	ATL::CComObject<SevenZipHelper>::CreateInstance(&sz);
	ATL::CComPtr<IUnknown> unk = sz->GetUnknown();
	HRESULT hr;
	
	if(!sz->Init(this)) {
		AbortConvert("Can't load 7zxa.dll or 7za.dll.");
		return;
	}
	
	ATL::CComPtr<IInArchive> ar;
	hr = sz->CreateObject(CLSID_CFormat7z, IID_IInArchive, (void**)&ar);
	if(FAILED(hr)) {
		AbortConvert(NULL, hr);
		return;
	}
	if(FAILED(ar->Open(sz, 0, 0))) {
		AbortConvert(NULL, hr);
		return;
	}
	
	UInt32 index = 0;
	hr = ar->Extract(&index, 1, 0, sz);
	if(FAILED(hr)) {
		if(HRESULT_CODE(hr) != ERROR_NO_DATA)
			AbortConvert(NULL, hr);
	} else {
		switch(sz->GetResult()) {
		case NArchive::NExtract::NOperationResult::kOK:
			break;
		case NArchive::NExtract::NOperationResult::kUnSupportedMethod:
			AbortConvert("7-zip: Unsupported method");
			break;
		case NArchive::NExtract::NOperationResult::kDataError:
			AbortConvert("7-zip: Data error");
			break;
		case NArchive::NExtract::NOperationResult::kCRCError:
			AbortConvert("7-zip: CRC error");
			break;
		default:
			AbortConvert("7-zip: Unknown error");
		}
	}
}
