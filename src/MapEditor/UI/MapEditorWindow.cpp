
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2022 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    MapEditorWindow.cpp
// Description: MapEditorWindow class, it's a map editor window.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "MapEditorWindow.h"
#include "App.h"
#include "Archive/ArchiveManager.h"
#include "Archive/Formats/WadArchive.h"
#include "Game/Configuration.h"
#include "General/Misc.h"
#include "General/UI.h"
#include "MainEditor/MainEditor.h"
#include "MapEditor/MapBackupManager.h"
#include "MapEditor/MapEditContext.h"
#include "MapEditor/MapEditor.h"
#include "MapEditor/MapTextureManager.h"
#include "MapEditor/NodeBuilders.h"
#include "MapEditor/UI/MapCanvas.h"
#include "MapEditor/UI/MapChecksPanel.h"
#include "MapEditor/UI/ObjectEditPanel.h"
#include "MapEditor/UI/PropsPanel/MapObjectPropsPanel.h"
#include "MapEditor/UI/ScriptEditorPanel.h"
#include "MapEditor/UI/ShapeDrawPanel.h"
#include "SLADEWxApp.h"
#include "Scripting/ScriptManager.h"
#include "UI/Controls/ConsolePanel.h"
#include "UI/Controls/UndoManagerHistoryPanel.h"
#include "UI/Dialogs/MapEditorConfigDialog.h"
#include "UI/Dialogs/Preferences/BaseResourceArchivesPanel.h"
#include "UI/Dialogs/Preferences/PreferencesDialog.h"
#include "UI/Dialogs/RunDialog.h"
#include "UI/SAuiTabArt.h"
#include "UI/SToolBar/SToolBar.h"
#include "UI/WxUtils.h"
#include "Utility/FileUtils.h"
#include "Utility/SFileDialog.h"
#include "Utility/Tokenizer.h"

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
namespace
{
bool nb_warned = false;
}
CVAR(Bool, mew_maximized, true, CVar::Flag::Save);
CVAR(String, nodebuilder_id, "zdbsp", CVar::Flag::Save);
CVAR(String, nodebuilder_options, "", CVar::Flag::Save);
CVAR(Bool, save_archive_with_map, true, CVar::Flag::Save);


// -----------------------------------------------------------------------------
//
// External Variables
//
// -----------------------------------------------------------------------------
EXTERN_CVAR(Int, flat_drawtype);


// -----------------------------------------------------------------------------
//
// MapEditorWindow Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// MapEditorWindow class constructor
// -----------------------------------------------------------------------------
MapEditorWindow::MapEditorWindow() : STopWindow{ "SLADE", "map" }
{
	if (mew_maximized)
		CallAfter(&MapEditorWindow::Maximize, this);
	setupLayout();
	wxTopLevelWindow::Show(false);
	custom_menus_begin_ = 2;

	// Set icon
	auto icon_filename = app::path(app::iconFile(), app::Dir::Temp);
	app::archiveManager().programResourceArchive()->entry(app::iconFile())->exportFile(icon_filename);
	SetIcon(wxIcon(wxString::FromUTF8(icon_filename), wxBITMAP_TYPE_ICO));
	fileutil::removeFile(icon_filename);

	// Bind events
	Bind(wxEVT_CLOSE_WINDOW, &MapEditorWindow::onClose, this);
	Bind(wxEVT_SIZE, &MapEditorWindow::onSize, this);
}

// -----------------------------------------------------------------------------
// MapEditorWindow class destructor
// -----------------------------------------------------------------------------
MapEditorWindow::~MapEditorWindow()
{
	wxAuiManager::GetManager(this)->UnInit();
}

// -----------------------------------------------------------------------------
// Loads the previously saved layout file for the window
// -----------------------------------------------------------------------------
void MapEditorWindow::loadLayout()
{
	// Open layout file
	Tokenizer tz;
	if (!tz.openFile(app::path("mapwindow.layout", app::Dir::User)))
		return;

	// Parse layout
	auto m_mgr = wxAuiManager::GetManager(this);
	while (true)
	{
		// Read component+layout pair
		auto component = tz.getToken();
		auto layout    = tz.getToken();

		// Load layout to component
		if (!component.empty() && !layout.empty())
			m_mgr->LoadPaneInfo(wxString::FromUTF8(layout), m_mgr->GetPane(wxString::FromUTF8(component)));

		// Check if we're done
		if (tz.peekToken().empty())
			break;
	}
}

// -----------------------------------------------------------------------------
// Saves the current window layout to a file
// -----------------------------------------------------------------------------
void MapEditorWindow::saveLayout()
{
	// Open layout file
	wxFile file(wxString::FromUTF8(app::path("mapwindow.layout", app::Dir::User)), wxFile::write);

	// Write component layout
	auto m_mgr = wxAuiManager::GetManager(this);

	// Console pane
	file.Write(wxS("\"console\" "));
	auto pinf = m_mgr->SavePaneInfo(m_mgr->GetPane(wxS("console")));
	file.Write(wxString::Format(wxS("\"%s\"\n"), pinf));

	// Item info pane
	file.Write(wxS("\"item_props\" "));
	pinf = m_mgr->SavePaneInfo(m_mgr->GetPane(wxS("item_props")));
	file.Write(wxString::Format(wxS("\"%s\"\n"), pinf));

	// Script editor pane
	file.Write(wxS("\"script_editor\" "));
	pinf = m_mgr->SavePaneInfo(m_mgr->GetPane(wxS("script_editor")));
	file.Write(wxString::Format(wxS("\"%s\"\n"), pinf));

	// Map checks pane
	file.Write(wxS("\"map_checks\" "));
	pinf = m_mgr->SavePaneInfo(m_mgr->GetPane(wxS("map_checks")));
	file.Write(wxString::Format(wxS("\"%s\"\n"), pinf));

	// Undo history pane
	file.Write(wxS("\"undo_history\" "));
	pinf = m_mgr->SavePaneInfo(m_mgr->GetPane(wxS("undo_history")));
	file.Write(wxString::Format(wxS("\"%s\"\n"), pinf));

	// Close file
	file.Close();
}

// -----------------------------------------------------------------------------
// Sets up the basic map editor window menu bar
// -----------------------------------------------------------------------------
void MapEditorWindow::setupMenu()
{
	// Get menu bar
	auto menu = GetMenuBar();
	if (menu)
	{
		// Clear existing menu bar
		unsigned n_menus = menu->GetMenuCount();
		for (unsigned a = 0; a < n_menus; a++)
		{
			auto sm = menu->Remove(0);
			delete sm;
		}
	}
	else // Create new menu bar
		menu = new wxMenuBar();

	// Map menu
	auto menu_map = new wxMenu();
	SAction::fromId("mapw_save")->addToMenu(menu_map);
	SAction::fromId("mapw_saveas")->addToMenu(menu_map);
	// SAction::fromId("mapw_rename")->addToMenu(menu_map);
	SAction::fromId("mapw_backup")->addToMenu(menu_map);
	menu_map->AppendSeparator();
	SAction::fromId("mapw_run_map")->addToMenu(menu_map);
	SAction::fromId("mapw_quick_run_map")->addToMenu(menu_map);
	menu_map->AppendSeparator();
	SAction::fromId("mapw_close")->addToMenu(menu_map);
	menu->Append(menu_map, wxS("&Map"));

	// Edit menu
	auto menu_editor = new wxMenu();
	SAction::fromId("mapw_undo")->addToMenu(menu_editor);
	SAction::fromId("mapw_redo")->addToMenu(menu_editor);
	menu_editor->AppendSeparator();
	SAction::fromId("mapw_clear_selection")->addToMenu(menu_editor);
	menu_editor->AppendSeparator();
	SAction::fromId("mapw_draw_lines")->addToMenu(menu_editor);
	SAction::fromId("mapw_draw_shape")->addToMenu(menu_editor);
	SAction::fromId("mapw_edit_objects")->addToMenu(menu_editor);
	SAction::fromId("mapw_mirror_x")->addToMenu(menu_editor);
	SAction::fromId("mapw_mirror_y")->addToMenu(menu_editor);
	menu_editor->AppendSeparator();
	SAction::fromId("mapw_preferences")->addToMenu(menu_editor);
	SAction::fromId("mapw_setbra")->addToMenu(menu_editor);
	menu->Append(menu_editor, wxS("&Edit"));

	// View menu
	auto menu_view   = new wxMenu();
	auto menu_window = new wxMenu();
	menu_view->AppendSubMenu(menu_window, wxS("Windows"));
	SAction::fromId("mapw_showproperties")->addToMenu(menu_window);
	SAction::fromId("mapw_showconsole")->addToMenu(menu_window);
	SAction::fromId("mapw_showundohistory")->addToMenu(menu_window);
	SAction::fromId("mapw_showchecks")->addToMenu(menu_window);
	SAction::fromId("mapw_showscripteditor")->addToMenu(menu_window);
	toolbar_menu_ = new wxMenu();
	menu_view->AppendSubMenu(toolbar_menu_, wxS("Toolbars"));
	menu_view->AppendSeparator();
	SAction::fromId("mapw_show_fullmap")->addToMenu(menu_view);
	SAction::fromId("mapw_show_item")->addToMenu(menu_view);
	menu_view->AppendSeparator();
	auto menu_grid = new wxMenu();
	menu_view->AppendSubMenu(menu_grid, wxS("Grid"));
	SAction::fromId("mapw_grid_increment")->addToMenu(menu_grid);
	SAction::fromId("mapw_grid_decrement")->addToMenu(menu_grid);
	SAction::fromId("mapw_grid_snap")->addToMenu(menu_grid);
	menu->Append(menu_view, wxS("View"));

	// Tools menu
	auto menu_tools = new wxMenu();
	menu_scripts_   = new wxMenu();
#ifndef NO_LUA
	scriptmanager::populateEditorScriptMenu(menu_scripts_, scriptmanager::ScriptType::Map, "mapw_script");
#endif
	menu_tools->AppendSubMenu(menu_scripts_, wxS("Run Script"));
	SAction::fromId("mapw_runscript")->addToMenu(menu_tools);
	menu->Append(menu_tools, wxS("&Tools"));

	SetMenuBar(menu);
}

// -----------------------------------------------------------------------------
// Sets up the basic map editor window layout
// -----------------------------------------------------------------------------
void MapEditorWindow::setupLayout()
{
	// Create the wxAUI manager & related things
	auto m_mgr = new wxAuiManager(this);
	m_mgr->SetArtProvider(new SAuiDockArt(this));
	wxAuiPaneInfo p_inf;

	// Map canvas
	map_canvas_ = new MapCanvas(this, -1, &mapeditor::editContext());
	p_inf.CenterPane();
	m_mgr->AddPane(map_canvas_, p_inf);

	// --- Menus ---
	setupMenu();


	// --- Toolbars ---
	toolbar_ = new SToolBar(this, true);

	// Map toolbar
	auto tbg_map = new SToolBarGroup(toolbar_, "_Map");
	tbg_map->addActionButton("mapw_save");
	tbg_map->addActionButton("mapw_saveas");
	// tbg_map->addActionButton("mapw_rename"); // TODO: Actually implement this one
	tbg_map->addActionButton("mapw_preferences");
	toolbar_->addGroup(tbg_map);

	// Mode toolbar
	auto tbg_mode = new SToolBarGroup(toolbar_, "_Mode");
	tbg_mode->addActionButton("mapw_mode_vertices");
	tbg_mode->addActionButton("mapw_mode_lines");
	tbg_mode->addActionButton("mapw_mode_sectors");
	tbg_mode->addActionButton("mapw_mode_things");
	tbg_mode->addActionButton("mapw_mode_3d");
	SAction::fromId("mapw_mode_lines")->setChecked(); // Lines mode by default
	toolbar_->addGroup(tbg_mode);

	// Flat type toolbar
	auto tbg_flats = new SToolBarGroup(toolbar_, "_Flats Type");
	tbg_flats->addActionButton("mapw_flat_none");
	tbg_flats->addActionButton("mapw_flat_untextured");
	tbg_flats->addActionButton("mapw_flat_textured");
	toolbar_->addGroup(tbg_flats);

	// Toggle current flat type
	if (flat_drawtype == 0)
		SAction::fromId("mapw_flat_none")->setChecked();
	else if (flat_drawtype == 1)
		SAction::fromId("mapw_flat_untextured")->setChecked();
	else
		SAction::fromId("mapw_flat_textured")->setChecked();

	// Edit toolbar
	auto tbg_edit = new SToolBarGroup(toolbar_, "_Edit");
	tbg_edit->addActionButton("mapw_draw_lines");
	tbg_edit->addActionButton("mapw_draw_shape");
	tbg_edit->addActionButton("mapw_edit_objects");
	tbg_edit->addActionButton("mapw_mirror_x");
	tbg_edit->addActionButton("mapw_mirror_y");
	toolbar_->addGroup(tbg_edit);

	// Extra toolbar
	auto tbg_misc = new SToolBarGroup(toolbar_, "_Misc");
	tbg_misc->addActionButton("mapw_run_map");
	tbg_misc->addActionButton("mapw_quick_run_map");
	toolbar_->addGroup(tbg_misc);

	// Add toolbar
	m_mgr->AddPane(
		toolbar_,
		wxAuiPaneInfo()
			.Top()
			.CaptionVisible(false)
			.MinSize(-1, SToolBar::getBarHeight())
			.Resizable(false)
			.PaneBorder(false)
			.Name(wxS("toolbar")));

	// Populate the 'View->Toolbars' menu
	populateToolbarsMenu();
	toolbar_->enableContextMenu();


	// Status bar
	CreateStatusBar(4);
	int status_widths[4] = { -1, ui::scalePx(240), ui::scalePx(240), ui::scalePx(240) };
	SetStatusWidths(4, status_widths);

	// -- Console Panel --
	auto panel_console = new ConsolePanel(this, -1);

	// Setup panel info & add panel
	p_inf.DefaultPane();
	p_inf.Bottom();
	p_inf.Dock();
	p_inf.BestSize(wxutil::scaledSize(480, 192));
	p_inf.FloatingSize(wxutil::scaledSize(600, 400));
	p_inf.FloatingPosition(100, 100);
	p_inf.MinSize(wxutil::scaledSize(-1, 192));
	p_inf.Show(false);
	p_inf.Caption(wxS("Console"));
	p_inf.Name(wxS("console"));
	m_mgr->AddPane(panel_console, p_inf);


	// -- Map Object Properties Panel --
	panel_obj_props_ = new MapObjectPropsPanel(this);

	// Setup panel info & add panel
	p_inf.Right();
	p_inf.BestSize(wxutil::scaledSize(256, 256));
	p_inf.FloatingSize(wxutil::scaledSize(400, 600));
	p_inf.FloatingPosition(120, 120);
	p_inf.MinSize(wxutil::scaledSize(256, 256));
	p_inf.Show(true);
	p_inf.Caption(wxS("Item Properties"));
	p_inf.Name(wxS("item_props"));
	m_mgr->AddPane(panel_obj_props_, p_inf);


	// --- Script Editor Panel ---
	panel_script_editor_ = new ScriptEditorPanel(this);

	// Setup panel info & add panel
	p_inf.Float();
	p_inf.BestSize(wxutil::scaledSize(300, 300));
	p_inf.FloatingSize(wxutil::scaledSize(500, 400));
	p_inf.FloatingPosition(150, 150);
	p_inf.MinSize(wxutil::scaledSize(300, 300));
	p_inf.Show(false);
	p_inf.Caption(wxS("Script Editor"));
	p_inf.Name(wxS("script_editor"));
	m_mgr->AddPane(panel_script_editor_, p_inf);


	// --- Shape Draw Options Panel ---
	auto panel_shapedraw = new ShapeDrawPanel(this);

	// Setup panel info & add panel
	auto msize = panel_shapedraw->GetMinSize();
	p_inf.DefaultPane();
	p_inf.Bottom();
	p_inf.Dock();
	p_inf.CloseButton(false);
	p_inf.CaptionVisible(false);
	p_inf.Resizable(false);
	p_inf.Layer(2);
	p_inf.BestSize(msize.x, msize.y);
	p_inf.FloatingSize(msize.x, msize.y);
	p_inf.FloatingPosition(140, 140);
	p_inf.MinSize(msize.x, msize.y);
	p_inf.Show(false);
	p_inf.Caption(wxS("Shape Drawing"));
	p_inf.Name(wxS("shape_draw"));
	m_mgr->AddPane(panel_shapedraw, p_inf);


	// --- Object Edit Panel ---
	panel_obj_edit_ = new ObjectEditPanel(this);

	// Setup panel info & add panel
	msize = panel_obj_edit_->GetBestSize();
	p_inf.Bottom();
	p_inf.Dock();
	p_inf.CloseButton(false);
	p_inf.CaptionVisible(false);
	p_inf.Resizable(false);
	p_inf.Layer(2);
	p_inf.BestSize(msize.x, msize.y);
	p_inf.FloatingSize(msize.x, msize.y);
	p_inf.FloatingPosition(140, 140);
	p_inf.MinSize(msize.x, msize.y);
	p_inf.Show(false);
	p_inf.Caption(wxS("Object Edit"));
	p_inf.Name(wxS("object_edit"));
	m_mgr->AddPane(panel_obj_edit_, p_inf);


	// --- Map Checks Panel ---
	panel_checks_ = new MapChecksPanel(this, &(mapeditor::editContext().map()));

	// Setup panel info & add panel
	msize = panel_checks_->GetBestSize();
	p_inf.DefaultPane();
	p_inf.Left();
	p_inf.Dock();
	p_inf.BestSize(msize.x, msize.y);
	p_inf.FloatingSize(msize.x, msize.y);
	p_inf.FloatingPosition(160, 160);
	p_inf.MinSize(msize.x, msize.y);
	p_inf.Show(false);
	p_inf.Caption(wxS("Map Checks"));
	p_inf.Name(wxS("map_checks"));
	p_inf.Layer(0);
	m_mgr->AddPane(panel_checks_, p_inf);


	// -- Undo History Panel --
	panel_undo_history_ = new UndoManagerHistoryPanel(this, nullptr);
	panel_undo_history_->setManager(mapeditor::editContext().undoManager());

	// Setup panel info & add panel
	p_inf.DefaultPane();
	p_inf.Right();
	p_inf.BestSize(wxutil::scaledSize(128, 480));
	p_inf.Caption(wxS("Undo History"));
	p_inf.Name(wxS("undo_history"));
	p_inf.Show(false);
	p_inf.Dock();
	m_mgr->AddPane(panel_undo_history_, p_inf);


	// Load previously saved window layout
	loadLayout();

	m_mgr->Update();
	Layout();

	// Initial focus on the canvas, so shortcuts work
	map_canvas_->SetFocus();
}

// -----------------------------------------------------------------------------
// Locks/unlocks the entries for the current map
// -----------------------------------------------------------------------------
void MapEditorWindow::lockMapEntries(bool lock) const
{
	// Don't bother if no map is open
	auto& map_desc = mapeditor::editContext().mapDesc();
	auto  head     = map_desc.head.lock();
	if (!head)
		return;

	// Just lock/unlock the 'head' entry if it's a pk3 map
	if (map_desc.archive)
	{
		if (lock)
			head->lock();
		else if (!app::archiveManager().getArchive(head.get()))
			head->unlock();
	}

	// Otherwise lock all map entries (head -> end)
	else
	{
		auto end_ptr = map_desc.end.lock();
		auto current = head.get();
		if (auto end = end_ptr.get())
		{
			while (current)
			{
				if (lock)
					current->lock();
				else
					current->unlock();

				if (current == end)
					break;

				current = current->nextEntry();
			}
		}
	}
}

// -----------------------------------------------------------------------------
// Opens the map editor launcher dialog to create or open a map
// -----------------------------------------------------------------------------
bool MapEditorWindow::chooseMap(Archive* archive)
{
	MapEditorConfigDialog dlg(maineditor::windowWx(), archive, archive != nullptr, archive == nullptr);

	if (dlg.ShowModal() == wxID_OK)
	{
		auto md = dlg.selectedMap();

		if (md.name.empty() || (archive && !md.head.lock()))
			return false;

		// Attempt to load selected game configuration
		if (!game::configuration().openConfig(dlg.selectedGame(), dlg.selectedPort(), md.format))
		{
			wxMessageBox(
				wxS("An error occurred loading the game configuration, see the console log for details"),
				wxS("Error"),
				wxICON_ERROR);
			return false;
		}

		// Show map editor window
		if (IsIconized())
			Restore();
		Raise();

		// Attempt to open map
		if (!openMap(md))
		{
			Hide();
			wxMessageBox(
				WX_FMT("Unable to open map {}: {}", md.name, global::error), wxS("Invalid map error"), wxICON_ERROR);
			return false;
		}
		else
			return true;
	}
	return false;
}

// -----------------------------------------------------------------------------
// Opens [map] in the editor
// -----------------------------------------------------------------------------
bool MapEditorWindow::openMap(const Archive::MapDesc& map)
{
	// If a map is currently open and modified, prompt to save changes
	if (mapeditor::editContext().map().isModified())
	{
		wxMessageDialog md{ this,
							WX_FMT("Save changes to map {}?", mapeditor::editContext().mapDesc().name),
							wxS("Unsaved Changes"),
							wxYES_NO | wxCANCEL };

		int answer = md.ShowModal();
		if (answer == wxID_YES)
			saveMap();
		else if (answer == wxID_CANCEL)
			return true;
	}

	// Show blank map
	Show(true);
	map_canvas_->Refresh();
	Layout();
	Update();
	Refresh();

	// Clear current map data
	map_data_.clear();

	// Get map parent archive
	Archive* archive = nullptr;
	if (auto head = map.head.lock())
	{
		archive = head->parent();

		// Load map data
		if (map.archive)
		{
			WadArchive temp;
			temp.open(head->data());
			for (unsigned a = 0; a < temp.numEntries(); a++)
				map_data_.emplace_back(new ArchiveEntry(*(temp.entryAt(a))));
		}
		else
		{
			auto entries = map.entries(*archive, true);
			for (auto entry : entries)
				map_data_.emplace_back(new ArchiveEntry(*entry));
		}
	}

	// Set texture manager archive
	mapeditor::textureManager().setArchive(app::archiveManager().shareArchive(archive));

	// Clear current map
	closeMap();

	// Attempt to open map
	ui::showSplash("Loading Map", true, this);
	bool ok = mapeditor::editContext().openMap(map);
	ui::hideSplash();

	// Show window if opened ok
	if (ok)
	{
		mapeditor::editContext().mapDesc() = map;

		// Update DECORATE and *MAPINFO definitions
		game::updateCustomDefinitions();

		// Load scripts if any
		loadMapScripts(map);

		// Lock map entries
		lockMapEntries();

		// Reset map checks panel
		panel_checks_->reset();

		mapeditor::editContext().renderer().viewFitToMap(true);
		map_canvas_->Refresh();

		// Set window title
		if (archive)
			SetTitle(WX_FMT("SLADE - {} of {}", map.name, archive->filename(false)));
		else
			SetTitle(WX_FMT("SLADE - {} (UNSAVED)", map.name));

		// Create backup
		auto head = map.head.lock();
		if (head
			&& !mapeditor::backupManager().writeBackup(
				map_data_, head->topParent()->filename(false), head->nameNoExt()))
			log::warning("Failed to backup map data");
	}

	return ok;
}

// -----------------------------------------------------------------------------
// Loads any scripts from [map] into the script editor
// -----------------------------------------------------------------------------
void MapEditorWindow::loadMapScripts(const Archive::MapDesc& map)
{
	// Don't bother if no scripting language specified
	if (game::configuration().scriptLanguage().empty())
	{
		// Hide script editor
		auto  m_mgr = wxAuiManager::GetManager(this);
		auto& p_inf = m_mgr->GetPane(wxS("script_editor"));
		p_inf.Show(false);
		m_mgr->Update();
		return;
	}

	// Don't bother if new map
	auto head = map.head.lock();
	if (!head)
	{
		panel_script_editor_->openScripts(nullptr, nullptr);
		return;
	}

	// Check for pk3 map
	if (map.archive)
	{
		auto wad = new WadArchive();
		wad->open(head->data());
		auto maps = wad->detectMaps();
		if (!maps.empty())
		{
			loadMapScripts(maps[0]);
			wad->close();
			return;
		}

		delete wad;
	}

	// Go through map entries
	ArchiveEntry* scripts  = nullptr;
	ArchiveEntry* compiled = nullptr;
	auto          entries  = map.entries(*head->parent());
	for (auto entry : entries)
	{
		// Check for SCRIPTS/BEHAVIOR
		if (game::configuration().scriptLanguage() == "acs_hexen"
			|| game::configuration().scriptLanguage() == "acs_zdoom")
		{
			if (entry->upperName() == "SCRIPTS")
				scripts = entry;
			if (entry->upperName() == "BEHAVIOR")
				compiled = entry;
		}
	}

	// Open scripts/compiled if found
	panel_script_editor_->openScripts(scripts, compiled);
}

// -----------------------------------------------------------------------------
// Builds nodes for the maps in [wad]
// -----------------------------------------------------------------------------
void MapEditorWindow::buildNodes(Archive* wad)
{
	// Save wad to disk
	auto filename = app::path("sladetemp.wad", app::Dir::Temp);
	wad->save(filename);

	// Get current nodebuilder
	auto   builder = nodebuilders::builder(nodebuilder_id);
	string command = builder.command;
	string options = nodebuilder_options;

	// Don't build if none selected
	if (builder.id == "none")
		return;

	// Switch to ZDBSP if UDMF
	if (mapeditor::editContext().mapDesc().format == MapFormat::UDMF && nodebuilder_id.value != "zdbsp")
	{
		wxMessageBox(wxS("Nodebuilder switched to ZDBSP for UDMF format"), wxS("Save Map"), wxICON_INFORMATION);
		builder = nodebuilders::builder("zdbsp");
		command = builder.command;
	}

	// Check for undefined path
	if (!fileutil::fileExists(builder.path) && !nb_warned)
	{
		// Open nodebuilder preferences
		PreferencesDialog::openPreferences(this, "Node Builders");

		// Get new builder if one was selected
		builder = nodebuilders::builder(nodebuilder_id);
		command = builder.command;

		// Check again
		if (!fileutil::fileExists(builder.path))
		{
			wxMessageBox(
				wxS("No valid Node Builder is currently configured, nodes will not be built!"),
				wxS("Warning"),
				wxICON_WARNING);
			nb_warned = true;
		}
	}

	// Build command line
	strutil::replaceIP(command, "$f", fmt::format("\"{}\"", filename));
	strutil::replaceIP(command, "$o", options);

	// Run nodebuilder
	if (fileutil::fileExists(builder.path))
	{
		wxArrayString out;
		log::info("execute \"{} {}\"", builder.path, command);
		wxGetApp().SetTopWindow(this);
		auto focus = wxWindow::FindFocus();
		wxExecute(wxString::FromUTF8(fmt::format("\"{}\" {}", builder.path, command)), out, wxEXEC_HIDE_CONSOLE);
		wxGetApp().SetTopWindow(maineditor::windowWx());
		if (focus)
			focus->SetFocusFromKbd();
		log::info(1, "Nodebuilder output:");
		for (const auto& line : out)
			log::info(line.utf8_string());

		// Re-load wad
		wad->close();
		wad->open(filename);
	}
	else if (nb_warned)
		log::info(1, "Nodebuilder path not set up, no nodes were built");
}

// -----------------------------------------------------------------------------
// Writes the current map as [name] to a wad archive and returns it
// -----------------------------------------------------------------------------
bool MapEditorWindow::writeMap(WadArchive& wad, const string& name, bool nodes)
{
	auto& mdesc_current = mapeditor::editContext().mapDesc();
	auto& map           = mapeditor::editContext().map();

	// Get map data entries
	vector<ArchiveEntry*> new_map_data;
	if (!map.writeMap(new_map_data))
		return false;

	// Check script language
	bool acs = false;
	if (game::configuration().scriptLanguage() == "acs_hexen" || game::configuration().scriptLanguage() == "acs_zdoom")
		acs = true;
	// Force ACS on for Hexen map format, and off for Doom map format
	if (mdesc_current.format == MapFormat::Doom)
		acs = false;
	if (mdesc_current.format == MapFormat::Hexen)
		acs = true;
	bool dialogue = false;
	if (game::configuration().scriptLanguage() == "usdf" || game::configuration().scriptLanguage() == "zsdf")
		dialogue = true;

	// Add map data to temporary wad
	wad.addNewEntry(name);
	// Handle fragglescript and similar content in the map header
	auto head = mdesc_current.head.lock();
	if (head && head->size() && !mdesc_current.archive)
		wad.entry(name)->importMemChunk(head->data());
	for (auto& entry : new_map_data)
		wad.addEntry(shared_ptr<ArchiveEntry>(entry));
	if (acs) // BEHAVIOR
		wad.addEntry(std::make_shared<ArchiveEntry>(*panel_script_editor_->compiledEntry()), "");
	if (acs && panel_script_editor_->scriptEntry()->size() > 0) // SCRIPTS (if any)
		wad.addEntry(std::make_shared<ArchiveEntry>(*panel_script_editor_->scriptEntry()), "");
	if (mdesc_current.format == MapFormat::UDMF)
	{
		// Add extra UDMF entries
		for (auto& entry : map.udmfExtraEntries())
			wad.addEntry(std::make_shared<ArchiveEntry>(*entry), -1, nullptr);

		wad.addNewEntry("ENDMAP");
	}

	// Build nodes
	if (nodes)
		buildNodes(&wad);

	// Clear current map data
	map_data_.clear();

	// Update map data
	for (unsigned a = 0; a < wad.numEntries(); a++)
		map_data_.emplace_back(new ArchiveEntry(*(wad.entryAt(a))));

	return true;
}

// -----------------------------------------------------------------------------
// Saves the current map to its archive, or opens the 'save as' dialog if it
// doesn't currently belong to one
// -----------------------------------------------------------------------------
bool MapEditorWindow::saveMap()
{
	auto& mdesc_current = mapeditor::editContext().mapDesc();

	// Check for newly created map
	auto current_head = mdesc_current.head.lock();
	if (!current_head)
		return saveMapAs();

	// Write map to temp wad
	WadArchive wad;
	if (!writeMap(wad))
		return false;

	// Check for map archive
	unique_ptr<Archive> tempwad;
	auto                map = mdesc_current;
	if (mdesc_current.archive && current_head)
	{
		tempwad = std::make_unique<WadArchive>();
		tempwad->open(current_head.get());
		auto amaps = tempwad->detectMaps();
		if (!amaps.empty())
			map = amaps[0];
		else
			return false;
	}

	// Unlock current map entries
	lockMapEntries(false);

	// Delete current map entries
	auto m_head  = map.head.lock();
	auto archive = m_head->parent();
	auto entries = map.entries(*archive);
	for (auto entry : entries)
		archive->removeEntry(entry);

	// Create backup
	if (!mapeditor::backupManager().writeBackup(map_data_, m_head->topParent()->filename(false), m_head->nameNoExt()))
		log::warning(1, "Warning: Failed to backup map data");

	// Add new map entries
	auto entry_end = map.head;
	for (unsigned a = 1; a < wad.numEntries(); a++)
	{
		auto copy = std::make_shared<ArchiveEntry>(*wad.entryAt(a));
		archive->addEntry(copy, archive->entryIndex(m_head.get()) + a, nullptr);
		entry_end = copy;
	}

	// Clean up
	if (tempwad)
		tempwad->save();
	else
		mdesc_current.end = entry_end; // Update map description

	// Finish
	mdesc_current.updateMapFormatHints();
	lockMapEntries();
	mapeditor::editContext().map().setOpenedTime();

	return true;
}

// -----------------------------------------------------------------------------
// Saves the current map to a new archive
// -----------------------------------------------------------------------------
bool MapEditorWindow::saveMapAs()
{
	auto& mdesc_current = mapeditor::editContext().mapDesc();

	// Show dialog
	filedialog::FDInfo info;
	if (!filedialog::saveFile(info, "Save Map As", "Wad Archives (*.wad)|*.wad", this))
		return false;

	// Create new, empty wad
	WadArchive               wad;
	auto                     head = wad.addNewEntry(mdesc_current.name);
	shared_ptr<ArchiveEntry> end;
	if (mdesc_current.format == MapFormat::UDMF)
	{
		wad.addNewEntry("TEXTMAP");
		end = wad.addNewEntry("ENDMAP");
	}
	else
	{
		wad.addNewEntry("THINGS");
		wad.addNewEntry("LINEDEFS");
		wad.addNewEntry("SIDEDEFS");
		wad.addNewEntry("VERTEXES");
		end = wad.addNewEntry("SECTORS");
	}

	// Save map data
	mdesc_current.head    = head;
	mdesc_current.archive = false;
	mdesc_current.end     = end;
	saveMap();

	// Write wad to file
	wad.save(info.filenames[0]);
	auto archive = app::archiveManager().openArchive(info.filenames[0], true, true);
	app::archiveManager().addRecentFile(info.filenames[0]);

	// Update current map description
	auto maps = archive->detectMaps();
	if (!maps.empty())
	{
		mdesc_current.head    = maps[0].head;
		mdesc_current.archive = false;
		mdesc_current.end     = maps[0].end;
	}

	// Set window title
	SetTitle(wxString::FromUTF8(fmt::format("SLADE - {} of {}", mdesc_current.name, wad.filename(false))));

	return true;
}

// -----------------------------------------------------------------------------
// Closes/clears the current map
// -----------------------------------------------------------------------------
void MapEditorWindow::closeMap() const
{
	// Close map in editor
	mapeditor::editContext().clearMap();

	// Unlock current map entries
	lockMapEntries(false);

	// Clear map info
	mapeditor::editContext().mapDesc().head.reset();
}

// -----------------------------------------------------------------------------
// Forces a refresh of the map canvas, and the renderer if [renderer] is true
// -----------------------------------------------------------------------------
void MapEditorWindow::forceRefresh(bool renderer) const
{
	if (!IsShown())
		return;

	if (renderer)
		mapeditor::editContext().forceRefreshRenderer();
	map_canvas_->Refresh();
}

// -----------------------------------------------------------------------------
// Refreshes the toolbar
// -----------------------------------------------------------------------------
void MapEditorWindow::refreshToolBar() const
{
	toolbar_->Refresh();
}

// -----------------------------------------------------------------------------
// Checks if the currently open map is modified and prompts to save.
// If 'Cancel' is clicked then this will return false (ie. we don't want to
// close the window)
// -----------------------------------------------------------------------------
bool MapEditorWindow::tryClose()
{
	if (mapeditor::editContext().map().isModified())
	{
		wxMessageDialog md{ this,
							WX_FMT("Save changes to map {}?", mapeditor::editContext().mapDesc().name),
							wxS("Unsaved Changes"),
							wxYES_NO | wxCANCEL };
		int             answer = md.ShowModal();
		if (answer == wxID_YES)
			return saveMap();
		else if (answer == wxID_CANCEL)
			return false;
	}

	return true;
}

// -----------------------------------------------------------------------------
// Returns true if the currently open map is from [archive]
// -----------------------------------------------------------------------------
bool MapEditorWindow::hasMapOpen(const Archive* archive) const
{
	auto& mdesc = mapeditor::editContext().mapDesc();
	if (auto head = mdesc.head.lock())
		return head->parent() == archive;

	return false;
}

// -----------------------------------------------------------------------------
// Reloads the map editor scripts menu
// -----------------------------------------------------------------------------
void MapEditorWindow::reloadScriptsMenu() const
{
	while (menu_scripts_->FindItemByPosition(0))
		menu_scripts_->Delete(menu_scripts_->FindItemByPosition(0));

#ifndef NO_LUA
	scriptmanager::populateEditorScriptMenu(menu_scripts_, scriptmanager::ScriptType::Map, "mapw_script");
#endif
}

// -----------------------------------------------------------------------------
// Sets the undo manager to show in the undo history panel
// -----------------------------------------------------------------------------
void MapEditorWindow::setUndoManager(UndoManager* manager) const
{
	panel_undo_history_->setManager(manager);
}

// -----------------------------------------------------------------------------
// Shows/hides the object edit panel (opens [group] if shown)
// -----------------------------------------------------------------------------
void MapEditorWindow::showObjectEditPanel(bool show, ObjectEditGroup* group)
{
	// Get panel
	auto  m_mgr = wxAuiManager::GetManager(this);
	auto& p_inf = m_mgr->GetPane(wxS("object_edit"));

	// Save current y offset
	double top = mapeditor::editContext().renderer().view().canvasY(0);

	// Enable/disable panel
	if (show)
		panel_obj_edit_->init(group);
	p_inf.Show(show);

	// Update layout
	map_canvas_->Enable(false);
	m_mgr->Update();

	// Restore y offset
	mapeditor::editContext().renderer().setTopY(top);
	map_canvas_->Enable(true);
	map_canvas_->SetFocus();
}

// -----------------------------------------------------------------------------
// Shows/hides the shape drawing panel
// -----------------------------------------------------------------------------
void MapEditorWindow::showShapeDrawPanel(bool show)
{
	// Get panel
	auto  m_mgr = wxAuiManager::GetManager(this);
	auto& p_inf = m_mgr->GetPane(wxS("shape_draw"));

	// Save current y offset
	double top = mapeditor::editContext().renderer().view().canvasY(0);

	// Enable/disable panel
	p_inf.Show(show);

	// Update layout
	map_canvas_->Enable(false);
	m_mgr->Update();

	// Restore y offset
	mapeditor::editContext().renderer().setTopY(top);
	map_canvas_->Enable(true);
	map_canvas_->SetFocus();
}

// -----------------------------------------------------------------------------
// Handles the action [id].
// Returns true if the action was handled, false otherwise
// -----------------------------------------------------------------------------
bool MapEditorWindow::handleAction(string_view id)
{
	auto& mdesc_current = mapeditor::editContext().mapDesc();

	// Don't handle actions if hidden
	if (!IsShown())
		return false;

	// Map->Save
	if (id == "mapw_save")
	{
		// Save map
		if (saveMap())
		{
			// Save archive
			if (auto head = mdesc_current.head.lock())
			{
				if (auto a = head->parent(); a && save_archive_with_map)
				{
					if (a->canSave())
						a->save();
					else
					{
						// Can't save archive, do Save As instead
						if (maineditor::saveArchiveAs(a))
							SetTitle(
								wxString::FromUTF8(
									fmt::format("SLADE - {} of {}", mdesc_current.name, a->filename(false))));
					}
				}
			}
		}
		mapeditor::editContext().renderer().forceUpdate();
		return true;
	}

	// Map->Save As
	if (id == "mapw_saveas")
	{
		saveMapAs();
		mapeditor::editContext().renderer().forceUpdate();
		return true;
	}

	// Map->Restore Backup
	if (id == "mapw_backup")
	{
		if (auto head = mdesc_current.head.lock())
		{
			auto data = mapeditor::backupManager().openBackup(head->topParent()->filename(false), mdesc_current.name);

			if (data)
			{
				auto maps = data->detectMaps();
				if (!maps.empty())
				{
					mapeditor::editContext().clearMap();
					mapeditor::editContext().openMap(maps[0]);
					loadMapScripts(maps[0]);
				}
			}
		}

		return true;
	}

	// Map->Close
	if (id == "mapw_close")
	{
		wxWindow::Close();
		return true;
	}

	// Edit->Undo
	if (id == "mapw_undo")
	{
		mapeditor::editContext().doUndo();
		return true;
	}

	// Edit->Redo
	if (id == "mapw_redo")
	{
		mapeditor::editContext().doRedo();
		return true;
	}

	// Editor->Set Base Resource Archive
	if (id == "mapw_setbra")
	{
		PreferencesDialog::openPreferences(this, "Base Resource Archive");

		return true;
	}

	// Editor->Preferences
	if (id == "mapw_preferences")
	{
		PreferencesDialog::openPreferences(this, "Map Editor");

		return true;
	}

	// View->Item Properties
	if (id == "mapw_showproperties")
	{
		auto  m_mgr = wxAuiManager::GetManager(this);
		auto& p_inf = m_mgr->GetPane(wxS("item_props"));

		// Toggle window and focus
		p_inf.Show(!p_inf.IsShown());
		map_canvas_->SetFocus();

		p_inf.MinSize(wxutil::scaledSize(256, 256));
		m_mgr->Update();
		return true;
	}

	// View->Console
	else if (id == "mapw_showconsole")
	{
		auto  m_mgr = wxAuiManager::GetManager(this);
		auto& p_inf = m_mgr->GetPane(wxS("console"));

		// Toggle window and focus
		if (p_inf.IsShown())
		{
			p_inf.Show(false);
			map_canvas_->SetFocus();
		}
		else
		{
			p_inf.Show(true);
			p_inf.window->SetFocus();
			dynamic_cast<ConsolePanel*>(p_inf.window)->focusInput();
		}

		p_inf.MinSize(wxutil::scaledSize(200, 128));
		m_mgr->Update();
		return true;
	}

	// View->Script Editor
	else if (id == "mapw_showscripteditor")
	{
		auto  m_mgr = wxAuiManager::GetManager(this);
		auto& p_inf = m_mgr->GetPane(wxS("script_editor"));

		// Toggle window and focus
		if (p_inf.IsShown())
		{
			p_inf.Show(false);
			map_canvas_->SetFocus();
		}
		else if (!game::configuration().scriptLanguage().empty())
		{
			p_inf.Show(true);
			p_inf.window->SetFocus();
			dynamic_cast<ScriptEditorPanel*>(p_inf.window)->updateUI();
		}

		p_inf.MinSize(wxutil::scaledSize(200, 128));
		m_mgr->Update();
		return true;
	}

	// View->Map Checks
	else if (id == "mapw_showchecks")
	{
		auto  m_mgr = wxAuiManager::GetManager(this);
		auto& p_inf = m_mgr->GetPane(wxS("map_checks"));

		// Toggle window and focus
		if (p_inf.IsShown())
		{
			p_inf.Show(false);
			map_canvas_->SetFocus();
		}
		else
		{
			p_inf.Show(true);
			p_inf.window->SetFocus();
		}

		p_inf.MinSize(panel_checks_->GetBestSize());
		m_mgr->Update();
		return true;
	}

	// View->Undo History
	else if (id == "mapw_showundohistory")
	{
		auto  m_mgr = wxAuiManager::GetManager(this);
		auto& p_inf = m_mgr->GetPane(wxS("undo_history"));

		// Toggle window
		p_inf.Show(!p_inf.IsShown());

		m_mgr->Update();
		return true;
	}

	// Run Map
	else if (id == "mapw_run_map" || id == "mapw_run_map_here" || id == "mapw_quick_run_map")
	{
		Archive* archive = nullptr;
		if (auto head = mdesc_current.head.lock())
			archive = head->parent();
		RunDialog dlg(this, archive, id == "mapw_run_map", true);
		if (id == "mapw_quick_run_map" || dlg.ShowModal() == wxID_OK)
		{
			auto& edit_context = mapeditor::editContext();
			// Move player 1 start if needed
			if (id == "mapw_run_map_here")
				edit_context.swapPlayerStart2d(edit_context.input().mouseDownPosMap());
			else if (dlg.start3dModeChecked())
				edit_context.swapPlayerStart3d();

			// Write temp wad
			WadArchive wad;
			if (writeMap(wad, mdesc_current.name))
				wad.save(app::path("sladetemp_run.wad", app::Dir::Temp));

			// Reset player 1 start if moved
			if (dlg.start3dModeChecked() || id == "mapw_run_map_here")
				mapeditor::editContext().resetPlayerStart();

			auto command = dlg.selectedCommandLine(archive, mdesc_current.name, wad.filename());
			if (!command.empty())
			{
				// Set working directory
				auto wd = wxGetCwd();
				wxSetWorkingDirectory(wxString::FromUTF8(dlg.selectedExeDir()));

				// Run
				wxExecute(wxString::FromUTF8(command), wxEXEC_ASYNC);

				// Restore working directory
				wxSetWorkingDirectory(wd);
			}
		}

		return true;
	}

#ifndef NO_LUA
	// Tools->Run Script
	else if (id == "mapw_script")
	{
		scriptmanager::runMapScript(&mapeditor::editContext().map(), wx_id_offset_, this);
		return true;
	}

	// Tools->Script Manager
	else if (id == "mapw_runscript")
	{
		scriptmanager::open();
		return true;
	}
#endif

	return false;
}


// -----------------------------------------------------------------------------
//
// MapEditorWindow Class Events
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Called when the window is closed
// -----------------------------------------------------------------------------
void MapEditorWindow::onClose(wxCloseEvent& e)
{
	// Unlock mouse cursor
	bool locked = mapeditor::editContext().mouseLocked();
	mapeditor::editContext().lockMouse(false);

	if (!tryClose())
	{
		// Restore mouse cursor lock
		mapeditor::editContext().lockMouse(locked);

		e.Veto();
		return;
	}

	// Save current layout
	saveLayout();
	const wxSize size = GetSize() * GetContentScaleFactor();
	if (!IsMaximized())
		misc::setWindowInfo(
			id_, size.x, size.y, GetPosition().x * GetContentScaleFactor(), GetPosition().y * GetContentScaleFactor());

	Show(false);
	closeMap();
}

// -----------------------------------------------------------------------------
// Called when the window is resized
// -----------------------------------------------------------------------------
void MapEditorWindow::onSize(wxSizeEvent& e)
{
	// Update maximized cvar
	mew_maximized = IsMaximized();

	e.Skip();
}
