// MainFrm.cpp : implmentation of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"
#include <atlmisc.h>

#include "aboutdlg.h"
#include "mControlUIView.h"
#include "MainFrm.h"
#include "SEHexception.h"


BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
	if(CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg))
		return TRUE;

	return mView.PreTranslateMessage(pMsg);
}

BOOL CMainFrame::OnIdle()
{
	UIUpdateToolBar();
	return FALSE;
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	::_set_se_translator(::trans_func);

	// create command bar window
// 	HWND hWndCmdBar = m_CmdBar.Create(m_hWnd, rcDefault, NULL, ATL_SIMPLE_CMDBAR_PANE_STYLE);
// 	// attach menu
// 	m_CmdBar.AttachMenu(GetMenu());
// 	// load command bar images
// 	m_CmdBar.LoadImages(IDR_MAINFRAME);
// 	// remove old menu
// 	SetMenu(NULL);

// 	HWND hWndToolBar = CreateSimpleToolBarCtrl(m_hWnd, IDR_MAINFRAME, FALSE, ATL_SIMPLE_TOOLBAR_PANE_STYLE);
// 
// 	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
// 	AddSimpleReBarBand(hWndCmdBar);
// 	AddSimpleReBarBand(hWndToolBar, NULL, TRUE);
// 
// 	CreateSimpleStatusBar();

	m_hWndClient = mView.Create(m_hWnd);

// 	UIAddToolBar(hWndToolBar);
// 	UISetCheck(ID_VIEW_TOOLBAR, 1);
// 	UISetCheck(ID_VIEW_STATUS_BAR, 1);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
//	pLoop->AddIdleHandler(this);

	// todo: read filenames for auto-open
	mUiFilename = "testdata.ui.xml";
	mConfigFilename = "testdata.config.xml";

	BOOL dummy;
	OnRefresh(0, 0, 0, dummy);

	return 0;
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// our dtor doesn't get called, so the view's dtor isn't either...
	mView.Unload();
	return DefWindowProc();
}

LRESULT CMainFrame::OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	PostMessage(WM_CLOSE);
	return 0;
}

LRESULT CMainFrame::OnFileOpen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	::SetCursor(AtlLoadSysCursor(IDC_WAIT));
	mView.Unload();
	::SetCursor(AtlLoadSysCursor(IDC_ARROW));

	mUiFilename.clear();
	mConfigFilename.clear();

	GetOpenFileName("Select Config Settings File", "Config files\0*.config.xml\0\0", mConfigFilename);
	if (mConfigFilename.empty())
		return 0;

	GetOpenFileName("Select UI Settings File", "UI files\0*.ui.xml\0\0", mUiFilename);
	if (mUiFilename.empty())
	{
		mConfigFilename.clear();
		return 0;
	}

	// todo: save filenames for auto-open

	BOOL dummy;
	OnRefresh(0, 0, NULL, dummy);
	return 0;
}

LRESULT CMainFrame::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	::SetCursor(AtlLoadSysCursor(IDC_WAIT));
	mView.Unload();
	mView.Load(mUiFilename, mConfigFilename);

	int width, height;
	mView.GetPreferredSize(width, height);
	if (width && height)
	{
		WINDOWPLACEMENT wp;
		ZeroMemory(&wp, sizeof(WINDOWPLACEMENT));
		GetWindowPlacement(&wp);

		if (SW_SHOWMAXIMIZED == wp.showCmd ||
			SW_SHOWMINIMIZED == wp.showCmd)
		{
			wp.rcNormalPosition.right = wp.rcNormalPosition.left + width;
			wp.rcNormalPosition.bottom = wp.rcNormalPosition.top + height;
			SetWindowPlacement(&wp);
		}
		else
		{
			CRect wndRc;
			GetWindowRect(&wndRc);
			wndRc.right = wndRc.left + width;
			wndRc.bottom = wndRc.top + height;
			MoveWindow(&wndRc);
		}
	}

	::SetCursor(AtlLoadSysCursor(IDC_ARROW));
	return 0;
}

LRESULT CMainFrame::OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	static BOOL bVisible = TRUE;	// initially visible
	bVisible = !bVisible;
	CReBarCtrl rebar = m_hWndToolBar;
	int nBandIndex = rebar.IdToIndex(ATL_IDW_BAND_FIRST + 1);	// toolbar is 2nd added band
	rebar.ShowBand(nBandIndex, bVisible);
	UISetCheck(ID_VIEW_TOOLBAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	BOOL bVisible = !::IsWindowVisible(m_hWndStatusBar);
	::ShowWindow(m_hWndStatusBar, bVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
	UISetCheck(ID_VIEW_STATUS_BAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CAboutDlg dlg;
	dlg.DoModal();
	return 0;
}

void
CMainFrame::GetOpenFileName(const char * const dlgTitle, 
							const char * const fileExt, 
							std::string & selection)
{
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(OPENFILENAME));
	const int kBufLen = 512;
	TCHAR buf[kBufLen + 1] = "";

	ofn.lStructSize       = sizeof(OPENFILENAME);
	ofn.hwndOwner         = m_hWnd;
	ofn.hInstance         = GetModuleHandle(NULL);
	ofn.lpstrFilter       = fileExt;
// 	ofn.lpstrCustomFilter = ;
// 	ofn.nMaxCustFilter    = 0;
// 	ofn.nFilterIndex      = 0;
	ofn.lpstrFile         = buf;
	ofn.nMaxFile          = kBufLen;
// 	ofn.lpstrFileTitle    = NULL;
// 	ofn.nMaxFileTitle     = 0;
//	ofn.lpstrInitialDir   = ;
	ofn.lpstrTitle        = dlgTitle;
	ofn.nFileOffset       = 0;
	ofn.nFileExtension    = 1;
//	ofn.lpstrDefExt       = fileExt;
	ofn.Flags             = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (::GetOpenFileName(&ofn))
		selection = ofn.lpstrFile;
}

void 
trans_func(unsigned int /*u*/, 
		   EXCEPTION_POINTERS* /*pExp*/)
{
    throw SEHexception();
}
