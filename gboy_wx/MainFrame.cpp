// gboy - a portable gameboy emulator
// Copyright (C) 2011  Garrett Smith.
// 
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version.
// 
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.
// 
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include <wx/wx.h>
#include <wx/aboutdlg.h>
#include "MainFrame.h"
#include "DisplayDialog.h"
#include "InputDialog.h"
#include "SoundDialog.h"
#include "RenderBitmap.h"
#include "RenderGL2.h"

// ----------------------------------------------------------------------------
MainFrame::MainFrame(wxWindow *parent, wxConfig *config, const wxChar *title)
: MainFrame_XRC(parent), m_config(config), m_recent(NULL), m_perftimer(NULL),
  m_gbx(NULL), m_render(NULL), m_lastCycles(0)
{
    SetTitle(title);
    SetupStatusBar();
    SetupToolBar();
    SetupRecentList();
    SetupEventHandlers();

    EmulatorEnabled(false);
    LoadConfiguration();

    CreateRenderWidget(m_outputModule);
    CreateEmulatorContext();

    // create a timer firing at one second intervals to report cycles/second
    m_perftimer = new wxTimer(this);
    Connect(m_perftimer->GetId(), wxEVT_TIMER,
            wxTimerEventHandler(MainFrame::OnPerfTimerTick));

    // TODO: make key mapping configurable from settings dialog
    m_keymap['Z']        = INPUT_A;
    m_keymap['X']        = INPUT_B;
    m_keymap[WXK_RETURN] = INPUT_START;
    m_keymap[WXK_SPACE]  = INPUT_SELECT;
    m_keymap[WXK_UP]     = INPUT_UP;
    m_keymap[WXK_DOWN]   = INPUT_DOWN;
    m_keymap[WXK_LEFT]   = INPUT_LEFT;
    m_keymap[WXK_RIGHT]  = INPUT_RIGHT;
}

// ----------------------------------------------------------------------------
MainFrame::~MainFrame()
{
    if (m_perftimer) {
        m_perftimer->Stop();
        SAFE_DELETE(m_perftimer);
    }

    if (m_gbx) {
        m_gbx->Terminate();
        SAFE_DELETE(m_gbx);
    }

    if (m_config) {
        SaveConfiguration();
        SAFE_DELETE(m_recent);
        SAFE_DELETE(m_config);
    }
}

// ----------------------------------------------------------------------------
void MainFrame::LoadConfiguration()
{
    bool showStatusBar, showToolBar;
    int width, height;

    // load window related settings
    m_config->SetPath(wxT("/window"));
    m_config->Read(wxT("show_statusbar"), &showStatusBar, true);
    m_config->Read(wxT("show_toolbar"), &showToolBar, true);
    m_config->Read(wxT("resolution_x"), &width, GBX_LCD_XRES * 3);
    m_config->Read(wxT("resolution_y"), &height, GBX_LCD_YRES * 3);

    GetMenuBar()->Check(XRCID("menu_view_statusbar"), showStatusBar);
    GetMenuBar()->Check(XRCID("menu_view_toolbar"), showToolBar);

    GetStatusBar()->Show(showStatusBar);
    // GetToolBar()->Show(showToolBar);

    SetClientSize(width, height);

    // load display related settings
    m_config->SetPath(wxT("/display"));
    m_config->Read(wxT("output_module"), &m_outputModule, 0);
    m_config->Read(wxT("filter_enable"), &m_filterEnable, false);
    m_config->Read(wxT("filter_type"), &m_filterType, 0);
    m_config->Read(wxT("scale_type"), &m_scalingType, 0);

    // load recent file list
    m_config->SetPath(wxT("/mru"));
    m_recent->Load(*m_config);
}

// ----------------------------------------------------------------------------
void MainFrame::SaveConfiguration()
{
    int width, height;

    // save window related settings
    m_config->SetPath(wxT("/window"));
    m_config->Write(wxT("show_statusbar"), 
            GetMenuBar()->IsChecked(XRCID("menu_view_statusbar")));
    m_config->Write(wxT("show_toolbar"),
            GetMenuBar()->IsChecked(XRCID("menu_view_toolbar")));

    GetClientSize(&width, &height);
    m_config->Write(wxT("resolution_x"), width);
    m_config->Write(wxT("resolution_y"), height);

    // save display related settings
    m_config->SetPath(wxT("/display"));
    m_config->Write(wxT("output_module"), m_outputModule);
    m_config->Write(wxT("filter_enable"), m_render->StretchFilter());
    m_config->Write(wxT("filter_type"), m_render->FilterType());
    m_config->Write(wxT("scale_type"), m_render->ScalingType());

    // save recent file list
    m_config->SetPath(wxT("/mru"));
    m_recent->Save(*m_config);
}

// ----------------------------------------------------------------------------
void MainFrame::SetupStatusBar()
{
    int widths[] = { -1, 120 };
    SetStatusWidths(2, widths);
}

// ----------------------------------------------------------------------------
void MainFrame::SetupToolBar()
{
}

// ----------------------------------------------------------------------------
void MainFrame::SetupRecentList()
{
    const int mru_count = 10;

    // locate the submenu where the recent file list will be placed
    wxMenu *recentMenu;
    if (!GetMenuBar()->FindItem(XRCID("menu_recent_lock"), &recentMenu)) {
        log_err("failed to locate submenu for recent file list\n");
        return;
    }

    // create and initialize the recent file list from the configuration file
    m_recent = new wxFileHistory(mru_count);
    m_recent->UseMenu(recentMenu);
    m_recent->AddFilesToMenu();

    wxWindowID base = m_recent->GetBaseId();
    Connect(base, base + mru_count, wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnRecentOpen));
}

// ----------------------------------------------------------------------------
void MainFrame::SetupEventHandlers()
{
    Connect(XRCID("menu_file_open"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnOpen));
    Connect(XRCID("menu_file_loadstate"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnLoadState));
    Connect(XRCID("menu_file_savestate"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnSaveState));
    Connect(XRCID("menu_file_quit"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnQuit));
    Connect(XRCID("menu_recent_clear"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnRecentClear));
    Connect(XRCID("menu_machine_reset"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnMachineReset));
    Connect(XRCID("menu_machine_pause"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnMachineTogglePause));
    Connect(XRCID("menu_machine_turbo"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnMachineToggleTurbo));
    Connect(XRCID("menu_machine_framestep"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnMachineStep));
    Connect(XRCID("menu_settings_input"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnInputDialog));
    Connect(XRCID("menu_settings_sound"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnSoundDialog));
    Connect(XRCID("menu_settings_display"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnDisplayDialog));
    Connect(XRCID("menu_settings_fs"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnToggleFullscreen));
    Connect(XRCID("menu_settings_vsync"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnToggleVsync));
    Connect(XRCID("menu_view_statusbar"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnToggleStatusbar));
    Connect(XRCID("menu_view_toolbar"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnToggleToolbar));
    Connect(XRCID("menu_help_reportbug"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnReportBug));
    Connect(XRCID("menu_help_about"), wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(MainFrame::OnAbout));

    Connect(wxID_ANY, wxEVT_GBX_SYNC,
            wxCommandEventHandler(MainFrame::OnGbxVideoSync));
    Connect(wxID_ANY, wxEVT_GBX_SPEED,
            wxCommandEventHandler(MainFrame::OnGbxSpeedChange));
    Connect(wxID_ANY, wxEVT_GBX_LCD,
            wxCommandEventHandler(MainFrame::OnGbxLcdEnabled));
    Connect(wxID_ANY, wxEVT_KEY_DOWN,
            wxKeyEventHandler(MainFrame::OnKeyDown));
    Connect(wxID_ANY, wxEVT_KEY_UP,
            wxKeyEventHandler(MainFrame::OnKeyUp));
    Connect(wxID_ANY, wxEVT_ERASE_BACKGROUND,
            wxEraseEventHandler(MainFrame::OnEraseBackground));
}

// ----------------------------------------------------------------------------
void MainFrame::LoadFile(const wxString &path)
{
    if (m_gbx->IsRunning())
        CreateEmulatorContext();

    if (!m_gbx->LoadFile(path)) {
        wxString error = wxT("Failed to open file:\n") + path;
        wxMessageBox(error, wxT("Error"), wxICON_ERROR);
        EmulatorEnabled(false);
        return;
    }

    m_gbx->Run();

    m_lastCycles = 0;
    m_perftimer->Start(1000, false);
    EmulatorEnabled(true);

    if (!GetMenuBar()->IsChecked(XRCID("menu_recent_lock"))) {
        // add the selected file to the MRU list (unless list is locked)
        m_recent->AddFileToHistory(path);
    }
}

// ----------------------------------------------------------------------------
void MainFrame::CreateRenderWidget(int type)
{
    int attrib_list[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, 0 };

    if (m_render) {
        // XXX: this is ugly, but delete operator is unsafe on a wxWindow
        m_filterEnable = m_render->StretchFilter();
        m_filterType = m_render->FilterType();
        m_scalingType = m_render->ScalingType();
        m_render->Window()->Destroy();
    }


    switch (type) {
    default:
    case 0:
        m_render = new RenderBitmap(this);
        break;
    case 1:
        // need to force window visibility here so the GL context can be used
        Show(true);
        m_render = new RenderGL2(this, NULL, attrib_list);
        break;
    }

    m_render->SetStretchFilter(m_filterEnable);
    m_render->SetFilterType(m_filterType);
    m_render->SetScalingType(m_scalingType);

    // have the main frame handle keyboard events sent to the render panel
    m_render->Window()->Connect(wxID_ANY, wxEVT_KEY_DOWN,
            wxKeyEventHandler(MainFrame::OnKeyDown), NULL, this);
    m_render->Window()->Connect(wxID_ANY, wxEVT_KEY_UP,
            wxKeyEventHandler(MainFrame::OnKeyUp), NULL, this);

    m_render->Window()->SetSize(GetClientSize());
    m_render->Window()->SetFocus();
}

// ----------------------------------------------------------------------------
void MainFrame::CreateEmulatorContext()
{
    if (m_gbx) {
        m_gbx->Terminate();
        delete m_gbx;
    }

    m_gbx = new gbxThread(this, SYSTEM_AUTO);
    if (wxTHREAD_NO_ERROR != m_gbx->Create())
        wxLogError(wxT("Failed to create gbx thread!"));
}

// ----------------------------------------------------------------------------
void MainFrame::EmulatorEnabled(bool enable)
{
    GetMenuBar()->Enable(XRCID("menu_machine_reset"), enable);
    GetMenuBar()->Enable(XRCID("menu_machine_pause"), enable);
    GetMenuBar()->Enable(XRCID("menu_machine_turbo"), enable);
    GetMenuBar()->Enable(XRCID("menu_machine_framestep"), enable);

    GetMenuBar()->Check(XRCID("menu_machine_pause"), false);
    GetMenuBar()->Check(XRCID("menu_machine_turbo"), false);
    
    SetStatusText(enable ? wxT("Running") : wxT("Stopped"), 0);
}

// ----------------------------------------------------------------------------
void MainFrame::OnOpen(wxCommandEvent &event)
{
    static const wxChar *SupportedFileTypes = wxT(
            "Game Boy Images|*.dmg;*.gb;*.gbc;*.cgb;*.sgb|"
            "DMG Game Boy Images|*.dmg;*.gb|"
            "Color Game Boy Images|*.gbc;*.cgb|"
            "All Files|*.*");

    wxFileDialog *fd = new wxFileDialog(this, wxT("Open"),
            wxT(""), wxT(""), SupportedFileTypes, wxOPEN);

    if (wxID_OK == fd->ShowModal()) {
        LoadFile(fd->GetPath());
    }
}

// ----------------------------------------------------------------------------
void MainFrame::OnRecentOpen(wxCommandEvent &event)
{
    int index = event.GetId() - m_recent->GetBaseId();
    LoadFile(m_recent->GetHistoryFile(index));
}

// ----------------------------------------------------------------------------
void MainFrame::OnRecentClear(wxCommandEvent &event)
{
    while (m_recent->GetCount())
        m_recent->RemoveFileFromHistory(0);
}

// ----------------------------------------------------------------------------
void MainFrame::OnLoadState(wxCommandEvent &event)
{
    wxMessageBox(wxT("TODO"), wxT("Load State"), wxOK | wxICON_ERROR, this);
}

// ----------------------------------------------------------------------------
void MainFrame::OnSaveState(wxCommandEvent &event)
{
    wxMessageBox(wxT("TODO"), wxT("Save State"), wxOK | wxICON_ERROR, this);
}

// ----------------------------------------------------------------------------
void MainFrame::OnQuit(wxCommandEvent &event)
{
    Close(true);
}

// ----------------------------------------------------------------------------
void MainFrame::OnMachineReset(wxCommandEvent &event)
{
    m_gbx->Reset();
}

// ----------------------------------------------------------------------------
void MainFrame::OnMachineTogglePause(wxCommandEvent &event)
{
    bool paused = GetMenuBar()->IsChecked(XRCID("menu_machine_pause"));
    m_gbx->SetPaused(paused);
    m_lastCycles = 0;

    if (paused) {
        log_info("Emulator paused.\n");
        SetStatusText(wxT("Paused"), 0);
        m_perftimer->Stop();
    }
    else {
        log_info("Emulator unpaused.\n");
        SetStatusText(wxT("Running"), 0);
        m_perftimer->Start();
    }
}

// ----------------------------------------------------------------------------
void MainFrame::OnMachineToggleTurbo(wxCommandEvent &event)
{
    bool turbo = GetMenuBar()->IsChecked(XRCID("menu_machine_turbo"));
    m_gbx->SetThrottleEnabled(!turbo);
}

// ----------------------------------------------------------------------------
void MainFrame::OnMachineStep(wxCommandEvent &event)
{
    wxMessageBox(wxT("TODO"), wxT("Frame Step"), wxOK | wxICON_ERROR, this);
}

// ----------------------------------------------------------------------------
void MainFrame::OnInputDialog(wxCommandEvent &event)
{
    InputDialog *dialog = new InputDialog(this);
    if (wxID_OK == dialog->ShowModal()) {
        // commit input settings
    }
}

// ----------------------------------------------------------------------------
void MainFrame::OnSoundDialog(wxCommandEvent &event)
{
    SoundDialog *dialog = new SoundDialog(this);
    if (wxID_OK == dialog->ShowModal()) {
        // commit sound settings
    }
}

// ----------------------------------------------------------------------------
void MainFrame::OnDisplayDialog(wxCommandEvent &event)
{
    bool paused = m_gbx->Paused();
    m_gbx->SetPaused(true);

    // create and initialize display dialog based on current settings
    DisplayDialog *dialog = new DisplayDialog(this);

    dialog->SetOutputModule(m_outputModule);
    dialog->SetFilterEnabled(m_render->StretchFilter());
    dialog->SetFilterType(m_render->FilterType());
    dialog->SetScalingType(m_render->ScalingType());

    if (wxID_OK == dialog->ShowModal()) {
        // commit updated settings
        if (m_outputModule != dialog->OutputModule()) {
            m_outputModule = dialog->OutputModule();
            CreateRenderWidget(m_outputModule);
        }

        m_render->SetStretchFilter(dialog->FilterEnabled());
        m_render->SetFilterType(dialog->FilterType());
        m_render->SetScalingType(dialog->ScalingType());
    }

    m_gbx->SetPaused(paused);
}

// ----------------------------------------------------------------------------
void MainFrame::OnToggleStatusbar(wxCommandEvent &event)
{
    bool checked = GetMenuBar()->IsChecked(XRCID("menu_view_statusbar"));
    GetStatusBar()->Show(checked);
    m_render->Window()->SetSize(GetClientSize());
}

// ----------------------------------------------------------------------------
void MainFrame::OnToggleToolbar(wxCommandEvent &event)
{
}

// ----------------------------------------------------------------------------
void MainFrame::OnToggleFullscreen(wxCommandEvent &event)
{
    bool fullscreen = GetMenuBar()->IsChecked(XRCID("menu_settings_fs"));

    log_info("Full screen %s.\n", fullscreen ? "enabled" : "disabled");
    ShowFullScreen(fullscreen);
}

// ----------------------------------------------------------------------------
void MainFrame::OnToggleVsync(wxCommandEvent &event)
{
    bool vsync = GetMenuBar()->IsChecked(XRCID("menu_settings_vsync"));

    log_info("Vertical sync %s.\n", vsync ? "enabled" : "disabled");
    m_render->SetSwapInterval(vsync ? 1 : 0);
}

// ----------------------------------------------------------------------------
void MainFrame::OnReportBug(wxCommandEvent &event)
{
    wxLaunchDefaultBrowser(wxT("http://code.google.com/p/gboy/issues/list"));
}

static const wxString License_GPLV2 =
wxT("This program is free software; you can redistribute it and/or modify\n")
wxT("it under the terms of the GNU General Public License as published by\n")
wxT("the Free Software Foundation; either version 2 of the License, or (at\n")
wxT("your option) any later version.\n\n")
wxT("This program is distributed in the hope that it will be useful, but\n")
wxT("WITHOUT ANY WARRANTY; without even the implied warranty of\n")
wxT("MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n")
wxT("GNU General Public License for more details.\n\n")
wxT("You should have received a copy of the GNU General Public License along\n")
wxT("with this program; if not, write to the Free Software Foundation, Inc.,\n")
wxT("51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n");

// ----------------------------------------------------------------------------
void MainFrame::OnAbout(wxCommandEvent &event)
{
    wxAboutDialogInfo info;
    info.SetName(wxT("gboy"));
    info.SetVersion(wxT("0.1-dev"));
    info.SetDescription(wxT("A portable Nintendo Game Boy emulator."));
    info.SetCopyright(wxT("(C) 2011 Garrett Smith"));
    info.SetLicense(License_GPLV2);
    info.SetWebSite(wxT("http://code.google.com/p/gboy"));
    info.AddDeveloper(wxT("Garrett Smith <gcs2980@rit.edu>"));
    wxAboutBox(info);
}

// ----------------------------------------------------------------------------
void MainFrame::OnGbxVideoSync(wxCommandEvent &event)
{
    m_render->UpdateFramebuffer(m_gbx->Framebuffer());
}

// ----------------------------------------------------------------------------
void MainFrame::OnGbxSpeedChange(wxCommandEvent &event)
{
    log_err("TODO: OnGbxSpeedChange\n");
}

// ----------------------------------------------------------------------------
void MainFrame::OnGbxLcdEnabled(wxCommandEvent &event)
{
    if (event.GetInt() == 0)
        m_render->ClearFramebuffer(0xFF);
}

// ----------------------------------------------------------------------------
void MainFrame::OnKeyDown(wxKeyEvent &event)
{
    KeyMap::iterator i = m_keymap.find(event.GetKeyCode());
    if (m_keymap.end() != i)
        m_gbx->SetInputState(i->second, 1);
}

// ----------------------------------------------------------------------------
void MainFrame::OnKeyUp(wxKeyEvent &event)
{
    KeyMap::iterator i = m_keymap.find(event.GetKeyCode());
    if (m_keymap.end() != i)
        m_gbx->SetInputState(i->second, 0);
}

// ----------------------------------------------------------------------------
void MainFrame::OnPerfTimerTick(wxTimerEvent &event)
{
    if (m_lastCycles <= 0) {
        m_lastCycles = m_gbx->CycleCount();
        return;
    }

    long cycles = m_gbx->CycleCount();
    float cps = cycles - m_lastCycles;
    m_lastCycles = cycles;

    float mhz = cps / 1000000.0f;
    int percent = (int)(0.5f + 100.0f * cps / CPU_FREQ_DMG);
    wxString label = wxString::Format(wxT("%.2f MHz / %d%%"), mhz, percent);
    SetStatusText(label, 1);
}

// ----------------------------------------------------------------------------
void MainFrame::OnEraseBackground(wxEraseEvent &event)
{
    // override EraseBackground to avoid flickering on some platforms
}
