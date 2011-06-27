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

#include <GL/glew.h>
#ifdef PLATFORM_WIN32
#include <GL/wglew.h>
#elif defined(PLATFORM_UNIX)
#include <GL/glxew.h>
#endif
#include "RenderWidget.h"
#include "gbx.h"

// ----------------------------------------------------------------------------
// Return the nearest square, pow2 texture dimension >= MAX(width, height).
unsigned int tex_pow2(unsigned int width, unsigned int height)
{
    unsigned int input = MAX(width, height);
    unsigned int value = 2;

    if (0 == (input & (input - 1)))
        return input;

    while (0 != (input >>= 1))
        value <<= 1;
    return value;
}

// ----------------------------------------------------------------------------
RenderWidget::RenderWidget(wxWindow *parent, wxGLContext *context, int *attrib)
: wxGLCanvas(parent, wxID_ANY, attrib, wxDefaultPosition, wxDefaultSize),
  m_parent(parent), m_context(context), m_init(false), m_stretch(false)
{
    if (!m_context)
        m_context = new wxGLContext(this);

    Connect(GetId(), wxEVT_SIZE,
            wxSizeEventHandler(RenderWidget::OnSize));
    Connect(GetId(), wxEVT_PAINT,
            wxPaintEventHandler(RenderWidget::OnPaint));
    Connect(GetId(), wxEVT_ERASE_BACKGROUND,
            wxEraseEventHandler(RenderWidget::OnEraseBackground));
    Connect(GetId(), wxEVT_KEY_DOWN,
            wxKeyEventHandler(RenderWidget::OnKeyDown));
    Connect(GetId(), wxEVT_KEY_UP,
            wxKeyEventHandler(RenderWidget::OnKeyUp));

    m_width  = GBX_LCD_XRES;
    m_height = GBX_LCD_YRES;

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
void RenderWidget::InitGL()
{
    log_info("Initializing GLEW...\n");

    GLint rc = glewInit();
    if (GLEW_OK != rc)
        log_err("failed to initialize GLEW (%s)\n", glewGetErrorString(rc));

    m_pboSize = m_width * m_height * 4;
    m_textureDim = tex_pow2(m_width, m_height);

    m_tu = m_width  / (float)(m_textureDim + 1);
    m_tv = m_height / (float)(m_textureDim + 1);

    m_x0 = m_y0 = -1.0f;
    m_x1 = m_y1 = 1.0f;

    // set some reasonable defaults for rendering a 2d scene
    glClearColor(0, 0, 0, 0);

    glShadeModel(GL_FLAT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    // create the render texture and pbo
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
            m_textureDim, m_textureDim, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenBuffers(1, &m_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo);

    ClearFramebuffer(0);
    SetSwapInterval(0);

    m_init = true;
}

// ----------------------------------------------------------------------------
RenderWidget::~RenderWidget()
{
}

// ----------------------------------------------------------------------------
void RenderWidget::SetEmulator(gbxThread *gbx)
{
    m_gbx = gbx;
}

// ----------------------------------------------------------------------------
void RenderWidget::SetSwapInterval(int interval)
{
#ifdef PLATFORM_WIN32
    if (WGLEW_EXT_swap_control)
        wglSwapIntervalEXT(interval);
    else
        log_err("WGL_EXT_swap_control is not available\n");
#elif defined(PLATFORM_UNIX)
    if (GLXEW_SGI_swap_control)
        glXSwapIntervalSGI(interval);
    else
        log_err("GLX_SGI_swap_control is not available\n");
#else
    log_err("vertical sync is not available on this platform\n");
#endif 
}

// ----------------------------------------------------------------------------
void RenderWidget::UpdateFramebuffer(const uint32_t *fb)
{
    // lock the pbo for write only access and copy over the framebuffer
    glBufferData(GL_PIXEL_UNPACK_BUFFER, m_pboSize, NULL, GL_DYNAMIC_DRAW);
    void *pbuf = (void *)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);

    memcpy(pbuf, (const void *)fb, m_pboSize);

    // unlock the pbo
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
            m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    // force the widget to update its contents
    Refresh(false);
}

// ----------------------------------------------------------------------------
void RenderWidget::ClearFramebuffer(uint8_t value)
{
    // lock the pbo for write only access and copy over the framebuffer
    glBufferData(GL_PIXEL_UNPACK_BUFFER, m_pboSize, NULL, GL_DYNAMIC_DRAW);
    void *pbuf = (void *)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);

    memset(pbuf, value, m_pboSize);

    // unlock the pbo
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
            m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    // force the widget to update its contents
    Refresh(false);
}

// ----------------------------------------------------------------------------
void RenderWidget::OnSize(wxSizeEvent &event)
{
    wxGLCanvas::OnSize(event);

    int client_width, client_height;
    GetClientSize(&client_width, &client_height);
    glViewport(0, 0, (GLint)client_width, (GLint)client_height);

    m_x0 = m_y0 = -1.0f;
    m_x1 = m_y1 = 1.0f;

    if (!m_stretch) {
        // fit the viewport while maintaining proper aspect ratio
        float wnd_aspect = (float)client_width / client_height;
        float gbx_aspect = (float)m_width / m_height;

        if (wnd_aspect > gbx_aspect) {
            float ratio = gbx_aspect / wnd_aspect;
            m_x0 = -ratio;
            m_x1 = ratio;
        }
        else {
            float ratio = wnd_aspect / gbx_aspect;
            m_y0 = -ratio;
            m_y1 = ratio;
        }
    }

    Refresh(false);
}

// ----------------------------------------------------------------------------
void RenderWidget::OnPaint(wxPaintEvent &event)
{
    SetCurrent(*m_context);
    if (!m_init)
        InitGL();

    wxPaintDC(this);
    glClear(GL_COLOR_BUFFER_BIT);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, m_tv); glVertex3f(m_x0, m_y0, 0.0f); 
    glTexCoord2f(m_tu, m_tv); glVertex3f(m_x1, m_y0, 0.0f); 
    glTexCoord2f(m_tu, 0.0f); glVertex3f(m_x1, m_y1, 0.0f); 
    glTexCoord2f(0.0f, 0.0f); glVertex3f(m_x0, m_y1, 0.0f);
    glEnd();

    SwapBuffers();
}

// ----------------------------------------------------------------------------
void RenderWidget::OnKeyDown(wxKeyEvent &event)
{
    KeyMap::iterator i = m_keymap.find(event.GetKeyCode());
    if (m_keymap.end() != i)
        m_gbx->SetInputState(i->second, 1);
}

// ----------------------------------------------------------------------------
void RenderWidget::OnKeyUp(wxKeyEvent &event)
{
    KeyMap::iterator i = m_keymap.find(event.GetKeyCode());
    if (m_keymap.end() != i)
        m_gbx->SetInputState(i->second, 0);
}

// ----------------------------------------------------------------------------
void RenderWidget::OnEraseBackground(wxEraseEvent &event)
{
    // override EraseBackground to avoid flickering on some platforms
}

