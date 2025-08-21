#include "source-copy.hpp"
#include <obs-module.h>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QWidgetAction>

#include "obs-websocket-api.h"
#include "util/config-file.h"
#include "util/platform.h"
#include "version.h"

#ifndef _WIN32
#include <dlfcn.h>
#endif

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("source-copy", "en-US")

#define MAX_PATH 260

static bool replace(std::string &str, const char *from, const char *to)
{
	size_t start_pos = str.find(from);
	if (start_pos == std::string::npos)
		return false;
	str.replace(start_pos, strlen(from), to);
	return true;
}

static void try_fix_paths(obs_data_t *data, const char *dir, char *path_buffer)
{
	obs_data_item_t *item = obs_data_first(data);
	while (item) {
		const enum obs_data_type type = obs_data_item_gettype(item);
		if (type == OBS_DATA_STRING) {
			std::string str = obs_data_item_get_string(item);
			bool edit = false;
			if (replace(str, "[U_COMBOBULATOR_PATH]", dir)) {
				obs_data_item_set_string(&item, str.c_str());
				edit = true;
			}
			bool local_url = false;
			if (str.substr(0, 7) == "file://") {
				str = str.substr(7);
				local_url = true;
			}
			std::size_t found = str.find_last_of("/\\");
			if (str.length() < MAX_PATH && found != std::string::npos && !os_file_exists(str.c_str())) {
				while (found != std::string::npos) {
					auto file = found == 0 && str[0] != '/' && str[0] != '\\' ? str : str.substr(found + 1);
					if (file.find('.') == std::string::npos)
						break;
					auto oldDir = str.substr(0, found + 1);
					std::string newFile = dir;
					newFile += file;
					if (os_file_exists(newFile.c_str())) {
						if (local_url) {
							str = "file://";
							if (os_get_abs_path(newFile.c_str(), path_buffer, MAX_PATH)) {
								for (auto i = 0; path_buffer[i] != '\0'; i++)
									if (path_buffer[i] == '\\')
										path_buffer[i] = '/';
								str += path_buffer;
							}
						} else {
							if (os_get_abs_path(newFile.c_str(), path_buffer, MAX_PATH)) {
								for (auto i = 0; path_buffer[i] != '\0'; i++)
									if (path_buffer[i] == '\\')
										path_buffer[i] = '/';
								str = path_buffer;
							} else {
								str = "";
							}
						}
						obs_data_item_set_string(&item, str.c_str());
						edit = true;
						break;
					}
					if (found == 0) {
						found = std::string::npos;
					} else {
						found = str.find_last_of("/\\", found - 1);
						if (found == std::string::npos) {
							found = 0;
						}
					}
				}
			}
			if (edit) {
				item = obs_data_first(data);
				continue;
			}
		} else if (type == OBS_DATA_OBJECT) {
			if (obs_data_t *obj = obs_data_item_get_obj(item)) {
				try_fix_paths(obj, dir, path_buffer);
				obs_data_release(obj);
			}
		} else if (type == OBS_DATA_ARRAY) {
			const auto array = obs_data_item_get_array(item);
			const auto count = obs_data_array_count(array);
			for (size_t i = 0; i < count; i++) {
				if (obs_data_t *obj = obs_data_array_item(array, i)) {
					try_fix_paths(obj, dir, path_buffer);
					obs_data_release(obj);
				}
			}
		}
		obs_data_item_next(&item);
	}
}

static void try_fix_paths(obs_data_t *data, QString fileName)
{
	char path_buffer[MAX_PATH];
	std::string dir = QT_TO_UTF8(fileName);
	const std::size_t slash = dir.find_last_of("/\\");
	if (slash != std::string::npos) {
		auto point = dir.find_last_of('.');
		if (point != std::string::npos && point > slash) {
			dir = dir.substr(0, point);
			dir += "/";
			try_fix_paths(data, dir.c_str(), path_buffer);
		}
		dir = dir.substr(0, slash + 1);
	}
	try_fix_paths(data, dir.c_str(), path_buffer);
}

static void LoadSourceMenu(QMenu *menu, obs_source_t *source, obs_sceneitem_t *item);

static void LoadSources(obs_data_array_t *data, obs_scene_t *scene, obs_canvas_t *canvas = nullptr)
{
	const size_t count = obs_data_array_count(data);
	std::vector<obs_source_t *> sources;
	sources.reserve(count);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *sourceData = obs_data_array_item(data, i);
		const char *name = obs_data_get_string(sourceData, "name");
		const char *canvas_uuid = obs_data_get_string(sourceData, "canvas_uuid");
		if (canvas && canvas_uuid && canvas_uuid[0] != '\0' && strcmp(canvas_uuid, obs_canvas_get_uuid(canvas)) != 0) {
			obs_data_set_string(sourceData, "canvas_uuid", obs_canvas_get_uuid(canvas));
			obs_source_t *found = obs_get_source_by_uuid(obs_data_get_string(sourceData, "uuid"));
			if (found) {
				obs_canvas_t *found_canvas = obs_source_get_canvas(found);
				if (found_canvas != canvas) {
					obs_data_unset_user_value(sourceData, "uuid");
				}
				obs_canvas_release(found_canvas);
				obs_source_release(found);
			}
		}
		obs_source_t *s =
			(canvas && obs_get_source_output_flags(obs_data_get_string(sourceData, "id")) & OBS_SOURCE_REQUIRES_CANVAS)
				? obs_canvas_get_source_by_name(canvas, name)
				: obs_get_source_by_name(name);
		if (!s)
			s = obs_load_source(sourceData);
		if (s) {
			sources.push_back(s);
			if (i == count - 1 && scene &&
			    (obs_source_get_type(s) == OBS_SOURCE_TYPE_SCENE || obs_source_get_type(s) == OBS_SOURCE_TYPE_INPUT))
				obs_scene_add(scene, s);
		}
		obs_scene_t *scene = obs_scene_from_source(s);
		if (!scene)
			scene = obs_group_from_source(s);
		if (scene) {
			obs_data_t *scene_settings = obs_data_get_obj(sourceData, "settings");
			obs_source_update(s, scene_settings);
			obs_data_release(scene_settings);
		}
		obs_data_release(sourceData);
	}

	for (obs_source_t *source : sources)
		obs_source_load(source);

	for (obs_source_t *source : sources)
		obs_source_release(source);
}

static void LoadSceneCanvas(obs_data_t *data, obs_canvas_t *canvas)
{
	if (!data)
		return;
	obs_data_array_t *sourcesData = obs_data_get_array(data, "sources");
	if (!sourcesData)
		return;
	LoadSources(sourcesData, nullptr, canvas);
	obs_data_array_release(sourcesData);
}

static void LoadScene(obs_data_t *data)
{
	LoadSceneCanvas(data, nullptr);
}

config_t *get_user_config(void)
{
	return obs_frontend_get_user_config();
}

obs_data_array_t *GetScriptsData()
{
	const auto config = get_user_config();
	if (!config)
		return nullptr;
	const std::string sceneCollection = config_get_string(config, "Basic", "SceneCollection");
	const std::string filename = config_get_string(config, "Basic", "SceneCollectionFile");
	std::string path = obs_module_config_path("../../basic/scenes/");
	path += filename;
	path += ".json";

	obs_frontend_save();
	auto data = obs_data_create_from_json_file(path.c_str());
	if (!data)
		return nullptr;

	auto modules = obs_data_get_obj(data, "modules");
	auto scripts = obs_data_get_array(modules, "scripts-tool");
	obs_data_release(modules);
	obs_data_release(data);
	return scripts;
}

void LoadScriptData(obs_data_t *script_data)
{
	const auto config = get_user_config();
	if (!config)
		return;

	obs_frontend_save();
	const std::string sceneCollection = config_get_string(config, "Basic", "SceneCollection");
	const std::string filename = config_get_string(config, "Basic", "SceneCollectionFile");
	std::string path = obs_module_config_path("../../basic/scenes/");
	path += filename;
	path += ".json";

	auto data = obs_data_create_from_json_file(path.c_str());
	if (!data)
		return;

	auto modules = obs_data_get_obj(data, "modules");
	auto scripts = obs_data_get_array(modules, "scripts-tool");
	obs_data_release(modules);
	if (scripts) {
		obs_data_array_push_back(scripts, script_data);
		obs_data_array_release(scripts);
		obs_data_save_json_safe(data, path.c_str(), "tmp", "bak");
		obs_data_release(data);
		config_set_string(config, "Basic", "SceneCollection", "Source Copy Temp");
		config_set_string(config, "Basic", "SceneCollectionFile", "source_copy_temp");
		obs_frontend_set_current_scene_collection(sceneCollection.c_str());
		std::string temp_path = obs_module_config_path("../../basic/scenes/source_copy_temp.json");
		os_unlink(temp_path.c_str());
	} else {
		obs_data_release(data);
	}
}

static void LoadScriptMenu(QMenu *menu)
{
	menu->clear();
	auto a = menu->addAction(QT_UTF8(obs_module_text("LoadScript")));
	QObject::connect(a, &QAction::triggered, [] {
		QString fileName = QFileDialog::getOpenFileName(nullptr, QT_UTF8(obs_module_text("LoadScript")), QString(),
								"JSON File (*.json)");
		if (fileName.isEmpty())
			return;
		obs_data_t *data = obs_data_create_from_json_file(QT_TO_UTF8(fileName));
		if (!data)
			return;
		try_fix_paths(data, fileName);
		LoadScriptData(data);
		obs_data_release(data);
	});
	a = menu->addAction(QT_UTF8(obs_module_text("PasteScript")));
	QObject::connect(a, &QAction::triggered, [] {
		QClipboard *clipboard = QGuiApplication::clipboard();
		const QString strData = clipboard->text();
		if (strData.isEmpty())
			return;
		const auto data = obs_data_create_from_json(QT_TO_UTF8(strData));
		if (!data)
			return;
		LoadScriptData(data);
		obs_data_release(data);
	});

	const auto scripts = GetScriptsData();
	if (!scripts)
		return;

	menu->addSeparator();
	const size_t size = obs_data_array_count(scripts);
	for (size_t i = 0; i < size; i++) {
		auto script = obs_data_array_item(scripts, i);
		const char *script_path = obs_data_get_string(script, "path");
		const char *slash = script_path && *script_path ? strrchr(script_path, '/') : nullptr;
		QMenu *m;
		if (slash) {
			slash++;
			m = menu->addMenu(QT_UTF8(slash));
		} else {
			m = menu->addMenu(QT_UTF8(script_path));
		}
		QString scriptData = QT_UTF8(obs_data_get_json(script));
		a = m->addAction(QT_UTF8(obs_module_text("SaveScript")));
		QObject::connect(a, &QAction::triggered, [scriptData] {
			const QString fileName = QFileDialog::getSaveFileName(nullptr, QT_UTF8(obs_module_text("SaveScript")),
									      QString(), "JSON File (*.json)");
			if (fileName.isEmpty())
				return;
			auto d = QT_TO_UTF8(scriptData);
			os_quick_write_utf8_file(QT_TO_UTF8(fileName), d, strlen(d), false);
		});
		a = m->addAction(QT_UTF8(obs_module_text("CopyScript")));
		QObject::connect(a, &QAction::triggered, [scriptData] {
			QClipboard *clipboard = QGuiApplication::clipboard();
			clipboard->setText(scriptData);
		});
	}
	obs_data_array_release(scripts);
}

static void LoadCanvasMenu(QMenu *menu, obs_canvas_t *canvas)
{
	menu->clear();
	QAction *a = menu->addAction(obs_module_text("LoadScene"));
	QObject::connect(a, &QAction::triggered, [canvas] {
		QString fileName = QFileDialog::getOpenFileName(nullptr, QT_UTF8(obs_module_text("LoadScene")), QString(),
								"JSON File (*.json)");
		if (fileName.isEmpty())
			return;
		obs_data_t *data = obs_data_create_from_json_file(QT_TO_UTF8(fileName));
		try_fix_paths(data, fileName);
		LoadSceneCanvas(data, canvas);
		obs_data_release(data);
	});
	a = menu->addAction(QT_UTF8(obs_module_text("PasteScene")));
	QObject::connect(a, &QAction::triggered, [canvas] {
		QClipboard *clipboard = QGuiApplication::clipboard();
		const QString strData = clipboard->text();
		if (strData.isEmpty())
			return;
		obs_data_t *data = obs_data_create_from_json(QT_TO_UTF8(strData));
		LoadSceneCanvas(data, canvas);
		obs_data_release(data);
	});
	auto label = new QLabel("<b>" + QT_UTF8(obs_module_text("Scenes")) + "</b>");
	label->setAlignment(Qt::AlignCenter);

	auto wa = new QWidgetAction(menu);
	wa->setDefaultWidget(label);
	menu->addAction(wa);

	wa = new QWidgetAction(menu);
	auto t = new QLineEdit;
	t->connect(t, &QLineEdit::textChanged, [menu](const QString text) {
		foreach(auto action, menu->actions())
		{
			if (!action->menu())
				continue;
			if (text.isEmpty()) {
				action->setVisible(true);
			} else {
				action->setVisible(action->text().contains(text, Qt::CaseInsensitive));
			}
		}
	});
	wa->setDefaultWidget(t);
	menu->addAction(wa);
	if (canvas) {
		obs_canvas_enum_scenes(
			canvas,
			[](void *data, obs_source_t *scene) -> bool {
				QMenu *menu = static_cast<QMenu *>(data);

				QMenu *submenu = menu->addMenu(obs_source_get_name(scene));
				QObject::connect(submenu, &QMenu::aboutToShow,
						 [submenu, scene] { LoadSourceMenu(submenu, scene, nullptr); });
				return true;
			},
			menu);
	} else {
		struct obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; i++) {
			obs_source_t *source = scenes.sources.array[i];
			QMenu *submenu = menu->addMenu(obs_source_get_name(scenes.sources.array[i]));
			QObject::connect(submenu, &QMenu::aboutToShow,
					 [submenu, source] { LoadSourceMenu(submenu, source, nullptr); });
		}
		obs_frontend_source_list_free(&scenes);
	}
}

static void LoadMenu(QMenu *menu)
{
	menu->clear();

	obs_enum_canvases(
		[](void *data, obs_canvas_t *canvas) -> bool {
			QMenu *menu = static_cast<QMenu *>(data);
			if (!canvas)
				return true;
			auto canvasMenu = menu->addMenu(QT_UTF8(obs_canvas_get_name(canvas)));
			QObject::connect(canvasMenu, &QMenu::aboutToShow,
					 [canvasMenu, canvas] { LoadCanvasMenu(canvasMenu, canvas); });
			return true;
		},
		menu);

	menu->addSeparator();

	QMenu *submenu = menu->addMenu(QT_UTF8(obs_module_text("Scripts")));
	QObject::connect(submenu, &QMenu::aboutToShow, [submenu] { LoadScriptMenu(submenu); });

	menu->addSeparator();
	menu->addAction(QString::fromUtf8("Source Copy (" PROJECT_VERSION ")"),
			[] { QDesktopServices::openUrl(QUrl("https://obsproject.com/forum/resources/source-copy.1261/")); });
	menu->addAction(QString::fromUtf8("By Exeldro"), [] { QDesktopServices::openUrl(QUrl("https://exeldro.com")); });
}

void CopyTransform(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window->isActiveWindow())
		return;

	QAction *t = main_window->findChild<QAction *>("actionCopyTransform");
	if (t)
		t->trigger();
}

void PasteTransform(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window->isActiveWindow())
		return;
	QAction *t = main_window->findChild<QAction *>("actionPasteTransform");
	if (t)
		t->trigger();
}

obs_hotkey_id copyTransformHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id pasteTransformHotkey = OBS_INVALID_HOTKEY_ID;

static void frontend_save_load(obs_data_t *save_data, bool saving, void *)
{
	if (saving) {
		obs_data_array_t *hotkey_save_array = obs_hotkey_save(copyTransformHotkey);
		obs_data_set_array(save_data, "copyTransformHotkey", hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
		hotkey_save_array = obs_hotkey_save(pasteTransformHotkey);
		obs_data_set_array(save_data, "pasteTransformHotkey", hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
	} else {
		obs_data_array_t *hotkey_save_array = obs_data_get_array(save_data, "copyTransformHotkey");
		obs_hotkey_load(copyTransformHotkey, hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
		hotkey_save_array = obs_data_get_array(save_data, "pasteTransformHotkey");
		obs_hotkey_load(pasteTransformHotkey, hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
	}
}

bool obs_module_load()
{
	blog(LOG_INFO, "[Source Copy] loaded version %s", PROJECT_VERSION);

	copyTransformHotkey =
		obs_hotkey_register_frontend("actionCopyTransform", obs_module_text("CopyTransform"), CopyTransform, nullptr);
	pasteTransformHotkey =
		obs_hotkey_register_frontend("actionPasteTransform", obs_module_text("PasteTransform"), PasteTransform, nullptr);
	obs_frontend_add_save_callback(frontend_save_load, nullptr);

	QAction *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("SourceCopy")));
	QMenu *menu = new QMenu();
	action->setMenu(menu);
	QObject::connect(menu, &QMenu::aboutToShow, [menu] { LoadMenu(menu); });
	return true;
}

void obs_module_unload()
{
	obs_frontend_remove_save_callback(frontend_save_load, nullptr);
	obs_hotkey_unregister(copyTransformHotkey);
	obs_hotkey_unregister(pasteTransformHotkey);
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("SourceCopy");
}

static void AddFilterMenu(obs_source_t *parent, obs_source_t *child, void *data)
{
	UNUSED_PARAMETER(parent);
	QMenu *menu = static_cast<QMenu *>(data);
	QMenu *submenu = menu->addMenu(QT_UTF8(obs_source_get_name(child)));
	QAction *a = submenu->addAction(QT_UTF8(obs_module_text("SaveFilter")));
	QObject::connect(a, &QAction::triggered, [child] {
		QString fileName = QFileDialog::getSaveFileName(nullptr, QT_UTF8(obs_module_text("SaveFilter")), QString(),
								"JSON File (*.json)");
		if (fileName.isEmpty())
			return;
		obs_data_t *data = obs_save_source(child);
		obs_data_save_json(data, QT_TO_UTF8(fileName));
		obs_data_release(data);
	});
	a = submenu->addAction(QT_UTF8(obs_module_text("CopyFilter")));
	QObject::connect(a, &QAction::triggered, [child] {
		obs_data_t *data = obs_save_source(child);
		QClipboard *clipboard = QGuiApplication::clipboard();
		clipboard->setText(QT_UTF8(obs_data_get_json(data)));
		obs_data_release(data);
	});
}

static bool AddSceneItemToMenu(obs_scene_t *scene, obs_sceneitem_t *item, void *data)
{
	UNUSED_PARAMETER(scene);
	QMenu *menu = static_cast<QMenu *>(data);
	obs_source_t *source = obs_sceneitem_get_source(item);
	QMenu *submenu = menu->addMenu(obs_source_get_name(source));
	QObject::connect(submenu, &QMenu::aboutToShow, [submenu, source, item] { LoadSourceMenu(submenu, source, item); });
	return true;
}

static bool SaveSource(obs_scene_t *scene, obs_sceneitem_t *item, void *data)
{
	UNUSED_PARAMETER(scene);
	obs_data_array_t *sources = static_cast<obs_data_array_t *>(data);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return true;
	const char *name = obs_source_get_name(source);
	const size_t count = obs_data_array_count(sources);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *sourceData = obs_data_array_item(sources, i);
		obs_data_release(sourceData);
		if (strcmp(name, obs_data_get_string(sourceData, "name")) == 0)
			return true;
	}
	obs_scene_t *nested_scene = obs_scene_from_source(source);
	if (!nested_scene)
		nested_scene = obs_group_from_source(source);
	if (nested_scene)
		obs_scene_enum_items(nested_scene, SaveSource, sources);
	obs_data_t *sceneData = obs_save_source(source);
	obs_data_array_push_back(sources, sceneData);
	obs_data_release(sceneData);
	return true;
}

static void LoadSingleSource(obs_scene_t *scene, obs_data_t *data)
{
	const char *name = obs_data_get_string(data, "name");
	obs_source_t *source = obs_get_source_by_name(name);
	if (!source)
		source = obs_load_source(data);
	if (source) {
		if (obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT || obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE) {
			obs_scene_add(scene, source);
			obs_source_load(source);
		}
		obs_source_release(source);
	}
}

static void LoadSource(obs_scene_t *scene, obs_data_t *data)
{
	if (!data)
		return;
	obs_data_array_t *sourcesData = obs_data_get_array(data, "sources");
	if (sourcesData) {
		LoadSources(sourcesData, scene);
		obs_data_array_release(sourcesData);
	} else {
		obs_data_t *sourceData = obs_data_get_obj(data, "source");
		if (sourceData) {
			LoadSingleSource(scene, sourceData);
			obs_data_release(sourceData);
		} else {
			LoadSingleSource(scene, data);
		}
	}
}

static obs_data_t *GetTransformData(obs_sceneitem_t *item)
{
	obs_data_t *temp = obs_data_create();
	obs_transform_info info{};
	obs_sceneitem_get_info2(item, &info);
	obs_data_set_bool(temp, "crop_to_bounds", info.crop_to_bounds);
	obs_data_set_vec2(temp, "pos", &info.pos);
	obs_data_set_vec2(temp, "scale", &info.scale);
	obs_data_set_double(temp, "rot", info.rot);
	obs_data_set_int(temp, "alignment", info.alignment);
	obs_data_set_int(temp, "bounds_type", info.bounds_type);
	obs_data_set_vec2(temp, "bounds", &info.bounds);
	obs_data_set_int(temp, "bounds_alignment", info.bounds_alignment);
	obs_sceneitem_crop crop{};
	obs_sceneitem_get_crop(item, &crop);
	obs_data_set_int(temp, "top", crop.top);
	obs_data_set_int(temp, "bottom", crop.bottom);
	obs_data_set_int(temp, "left", crop.left);
	obs_data_set_int(temp, "right", crop.right);
	return temp;
}

void LoadTransform(obs_sceneitem_t *item, obs_data_t *data)
{
	obs_transform_info info{};
	obs_sceneitem_get_info2(item, &info);
	info.crop_to_bounds = obs_data_get_bool(data, "crop_to_bounds");
	obs_data_get_vec2(data, "pos", &info.pos);
	obs_data_get_vec2(data, "scale", &info.scale);
	info.rot = obs_data_get_double(data, "rot");
	info.alignment = obs_data_get_int(data, "alignment");
	info.bounds_type = (enum obs_bounds_type)obs_data_get_int(data, "bounds_type");
	obs_data_get_vec2(data, "bounds", &info.bounds);
	info.bounds_alignment = obs_data_get_int(data, "bounds_alignment");
	obs_sceneitem_set_info2(item, &info);
	obs_sceneitem_crop crop{};
	crop.top = obs_data_get_int(data, "top");
	crop.bottom = obs_data_get_int(data, "bottom");
	crop.left = obs_data_get_int(data, "left");
	crop.right = obs_data_get_int(data, "right");
	obs_sceneitem_set_crop(item, &crop);
}

static void LoadSourceMenu(QMenu *menu, obs_source_t *source, obs_sceneitem_t *item)
{
	menu->clear();

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene)
		scene = obs_group_from_source(source);

	QAction *a;
	if (scene) {
		a = menu->addAction(
			QT_UTF8(obs_scene_is_group(scene) ? obs_module_text("SaveGroup") : obs_module_text("SaveScene")));
		QObject::connect(a, &QAction::triggered, [scene, source] {
			QString fileName = QFileDialog::getSaveFileName(
				nullptr,
				QT_UTF8(obs_scene_is_group(scene) ? obs_module_text("SaveGroup") : obs_module_text("SaveScene")),
				QString(), "JSON File (*.json)");
			if (fileName.isEmpty())
				return;
			obs_data_t *data = obs_data_create();
			obs_data_array_t *sources = obs_data_array_create();
			obs_data_set_array(data, "sources", sources);
			obs_scene_enum_items(scene, SaveSource, sources);
			obs_data_t *sceneData = obs_save_source(source);
			obs_data_array_push_back(sources, sceneData);
			obs_data_release(sceneData);
			obs_data_save_json(data, QT_TO_UTF8(fileName));
			obs_data_array_release(sources);
			obs_data_release(data);
		});
		a = menu->addAction(
			QT_UTF8(obs_scene_is_group(scene) ? obs_module_text("CopyGroup") : obs_module_text("CopyScene")));
		QObject::connect(a, &QAction::triggered, [scene, source] {
			obs_data_t *data = obs_data_create();
			obs_data_array_t *sources = obs_data_array_create();
			obs_data_set_array(data, "sources", sources);
			obs_scene_enum_items(scene, SaveSource, sources);
			obs_data_t *sceneData = obs_save_source(source);
			obs_data_array_push_back(sources, sceneData);
			obs_data_release(sceneData);
			QClipboard *clipboard = QGuiApplication::clipboard();
			clipboard->setText(QT_UTF8(obs_data_get_json(data)));
			obs_data_array_release(sources);
			obs_data_release(data);
		});
		a = menu->addAction(QT_UTF8(obs_module_text("LoadSource")));
		QObject::connect(a, &QAction::triggered, [scene] {
			QString fileName = QFileDialog::getOpenFileName(nullptr, QT_UTF8(obs_module_text("LoadSource")), QString(),
									"JSON File (*.json)");
			if (fileName.isEmpty())
				return;
			obs_data_t *data = obs_data_create_from_json_file(QT_TO_UTF8(fileName));
			try_fix_paths(data, fileName);
			LoadSource(scene, data);
			obs_data_release(data);
		});
		a = menu->addAction(QT_UTF8(obs_module_text("PasteSource")));
		QObject::connect(a, &QAction::triggered, [scene] {
			QClipboard *clipboard = QGuiApplication::clipboard();
			const QString strData = clipboard->text();
			if (strData.isEmpty())
				return;
			obs_data_t *data = obs_data_create_from_json(QT_TO_UTF8(strData));
			LoadSource(scene, data);
			obs_data_release(data);
		});
	} else {
		a = menu->addAction(QT_UTF8(obs_module_text("SaveSource")));
		QObject::connect(a, &QAction::triggered, [source] {
			QString fileName = QFileDialog::getSaveFileName(nullptr, QT_UTF8(obs_module_text("SaveSource")), QString(),
									"JSON File (*.json)");
			if (fileName.isEmpty())
				return;
			obs_data_t *data = obs_save_source(source);
			obs_data_save_json(data, QT_TO_UTF8(fileName));
			obs_data_release(data);
		});
		a = menu->addAction(QT_UTF8(obs_module_text("CopySource")));
		QObject::connect(a, &QAction::triggered, [source] {
			obs_data_t *data = obs_save_source(source);
			QClipboard *clipboard = QGuiApplication::clipboard();
			clipboard->setText(QT_UTF8(obs_data_get_json(data)));
			obs_data_release(data);
		});
	}
	if (item) {
		menu->addSeparator();
		a = menu->addAction(obs_module_text("LoadTransform"));
		QObject::connect(a, &QAction::triggered, [item] {
			QString fileName = QFileDialog::getOpenFileName(nullptr, QT_UTF8(obs_module_text("LoadTransform")),
									QString(), "JSON File (*.json)");
			if (fileName.isEmpty())
				return;
			obs_data_t *data = obs_data_create_from_json_file(QT_TO_UTF8(fileName));
			LoadTransform(item, data);
			obs_data_release(data);
		});
		a = menu->addAction(QT_UTF8(obs_module_text("PasteTransform")));
		QObject::connect(a, &QAction::triggered, [item] {
			QClipboard *clipboard = QGuiApplication::clipboard();
			const QString strData = clipboard->text();
			if (strData.isEmpty())
				return;
			obs_data_t *data = obs_data_create_from_json(QT_TO_UTF8(strData));
			LoadTransform(item, data);
			obs_data_release(data);
		});
		a = menu->addAction(QT_UTF8(obs_module_text("SaveTransform")));
		QObject::connect(a, &QAction::triggered, [item] {
			QString fileName = QFileDialog::getSaveFileName(nullptr, QT_UTF8(obs_module_text("SaveSource")), QString(),
									"JSON File (*.json)");
			if (fileName.isEmpty())
				return;
			obs_data_t *temp = GetTransformData(item);
			obs_data_save_json(temp, QT_TO_UTF8(fileName));
			obs_data_release(temp);
		});
		a = menu->addAction(QT_UTF8(obs_module_text("CopyTransform")));
		QObject::connect(a, &QAction::triggered, [item] {
			obs_data_t *temp = GetTransformData(item);
			QClipboard *clipboard = QGuiApplication::clipboard();
			clipboard->setText(QT_UTF8(obs_data_get_json(temp)));
			obs_data_release(temp);
		});
		menu->addSeparator();

		a = menu->addAction(obs_module_text("LoadShowTransition"));
		QObject::connect(a, &QAction::triggered, [item] {
			QString fileName = QFileDialog::getOpenFileName(nullptr, QT_UTF8(obs_module_text("LoadShowTransition")),
									QString(), "JSON File (*.json)");
			if (fileName.isEmpty())
				return;
			obs_data_t *data = obs_data_create_from_json_file(QT_TO_UTF8(fileName));
			if (const auto t = obs_load_private_source(data)) {
				obs_sceneitem_set_transition(item, true, t);
				obs_source_release(t);
			}
			obs_data_release(data);
		});
		a = menu->addAction(QT_UTF8(obs_module_text("PasteShowTransition")));
		QObject::connect(a, &QAction::triggered, [item] {
			QClipboard *clipboard = QGuiApplication::clipboard();
			const QString strData = clipboard->text();
			if (strData.isEmpty())
				return;
			obs_data_t *data = obs_data_create_from_json(QT_TO_UTF8(strData));
			if (const auto t = obs_load_private_source(data)) {
				obs_sceneitem_set_transition(item, true, t);
				obs_source_release(t);
			}
			obs_data_release(data);
		});

		a = menu->addAction(obs_module_text("LoadHideTransition"));
		QObject::connect(a, &QAction::triggered, [item] {
			QString fileName = QFileDialog::getOpenFileName(nullptr, QT_UTF8(obs_module_text("LoadHideTransition")),
									QString(), "JSON File (*.json)");
			if (fileName.isEmpty())
				return;
			obs_data_t *data = obs_data_create_from_json_file(QT_TO_UTF8(fileName));
			if (const auto t = obs_load_private_source(data)) {
				obs_sceneitem_set_transition(item, false, t);
				obs_source_release(t);
			}
			obs_data_release(data);
		});
		a = menu->addAction(QT_UTF8(obs_module_text("PasteHideTransition")));
		QObject::connect(a, &QAction::triggered, [item] {
			QClipboard *clipboard = QGuiApplication::clipboard();
			const QString strData = clipboard->text();
			if (strData.isEmpty())
				return;
			obs_data_t *data = obs_data_create_from_json(QT_TO_UTF8(strData));
			if (const auto t = obs_load_private_source(data)) {
				obs_sceneitem_set_transition(item, false, t);
				obs_source_release(t);
			}
			obs_data_release(data);
		});

		auto st = obs_sceneitem_get_transition(item, true);
		if (st) {
			a = menu->addAction(QT_UTF8(obs_module_text("SaveShowTransition")));
			QObject::connect(a, &QAction::triggered, [st] {
				QString fileName = QFileDialog::getSaveFileName(
					nullptr, QT_UTF8(obs_module_text("SaveShowTransition")), QString(), "JSON File (*.json)");
				if (fileName.isEmpty())
					return;
				obs_data_t *temp = obs_save_source(st);
				obs_data_save_json(temp, QT_TO_UTF8(fileName));
				obs_data_release(temp);
			});
			a = menu->addAction(QT_UTF8(obs_module_text("CopyShowTransition")));
			QObject::connect(a, &QAction::triggered, [st] {
				obs_data_t *temp = obs_save_source(st);
				QClipboard *clipboard = QGuiApplication::clipboard();
				clipboard->setText(QT_UTF8(obs_data_get_json(temp)));
				obs_data_release(temp);
			});
		}
		auto ht = obs_sceneitem_get_transition(item, false);
		if (ht) {
			a = menu->addAction(QT_UTF8(obs_module_text("SaveHideTransition")));
			QObject::connect(a, &QAction::triggered, [ht] {
				QString fileName = QFileDialog::getSaveFileName(
					nullptr, QT_UTF8(obs_module_text("SaveHideTransition")), QString(), "JSON File (*.json)");
				if (fileName.isEmpty())
					return;
				obs_data_t *temp = obs_save_source(ht);
				obs_data_save_json(temp, QT_TO_UTF8(fileName));
				obs_data_release(temp);
			});
			a = menu->addAction(QT_UTF8(obs_module_text("CopyHideTransition")));
			QObject::connect(a, &QAction::triggered, [ht] {
				obs_data_t *temp = obs_save_source(ht);
				QClipboard *clipboard = QGuiApplication::clipboard();
				clipboard->setText(QT_UTF8(obs_data_get_json(temp)));
				obs_data_release(temp);
			});
		}
	}
	menu->addSeparator();
	a = menu->addAction(QT_UTF8(obs_module_text("LoadFilter")));
	QObject::connect(a, &QAction::triggered, [source] {
		QString fileName = QFileDialog::getOpenFileName(nullptr, QT_UTF8(obs_module_text("LoadFilter")), QString(),
								"JSON File (*.json)");
		if (fileName.isEmpty())
			return;
		obs_data_t *data = obs_data_create_from_json_file(QT_TO_UTF8(fileName));
		if (!data)
			return;
		const char *name = obs_data_get_string(data, "name");
		obs_source_t *filter = obs_source_get_filter_by_name(source, name);
		if (!filter) {
			try_fix_paths(data, fileName);
			filter = obs_load_source(data);
			if (filter && obs_source_get_type(filter) == OBS_SOURCE_TYPE_FILTER) {
				obs_source_filter_add(source, filter);
				obs_source_load(filter);
			}
		}
		obs_source_release(filter);
		obs_data_release(data);
	});
	a = menu->addAction(QT_UTF8(obs_module_text("PasteFilter")));
	QObject::connect(a, &QAction::triggered, [source] {
		QClipboard *clipboard = QGuiApplication::clipboard();
		const QString strData = clipboard->text();
		if (strData.isEmpty())
			return;
		obs_data_t *data = obs_data_create_from_json(QT_TO_UTF8(strData));
		if (!data)
			return;
		const char *name = obs_data_get_string(data, "name");
		obs_source_t *filter = obs_source_get_filter_by_name(source, name);
		if (!filter) {
			filter = obs_load_source(data);
			if (filter && obs_source_get_type(filter) == OBS_SOURCE_TYPE_FILTER) {
				obs_source_filter_add(source, filter);
				obs_source_load(filter);
			}
		}
		obs_source_release(filter);
		obs_data_release(data);
	});

	if (scene) {
		auto label = new QLabel("<b>" + QT_UTF8(obs_module_text("Sources")) + "</b>");
		label->setAlignment(Qt::AlignCenter);

		auto wa = new QWidgetAction(menu);
		wa->setDefaultWidget(label);
		menu->addAction(wa);
		obs_scene_enum_items(scene, AddSceneItemToMenu, menu);
		if (menu->actions().last() == wa) {
			menu->removeAction(wa);
			delete wa;
		}
	}
	auto label = new QLabel("<b>" + QT_UTF8(obs_module_text("Filters")) + "</b>");
	label->setAlignment(Qt::AlignCenter);

	auto wa = new QWidgetAction(menu);
	wa->setDefaultWidget(label);
	menu->addAction(wa);
	obs_source_enum_filters(source, AddFilterMenu, menu);
	if (menu->actions().last() == wa) {
		menu->removeAction(wa);
		delete wa;
	}
}

static void *vendor;

void websocket_add_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	obs_queue_task(OBS_TASK_UI, (obs_task_t)LoadScene, request_data, true);
	obs_data_set_bool(response_data, "success", true);
}

void websocket_get_version(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	UNUSED_PARAMETER(request_data);
	obs_data_set_string(response_data, "version", PROJECT_VERSION);
	obs_data_set_bool(response_data, "success", true);
}

void websocket_get_current_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	UNUSED_PARAMETER(request_data);
	obs_source_t *source = nullptr;
	const char *canvas_name = obs_data_get_string(request_data, "canvas");
	if (strlen(canvas_name)) {
		obs_canvas_t *canvas = obs_get_canvas_by_name(canvas_name);
		if (!canvas) {
			obs_data_set_string(response_data, "error", "canvas not found");
			obs_data_set_bool(response_data, "success", false);
			return;
		}
		source = obs_canvas_get_channel(canvas, 0);
		if (source && obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *ts = obs_transition_get_active_source(source);
			if (ts) {
				obs_source_release(source);
				source = ts;
			}
		}
		obs_canvas_release(canvas);
	} else {
		source = obs_frontend_get_current_scene();
	}
	if (!source) {
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_scene_t *scene = obs_scene_from_source(source);
	obs_data_array_t *sources = obs_data_array_create();
	obs_data_set_array(response_data, "sources", sources);
	obs_scene_enum_items(scene, SaveSource, sources);
	obs_data_t *sceneData = obs_save_source(source);
	obs_data_array_push_back(sources, sceneData);
	obs_data_release(sceneData);
	obs_data_array_release(sources);
	obs_source_release(source);
	obs_data_set_bool(response_data, "success", true);
}

void websocket_get_scene(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *name = obs_data_get_string(request_data, "scene");
	if (!name || !strlen(name)) {
		obs_data_set_string(response_data, "error", "scene not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}

	obs_source_t *source = nullptr;
	const char *canvas_name = obs_data_get_string(request_data, "canvas");
	if (strlen(canvas_name)) {
		obs_canvas_t *canvas = obs_get_canvas_by_name(canvas_name);
		if (!canvas) {
			obs_data_set_string(response_data, "error", "canvas not found");
			obs_data_set_bool(response_data, "success", false);
			return;
		}
		source = obs_canvas_get_source_by_name(canvas, name);
		obs_canvas_release(canvas);
	} else {
		source = obs_get_source_by_name(name);
	}

	if (!source) {
		obs_data_set_string(response_data, "error", "scene not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		obs_source_release(source);
		obs_data_set_string(response_data, "error", "not a scene");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_array_t *sources = obs_data_array_create();
	obs_data_set_array(response_data, "sources", sources);
	obs_scene_enum_items(scene, SaveSource, sources);
	obs_data_t *sceneData = obs_save_source(source);
	obs_data_array_push_back(sources, sceneData);
	obs_data_release(sceneData);
	obs_data_array_release(sources);
	obs_source_release(source);
	obs_data_set_bool(response_data, "success", true);
}

void websocket_get_source(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	const char *name = obs_data_get_string(request_data, "source");
	if (!name || !strlen(name)) {
		obs_data_set_string(response_data, "error", "source not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}

	obs_source_t *source = nullptr;
	const char *canvas_name = obs_data_get_string(request_data, "canvas");
	if (strlen(canvas_name)) {
		obs_canvas_t *canvas = obs_get_canvas_by_name(canvas_name);
		if (!canvas) {
			obs_data_set_string(response_data, "error", "canvas not found");
			obs_data_set_bool(response_data, "success", false);
			return;
		}
		source = obs_canvas_get_source_by_name(canvas, name);
		obs_canvas_release(canvas);
	} else {
		source = obs_get_source_by_name(name);
	}
	if (!source) {
		obs_data_set_string(response_data, "error", "source not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_data_t *data = obs_save_source(source);
	obs_data_set_obj(response_data, "source", data);
	obs_data_release(data);
	obs_source_release(source);
	obs_data_set_bool(response_data, "success", true);
}

void websocket_add_source(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	UNUSED_PARAMETER(param);
	obs_source_t *source = nullptr;
	const char *name = obs_data_get_string(request_data, "scene");
	if (!name || !strlen(name)) {
		const char *canvas_name = obs_data_get_string(request_data, "canvas");
		if (strlen(canvas_name)) {
			obs_canvas_t *canvas = obs_get_canvas_by_name(canvas_name);
			if (!canvas) {
				obs_data_set_string(response_data, "error", "canvas not found");
				obs_data_set_bool(response_data, "success", false);
				return;
			}
			source = obs_canvas_get_source_by_name(canvas, name);
			obs_canvas_release(canvas);
		} else {
			source = obs_get_source_by_name(name);
		}
	} else {
		source = obs_frontend_get_current_scene();
	}
	if (!source) {
		obs_data_set_string(response_data, "error", "scene not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		obs_source_release(source);
		obs_data_set_string(response_data, "error", "not a scene");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	LoadSource(scene, request_data);
	obs_source_release(source);
	obs_data_set_bool(response_data, "success", true);
}

void obs_module_post_load(void)
{
	vendor = obs_websocket_register_vendor("source-copy");
	if (!vendor)
		return;
	obs_websocket_vendor_register_request(vendor, "get_version", websocket_get_version, nullptr);
	obs_websocket_vendor_register_request(vendor, "get_current_scene", websocket_get_current_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "get_scene", websocket_get_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "add_scene", websocket_add_scene, nullptr);

	obs_websocket_vendor_register_request(vendor, "get_source", websocket_get_source, nullptr);
	obs_websocket_vendor_register_request(vendor, "add_source", websocket_add_source, nullptr);
}
