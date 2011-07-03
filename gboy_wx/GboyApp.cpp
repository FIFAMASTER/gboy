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
#include <wx/cmdline.h>
#include <wx/xrc/xmlres.h>
#include "gbx.h"
#include "GboyApp.h"
#include "MainFrame.h"
#include "gbxThread.h"

IMPLEMENT_APP(GboyApp);

// generated from the XRC compiler
extern void InitXmlResource();

static const wxCmdLineEntryDesc g_cmdlineDesc[] = {
    cmd_opts("b", "bios-dir",    "specify where bios files are located"),
    cmd_optb("d", "debugger",    "enable debugging interface"),
    cmd_optb("f", "fullscreen",  "run in fullscreen mode"),
    cmd_opts("",  "log-serial",  "log serial output to file"),
    cmd_optb("",  "no-sound",    "disable sound playback"),
    cmd_opts("r", "rom-file",    "load and execute specified rom file"),
    cmd_optb("",  "system-dmg",  "force system type to original game boy"),
    cmd_optb("",  "system-cgb",  "force system type to game boy color"),
    cmd_optb("",  "system-sgb",  "force system type to super game boy"),
    cmd_optb("",  "system-sgb2", "force system type to super game boy 2"),
    cmd_optb("",  "system-gba",  "force system type to game boy advance"),
    cmd_optb("t", "turbo",       "disable cpu throttling (no speed limit)"),
    cmd_optb("v", "vsync",       "enable vertical sync mode"),
    cmd_help("h", "help",        "display this usage message"),
    cmd_optb("",  "version",     "display program version information"),
    cmd_parm("rom-file-path"),
    cmd_term
};

// ----------------------------------------------------------------------------
GboyApp::GboyApp()
: m_vsync(false), m_turbo(false), m_system(SYSTEM_AUTO)
{
}

// ----------------------------------------------------------------------------
bool GboyApp::OnInit()
{
    if (!wxApp::OnInit())
        return false;

    // initialize the xml resource system
    wxXmlResource::Get()->InitAllHandlers();
    InitXmlResource();

    wxConfig *config = new wxConfig(wxT("gboy_wx"));

    // allow command line arguments to override the configuration settings
    if (m_fullscreen)
        config->Write(wxT("/window/fullscreen"), true);
    if (m_vsync)
        config->Write(wxT("/display/vsync_enable"), true);
    if (m_turbo)
        config->Write(wxT("/machine/turbo"), true);

    MainFrame *frame = new MainFrame(NULL, config, wxT("gboy " GBOY_VER_STR));
    frame->Show(true);

    if (m_romfile.length())
        frame->LoadFile(m_romfile);

    SetTopWindow(frame);
    return true;
}

// ----------------------------------------------------------------------------
int GboyApp::OnExit()
{
    return 0;
}

// ----------------------------------------------------------------------------
void GboyApp::OnInitCmdLine(wxCmdLineParser &parser)
{
    parser.SetDesc(g_cmdlineDesc);
    parser.SetSwitchChars(wxT("-"));
}

// ----------------------------------------------------------------------------
bool GboyApp::OnCmdLineParsed(wxCmdLineParser &parser)
{
    wxArrayString params;

    // check if user specified rom path with --rom-file or as an extra param
    wxString romfile;
    if (parser.Found(wxT("r"), &romfile))
        params.Add(romfile);

    for (unsigned int i = 0; i < parser.GetParamCount(); ++i)
        params.Add(parser.GetParam(i));

    if (params.Count()) {
        if (params.Count() > 1) {
            log_err("Unexpected trailing arguments\n");
            parser.Usage();
            return false;
        }
        m_romfile = params[0];
    }

    // check the remaining switches
    m_fullscreen = parser.Found(wxT("fullscreen"));
    m_vsync = parser.Found(wxT("vsync"));
    m_turbo = parser.Found(wxT("turbo"));

    return true;
}

