#include "source-copy.hpp"
#include <obs-module.h>
#include <QClipboard>
#include <QGuiApplication>
#include <QMenu>

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("source-copy", "en-US")

static void LoadSourceMenu(QMenu *menu, obs_source_t *source);

static void LoadMenu(QMenu *menu)
{
	menu->clear();
	//menu->addAction(obs_module_text("Load Scene"));
	QAction *a = menu->addAction(QT_UTF8(obs_module_text("PasteScene")));
	QObject::connect(a, &QAction::triggered, [] {
		QClipboard *clipboard = QGuiApplication::clipboard();
		const QString strData = clipboard->text();
		if (strData.isEmpty())
			return;
		obs_data_t *data =
			obs_data_create_from_json(QT_TO_UTF8(strData));
		if (!data)
			return;
		obs_data_array_t *sourcesData =
			obs_data_get_array(data, "sources");
		if (sourcesData) {
			const size_t count = obs_data_array_count(sourcesData);
			std::list<obs_source_t *> sources;
			for (size_t i = 0; i < count; i++) {
				obs_data_t *sourceData =
					obs_data_array_item(sourcesData, i);
				obs_source_t *s = obs_load_source(sourceData);
				if (s) {
					sources.push_back(s);
				}
				obs_data_release(sourceData);
			}
			for (auto it = sources.begin(); it != sources.end();
			     ++it) {
				obs_source_load(*it);
			}
			for (auto it = sources.begin(); it != sources.end();
			     ++it) {
				obs_source_release(*it);
			}
			obs_data_array_release(sourcesData);
		}
		obs_data_release(data);
	});
	menu->addSection(QT_UTF8(obs_module_text("Scenes")));
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

bool obs_module_load()
{
	QAction *action =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(
			obs_module_text("SourceCopy")));
	QMenu *menu = new QMenu();
	action->setMenu(menu);
	QObject::connect(menu, &QMenu::aboutToShow, [menu] { LoadMenu(menu); });
	return true;
}

void obs_module_unload() {}

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
	QAction *a = menu->addAction(obs_source_get_name(child));
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

static void LoadSourceMenu(QMenu *menu, obs_source_t *source)
{
	menu->clear();
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene)
		scene = obs_group_from_source(source);

	QAction *a;
	if (scene) {
		//a = menu->addAction(obs_scene_is_group(scene)
		//			    ? obs_module_text("Save Group")
		//			    : obs_module_text("Save Scene"));
		a = menu->addAction(QT_UTF8(obs_scene_is_group(scene)
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
		menu->addSeparator();
		//menu->addAction(obs_module_text("Load Source"));
		a = menu->addAction(QT_UTF8(obs_module_text("PasteSource")));
		QObject::connect(a, &QAction::triggered, [scene] {
			QClipboard *clipboard = QGuiApplication::clipboard();
			const QString strData = clipboard->text();
			if (strData.isEmpty())
				return;
			obs_data_t *data =
				obs_data_create_from_json(QT_TO_UTF8(strData));
			if (!data)
				return;
			obs_data_array_t *sourcesData =
				obs_data_get_array(data, "sources");
			if (sourcesData) {
				const size_t count =
					obs_data_array_count(sourcesData);
				std::list<obs_source_t *> sources;
				for (size_t i = 0; i < count; i++) {
					obs_data_t *sourceData =
						obs_data_array_item(sourcesData,
								    i);
					obs_source_t *s =
						obs_load_source(sourceData);
					if (s) {
						sources.push_back(s);
						if (obs_source_get_type(s) ==
						    OBS_SOURCE_TYPE_SCENE) {
							obs_scene_add(scene, s);
						}
					}
					obs_data_release(sourceData);
				}
				for (auto it = sources.begin();
				     it != sources.end(); ++it) {
					obs_source_load(*it);
				}
				for (auto it = sources.begin();
				     it != sources.end(); ++it) {
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
		});
	} else {
		//a = menu->addAction(obs_module_text("Save Source"));
		a = menu->addAction(QT_UTF8(obs_module_text("CopySource")));
		QObject::connect(a, &QAction::triggered, [source] {
			obs_data_t *data = obs_save_source(source);
			QClipboard *clipboard = QGuiApplication::clipboard();
			clipboard->setText(QT_UTF8(obs_data_get_json(data)));
			obs_data_release(data);
		});
	}
	menu->addSeparator();
	//menu->addAction(obs_module_text("Load Filter"));
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
			obs_source_release(source);
		}
		obs_data_release(data);
	});

	if (scene) {
		menu->addSection(obs_module_text("Sources"));
		obs_scene_enum_items(scene, AddSceneItemToMenu, menu);
	}
	menu->addSection(obs_module_text("Filters"));
	obs_source_enum_filters(source, AddFilterMenu, menu);
}
