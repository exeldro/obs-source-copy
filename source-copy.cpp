#include "source-copy.hpp"
#include <obs-module.h>
#include <QClipboard>
#include <QFileDialog>
#include <QGuiApplication>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QWidgetAction>

#include "version.h"

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("source-copy", "en-US")

static void LoadSourceMenu(QMenu *menu, obs_source_t *source);

static void LoadScene(obs_data_t *data)
{
	if (!data)
		return;
	obs_data_array_t *sourcesData = obs_data_get_array(data, "sources");
	if (!sourcesData)
		return;
	const size_t count = obs_data_array_count(sourcesData);
	std::list<obs_source_t *> sources;
	for (size_t i = 0; i < count; i++) {
		obs_data_t *sourceData = obs_data_array_item(sourcesData, i);
		obs_source_t *s = obs_load_source(sourceData);
		if (s) {
			sources.push_back(s);
		}
		obs_data_release(sourceData);
	}
	for (auto it = sources.begin(); it != sources.end(); ++it) {
		obs_source_load(*it);
	}
	for (auto it = sources.begin(); it != sources.end(); ++it) {
		obs_source_release(*it);
	}
	obs_data_array_release(sourcesData);
}

static void LoadMenu(QMenu *menu)
{
	menu->clear();
	QAction *a = menu->addAction(obs_module_text("LoadScene"));
	QObject::connect(a, &QAction::triggered, [] {
		QString fileName = QFileDialog::getOpenFileName(
			nullptr, QT_UTF8(obs_module_text("LoadScene")),
			QString(), "JSON File (*.json)");
		if (fileName.isEmpty())
			return;
		obs_data_t *data =
			obs_data_create_from_json_file(QT_TO_UTF8(fileName));
		LoadScene(data);
		obs_data_release(data);
	});
	a = menu->addAction(QT_UTF8(obs_module_text("PasteScene")));
	QObject::connect(a, &QAction::triggered, [] {
		QClipboard *clipboard = QGuiApplication::clipboard();
		const QString strData = clipboard->text();
		if (strData.isEmpty())
			return;
		obs_data_t *data =
			obs_data_create_from_json(QT_TO_UTF8(strData));
		LoadScene(data);
		obs_data_release(data);
	});
	auto label =
		new QLabel("<b>" + QT_UTF8(obs_module_text("Scenes")) + "</b>");
	label->setAlignment(Qt::AlignCenter);

	auto wa = new QWidgetAction(menu);
	wa->setDefaultWidget(label);
	menu->addAction(wa);

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *source = scenes.sources.array[i];
		QMenu *submenu = menu->addMenu(
			obs_source_get_name(scenes.sources.array[i]));
		QObject::connect(submenu, &QMenu::aboutToShow,
				 [submenu, source] {
					 LoadSourceMenu(submenu, source);
				 });
	}

	obs_frontend_source_list_free(&scenes);
}

void CopyTransform(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey,
		   bool pressed)
{
	if (!pressed)
		return;
	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());

	QAction *t = main_window->findChild<QAction *>("actionCopyTransform");
	if (t)
		t->trigger();
}

void PasteTransform(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey,
		    bool pressed)
{
	if (!pressed)
		return;
	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	QAction *t = main_window->findChild<QAction *>("actionPasteTransform");
	if (t)
		t->trigger();
}

obs_hotkey_id copyTransformHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id pasteTransformHotkey = OBS_INVALID_HOTKEY_ID;

static void frontend_save_load(obs_data_t *save_data, bool saving, void *)
{
	if (saving) {
		obs_data_array_t *hotkey_save_array =
			obs_hotkey_save(copyTransformHotkey);
		obs_data_set_array(save_data, "copyTransformHotkey",
				   hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
		hotkey_save_array = obs_hotkey_save(pasteTransformHotkey);
		obs_data_set_array(save_data, "pasteTransformHotkey",
				   hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
	} else {
		obs_data_array_t *hotkey_save_array =
			obs_data_get_array(save_data, "copyTransformHotkey");
		obs_hotkey_load(copyTransformHotkey, hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
		hotkey_save_array =
			obs_data_get_array(save_data, "pasteTransformHotkey");
		obs_hotkey_load(pasteTransformHotkey, hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
	}
}

bool obs_module_load()
{
	blog(LOG_INFO, "[Source Copy] loaded version %s", PROJECT_VERSION);

	copyTransformHotkey = obs_hotkey_register_frontend("actionCopyTransform",
		obs_module_text("CopyTransform"),
		CopyTransform, nullptr);
	pasteTransformHotkey = obs_hotkey_register_frontend(
		"actionPasteTransform",
				     obs_module_text("PasteTransform"),
				     PasteTransform, nullptr);
	obs_frontend_add_save_callback(frontend_save_load, nullptr);
	
	QAction *action =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(
			obs_module_text("SourceCopy")));
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
	QMenu *menu = static_cast<QMenu *>(data);
	QMenu *submenu = menu->addMenu(QT_UTF8(obs_source_get_name(child)));
	QAction *a = submenu->addAction(QT_UTF8(obs_module_text("SaveFilter")));
	QObject::connect(a, &QAction::triggered, [child] {
		QString fileName = QFileDialog::getSaveFileName(
			nullptr, QT_UTF8(obs_module_text("SaveFilter")),
			QString(), "JSON File (*.json)");
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

static bool AddSceneItemToMenu(obs_scene_t *scene, obs_sceneitem_t *item,
			       void *data)
{
	QMenu *menu = static_cast<QMenu *>(data);
	obs_source_t *source = obs_sceneitem_get_source(item);
	QMenu *submenu = menu->addMenu(obs_source_get_name(source));
	QObject::connect(submenu, &QMenu::aboutToShow, [submenu, source] {
		LoadSourceMenu(submenu, source);
	});
	return true;
}

static bool SaveSource(obs_scene_t *scene, obs_sceneitem_t *item, void *data)
{
	obs_data_array_t *sources = static_cast<obs_data_array_t *>(data);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return true;
	obs_data_t *sceneData = obs_save_source(source);
	obs_data_array_push_back(sources, sceneData);
	obs_data_release(sceneData);
	return true;
}

static void LoadSource(obs_scene_t *scene, obs_data_t *data)
{
	if (!data)
		return;
	obs_data_array_t *sourcesData = obs_data_get_array(data, "sources");
	if (sourcesData) {
		const size_t count = obs_data_array_count(sourcesData);
		std::list<obs_source_t *> sources;
		for (size_t i = 0; i < count; i++) {
			obs_data_t *sourceData =
				obs_data_array_item(sourcesData, i);
			obs_source_t *s = obs_load_source(sourceData);
			if (s) {
				sources.push_back(s);
				if (obs_source_get_type(s) ==
				    OBS_SOURCE_TYPE_SCENE) {
					obs_scene_add(scene, s);
				}
			}
			obs_data_release(sourceData);
		}
		for (auto it = sources.begin(); it != sources.end(); ++it) {
			obs_source_load(*it);
		}
		for (auto it = sources.begin(); it != sources.end(); ++it) {
			obs_source_release(*it);
		}
		obs_data_array_release(sourcesData);
	} else {
		obs_source_t *source = obs_load_source(data);
		if (source) {
			if (obs_source_get_type(source) ==
			    OBS_SOURCE_TYPE_INPUT) {
				obs_scene_add(scene, source);
				obs_source_load(source);
			}
			obs_source_release(source);
		}
	}
	obs_data_release(data);
}

static void LoadSourceMenu(QMenu *menu, obs_source_t *source)
{
	menu->clear();
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene)
		scene = obs_group_from_source(source);

	QAction *a;
	if (scene) {
		a = menu->addAction(
			QT_UTF8(obs_scene_is_group(scene)
					? obs_module_text("SaveGroup")
					: obs_module_text("SaveScene")));
		QObject::connect(a, &QAction::triggered, [scene, source] {
			QString fileName = QFileDialog::getSaveFileName(
				nullptr,
				QT_UTF8(obs_scene_is_group(scene)
						? obs_module_text("SaveGroup")
						: obs_module_text("SaveScene")),
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
			obs_data_release(data);
		});
		a = menu->addAction(
			QT_UTF8(obs_scene_is_group(scene)
					? obs_module_text("CopyGroup")
					: obs_module_text("CopyScene")));
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
			obs_data_release(data);
		});
		a = menu->addAction(obs_module_text("LoadSource"));
		QObject::connect(a, &QAction::triggered, [scene] {
			QString fileName = QFileDialog::getOpenFileName(
				nullptr, QT_UTF8(obs_module_text("LoadSource")),
				QString(), "JSON File (*.json)");
			if (fileName.isEmpty())
				return;
			obs_data_t *data = obs_data_create_from_json_file(
				QT_TO_UTF8(fileName));
			LoadSource(scene, data);
			obs_data_release(data);
		});
		a = menu->addAction(QT_UTF8(obs_module_text("PasteSource")));
		QObject::connect(a, &QAction::triggered, [scene] {
			QClipboard *clipboard = QGuiApplication::clipboard();
			const QString strData = clipboard->text();
			if (strData.isEmpty())
				return;
			obs_data_t *data =
				obs_data_create_from_json(QT_TO_UTF8(strData));
			LoadSource(scene, data);
			obs_data_release(data);
		});
	} else {
		a = menu->addAction(QT_UTF8(obs_module_text("SaveSource")));
		QObject::connect(a, &QAction::triggered, [source] {
			QString fileName = QFileDialog::getSaveFileName(
				nullptr, QT_UTF8(obs_module_text("SaveSource")),
				QString(), "JSON File (*.json)");
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
	a = menu->addAction(QT_UTF8(obs_module_text("LoadFilter")));
	QObject::connect(a, &QAction::triggered, [source] {
		QString fileName = QFileDialog::getOpenFileName(
			nullptr, QT_UTF8(obs_module_text("LoadFilter")),
			QString(), "JSON File (*.json)");
		if (fileName.isEmpty())
			return;
		obs_data_t *data =
			obs_data_create_from_json_file(QT_TO_UTF8(fileName));
		if (!data)
			return;
		obs_source_t *filter = obs_load_source(data);
		if (filter) {
			if (obs_source_get_type(filter) ==
			    OBS_SOURCE_TYPE_FILTER) {
				obs_source_filter_add(source, filter);
				obs_source_load(filter);
			}
			obs_source_release(filter);
		}
		obs_data_release(data);
	});
	a = menu->addAction(QT_UTF8(obs_module_text("PasteFilter")));
	QObject::connect(a, &QAction::triggered, [source] {
		QClipboard *clipboard = QGuiApplication::clipboard();
		const QString strData = clipboard->text();
		if (strData.isEmpty())
			return;
		obs_data_t *data =
			obs_data_create_from_json(QT_TO_UTF8(strData));
		if (!data)
			return;
		obs_source_t *filter = obs_load_source(data);
		if (filter) {
			if (obs_source_get_type(filter) ==
			    OBS_SOURCE_TYPE_FILTER) {
				obs_source_filter_add(source, filter);
				obs_source_load(filter);
			}
			obs_source_release(filter);
		}
		obs_data_release(data);
	});

	if (scene) {
		auto label = new QLabel(
			"<b>" + QT_UTF8(obs_module_text("Sources")) + "</b>");
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
	auto label = new QLabel("<b>" + QT_UTF8(obs_module_text("Filters")) +
				"</b>");
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
