/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Climatology Plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2013 by Sean D'Epagnier                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 *
 */

#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
  #include <wx/glcanvas.h>
#endif //precompiled headers

#include <wx/progdlg.h>

#include <netcdfcpp.h>

#include "zuFile.h"

#include "climatology_pi.h"
#include "IsoLine.h"

double deg2rad(double degrees)
{
  return M_PI * degrees / 180.0;
}

double rad2deg(double radians)
{
  return 180.0 * radians / M_PI;
}

double positive_degrees(double degrees)
{
    while(degrees < 0)
        degrees += 360;
    while(degrees >= 360)
        degrees -= 360;
    return degrees;
}

ClimatologyOverlay::~ClimatologyOverlay()
{
    if(m_iTexture)
        glDeleteTextures( 1, &m_iTexture );
    delete m_pDCBitmap, delete[] m_pRGBA;
}

//----------------------------------------------------------------------------------------------------------
//    Climatology Overlay Factory Implementation
//----------------------------------------------------------------------------------------------------------
ClimatologyOverlayFactory::ClimatologyOverlayFactory( ClimatologyDialog &dlg )
    : m_dlg(dlg), m_Settings(dlg.m_cfgdlg->m_Settings), m_cyclonelist(0)
{
    wxProgressDialog progressdialog( _("Climatology"), wxString(), 7, &m_dlg,
                                     wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
    ClearCachedData();

    wxString path = *GetpSharedDataLocation() + _T("plugins/climatology/data/");

    /* load wind data */
    if(!progressdialog.Update(0, _("wind data")))
        return;

    for(int month = 0; month < 13; month++)
        ReadWindData(month, path+wxString::Format(_T("wind%02d.gz"), month+1));

    /* load sea level pressure data */
    if(!progressdialog.Update(1, _("sea level pressure data")))
        return;

    wxString slp_path = path + _T("slpcoadsclim5079.nc");

    NcError err(NcError::silent_nonfatal);
    NcFile slp(slp_path.mb_str(), NcFile::ReadOnly);
    if(!slp.is_valid() || slp.num_dims() != 3 || slp.num_vars() != 4)
        wxLogMessage(_("climatology_pi: failed to read file: ") + slp_path);
    else {
        NcVar* data = slp.get_var("data");
        if(!data->is_valid() || data->num_dims() != 3)
            wxLogMessage(_("climatology_pi: file, ") + slp_path + _(" Has incorrect dimensions"));
        else {
            m_dlg.m_cbPressure->Enable();
            data->get(&m_slp[0][0][0], 13, 90, 180);
        }
    }

    /* load sea temperature data */
    if(!progressdialog.Update(2, _("sea temperature data")))
        return;
    wxString sst_path = path + _T("sstcoadsclim6079.1deg.nc");

    NcFile sst(sst_path.mb_str(), NcFile::ReadOnly);
    if(!sst.is_valid() || sst.num_dims() != 3 || sst.num_vars() != 4)
        wxLogMessage(_("climatology_pi: failed to read file: ") + sst_path);
    else {
        NcVar* data = sst.get_var("data");
        if(!data->is_valid() || data->num_dims() != 3)
            wxLogMessage(_("climatology_pi: file, ") + slp_path + _(" Has incorrect dimensions"));
        else {
            m_dlg.m_cbSeaTemperature->Enable();
            data->get(&m_sst[0][0][0], 13, 180, 360);
        }
    }

    /* load cyclone tracks */
    if(!progressdialog.Update(3, _("cyclone data (pacific)")))
        return;
    if(!ReadCycloneDatabase(path + _T("tracks.epa"), m_epa))
        m_dlg.m_cfgdlg->m_cbEastPacific->Disable();
    if(!ReadCycloneDatabase(path + _T("tracks.bwp"), m_bwp))
        m_dlg.m_cfgdlg->m_cbWestPacific->Disable();
    if(!ReadCycloneData(path + _T("tracks.spa.dat"), m_spa))
        m_dlg.m_cfgdlg->m_cbSouthPacific->Disable();
    if(!progressdialog.Update(4, _("cyclone data (atlantic)")))
        return;
    if(!ReadCycloneDatabase(path + _T("tracks.atl"), m_atl))
        m_dlg.m_cfgdlg->m_cbAtlantic->Disable();
    if(!progressdialog.Update(5, _("cyclone data (indian)")))
        return;
    if(!ReadCycloneDatabase(path + _T("tracks.nio"), m_nio))
        m_dlg.m_cfgdlg->m_cbNorthIndian->Disable();
    if(!ReadCycloneDatabase(path + _T("tracks.she"), m_she, true))
        m_dlg.m_cfgdlg->m_cbSouthIndian->Disable();

    m_dlg.m_cbCyclones->Enable();

    if(!progressdialog.Update(6, _("el nino years")))
        return;
    if(!ReadElNinoYears(path + _T("elnino_years.txt"))) {
        m_dlg.m_cfgdlg->m_cbElNino->Disable();
        m_dlg.m_cfgdlg->m_cbLaNina->Disable();
        m_dlg.m_cfgdlg->m_cbNeutral->Disable();
    }

    wxDateTime datetime;
    datetime.SetYear(1980);
    m_dlg.m_cfgdlg->m_dPStart->SetValue(datetime);
}

ClimatologyOverlayFactory::~ClimatologyOverlayFactory()
{
    glDeleteLists(m_cyclonelist, 1);
    ClearCachedData();
}

void ClimatologyOverlayFactory::ReadWindData(int month, wxString filename)
{
    m_WindData[month] = NULL;
    ZUFILE *f = zu_open(filename.mb_str(), "rb", ZU_COMPRESS_AUTO);
    if(!f) {
        wxLogMessage(_("climatology_pi: failed to read file: ") + filename);
        return;
    }

    m_dlg.m_cbWind->Enable();

    wxUint16 header[3];
    zu_read(f, header, sizeof header);
    m_WindData[month] = new WindData(header[0], header[1], header[2]);

    for(int lati = 0; lati < m_WindData[month]->latitudes; lati++) {
        for(int loni = 0; loni < m_WindData[month]->longitudes; loni++) {
            WindData::WindPolar &wp = m_WindData[month]->data[lati*m_WindData[month]->longitudes + loni];
            int dirs = m_WindData[month]->dir_cnt;
            zu_read(f, &wp.storm, 1);
            if(wp.storm != 255) {
                zu_read(f, &wp.calm, 1);
                wp.directions = new wxUint8[dirs];
                zu_read(f, wp.directions, dirs);
                wp.speeds = new wxUint8[dirs];
                zu_read(f, wp.speeds, dirs);
            }
        }
    }
    zu_close(f);
}

bool ClimatologyOverlayFactory::ReadCycloneDatabase(
    wxString filename, std::list<Cyclone*> &cyclones, bool south)
{
    FILE *f = fopen(filename.mb_str(), "r");
    if (!f) {
        wxLogMessage(_T("climatology_pi: failed to open file: ") + filename);
        return false;
    }

    char line[128];
    while(fgets(line, sizeof line, f)) {
        char *year  = line+12; line[16] = 0;
        char *days  = line+19; line[21] = 0;
//        char *synum = line+22; line[24] = 0; /* cyclone this year */
//        char *snum  = line+30; line[34] = 0; /* cyclone number in total */
//        char *sname = line+35; line[46] = 0;
        
        long lyear = strtol(year, 0, 10);
        long ldays = strtol(days, 0, 10);

        Cyclone *cyclone = new Cyclone;

        /* daily data */
        for(int i=0; i<ldays; i++) {
            if(!fgets(line, sizeof line, f)) {
                wxLogMessage(_T("climatology_pi: cyclone text file ended before it should"));
                fclose(f);
                return false;
            }

            char month[3] = {line[6], line[7], 0};
            char day[3] =   {line[9], line[10], 0};

            int lmonth = strtol(month, 0, 10);
            int lday = strtol(day, 0, 10);

            char *s = line + 11;
            for(int h = 0; h<4; h++) {
                char state = s[0];
                char lat[5] = {s[1], s[2], '.', s[3], 0};
                char lon[6] = {s[4], s[5], s[6], '.', s[7], 0};
                char *wk = s+9; s[12] = 0;
                char press[5] = {s[13], s[14], s[15], s[15], 0};

                CycloneState::State cyclonestate;
                switch(state) {
                case '*': cyclonestate = CycloneState::TROPICAL; break;
                case 'S': cyclonestate = CycloneState::SUBTROPICAL; break;
                case 'E': cyclonestate = CycloneState::EXTRATROPICAL; break;
                case 'W': cyclonestate = CycloneState::WAVE; break;
                case 'L': cyclonestate = CycloneState::REMANENT; break;
                default: cyclonestate = CycloneState::UNKNOWN; break;
                }

                double dlat = (south ? -1 : 1) * strtod(lat, 0), dlon = -strtod(lon, 0);
                if(dlat || dlon) {
                    wxDateTime datetime;
                    datetime.SetDay(lday);
                    datetime.SetMonth((wxDateTime::Month)(lmonth-1));
                    datetime.SetYear(lyear);
                    datetime.SetHour(h*6);
                    cyclone->states.push_back
                        (new CycloneState(cyclonestate, datetime,
                                        dlat, dlon, strtod(wk, 0), strtod(press, 0)));
                }
                s += 17;
            }
        }

        /* trailer */
        if(!fgets(line, sizeof line, f)) {
            wxLogMessage(_T("climatology_pi: cyclone text file ended before it should"));
            fclose(f);
            return false;
        }

        if(line[6] == 'H' && line[7] == 'R')
            cyclone->type = Cyclone::HURRICANE;
        else
        if(line[6] == 'T' && line[7] == 'S')
            cyclone->type = Cyclone::TROPICAL_CYCLONE;
        else
        if(line[6] == 'S' && line[7] == 'S')
            cyclone->type = Cyclone::SUBTROPICAL_CYCLONE;
        else
            cyclone->type = Cyclone::UNKNOWN;
        
        cyclones.push_back(cyclone);
    }

    fclose(f);
    return true;
}

bool ClimatologyOverlayFactory::ReadCycloneData(wxString filename, std::list<Cyclone*> &cyclones)
{
    FILE *f = fopen(filename.mb_str(), "r");
    if (!f) {
        wxLogMessage(_T("climatology_pi: failed to open file: ") + filename);
        return false;
    }

    char line[1024];
    int year;
    wxDateTime datetime;
    Cyclone *cyclone = NULL;
    int header = 0;
    while(fgets(line, sizeof line, f)) {
        if(header) {
            header--;
        } else
        if(!strncmp(line, "Date", 4)) {
            wxString s = wxString::FromUTF8(line+6);
            char *saveptr;
            strtok_r(line, " ", &saveptr);
            strtok_r(0, " ", &saveptr);
            strtok_r(0, " ", &saveptr);
            year = strtol(strtok_r(0, " ", &saveptr), 0, 10);
            datetime.SetMonth((wxDateTime::Month)0);

            if(cyclone)
                cyclones.push_back(cyclone);
            cyclone = new Cyclone;
            header = 2;
        } else if(cyclone) {
            char *saveptr;
            strtok_r(line, " ", &saveptr);
            double lat = strtod(strtok_r(0, " ", &saveptr), 0);
            double lon = strtod(strtok_r(0, " ", &saveptr), 0);
            int month = strtol(strtok_r(0, "/", &saveptr), 0, 10);
            int day = strtol(strtok_r(0, "/", &saveptr), 0, 10);
            int hour = strtol(strtok_r(0, " ", &saveptr), 0, 10);
            double wind = strtod(strtok_r(0, " ", &saveptr), 0);
            double pressure = strtod(strtok_r(0, " ", &saveptr), 0);
            char *stat = strtok_r(0, "\n", &saveptr);

            if(month < datetime.GetMonth())
                year++;

            datetime.SetYear(year);
            datetime.SetMonth((wxDateTime::Month)(month-1));
            datetime.SetDay(day);
            datetime.SetHour(hour);

            CycloneState::State s;
            if(!strncmp(stat, "TROPICAL DEPRESSION", 19))
                s = CycloneState::REMANENT;
            else
                s = CycloneState::TROPICAL;

            cyclone->states.push_back(new CycloneState(s, datetime, lat, lon, wind, pressure));
        }
    }

    if(cyclone)
        cyclones.push_back(cyclone);
    fclose(f);
    return true;
}

static double strtod_nan(const char *str)
{
    if(!str)
        return NAN;
    char *endptr;
    double ret = strtod(str, &endptr);
    if(endptr == str)
        return NAN;
    return ret;
}

bool ClimatologyOverlayFactory::ReadElNinoYears(wxString filename)
{
    FILE *f = fopen(filename.mb_str(), "r");
    if (!f) {
        wxLogMessage(_T("climatology_pi: failed to open file: ") + filename);
        return false;
    }

    char line[128];
    int header = 1;
    while(fgets(line, sizeof line, f)) {
        if(header)
            header--;
        else {
            char *saveptr;
            int year = strtol(strtok_r(line, " ", &saveptr), 0, 10);
            ElNinoYear elninoyear;
            for(int i=0; i<12; i++)
                elninoyear.months[i] = strtod_nan(strtok_r(0, " ", &saveptr));
            m_ElNinoYears[year] = elninoyear;
        }
    }
    return true;
}

void ClimatologyOverlayFactory::ClearCachedData()
{
    for(int i=0; i<ClimatologyOverlaySettings::SETTINGS_COUNT; i++) {
        delete m_Settings.Settings[i].m_pIsobarArray;
        m_Settings.Settings[i].m_pIsobarArray = NULL;
    }
}

void DrawGLLine( double x1, double y1, double x2, double y2, double width )
{
    glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
                 GL_POLYGON_BIT | GL_HINT_BIT ); //Save state
    {
        //      Enable anti-aliased lines, at best quality
        glEnable( GL_LINE_SMOOTH );
        glEnable( GL_BLEND );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        glHint( GL_LINE_SMOOTH_HINT, GL_NICEST );
        glLineWidth( width );
        
        glBegin( GL_LINES );
        glVertex2d( x1, y1 );
        glVertex2d( x2, y2 );
        glEnd();
    }
    glPopAttrib();
}

static void DrawGLCircle( double x, double y, double r, double width )
{
    glPushAttrib(GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
                 GL_POLYGON_BIT | GL_HINT_BIT ); //Save state
    {
        //      Enable anti-aliased lines, at best quality
        glEnable( GL_LINE_SMOOTH );
        glEnable( GL_BLEND );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        glHint( GL_LINE_SMOOTH_HINT, GL_NICEST );
        glLineWidth( width );
        
        glBegin( GL_LINE_LOOP );
        for(double t=0; t<2*M_PI; t+=M_PI/30)
            glVertex2d(x+r*cos(t), y+r*sin(t));
        glEnd();
    }
    glPopAttrib();
}

struct ColorMap {
    double val;
    wxString text;
};

ColorMap WindMap[] =
{{0,  _T("#ffffff")}, {2, _T("#00ffff")},  {4,  _T("#00e4e4")}, {5,  _T("#00d9e4")},
 {6,  _T("#00d9d4")}, {7, _T("#00d9b2")},  {8,  _T("#00d96e")}, {9,  _T("#00d92a")},
 {10, _T("#00d900")}, {11, _T("#2ad900")}, {12, _T("#6ed900")}, {13, _T("#b2d900")},
 {14, _T("#d4d400")}, {15, _T("#d9a600")}, {16, _T("#d90000")}, {17, _T("#d90040")},
 {18, _T("#d90060")}, {19, _T("#ae0080")}, {20, _T("#8300a0")}, {21, _T("#5700c0")},
 {25, _T("#0000d0")}, {30, _T("#000000")}};

ColorMap CurrentMap[] =
{{0,  _T("#d90000")},  {1, _T("#d92a00")},  {2, _T("#d96e00")},  {3, _T("#d9b200")},
 {4,  _T("#d4d404")},  {5, _T("#a6d906")},  {7, _T("#06d9a0")},  {9, _T("#00d9b0")},
 {12, _T("#00d9c0")}, {15, _T("#00aed0")}, {18, _T("#0083e0")}, {21, _T("#0057e0")},
 {24, _T("#0000f0")}, {27, _T("#0400f0")}, {30, _T("#1c00f0")}, {36, _T("#4800f0")},
 {42, _T("#6900f0")}, {48, _T("#a000f0")}, {56, _T("#f000f0")}};

ColorMap PressureMap[] =
{{900,  _T("#283282")}, {980,  _T("#273c8c")}, {990,  _T("#264696")}, {1000,  _T("#2350a0")},
 {1001, _T("#1f5aaa")}, {1002, _T("#1a64b4")}, {1003, _T("#136ec8")}, {1004, _T("#0c78e1")},
 {1005, _T("#0382e6")}, {1006, _T("#0091e6")}, {1007, _T("#009ee1")}, {1008, _T("#00a6dc")},
 {1009, _T("#00b2d7")}, {1010, _T("#00bed2")}, {1011, _T("#28c8c8")}, {1012, _T("#78d2aa")},
 {1013, _T("#8cdc78")}, {1014, _T("#a0eb5f")}, {1015, _T("#c8f550")}, {1016, _T("#f3fb02")},
 {1017, _T("#ffed00")}, {1018, _T("#ffdd00")}, {1019, _T("#ffc900")}, {1020, _T("#ffab00")},
 {1021, _T("#ff8100")}, {1022, _T("#f1780c")}, {1024, _T("#e26a23")}, {1028, _T("#d5453c")},
 {1040, _T("#b53c59")}};

ColorMap SeaTempMap[] =
{{0, _T("#0000d9")},  {3, _T("#002ad9")},  {6, _T("#006ed9")},  {9, _T("#00b2d9")},
 {12, _T("#00d4d4")}, {15, _T("#00d9a6")}, {18, _T("#00d900")}, {20, _T("#95d900")},
 {22, _T("#d9d900")}, {23, _T("#d9ae00")}, {24, _T("#d98300")}, {25, _T("#d95700")},
 {26, _T("#d90000")}, {27, _T("#ae0000")}, {28, _T("#8c0000")}, {29, _T("#870000")},
 {30, _T("#690000")}, {32, _T("#550000")}, {35, _T("#410000")}};

ColorMap *ColorMaps[] = {WindMap, CurrentMap, PressureMap, SeaTempMap};
enum {WIND_GRAPHIC, CURRENT_GRAPHIC, PRESSURE_GRAPHIC, SEATEMP_GRAPHIC};

wxColour ClimatologyOverlayFactory::GetGraphicColor(int setting, double val_in)
{
    int colormap_index = setting;
    ColorMap *map;
    int maplen;

#if 0
    /* normalize input value */
    double min = GetMin(setting), max = GetMax(setting);

    val_in -= min;
    val_in /= max-min;
#endif

    switch(colormap_index) {
    case WIND_GRAPHIC:
        map = WindMap;
        maplen = (sizeof WindMap) / (sizeof *WindMap);
        break;
    case CURRENT_GRAPHIC:
        map = CurrentMap;
        maplen = (sizeof CurrentMap) / (sizeof *CurrentMap);
        break;
    case PRESSURE_GRAPHIC: 
        map = PressureMap;
        maplen = (sizeof PressureMap) / (sizeof *PressureMap);
        break;
    case SEATEMP_GRAPHIC: 
        map = SeaTempMap;
        maplen = (sizeof SeaTempMap) / (sizeof *SeaTempMap);
        break;
    }

#if 0
    /* normalize map from 0 to 1 */
    double cmax = map[maplen-1].val;
#else
    double cmax = 1;
#endif

    for(int i=1; i<maplen; i++) {
        double nmapvala = map[i-1].val/cmax;
        double nmapvalb = map[i].val/cmax;
        if(nmapvalb > val_in || i==maplen-1) {
            wxColour b, c;
            c.Set(map[i].text);
            b.Set(map[i-1].text);
            double d = (val_in-nmapvala)/(nmapvalb-nmapvala);
            c.Set((1-d)*b.Red()   + d*c.Red(),
                  (1-d)*b.Green() + d*c.Green(),
                  (1-d)*b.Blue()  + d*c.Blue());
            return c;
        }
    }
    return wxColour(0, 0, 0); /* unreachable */
}

bool ClimatologyOverlayFactory::CreateGLTexture( ClimatologyOverlay &O, int setting,
                                                 PlugIn_ViewPort &vp)
{
    double s;
    switch(setting) {
    case ClimatologyOverlaySettings::WIND: s = 2; break;
    case ClimatologyOverlaySettings::SLP:  s = .5;   break;
    default: s=1;
    }

    int width = s*360+1;
    int height = s*360;

    unsigned char *data = new unsigned char[width*height*4];

    for(int x = 0; x < width; x++) {
        for(int y = 0; y < height; y++) {
            /* put in mercator coordinates */
            double lat = M_PI*(2.0*y/height-1);
            lat = 2*rad2deg(atan(exp(lat))) - 90;
            double lon = x/s;

            double v = getValue(setting, lat, lon);
            unsigned char r, g, b, a;
            if(isnan(v))
                a = 0; /* transparent */
            else {
                wxColour c = GetGraphicColor(setting, v);
                r = c.Red();
                g = c.Green();
                b = c.Blue();
                a = 220;
            }

            int doff = 4*(y*width + x);
            data[doff + 0] = 255-r;
            data[doff + 1] = 255-g;
            data[doff + 2] = 255-b;
            data[doff + 3] = a;
        }
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, texture);

    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, width);

    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    glPopClientAttrib();

    delete [] data;

    O.m_iTexture = texture;
    O.m_width = width - 1;
    O.m_height = height;

    return true;
}

void ClimatologyOverlayFactory::DrawGLTexture( ClimatologyOverlay &O, PlugIn_ViewPort &vp)
{ 
    if( !O.m_iTexture )
        return;

    glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, O.m_iTexture);

    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );

    glDisable( GL_MULTISAMPLE );

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(1, 1, 1, 1);
    glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
    
    int w = vp.pix_width, h = vp.pix_height;

    double lat1, lon1, lat2, lon2;
    GetCanvasLLPix( &vp, wxPoint(0, 0), &lat1, &lon1 );
    GetCanvasLLPix( &vp, wxPoint(w, h), &lat2, &lon2 );

    lon1 = positive_degrees(lon1), lon2 = positive_degrees(lon2);
    lon2 *= O.m_width/360.0, lon1 *= O.m_width/360.0;

    double s1 = sin(deg2rad(lat1)), s2 = sin(deg2rad(lat2));
    double y1 = .5 * log((1 + s1) / (1 - s1)), y2 = .5 * log((1 + s2) / (1 - s2));
    y1 = O.m_height*(1 + y1/M_PI)/2, y2 = O.m_height*(1 + y2/M_PI)/2;

    lon2+=1;
    y2+=1;
    
    glBegin(GL_QUADS);

    double x = 0;
    if(lon1 > lon2) {
        wxPoint p;
        GetCanvasPixLL( &vp, &p, 0, 0 );
        x = p.x;

        glTexCoord2d(lon1,        y1), glVertex2i(0, 0);
        glTexCoord2d(O.m_width+1, y1), glVertex2i(x, 0);
        glTexCoord2d(O.m_width+1, y2), glVertex2i(x, h);
        glTexCoord2d(lon1,        y2), glVertex2i(0, h);

        lon1 = 0;
    }

    glTexCoord2d(lon1, y1), glVertex2i(x, 0);
    glTexCoord2d(lon2, y1), glVertex2i(w, 0);
    glTexCoord2d(lon2, y2), glVertex2i(w, h);
    glTexCoord2d(lon1, y2), glVertex2i(x, h);
    glEnd();
    
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
}

/* return cached wxImage for a given number, or create it if not in the cache */
wxImage &ClimatologyOverlayFactory::getLabel(double value)
{
    std::map <double, wxImage >::iterator it;
    double nvalue = isnan(value) ? 99999999 : value;
            
    it = m_labelCache.find(nvalue);
    if (it != m_labelCache.end())
        return m_labelCache[nvalue];

    wxString labels;
    if(isnan(value))
        labels = _("N/A");
    else
        labels.Printf(_T("%.0f"), value);
    wxColour text_color;
    GetGlobalColor( _T ( "DILG3" ), &text_color );
    wxColour back_color;
    wxPen penText(text_color);

    GetGlobalColor( _T ( "DILG0" ), &back_color );
    wxBrush backBrush(back_color);

    wxMemoryDC mdc(wxNullBitmap);

    wxFont mfont( 12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL );
    mdc.SetFont( mfont );

    int w, h;
    mdc.GetTextExtent(labels, &w, &h);

    int label_offset = 5;

    wxBitmap bm(w +  label_offset*2, h + 2);
    mdc.SelectObject(bm);
    mdc.Clear();

    mdc.SetPen(penText);
    mdc.SetBrush(backBrush);
    mdc.SetTextForeground(text_color);
    mdc.SetTextBackground(back_color);
          
    int xd = 0;
    int yd = 0;
//    mdc.DrawRoundedRectangle(xd, yd, w+(label_offset * 2), h+2, -.25);
//    mdc.DrawRectangle(xd, yd, w+(label_offset * 2), h+2);
    mdc.DrawText(labels, label_offset + xd, yd+1);
          
    mdc.SelectObject(wxNullBitmap);

    m_labelCache[nvalue] = bm.ConvertToImage();
    m_labelCache[nvalue].InitAlpha();

    wxImage &image = m_labelCache[nvalue];

    unsigned char *d = image.GetData();
    unsigned char *a = image.GetAlpha();

    w = image.GetWidth(), h = image.GetHeight();
    for( int y = 0; y < h; y++ )
        for( int x = 0; x < w; x++ ) {
            int r, g, b;
            int ioff = (y * w + x);
            r = d[ioff* 3 + 0];
            g = d[ioff* 3 + 1];
            b = d[ioff* 3 + 2];

            a[ioff] = 255-(r+g+b)/3;
        }

    return m_labelCache[nvalue];
}

/* give value for y at a given x location on a segment */
double interp_value(double x, double x1, double x2, double y1, double y2)
{
    return x2 - x1 ? (y2 - y1)*(x - x1)/(x2 - x1) + y1 : y1;
}

double ArrayValue(wxInt16 *a, int index)
{
    int v = a[index];
    if(v == 32767)
        return NAN;
    return v;
}

double InterpArray(double x, double y, wxInt16 *a, int h)
{
    if(y<0) y+=h;
    int x0 = floor(x), x1 = x0+1;
    int y0 = floor(y), y1 = y0+1;
    int y1v = y1;
    if(y1v == h) y1v = 0;

    double v00 = ArrayValue(a, x0*h + y0), v01 = ArrayValue(a, x0*h + y1v);
    double v10 = ArrayValue(a, x1*h + y0), v11 = ArrayValue(a, x1*h + y1v);

    double v0 = interp_value(y, y0, y1, v00, v01);
    double v1 = interp_value(y, y0, y1, v10, v11);
    return      interp_value(x, x0, x1, v0,  v1 );
}

double WindData::InterpWindSpeed(double x, double y)
{
    y = positive_degrees(y);
    double xi = latitudes*(.5 + (x-.25)/180.0);
    double yi = longitudes*y/360.0;
    int h = longitudes, d = dir_cnt;

    if(xi<0) xi+=latitudes;

    int x0 = floor(xi), x1 = x0+1;
    int y0 = floor(yi), y1 = y0+1;
    int y1v = y1;
    if(y1v == h) y1v = 0;

    double v00 = data[x0*h + y0].AvgSpeed(d), v01 = data[x0*h + y1v].AvgSpeed(d);
    double v10 = data[x1*h + y0].AvgSpeed(d), v11 = data[x1*h + y1v].AvgSpeed(d);

    double v0 = interp_value(yi, y0, y1, v00, v01);
    double v1 = interp_value(yi, y0, y1, v10, v11);
    return      interp_value(xi, x0, x1, v0,  v1 );
}

double ClimatologyOverlayFactory::getValue(int setting, double lat, double lon)
{
    switch(setting) {
    case ClimatologyOverlaySettings::WIND:
        if(m_WindData[m_CurrentMonth])
            return m_WindData[m_CurrentMonth]->InterpWindSpeed(lat, lon);
        return NAN;
    case ClimatologyOverlaySettings::CURRENT: return NAN;
    case ClimatologyOverlaySettings::SLP:
        return InterpArray((-lat+90)/2-.5, positive_degrees(lon-1.5)/2,
                           m_slp[m_CurrentMonth][0], 180) * .01f + 1000.0;
    case ClimatologyOverlaySettings::SST:
        return InterpArray((-lat+90)-.5, positive_degrees(lon-.5),
                           m_sst[m_CurrentMonth][0], 360) * .001f + 15.0;
    default: return 0;
    }
}

double ClimatologyOverlayFactory::GetMin(int setting)
{
    switch(setting) {
    case ClimatologyOverlaySettings::WIND: return 0;
    case ClimatologyOverlaySettings::CURRENT: return 0;
    case ClimatologyOverlaySettings::SLP: return 920;
    case ClimatologyOverlaySettings::SST: return 0;
    default: return 0;
    }
}

double ClimatologyOverlayFactory::GetMax(int setting)
{
    switch(setting) {
    case ClimatologyOverlaySettings::WIND: return 100;
    case ClimatologyOverlaySettings::CURRENT: return 10;
    case ClimatologyOverlaySettings::SLP: return 1080;
    case ClimatologyOverlaySettings::SST: return 35;
    default: return NAN;
    }
}

void ClimatologyOverlayFactory::RenderOverlayMap( int setting, PlugIn_ViewPort &vp)
{
    if(!m_Settings.Settings[setting].m_bOverlayMap)
        return;

    ClimatologyOverlay &O = m_pOverlay[m_CurrentMonth][setting];

    if( !m_pdc )
    {
        if( !O.m_iTexture )
            CreateGLTexture( O, setting, vp);

        DrawGLTexture( O, vp );
    }
    else
    {
#if 0  
        if( !pO.m_pDCBitmap ) {
            wxImage bl_image = CreateImage( settings, pGRA, vp, porg );
            if( bl_image.IsOk() ) {
                //    Create a Bitmap
                pGO->m_pDCBitmap = new wxBitmap( bl_image );
                wxMask *gr_mask = new wxMask( *( pGO->m_pDCBitmap ), wxColour( 0, 0, 0 ) );
                pGO->m_pDCBitmap->SetMask( gr_mask );
            }
        }
      
        if( pGO->m_pDCBitmap )
            m_pdc->DrawBitmap( *( pGO->m_pDCBitmap ), porg.x, porg.y, true );
#endif        
    }
}

void ClimatologyOverlayFactory::RenderNumber(wxPoint p, double v)
{
    wxImage &label = getLabel(v);
    int w = label.GetWidth(), h = label.GetHeight();

    if( m_pdc )
        m_pdc->DrawBitmap(label, p.x-w/2, p.y-h/2, true);
    else {
        glEnable( GL_BLEND );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
                     w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, label.GetData());
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, w, h,
                        GL_ALPHA, GL_UNSIGNED_BYTE, label.GetAlpha());
        
        glEnable(GL_TEXTURE_RECTANGLE_ARB);
        glBegin(GL_QUADS);
        glTexCoord2i(0, 0), glVertex2i(p.x-w/2, p.y-h/2);
        glTexCoord2i(w, 0), glVertex2i(p.x+w/2, p.y-h/2);
        glTexCoord2i(w, h), glVertex2i(p.x+w/2, p.y+h/2);
        glTexCoord2i(0, h), glVertex2i(p.x-w/2, p.y+h/2);
        glEnd();
        glDisable(GL_TEXTURE_RECTANGLE_ARB);
    }
}

void ClimatologyOverlayFactory::RenderIsoBars(int setting, PlugIn_ViewPort &vp)
{
    if(!m_Settings.Settings[setting].m_bIsoBars)
        return;

    wxArrayPtrVoid *&pIsobarArray = m_Settings.Settings[setting].m_pIsobarArray;
    if( !pIsobarArray ) {
        pIsobarArray = new wxArrayPtrVoid;
        IsoLine *piso;

        wxProgressDialog *progressdialog = NULL;
        wxDateTime start = wxDateTime::Now();

        double min = GetMin(setting);
        double max = GetMax(setting);

        /* convert min and max to units being used */
        for( double press = min; press <= max; press += m_Settings.Settings[setting].m_iIsoBarSpacing) {
            if(progressdialog)
                progressdialog->Update(press-min);
            else {
                wxDateTime now = wxDateTime::Now();
                if((now-start).GetSeconds() > 1 && press-min < (max-min)/2) {
                    progressdialog = new wxProgressDialog(
                        _("Building Isobar map"), _("Climatology"), max-min+1, &m_dlg,
                        wxPD_SMOOTH | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
                }
            }

            piso = new IsoLine( press, *this, setting);
            pIsobarArray->Add( piso );
        }
        delete progressdialog;
    }

    //    Draw the Isobars
    for( unsigned int i = 0; i < pIsobarArray->GetCount(); i++ ) {
        IsoLine *piso = (IsoLine *) pIsobarArray->Item( i );
        piso->drawIsoLine( m_pdc, &vp, true);

        // Draw Isobar labels
        int density = 400;
        int first = 0;
        piso->drawIsoLineLabels( m_pdc, &vp, density,
                                 first, getLabel(piso->getValue()) );
    }
}

void ClimatologyOverlayFactory::RenderNumbers(int setting, PlugIn_ViewPort &vp)
{
    if(!m_Settings.Settings[setting].m_bNumbers)
        return;

    double space = m_Settings.Settings[setting].m_iNumbersSpacing;
    wxPoint p;
    for(p.y = space; p.y <= vp.rv_rect.height-space; p.y+=space)
        for(p.x = space; p.x <= vp.rv_rect.width-space; p.x+=space) {
            double lat, lon;
            GetCanvasLLPix( &vp, p, &lat, &lon);
            glColor4f(0, 0, 0, 1);
            RenderNumber(p, getValue(setting, lat, lon));
        }
}

void ClimatologyOverlayFactory::RenderCyclonesTheatre(PlugIn_ViewPort &vp, std::list<Cyclone*> &cyclones)
{
    for(std::list<Cyclone*>::iterator it = cyclones.begin(); it != cyclones.end(); it++) {
        Cyclone *s = *it;

        bool first = true;
        double lastlon;
        wxPoint lastp;

        for(std::list<CycloneState*>::iterator it2 = s->states.begin(); it2 != s->states.end(); it2++) {
            wxPoint p;
            CycloneState *ss = *it2;

            if(m_dlg.m_sMonth->GetValue() != 12 &&
               m_dlg.m_sMonth->GetValue() != ss->datetime.GetMonth())
                continue;

            if((ss->datetime < m_dlg.m_cfgdlg->m_dPStart->GetValue()) ||
               (ss->datetime > m_dlg.m_cfgdlg->m_dPEnd->GetValue()))
                continue;

            int year = ss->datetime.GetYear();
            std::map<int, ElNinoYear>::iterator it;
            it = m_ElNinoYears.find(year);
            if (it == m_ElNinoYears.end()) {
                if(!m_dlg.m_cfgdlg->m_cbNotAvailable->GetValue())
                    continue;
            } else {
                ElNinoYear elninoyear = m_ElNinoYears[year];
                int month = ss->datetime.GetMonth();
                double value = elninoyear.months[month];

                if(isnan(value)) {
                    if(!m_dlg.m_cfgdlg->m_cbNotAvailable->GetValue())
                        continue;
                } else {
                    if(value >= 1) {
                        if(!m_dlg.m_cfgdlg->m_cbElNino->GetValue())
                            continue;
                    } else if(value <= -1) {
                        if(!m_dlg.m_cfgdlg->m_cbLaNina->GetValue())
                            continue;
                    } else if(!m_dlg.m_cfgdlg->m_cbNeutral->GetValue())
                        continue;
                }
            }

            switch(ss->state) {
            case CycloneState::TROPICAL: if(m_dlg.m_cfgdlg->m_cbTropical->GetValue()) break;
            case CycloneState::SUBTROPICAL: if(m_dlg.m_cfgdlg->m_cbSubTropical->GetValue()) break;
            case CycloneState::EXTRATROPICAL: if(m_dlg.m_cfgdlg->m_cbExtraTropical->GetValue()) break;
            case CycloneState::REMANENT: if(m_dlg.m_cfgdlg->m_cbRemanent->GetValue()) break;
            default: continue;
            }

            if(ss->windknots < m_dlg.m_cfgdlg->m_sMinWindSpeed->GetValue())
                continue;

            if(ss->pressure > m_dlg.m_cfgdlg->m_sMaxPressure->GetValue())
                continue;

            double lat = ss->latitude, lon = ss->longitude;
            GetCanvasPixLL( &vp, &p, lat, lon );

            if(first) {
                first = false;
            } else {

#if 1 /* if you want to zoom in really really far and still see tracks */
                if((lastlon+180 > vp.clon || lon+180 < vp.clon) &&
                   (lastlon+180 < vp.clon || lon+180 > vp.clon) &&
                   (lastlon-180 > vp.clon || lon-180 < vp.clon) &&
                   (lastlon-180 < vp.clon || lon-180 > vp.clon))
#else
                if(abs(lastp.x-p.x) < vp.pix_width)
#endif
                {

                    double intense =  (ss->windknots / 150);

                    if(intense > 1) intense = 1;
                    switch(ss->state) {
                    case CycloneState::TROPICAL:      glColor3d(intense, 0, 0); break;
                    case CycloneState::SUBTROPICAL:   glColor3d(intense/2, intense/2, 0); break;
                    case CycloneState::EXTRATROPICAL: glColor3d(0, intense, 0); break;
                    default:                        glColor3d(0, 0, intense); break;
                    }
                    
                    glVertex2i(lastp.x, lastp.y);
                    glVertex2i(p.x, p.y);
                }
            }

            lastlon = lon;
            lastp = p;
        }
    }
}

void ClimatologyOverlayFactory::RenderWindAtlas(PlugIn_ViewPort &vp)
{
    if(!m_dlg.m_cfgdlg->m_cbWindAtlas->GetValue() || !m_WindData[m_CurrentMonth])
        return;

    double step = 360.0 / (m_WindData[m_CurrentMonth]->longitudes);
    const double r = 12, t = 70;
    int w = vp.pix_width, h = vp.pix_height;
    while((vp.lat_max - vp.lat_min) / step > h / 70 ||
          (vp.lon_max - vp.lon_min) / step > w / 70)
        step *= 2;

    int dir_cnt = m_WindData[m_CurrentMonth]->dir_cnt;
    for(double lat = round(vp.lat_min/step)*step-1; lat <= vp.lat_max+1; lat+=step)
        for(double lon = round(vp.lon_min/step)*step-1; lon <= vp.lon_max+1; lon+=step) {
            WindData::WindPolar &polar = m_WindData[m_CurrentMonth]->GetPolar(lat, positive_degrees(lon));
            if(polar.storm == 255)
                continue;

            wxPoint p;
            GetCanvasPixLL(&vp, &p, lat, lon);

            glColor4f(0, 0, 0, .8);
            DrawGLCircle(p.x, p.y, r, 2);
            
            int totald = 0;
            for(int i=0; i<dir_cnt; i++)
                totald += polar.directions[i];
            
            if(polar.storm*2 > polar.calm) {
                glColor3f(1, 0, 0);
                RenderNumber(p, polar.storm);
            } else if(polar.calm > 0) {
                glColor3f(0, 0, .7);
                RenderNumber(p, polar.calm);
            }
            
            glColor4f(0, 0, 0, 1);
            for(int d = 0; d<dir_cnt; d++) {
                double theta = 2*M_PI*d/dir_cnt;
                double x1 = p.x+r*cos(theta), y1 = p.y+r*sin(theta);
                
                double per = (double)polar.directions[d]/totald;

                const double maxper = .4;
                bool split = per >= maxper;
                double u = r + t*(split ? maxper : per);
                double x2 = p.x+u*cos(theta), y2 = p.y+u*sin(theta);

                if(split) {
                    wxPoint q((x1 + x2)/2, (y1 + y2)/2);
                    RenderNumber(q, 100*per);

                    DrawGLLine(x1, y1, (3*x1+x2)/4, (3*y1+y2)/4, 2);
                    DrawGLLine((x1+3*x2)/4, (y1+3*y2)/4, x2, y2, 2);
                } else
                    DrawGLLine(x1, y1, x2, y2, 2);

                double avg_speed = (double)polar.speeds[d] / polar.directions[d] * 3.6 / 1.852; /* knots */
                double dir = 1;
                const double a = 10, b = M_PI*2/3;
                while(avg_speed > 2) {
                    DrawGLLine(x2, y2, x2-a*cos(theta+dir*b), y2-a*sin(theta+dir*b), 2);
                    dir = -dir;
                    if(dir > 0)
                        x2 -= 3*cos(theta), y2 -= 3*sin(theta);
                    avg_speed -= 5;
                }
            }
            
            /* todo: draw barbs */
        }
}

void ClimatologyOverlayFactory::RenderCyclones(PlugIn_ViewPort &vp)
{
#if 0
    if(!m_cyclonelist)
        m_cyclonelist = glGenLists(1);

    if(m_cyclonelistok) {
        glCallList(m_cyclonelist);
        return;
    }

    glNewList(m_cyclonelist, GL_COMPILE_AND_EXECUTE);
#endif

    glLineWidth(1);

    glBegin(GL_LINES);

    if(m_dlg.m_cfgdlg->m_cbEastPacific->GetValue())
        RenderCyclonesTheatre(vp, m_epa);
    if(m_dlg.m_cfgdlg->m_cbWestPacific->GetValue())
        RenderCyclonesTheatre(vp, m_bwp);
    if(m_dlg.m_cfgdlg->m_cbSouthPacific->GetValue())
        RenderCyclonesTheatre(vp, m_spa);
    if(m_dlg.m_cfgdlg->m_cbAtlantic->GetValue())
        RenderCyclonesTheatre(vp, m_atl);
    if(m_dlg.m_cfgdlg->m_cbNorthIndian->GetValue())
        RenderCyclonesTheatre(vp, m_nio);
    if(m_dlg.m_cfgdlg->m_cbSouthIndian->GetValue())
        RenderCyclonesTheatre(vp, m_she);

    glEnd();
    
#if 0
    glEndList();
    m_cyclonelistok = true;
#endif
}

bool ClimatologyOverlayFactory::RenderOverlay( wxDC &dc, PlugIn_ViewPort &vp )
{
    m_pdc = &dc;
    return true;
}

bool ClimatologyOverlayFactory::RenderGLOverlay( wxGLContext *pcontext, PlugIn_ViewPort &vp )
{
    m_pdc = NULL;

    for(int overlay = 1; overlay >= 0; overlay--)
    for(int i=0; i<ClimatologyOverlaySettings::SETTINGS_COUNT; i++) {
        if((i == ClimatologyOverlaySettings::WIND    && !m_dlg.m_cbWind->GetValue()) ||
           (i == ClimatologyOverlaySettings::CURRENT && !m_dlg.m_cbPressure->GetValue()) ||
           (i == ClimatologyOverlaySettings::SLP     && !m_dlg.m_cbPressure->GetValue()) ||
           (i == ClimatologyOverlaySettings::SST     && !m_dlg.m_cbSeaTemperature->GetValue()))
            continue;

        if(overlay) /* render overlays first */
            RenderOverlayMap( i, vp );
        else {
            RenderIsoBars(i, vp);
            RenderNumbers(i, vp);
        }
    }

    if(m_dlg.m_cbWind->GetValue())
        RenderWindAtlas(vp);

    if(m_dlg.m_cbCyclones->GetValue())
        RenderCyclones(vp);

    return true;
}