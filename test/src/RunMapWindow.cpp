/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000 - 2009

	M Roberts (original release)
	Robin Birch <robinb@ruffnready.co.uk>
	Samuel Gisiger <samuel.gisiger@triadis.ch>
	Jeff Goodenough <jeff@enborne.f2s.com>
	Alastair Harrison <aharrison@magic.force9.co.uk>
	Scott Penrose <scottp@dd.com.au>
	John Wharington <jwharington@gmail.com>
	Lars H <lars_hn@hotmail.com>
	Rob Dunning <rob@raspberryridgesheepfarm.com>
	Russell King <rmk@arm.linux.org.uk>
	Paolo Ventafridda <coolwind@email.it>
	Tobias Lohner <tobias@lohner-net.de>
	Mirek Jezek <mjezek@ipplc.cz>
	Max Kellermann <max@duempel.org>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "MapWindow.hpp"
#include "Screen/SingleWindow.hpp"
#include "Screen/ButtonWindow.hpp"
#include "Screen/Blank.hpp"
#include "Screen/BufferCanvas.hpp"
#include "Screen/Fonts.hpp"
#include "Screen/Graphics.hpp"
#include "Screen/Layout.hpp"
#include "ResourceLoader.hpp"
#include "Interface.hpp"
#include "InfoBoxes/InfoBoxLayout.hpp"
#include "Logger/Logger.hpp"
#include "Terrain/RasterWeather.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "UtilsSystem.hpp"
#include "ProfileKeys.hpp"
#include "LocalTime.hpp"
#include "LocalPath.hpp"
#include "WayPointGlue.hpp"
#include "Device/device.hpp"
#include "InputEvents.h"
#include "Topology/TopologyStore.hpp"
#include "Dialogs.h"
#include "Protection.hpp"
#include "Gauge/GaugeCDI.hpp"
#include "Logger/LoggerImpl.hpp"
#include "Audio/Sound.hpp"
#include "ButtonLabel.hpp"
#include "DeviceBlackboard.hpp"
#include "Airspace/AirspaceParser.hpp"
#include "Profile.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Engine/Airspace/Airspaces.hpp"
#include "Engine/Task/TaskManager.hpp"
#include "Engine/Task/TaskEvents.hpp"
#include "LogFile.hpp"
#include "IO/FileLineReader.hpp"

#ifndef _MSC_VER
#include <algorithm>
using std::min;
#endif

#ifdef HAVE_BLANK
int DisplayTimeOut;
#endif

DeviceBlackboard device_blackboard;

void DeviceBlackboard::SetTrackBearing(Angle val) {}
void DeviceBlackboard::SetSpeed(fixed val) {}

void
DeviceBlackboard::SetStartupLocation(const GEOPOINT &loc, const double alt) {}

int
Profile::GetScaleList(fixed *List, size_t Size)
{
  return 0;
}

Trigger targetManipEvent(_T("targetManip"));

static Waypoints way_points;

static TaskBehaviour task_behaviour;
static TaskEvents task_events;

static TaskManager task_manager(task_events,
                                way_points);

static Airspaces airspace_database;

static AIRCRAFT_STATE ac_state; // dummy

static TopologyStore *topology;
static RasterTerrain terrain;
Logger logger;

unsigned InfoBoxLayout::ControlWidth;

InterfaceBlackboard CommonInterface::blackboard;

void InputEvents::ShowMenu() {}
bool InputEvents::processKey(int key) {
  return false;
}

void InputEvents::setMode(mode mode) {}
InputEvents::mode InputEvents::getModeID() { return MODE_DEFAULT; }

void InputEvents::sub_ScaleZoom(int vswitch) {}

GaugeCDI::GaugeCDI(ContainerWindow &parent) {}
void GaugeCDI::update_async(Angle TrackBearing, Angle WaypointBearing) {}
void GaugeCDI::hide_async() {}
bool GaugeCDI::on_user(unsigned id) { return true; }

int
propGetScaleList(fixed *List, size_t Size)
{
  return 0;
}

bool
PopupNearestWaypointDetails(const Waypoints &way_points,
                            const GEOPOINT &location, double range)
{
  return false;
}

int dlgWaypointOutOfTerrain(const TCHAR *Message)
{
  _ftprintf(stderr, _T("%s\n"), Message);
  return mrCancel;
}

void dlgAirspaceDetails(const AbstractAirspace& the_airspace) {}

Logger::Logger() {}
Logger::~Logger() {}

bool
Logger::isLoggerActive()
{
  return false;
}

bool PlayResource(const TCHAR* lpName)
{
  return false;
}

int
TimeLocal(int d)
{
  return d;
}

class TestWindow : public SingleWindow {
public:
  MapWindow map;
  ButtonWindow close_button;

  enum {
    ID_START = 100,
    ID_CLOSE,
  };

public:
  TestWindow() {}

  static bool register_class(HINSTANCE hInstance) {
#ifdef ENABLE_SDL
    return true;
#else /* !ENABLE_SDL */
    WNDCLASS wc;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Window::WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = _T("RunMapWindow");

    return RegisterClass(&wc);
#endif /* !ENABLE_SDL */
  }

  void set(int left, int top, unsigned width, unsigned height) {
    SingleWindow::set(_T("RunMapWindow"), _T("RunMapWindow"),
                      left, top, width, height);

    RECT rc = get_client_rect();
    map.set(*this, rc, rc);
    map.set_way_points(&way_points);
    map.set_airspaces(&airspace_database, NULL);
    map.set_topology(topology);
    map.set_terrain(&terrain);

    close_button.set(*this, _T("Close"), ID_CLOSE, 5, 5, 65, 25);
    close_button.bring_to_top();
  }

protected:
  virtual bool on_command(unsigned id, unsigned code) {
    switch (id) {
    case ID_CLOSE:
      close();
      return true;
    }

    return TopWindow::on_command(id, code);
  }
};

void TriggerGPSUpdate() {}

class Blackboard : public SettingsMapBlackboard {
public:
  Blackboard() {
    settings_map.DisplayOrientation = NORTHUP;
    settings_map.DisplayTextType = DISPLAYNAME;
    settings_map.EnableTopology = true;
    settings_map.EnableTerrain = true;
    settings_map.MapScale = 3.0;
  }
};

static Blackboard blackboard;

static void
LoadFiles()
{
  topology = new TopologyStore();

  terrain.OpenTerrain();

  WayPointGlue::ReadWaypoints(way_points, &terrain);

  TCHAR tpath[MAX_PATH];
  Profile::Get(szProfileAirspaceFile, tpath, MAX_PATH);
  if (tpath[0] != 0) {
    ExpandLocalPath(tpath);

    FileLineReader reader(tpath);
    if (reader.error() || !ReadAirspace(airspace_database, reader))
      LogStartUp(_T("No airspace file 1"));

    airspace_database.optimise();
  }
}

static void
GenerateBlackboard(MapWindow &map)
{
  NMEA_INFO nmea_info;
  DERIVED_INFO derived_info;
  SETTINGS_COMPUTER settings_computer;

  memset(&nmea_info, 0, sizeof(nmea_info));
  nmea_info.gps.Connected = 2;
  nmea_info.gps.SatellitesUsed = 4;
  nmea_info.Location.Latitude = Angle::degrees(fixed(51.2));
  nmea_info.Location.Longitude = Angle::degrees(fixed(7.7));
  nmea_info.TrackBearing = Angle::degrees(fixed_90);
  nmea_info.GroundSpeed = 50;
  nmea_info.GPSAltitude = 1500;

  memset(&derived_info, 0, sizeof(derived_info));
  derived_info.TerrainValid = true;

  memset(&settings_computer, 0, sizeof(settings_computer));

  terrain.ServiceTerrainCenter(nmea_info.Location);

  for (unsigned i = 0; i <AIRSPACECLASSCOUNT; ++i)
    settings_computer.iAirspaceMode[i] = 3;

  map.ReadBlackboard(nmea_info, derived_info, settings_computer,
                     blackboard.SettingsMap());
  map.UpdateProjection();
}

class DrawThread {
public:
  static void Draw(MapWindow &map) {
    map.DrawThreadLoop();
    map.SmartBounds(true);
    map.UpdateAll();
    map.DrawThreadLoop();
  }
};

#ifndef WIN32
int main(int argc, char **argv)
#else
int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
#ifdef _WIN32_WCE
        LPWSTR lpCmdLine,
#else
        LPSTR lpCmdLine2,
#endif
        int nCmdShow)
#endif
{
  LoadFiles();

#ifdef WIN32
  ResourceLoader::Init(hInstance);
  CommonInterface::hInst = hInstance;

  TestWindow::register_class(hInstance);
  MapWindow::register_class(hInstance);
#endif

  MapGfx.Initialise();
  MapGfx.InitialiseConfigured(blackboard.SettingsMap());

  TestWindow window;
  GenerateBlackboard(window.map);
  window.set(0, 0, 640, 480);
  DrawThread::Draw(window.map);
  window.show();

  window.event_loop();

  delete topology;

  return 0;
}
