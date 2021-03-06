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
#include "RenderWidget.h"

// ----------------------------------------------------------------------------
MainFrame::MainFrame(wxWindow *parent, wxConfig *config, const wxString &title)
: MainFrame_XRC(parent), m_title(title), m_config(config), m_recent(NULL),
  m_perftimer(NULL), m_gbx(NULL), m_render(NULL), m_lastCycles(0)
{
    SetTitle(title);
    SetupStatusBar();
    SetupToolBar();
    SetupRecentList();
    SetupEventHandlers();
    SetMinSize(wxSize(80, 80));

    SetEmulatorEnabled(false);
    LoadConfiguration();

    CreateRenderWidget(m_outputModule);
    CreateEmulatorContext();

    // create a timer firing at one second intervals to report cycles/second
    m_perftimer = new wxTimer(this);
    Bind(wxEVT_TIMER, &MainFrame::OnPerfTimerTick, this, m_perftimer->GetId());

    m_console = new ConsoleFrame(this);
    m_console->Show(false);
    m_console->Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnCloseConsole, this);
}

// ----------------------------------------------------------------------------
MainFrame::~MainFrame()
{
    Shutdown();
}

// ----------------------------------------------------------------------------
void MainFrame::Shutdown()
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
    bool showStatusBar, showToolBar, fullscreen, vsync;
    int width, height, keycode;

    // load window related settings
    m_config->SetPath("/window");
    m_config->Read("show_statusbar", &showStatusBar, true);
    m_config->Read("show_toolbar", &showToolBar, true);
    m_config->Read("resolution_x", &width, GBX_LCD_XRES * 3);
    m_config->Read("resolution_y", &height, GBX_LCD_YRES * 3);
    m_config->Read("fullscreen", &fullscreen, false);

    // load display related settings
    m_config->SetPath("/display");
    m_config->Read("output_module", &m_outputModule, 0);
    m_config->Read("filter_enable", &m_filterEnable, false);
    m_config->Read("filter_type", &m_filterType, 0);
    m_config->Read("scale_type", &m_scalingType, 0);
    m_config->Read("vsync_enable", &vsync, false);

    // load key mappings
    static const int default_keycodes[8] = {
        WXK_RIGHT, WXK_LEFT, WXK_UP, WXK_DOWN, 'Z', 'X', WXK_SPACE, WXK_RETURN
    };

    m_config->SetPath("/input");
    for (int i = 0; i < 8; i++) {
        wxString input_str = wxString::Format("input_keycode_%d", i);
        if (m_config->Read(input_str, &keycode))
            m_keymap[keycode] = i;
        else
            m_keymap[default_keycodes[i]] = i;
    }

    // load recent file list
    m_config->SetPath("/mru");
    m_recent->Load(*m_config);

    SetStatusBarEnabled(showStatusBar);
    SetToolBarEnabled(showToolBar);
    SetFullscreenEnabled(fullscreen);
    SetVsyncEnabled(vsync);

    SetClientSize(width, height);
}

// ----------------------------------------------------------------------------
void MainFrame::SaveConfiguration()
{
    int width, height;
    GetClientSize(&width, &height);

    // save window related settings
    m_config->SetPath("/window");
    m_config->Write("show_statusbar", StatusBarEnabled());
    m_config->Write("show_toolbar", ToolBarEnabled());
    m_config->Write("resolution_x", width);
    m_config->Write("resolution_y", height);
    m_config->Write("fullscreen", FullscreenEnabled());

    // save display related settings
    m_config->SetPath("/display");
    m_config->Write("output_module", m_outputModule);
    m_config->Write("filter_enable", m_render->StretchFilter());
    m_config->Write("filter_type", m_render->FilterType());
    m_config->Write("scale_type", m_render->ScalingType());
    m_config->Write("vsync_enable", VsyncEnabled());

    // save key mappings
    m_config->SetPath("/input");
    for (int i = 0; i < 8; i++) {
        // need to scan the map values rather than keys, but map is small...
        int keycode = -1;
        KeyMap::iterator iter;
        for (iter = m_keymap.begin(); iter != m_keymap.end(); ++iter) {
            if (iter->second == i) keycode = iter->first;
        }

        wxString input_str = wxString::Format("input_keycode_%d", i);
        m_config->Write(input_str, keycode);
    }

    // save recent file list
    m_config->SetPath("/mru");
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
        //log_err("failed to locate submenu for recent file list\n");
        return;
    }

    // create and initialize the recent file list from the configuration file
    m_recent = new wxFileHistory(mru_count);
    m_recent->UseMenu(recentMenu);
    m_recent->AddFilesToMenu();

    wxWindowID base = m_recent->GetBaseId();
    Bind(wxEVT_COMMAND_MENU_SELECTED, &MainFrame::OnRecentOpen, this,
         base, base + mru_count);
}

// some helper macros to make the event handler setup more readable

#define xrc_menu_evt(id, fn) \
    Bind(wxEVT_COMMAND_MENU_SELECTED, &fn, this, XRCID(id))

// ----------------------------------------------------------------------------
void MainFrame::SetupEventHandlers()
{
    xrc_menu_evt("menu_file_loadstate",   MainFrame::OnLoadState);
    xrc_menu_evt("menu_file_savestate",   MainFrame::OnSaveState);
    xrc_menu_evt("menu_recent_clear",     MainFrame::OnRecentClear);
    xrc_menu_evt("menu_machine_reset",    MainFrame::OnMachineReset);
    xrc_menu_evt("menu_machine_pause",    MainFrame::OnMachineTogglePause);
    xrc_menu_evt("menu_machine_turbo",    MainFrame::OnMachineToggleTurbo);
    xrc_menu_evt("menu_machine_step",     MainFrame::OnMachineStep);
    xrc_menu_evt("menu_settings_input",   MainFrame::OnInputDialog);
    xrc_menu_evt("menu_settings_sound",   MainFrame::OnSoundDialog);
    xrc_menu_evt("menu_settings_display", MainFrame::OnDisplayDialog);
    xrc_menu_evt("menu_settings_fs",      MainFrame::OnToggleFullscreen);
    xrc_menu_evt("menu_settings_vsync",   MainFrame::OnToggleVsync);
    xrc_menu_evt("menu_view_statusbar",   MainFrame::OnToggleStatusbar);
    xrc_menu_evt("menu_view_toolbar",     MainFrame::OnToggleToolbar);
    xrc_menu_evt("menu_view_console",     MainFrame::OnToggleConsoleWindow);
    xrc_menu_evt("menu_help_reportbug",   MainFrame::OnReportBug);

    // bind standard menu item events
    
    Bind(wxEVT_COMMAND_MENU_SELECTED, &MainFrame::OnOpen,  this, wxID_OPEN);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &MainFrame::OnClose, this, wxID_CLOSE);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &MainFrame::OnQuit,  this, wxID_EXIT);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &MainFrame::OnAbout, this, wxID_ABOUT);

    // bind gbx emulator events

    Bind(wxEVT_GBX_LOG,   &MainFrame::OnGbxLogMessage,  this);
    Bind(wxEVT_GBX_SYNC,  &MainFrame::OnGbxVideoSync,   this);
    Bind(wxEVT_GBX_SPEED, &MainFrame::OnGbxSpeedChange, this);
    Bind(wxEVT_GBX_LCD,   &MainFrame::OnGbxLcdEnabled,  this);

    // bind window events

    Bind(wxEVT_CLOSE_WINDOW,     &MainFrame::OnCloseWindow,     this);
    Bind(wxEVT_KEY_DOWN,         &MainFrame::OnKeyDown,         this);
    Bind(wxEVT_KEY_UP,           &MainFrame::OnKeyUp,           this);
    Bind(wxEVT_ERASE_BACKGROUND, &MainFrame::OnEraseBackground, this);
}

// ----------------------------------------------------------------------------
void MainFrame::LoadFile(const wxString &path)
{
    wxFileName filename(path);
    filename.MakeAbsolute();

    if (m_gbx->IsRunning())
        CreateEmulatorContext();

    if (!m_gbx->LoadFile(path)) {
        wxString error = "Failed to open file:\n" + path;
        wxMessageBox(error, "Error", wxICON_ERROR);
        SetEmulatorEnabled(false);
        return;
    }

    m_gbx->Run();

    m_lastCycles = 0;
    m_perftimer->Start(1000, false);
    SetEmulatorEnabled(true);

    if (!GetMenuBar()->IsChecked(XRCID("menu_recent_lock"))) {
        // add the selected file to the MRU list (unless list is locked)
        m_recent->AddFileToHistory(filename.GetFullPath());
    }

    SetTitle(filename.GetFullName());
}

// ----------------------------------------------------------------------------
void MainFrame::CreateRenderWidget(int type)
{
    if (m_render) {
        m_filterEnable = m_render->StretchFilter();
        m_filterType = m_render->FilterType();
        m_scalingType = m_render->ScalingType();
        SAFE_DELETE(m_render);
    }

    m_render = RenderWidget::Allocate(type);
    m_render->Create(this, GBX_LCD_XRES, GBX_LCD_YRES);

    m_render->SetStretchFilter(m_filterEnable);
    m_render->SetFilterType(m_filterType);
    m_render->SetScalingType(m_scalingType);
    m_render->SetSwapInterval(VsyncEnabled() ? 1 : 0);

    // have the main frame handle keyboard events sent to the render panel
    m_render->Window()->Bind(wxEVT_KEY_DOWN, &MainFrame::OnKeyDown, this);
    m_render->Window()->Bind(wxEVT_KEY_UP,   &MainFrame::OnKeyUp,   this);

    m_render->Window()->SetSize(GetClientSize());
    m_render->Window()->SetFocus();
}

// ----------------------------------------------------------------------------
void MainFrame::CreateEmulatorContext()
{
    if (m_gbx) {
        m_gbx->Terminate();
        SAFE_DELETE(m_gbx);
    }

    m_gbx = new gbxThread(this, SYSTEM_AUTO);
    if (wxTHREAD_NO_ERROR != m_gbx->Create()) {
        wxLogError("Failed to create gbx thread!");
    }
}

// ----------------------------------------------------------------------------
void MainFrame::SetEmulatorEnabled(bool enable)
{
    GetMenuBar()->Enable(XRCID("menu_machine_reset"), enable);
    GetMenuBar()->Enable(XRCID("menu_machine_pause"), enable);
    GetMenuBar()->Enable(XRCID("menu_machine_turbo"), enable);
    GetMenuBar()->Enable(XRCID("menu_machine_step"), enable);
    GetMenuBar()->Enable(wxID_CLOSE, enable);

    GetMenuBar()->Check(XRCID("menu_machine_pause"), false);
    GetMenuBar()->Check(XRCID("menu_machine_turbo"), false);
    
    SetStatusText(enable ? "Running" : "Stopped", 0);
}

// ----------------------------------------------------------------------------
void MainFrame::SetStatusBarEnabled(bool enable)
{
    if (GetStatusBar())
        GetStatusBar()->Show(enable);
    if (m_render)
        m_render->Window()->SetSize(GetClientSize());
    GetMenuBar()->Check(XRCID("menu_view_statusbar"), enable);
}

// ----------------------------------------------------------------------------
void MainFrame::SetToolBarEnabled(bool enable)
{
    if (GetToolBar())
        GetToolBar()->Show(enable);
    if (m_render)
        m_render->Window()->SetSize(GetClientSize());
    GetMenuBar()->Check(XRCID("menu_view_toolbar"), enable);
}

// ----------------------------------------------------------------------------
void MainFrame::SetConsoleEnabled(bool enable)
{
    m_console->Show(enable);
    GetMenuBar()->Check(XRCID("menu_view_console"), enable);
}

// ----------------------------------------------------------------------------
void MainFrame::SetFullscreenEnabled(bool enable)
{
    //log_info("Full screen %s.\n", enable ? "enabled" : "disabled");
    ShowFullScreen(enable);
    GetMenuBar()->Check(XRCID("menu_settings_fs"), enable);
}

// ----------------------------------------------------------------------------
void MainFrame::SetVsyncEnabled(bool enable)
{
    //log_info("Vertical sync %s.\n", enable ? "enabled" : "disabled");
    if (m_render)
        m_render->SetSwapInterval(enable ? 1 : 0);

    GetMenuBar()->Check(XRCID("menu_settings_vsync"), enable);
}

// ----------------------------------------------------------------------------
void MainFrame::OnOpen(wxCommandEvent &event)
{
    static const char *SupportedFileTypes =
            "Game Boy Images|*.dmg;*.gb;*.gbc;*.cgb;*.sgb|"
            "DMG Game Boy Images|*.dmg;*.gb|"
            "Color Game Boy Images|*.gbc;*.cgb|"
            "All Files|*.*";

    wxFileDialog *fd = new wxFileDialog(this,
        "Open", "", "", SupportedFileTypes, wxFD_OPEN);

    if (wxID_OK == fd->ShowModal()) {
        LoadFile(fd->GetPath());
    }
}

// ----------------------------------------------------------------------------
void MainFrame::OnClose(wxCommandEvent &event)
{
    assert(NULL != m_render);

    SetTitle(m_title);
    CreateEmulatorContext();
    SetEmulatorEnabled(false);
    m_render->ClearFramebuffer(0);
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
    wxMessageBox("TODO", "Load State", wxOK | wxICON_ERROR, this);
}

// ----------------------------------------------------------------------------
void MainFrame::OnSaveState(wxCommandEvent &event)
{
    wxMessageBox("TODO", "Save State", wxOK | wxICON_ERROR, this);
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
        //log_info("Emulator paused.\n");
        SetStatusText("Paused", 0);
        m_perftimer->Stop();
    }
    else {
        //log_info("Emulator unpaused.\n");
        SetStatusText("Running", 0);
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
    wxMessageBox("TODO", "Frame Step", wxOK | wxICON_ERROR, this);
}

// ----------------------------------------------------------------------------
void MainFrame::OnInputDialog(wxCommandEvent &event)
{
    bool oldPauseState = m_gbx->SetPaused(true);
    InputDialog *dialog = new InputDialog(this);
    dialog->SetKeyMappings(m_keymap);

    if (wxID_OK == dialog->ShowModal()) {
        // commit input settings
        dialog->GetKeyMappings(m_keymap);
    }

    m_gbx->SetPaused(oldPauseState);
}

// ----------------------------------------------------------------------------
void MainFrame::OnSoundDialog(wxCommandEvent &event)
{
    bool oldPauseState = m_gbx->SetPaused(true);
    SoundDialog *dialog = new SoundDialog(this);

    if (wxID_OK == dialog->ShowModal()) {
        // commit sound settings
    }

    m_gbx->SetPaused(oldPauseState);
}

// ----------------------------------------------------------------------------
void MainFrame::OnDisplayDialog(wxCommandEvent &event)
{
    assert(NULL != m_gbx);
    assert(NULL != m_render);

    bool oldPauseState = m_gbx->SetPaused(true);

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

    m_gbx->SetPaused(oldPauseState);
}

// ----------------------------------------------------------------------------
void MainFrame::OnToggleStatusbar(wxCommandEvent &event)
{
    SetStatusBarEnabled(GetMenuBar()->IsChecked(XRCID("menu_view_statusbar")));
}

// ----------------------------------------------------------------------------
void MainFrame::OnToggleToolbar(wxCommandEvent &event)
{
    SetToolBarEnabled(GetMenuBar()->IsChecked(XRCID("menu_view_toolbar")));
}

// ----------------------------------------------------------------------------
void MainFrame::OnToggleConsoleWindow(wxCommandEvent &event)
{
    SetConsoleEnabled(GetMenuBar()->IsChecked(XRCID("menu_view_console")));
}

// ----------------------------------------------------------------------------
void MainFrame::OnToggleFullscreen(wxCommandEvent &event)
{
    SetFullscreenEnabled(GetMenuBar()->IsChecked(XRCID("menu_settings_fs")));
}

// ----------------------------------------------------------------------------
void MainFrame::OnToggleVsync(wxCommandEvent &event)
{
    SetVsyncEnabled(GetMenuBar()->IsChecked(XRCID("menu_settings_vsync")));
}

// ----------------------------------------------------------------------------
void MainFrame::OnReportBug(wxCommandEvent &event)
{
    wxLaunchDefaultBrowser("http://code.google.com/p/gboy/issues/list");
}

static const wxString License_GPLV2 =
    "This program is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 2 of the License, or (at\n"
    "your option) any later version.\n\n"
    "This program is distributed in the hope that it will be useful, but\n"
    "WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n\n"
    "You should have received a copy of the GNU General Public License along\n"
    "with this program; if not, write to the Free Software Foundation, Inc.,\n"
    "51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n";

// ----------------------------------------------------------------------------
void MainFrame::OnAbout(wxCommandEvent &event)
{
    wxAboutDialogInfo info;
    info.SetName("gboy");
    info.SetVersion(GBOY_VERSION_STR);
    info.SetDescription("A portable Nintendo Game Boy emulator.");
    info.SetCopyright("(C) 2011-2012 Garrett Smith");
    info.SetLicense(License_GPLV2);
    info.SetWebSite("http://github.com/gcsmith/gboy");
    info.AddDeveloper("Garrett Smith <gcs2980@rit.edu>");
    wxAboutBox(info);
}

// ----------------------------------------------------------------------------
void MainFrame::OnGbxLogMessage(wxThreadEvent &event)
{
    m_console->Write(event.GetString());
}

// ----------------------------------------------------------------------------
void MainFrame::OnGbxVideoSync(wxThreadEvent &event)
{
    assert(NULL != m_gbx);
    assert(NULL != m_render);

    m_render->UpdateFramebuffer(m_gbx->Framebuffer());
}

// ----------------------------------------------------------------------------
void MainFrame::OnGbxSpeedChange(wxThreadEvent &event)
{
    //log_err("TODO: OnGbxSpeedChange\n");
}

// ----------------------------------------------------------------------------
void MainFrame::OnGbxLcdEnabled(wxThreadEvent &event)
{
    assert(NULL != m_render);

    if (event.GetInt() == 0)
        m_render->ClearFramebuffer(0xFF);
}

// ----------------------------------------------------------------------------
void MainFrame::OnCloseConsole(wxCloseEvent &evt)
{
    SetConsoleEnabled(false);
    evt.Skip();
}

// ----------------------------------------------------------------------------
void MainFrame::OnCloseWindow(wxCloseEvent &evt)
{
    Shutdown();
    evt.Skip();
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
        m_timer.Start();
        return;
    }

    long curr_cycles = m_gbx->CycleCount();
    long cycle_delta = curr_cycles - m_lastCycles;

    PrecisionTimer::Real hz = cycle_delta / m_timer.Elapsed();
    PrecisionTimer::Real mhz = hz / 1000000;

    int percent = (int)(0.5 + 100 * hz / CPU_FREQ_DMG);
    wxString label = wxString::Format("%.2f MHz / %d%%", mhz, percent);
    SetStatusText(label, 1);

    m_lastCycles = curr_cycles;
    m_timer.Start();
}

// ----------------------------------------------------------------------------
void MainFrame::OnEraseBackground(wxEraseEvent &event)
{
    // override EraseBackground to avoid flickering on some platforms
}

