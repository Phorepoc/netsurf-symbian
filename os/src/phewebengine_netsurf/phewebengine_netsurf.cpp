// Copyright The Phorepoc Authors
// SPDX-License-Identifier: GPL-2.0-only

// phewebengine_netsurf.cpp -- the ECom shell of the NetSurf web-engine package (GPL-2.0-only, with NetSurf; see phengine_ns.c).
// Interface 0xE7016B01 (phewebengine_ecom.h), implementation 0xE7016B11: the frontend (phebrowser) picks WebKit (0xE7016B10) first,
// then this. The engine itself is plain C (phengine_ns.c).
#include <e32std.h>
#include <e32base.h>
#include <e32debug.h>
#include <f32file.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ecom/implementationproxy.h>
#include <phorepoc/phewebengine_ecom.h>

extern "C" const PheWebEngineApi* PheNetSurfGetApi(unsigned aWanted);

// trace: the serial log (Phorepoc), and C:\data\phenetsurf_trace.txt only when someone created that file (a device has no
// debug port; nothing is written to a user's disk unasked)
extern "C" void phns_log(const char* aMsg)
	{
	TPtrC8 p(reinterpret_cast<const TUint8*>(aMsg), Min((TInt)User::StringLength(reinterpret_cast<const TUint8*>(aMsg)), 239));
	RDebug::Printf("PH: [netsurf] %S", &p);   // (RDebug::Printf's %S takes an 8-bit descriptor)
	RFs fs;
	if (fs.Connect() != KErrNone)
		return;
	RFile f;
	TInt r = f.Open(fs, _L("C:\\data\\phenetsurf_trace.txt"), EFileWrite | EFileShareAny);
	if (r == KErrNone)
		{
		TInt pos = 0;
		f.Seek(ESeekEnd, pos);
		TBuf8<260> line;
		line.Format(_L8("%d "), User::NTickCount());
		line.Append(p);
		line.Append(_L8("\r\n"));
		f.Write(line);
		f.Close();
		}
	fs.Close();
	}

// Crash finder (a device has no debug port and no crash log): an exception handler on the engine threads. On a fault it
// logs the plug-in's runtime address of phns_log (to rebase the link map) and every stack word that points into the plug-in's
// code (the faulting pc/lr are among them: the kernel builds the exception frame on this stack), then lets the fault proceed.
static void FaultHandler(TExcType aType)
	{
	User::SetExceptionHandler(NULL, 0);           // a second fault panics as usual
	const TUint anchor = (TUint)&phns_log;
	TBuf8<200> b;
	b.Format(_L8("FAULT exc=%d phns_log=0x%08x memcpy=0x%08x fopen=0x%08x malloc=0x%08x sp=0x%08x"), (TInt)aType, anchor,
	         (TUint)&memcpy, (TUint)&fopen, (TUint)&malloc, (TUint)&b);
	b.Append(TChar(0));
	phns_log((const char*)b.Ptr());
	TThreadStackInfo si;
	TUint sp = (TUint)&b;
	const TInt sr = RThread().StackInfo(si);
	TUint top = (sp & ~3u) + 0x6000;              // no stack info: scan 24 KB above sp
	if (sr == KErrNone && (TUint)si.iBase > sp && (TUint)si.iBase < top)
		top = (TUint)si.iBase;
	b.Format(_L8("FAULT stackinfo r=%d base=0x%08x limit=0x%08x scan to 0x%08x"), sr, (TUint)si.iBase, (TUint)si.iLimit, top);
	b.Append(TChar(0));
	phns_log((const char*)b.Ptr());
	for (TUint a = sp & ~31u; a < top; a += 32)      // the whole used stack, raw, 8 words a line
		{
		const TUint* q = (const TUint*)a;
		b.Format(_L8("RAW %08x: %08x %08x %08x %08x %08x %08x %08x %08x"), a, q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]);
		b.Append(TChar(0));
		phns_log((const char*)b.Ptr());
		}
	TInt n = 0;
	for (TUint a = sp & ~3u; a < top && n < 200; a += 4)
		{
		const TUint w = *(const TUint*)a;
		if (w >= 0x70000000u && w < 0x90000000u && (w & 1 || (w & 3) == 0))   // code in the 0x7xxxxxxx-0x8xxxxxxx range
			{
			b.Format(_L8("FAULT stk+0x%x 0x%08x (anchor%+d)"), a - sp, w, (TInt)(w - anchor));
			b.Append(TChar(0));
			phns_log((const char*)b.Ptr());
			n++;
			}
		}
	}

extern "C" void phns_install_fault_logger(void)
	{
	User::SetExceptionHandler(FaultHandler, KExceptionFault);
	}

namespace
{
class CNetSurfEngine : public CPheWebEnginePlugin
	{
public:
	static CNetSurfEngine* NewL() { return new (ELeave) CNetSurfEngine; }
	const PheWebEngineApi* Api() { return PheNetSurfGetApi(PHE_WEBENGINE_VERSION); }
	};

const TImplementationProxy KImplementationTable[] =
	{
	IMPLEMENTATION_PROXY_ENTRY(0xE7016B11, CNetSurfEngine::NewL)
	};
}

EXPORT_C const TImplementationProxy* ImplementationGroupProxy(TInt& aTableCount)
	{
	aTableCount = sizeof(KImplementationTable) / sizeof(TImplementationProxy);
	return KImplementationTable;
	}
