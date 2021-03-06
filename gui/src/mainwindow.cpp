#include "mainwindow.h"
#include <QCloseEvent>
#include <QCompleter>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkProxy>
#include <QScrollBar>
#include <QShortcut>
#include <QSound>
#include <QTimer>
#include <algorithm>
#if (QT_VERSION >= QT_VERSION_CHECK(5, 4, 0))
	#include <QStorageInfo>
#endif
#if defined(Q_OS_WIN)
	#include <cfloat>
	#include "windows.h"
#endif
#include <qmath.h>
#include <ui_mainwindow.h>
#include "aboutwindow.h"
#include "batch/addgroupwindow.h"
#include "batch/adduniquewindow.h"
#include "batch/batchwindow.h"
#include "commands/commands.h"
#include "danbooru-downloader-importer.h"
#include "downloader/download-query-group.h"
#include "downloader/download-query-image.h"
#include "downloader/download-query-loader.h"
#include "downloader/downloader.h"
#include "downloader/image-downloader.h"
#include "functions.h"
#include "helpers.h"
#include "models/api/api.h"
#include "models/favorite.h"
#include "models/filename.h"
#include "models/page.h"
#include "models/post-filter.h"
#include "models/profile.h"
#include "monitoring-center.h"
#include "settings/optionswindow.h"
#include "settings/startwindow.h"
#include "tabs/favorites-tab.h"
#include "tabs/pool-tab.h"
#include "tabs/search-tab.h"
#include "tabs/tabs-loader.h"
#include "tabs/tag-tab.h"
#include "tag-context-menu.h"
#include "tags/tag-stylist.h"
#include "theme-loader.h"
#include "ui/QAffiche.h"
#include "updater/update-dialog.h"
#include "utils/blacklist-fix/blacklist-fix-1.h"
#include "utils/empty-dirs-fix/empty-dirs-fix-1.h"
#include "utils/md5-fix/md5-fix.h"
#include "utils/rename-existing/rename-existing-1.h"
#include "utils/tag-loader/tag-loader.h"


mainWindow::mainWindow(Profile *profile)
	: ui(new Ui::mainWindow), m_profile(profile), m_favorites(m_profile->getFavorites()), m_downloads(0), m_loaded(false), m_getAll(false), m_forcedTab(-1), m_batchAutomaticRetries(0), m_showLog(true)
{ }
void mainWindow::init(const QStringList &args, const QMap<QString, QString> &params)
{
	m_settings = m_profile->getSettings();
	auto sites = m_profile->getSites();

	ThemeLoader themeLoader(savePath("themes/", true));
	themeLoader.setTheme(m_settings->value("theme", "Default").toString());
	ui->setupUi(this);

	m_showLog = m_settings->value("Log/show", true).toBool();
	if (!m_showLog)
	{ ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->tabLog)); }
	else
	{
		QFile logFile(Logger::getInstance().logFile());
		if (logFile.open(QFile::ReadOnly | QFile::Text))
		{
			while (!logFile.atEnd())
			{
				QString line = logFile.readLine();
				logShow(line);
			}
			logFile.close();
		}

		connect(&Logger::getInstance(), &Logger::newLog, this, &mainWindow::logShow);
	}

	log(QStringLiteral("New session started."), Logger::Info);
	log(QStringLiteral("Software version: %1.").arg(VERSION), Logger::Info);
	log(QStringLiteral("Path: %1").arg(qApp->applicationDirPath()), Logger::Info);
	log(QStringLiteral("Loading preferences from <a href=\"file:///%1\">%1</a>").arg(m_settings->fileName()), Logger::Info);

	if (!QSslSocket::supportsSsl())
	{ log(QStringLiteral("Missing SSL libraries"), Logger::Error); }
	else
	{ log(QStringLiteral("SSL libraries: %1").arg(QSslSocket::sslLibraryVersionString()), Logger::Info); }

	bool crashed = m_settings->value("crashed", false).toBool();
	m_settings->setValue("crashed", true);
	m_settings->sync();

	// On first launch after setup, we restore the setup's language
	QString setupSettingsFile = savePath("innosetup.ini");
	if (QFile::exists(setupSettingsFile))
	{
		QSettings setupSettings(setupSettingsFile, QSettings::IniFormat);
		QString setupLanguage = setupSettings.value("language", "en").toString();

		QSettings associations(savePath("languages/languages.ini"), QSettings::IniFormat);
		associations.beginGroup("innosetup");
		QStringList keys = associations.childKeys();

		// Only if the setup language is available in Grabber
		if (keys.contains(setupLanguage))
		{
			m_settings->setValue("language", associations.value(setupLanguage).toString());
		}

		// Remove the setup settings file to not do this every time
		QFile::remove(setupSettingsFile);
	}

	// Load translations
	qApp->installTranslator(&m_translator);
	qApp->installTranslator(&m_qtTranslator);
	loadLanguage(m_settings->value("language", "English").toString());

	tabifyDockWidget(ui->dock_internet, ui->dock_wiki);
	tabifyDockWidget(ui->dock_wiki, ui->dock_kfl);
	tabifyDockWidget(ui->dock_kfl, ui->dock_favorites);
	ui->dock_internet->raise();

	ui->menuView->addAction(ui->dock_internet->toggleViewAction());
	ui->menuView->addAction(ui->dock_wiki->toggleViewAction());
	ui->menuView->addAction(ui->dock_kfl->toggleViewAction());
	ui->menuView->addAction(ui->dock_favorites->toggleViewAction());
	ui->menuView->addAction(ui->dockOptions->toggleViewAction());

	m_favorites = m_profile->getFavorites();

	if (m_settings->value("Proxy/use", false).toBool())
	{
		bool useSystem = m_settings->value("Proxy/useSystem", false).toBool();
		QNetworkProxyFactory::setUseSystemConfiguration(useSystem);

		if (!useSystem)
		{
			QNetworkProxy::ProxyType type = m_settings->value("Proxy/type", "http").toString() == "http" ? QNetworkProxy::HttpProxy : QNetworkProxy::Socks5Proxy;
			QNetworkProxy proxy(type, m_settings->value("Proxy/hostName").toString(), m_settings->value("Proxy/port").toInt());
			QNetworkProxy::setApplicationProxy(proxy);
			log(QStringLiteral("Enabling application proxy on host \"%1\" and port %2.").arg(m_settings->value("Proxy/hostName").toString()).arg(m_settings->value("Proxy/port").toInt()), Logger::Info);
		}
		else
		{ log(QStringLiteral("Enabling system-wide proxy."), Logger::Info); }
	}

	m_progressDialog = nullptr;

	ui->tableBatchGroups->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	ui->tableBatchUniques->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

	log(QStringLiteral("Loading sources"), Logger::Debug);
	if (sites.empty())
	{
		QMessageBox::critical(this, tr("No source found"), tr("No source found. Do you have a configuration problem? Try to reinstall the program."));
		qApp->quit();
		this->deleteLater();
		return;
	}
	else
	{
		QString srsc;
		QStringList keys = sites.keys();
		for (const QString &key : keys)
		{ srsc += (!srsc.isEmpty() ? ", " : "") + key + " (" + sites.value(key)->type() + ")"; }
		log(QStringLiteral("%1 source%2 found: %3").arg(sites.size()).arg(sites.size() > 1 ? "s" : "", srsc), Logger::Info);
	}

	// System tray icon
	if (m_settings->value("Monitoring/enableTray", false).toBool())
	{
		auto quitAction = new QAction(tr("&Quit"), this);
		connect(quitAction, &QAction::triggered, this, &mainWindow::trayClose);

		auto trayIconMenu = new QMenu(this);
		trayIconMenu->addAction(quitAction);

		m_trayIcon = new QSystemTrayIcon(this);
		m_trayIcon->setContextMenu(trayIconMenu);
		m_trayIcon->setIcon(windowIcon());
		m_trayIcon->show();

		connect(m_trayIcon, &QSystemTrayIcon::activated, this, &mainWindow::trayIconActivated);
		connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, &mainWindow::trayMessageClicked);
	}
	else
	{ m_trayIcon = nullptr; }

	ui->actionClosetab->setShortcut(QKeySequence::Close);
	QShortcut *actionCloseTabW = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), this);
	connect(actionCloseTabW, &QShortcut::activated, ui->actionClosetab, &QAction::trigger);

	QShortcut *actionFocusSearch = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_L), this);
	connect(actionFocusSearch, &QShortcut::activated, this, &mainWindow::focusSearch);

	QShortcut *actionDeleteBatchGroups = new QShortcut(QKeySequence::Delete, ui->tableBatchGroups);
	actionDeleteBatchGroups->setContext(Qt::WidgetWithChildrenShortcut);
	connect(actionDeleteBatchGroups, &QShortcut::activated, this, &mainWindow::batchClearSelGroups);

	QShortcut *actionDeleteBatchUniques = new QShortcut(QKeySequence::Delete, ui->tableBatchUniques);
	actionDeleteBatchUniques->setContext(Qt::WidgetWithChildrenShortcut);
	connect(actionDeleteBatchUniques, &QShortcut::activated, this, &mainWindow::batchClearSelUniques);

	QShortcut *actionNextTab = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_PageDown), this);
	connect(actionNextTab, &QShortcut::activated, this, &mainWindow::tabNext);
	QShortcut *actionPrevTab = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_PageUp), this);
	connect(actionPrevTab, &QShortcut::activated, this, &mainWindow::tabPrev);

	ui->actionAddtab->setShortcut(QKeySequence::AddTab);
	ui->actionQuit->setShortcut(QKeySequence::Quit);
	ui->actionFolder->setShortcut(QKeySequence::Open);

	connect(ui->actionQuit, &QAction::triggered, qApp, &QApplication::quit);
	connect(ui->actionAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);

	// Action on first load
	if (m_settings->value("firstload", true).toBool())
	{
		this->onFirstLoad();
		m_settings->setValue("firstload", false);
	}

	// Crash restoration
	m_restore = m_settings->value("start", "none").toString() == "restore";
	if (crashed)
	{
		log(QStringLiteral("It seems that Imgbrd-Grabber hasn't shut down properly last time."), Logger::Warning);

		QString msg = tr("It seems that the application was not properly closed for its last use. Do you want to restore your last session?");
		QMessageBox dlg(QMessageBox::Question, QStringLiteral("Grabber"), msg, QMessageBox::Yes | QMessageBox::No);
		dlg.setWindowIcon(windowIcon());
		dlg.setDefaultButton(QMessageBox::Yes);

		int response = dlg.exec();
		m_restore = response == QMessageBox::Yes;
	}

	// Restore download lists
	if (m_restore)
	{ loadLinkList(m_profile->getPath() + "/restore.igl"); }

	// Loading last window state, size and position from the settings file
	restoreGeometry(m_settings->value("geometry").toByteArray());
	restoreState(m_settings->value("state").toByteArray());

	// Tab add button
	QPushButton *add = new QPushButton(QIcon(":/images/add.png"), "", this);
		add->setFlat(true);
		add->resize(QSize(12, 12));
		connect(add, SIGNAL(clicked()), this, SLOT(addTab()));
		ui->tabWidget->setCornerWidget(add);

	// Favorites tab
	m_favoritesTab = new favoritesTab(m_profile, this);
	connect(m_favoritesTab, &searchTab::batchAddGroup, this, &mainWindow::batchAddGroup);
	connect(m_favoritesTab, SIGNAL(batchAddUnique(DownloadQueryImage)), this, SLOT(batchAddUnique(DownloadQueryImage)));
	connect(m_favoritesTab, &searchTab::titleChanged, this, &mainWindow::updateTabTitle);
	connect(m_favoritesTab, &searchTab::changed, this, &mainWindow::updateTabs);
	ui->tabWidget->insertTab(m_tabs.size(), m_favoritesTab, tr("Favorites"));
	ui->tabWidget->setCurrentIndex(0);

	// Load given files
	parseArgs(args, params);

	// Get list of selected sources
	QStringList sav = m_settings->value("sites", "").toStringList();
	for (const QString &key : sav)
	{
		if (!sites.contains(key))
			continue;

		Site *site = sites.value(key);
		connect(site, &Site::loggedIn, this, &mainWindow::initialLoginsFinished);
		m_selectedSites.append(site);
	}

	// Initial login on selected sources
	m_waitForLogin = 0;
	if (m_selectedSites.isEmpty())
	{
		initialLoginsDone();
	}
	else
	{
		m_waitForLogin += m_selectedSites.count();
		for (Site *site : qAsConst(m_selectedSites))
			site->login();
	}

	ui->tableBatchGroups->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	ui->tableBatchUniques->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	on_buttonInitSettings_clicked();

	QStringList sizes = m_settings->value("batch", "100,100,100,100,100,100,100,100,100").toString().split(',');
	int m = sizes.size() > ui->tableBatchGroups->columnCount() ? ui->tableBatchGroups->columnCount() : sizes.size();
	for (int i = 0; i < m; i++)
	{ ui->tableBatchGroups->horizontalHeader()->resizeSection(i, sizes.at(i).toInt()); }

	m_lineFolder_completer = QStringList(m_settings->value("Save/path").toString());
	ui->lineFolder->setCompleter(new QCompleter(m_lineFolder_completer, ui->lineFolder));
	//m_lineFilename_completer = QStringList(m_settings->value("Save/filename").toString());
	//ui->lineFilename->setCompleter(new QCompleter(m_lineFilename_completer));
	ui->comboFilename->setAutoCompletionCaseSensitivity(Qt::CaseSensitive);

	connect(m_profile, &Profile::favoritesChanged, this, &mainWindow::updateFavorites);
	connect(m_profile, &Profile::keptForLaterChanged, this, &mainWindow::updateKeepForLater);
	connect(m_profile, &Profile::siteDeleted, this, &mainWindow::siteDeleted);
	updateFavorites();
	updateKeepForLater();

	m_currentTab = nullptr;
	log(QStringLiteral("End of initialization"), Logger::Debug);
}

void mainWindow::parseArgs(const QStringList &args, const QMap<QString, QString> &params)
{
	// When we use Grabber to open a file
	QStringList tags;
	if (args.count() == 1 && QFile::exists(args[0]))
	{
		// Load an IGL file
		QFileInfo info(args[0]);
		if (info.suffix() == QLatin1String("igl"))
		{
			loadLinkList(info.absoluteFilePath());
			m_forcedTab = m_tabs.size() + 1;
			return;
		}

		// Search any image by its MD5
		loadMd5(info.absoluteFilePath(), true, false, false);
		return;
	}

	// Other positional arguments are treated as tags
	tags.append(args);
	tags.append(params.value("tags").split(' ', QString::SkipEmptyParts));
	if (!tags.isEmpty() || m_settings->value("start", "none").toString() == "firstpage")
	{
		loadTag(tags.join(' '), true, false, false);
	}
}

void mainWindow::initialLoginsFinished()
{
	Site *site = qobject_cast<Site*>(sender());
	disconnect(site, &Site::loggedIn, this, &mainWindow::initialLoginsFinished);

	m_waitForLogin--;
	if (m_waitForLogin != 0)
		return;

	initialLoginsDone();
}

void mainWindow::initialLoginsDone()
{
	if (m_restore)
	{ loadTabs(m_profile->getPath() + "/tabs.txt"); }
	if (m_tabs.isEmpty())
	{ addTab(); }

	m_currentTab = ui->tabWidget->currentWidget();
	m_loaded = true;

	ui->tabWidget->setCurrentIndex(qMax(0, m_forcedTab));
	m_forcedTab = -1;

	m_monitoringCenter = new MonitoringCenter(m_profile, m_trayIcon, this);
	m_monitoringCenter->start();
}

mainWindow::~mainWindow()
{
	delete m_profile;
	delete ui;
}

void mainWindow::focusSearch()
{
	if (ui->tabWidget->widget(ui->tabWidget->currentIndex())->maximumWidth() != 16777214)
	{
		auto *tab = dynamic_cast<searchTab *>(ui->tabWidget->widget(ui->tabWidget->currentIndex()));
		tab->focusSearch();
	}
}

void mainWindow::onFirstLoad()
{
	// Save all default settings
	auto *ow = new optionsWindow(m_profile, this);
	ow->save();
	ow->deleteLater();

	// Detect and Danbooru Downloader settings
	DanbooruDownloaderImporter ddImporter;
	if (ddImporter.isInstalled())
	{
		int reponse = QMessageBox::question(this, "", tr("The Mozilla Firefox addon \"Danbooru Downloader\" has been detected on your system. Do you want to load its preferences?"), QMessageBox::Yes | QMessageBox::No);
		if (reponse == QMessageBox::Yes)
		{
			ddImporter.import(m_settings);
			return;
		}
	}

	// Open startup window
	auto *swin = new startWindow(m_profile, this);
	connect(swin, SIGNAL(languageChanged(QString)), this, SLOT(loadLanguage(QString)));
	connect(swin, &startWindow::settingsChanged, this, &mainWindow::on_buttonInitSettings_clicked);
	connect(swin, &startWindow::sourceChanged, this, &mainWindow::setSource);
	swin->show();
}

void mainWindow::addTab(const QString &tag, bool background, bool save)
{
	auto *w = new tagTab(m_profile, this);
	this->addSearchTab(w, background, save);

	if (!tag.isEmpty())
	{ w->setTags(tag); }
	else
	{ w->focusSearch(); }
}
void mainWindow::addPoolTab(int pool, const QString &site, bool background, bool save)
{
	auto *w = new poolTab(m_profile, this);
	this->addSearchTab(w, background, save);

	if (!site.isEmpty())
	{ w->setSite(site); }
	if (pool != 0)
	{ w->setPool(pool, site); }
	else
	{ w->focusSearch(); }
}
void mainWindow::addSearchTab(searchTab *w, bool background, bool save)
{
	if (m_tabs.size() > ui->tabWidget->currentIndex())
	{
		w->setSources(m_tabs[ui->tabWidget->currentIndex()]->sources());
		w->setImagesPerPage(m_tabs[ui->tabWidget->currentIndex()]->imagesPerPage());
		w->setColumns(m_tabs[ui->tabWidget->currentIndex()]->columns());
		w->setPostFilter(m_tabs[ui->tabWidget->currentIndex()]->postFilter());
	}
	connect(w, &searchTab::batchAddGroup, this, &mainWindow::batchAddGroup);
	connect(w, SIGNAL(batchAddUnique(const DownloadQueryImage &)), this, SLOT(batchAddUnique(const DownloadQueryImage &)));
	connect(w, &searchTab::titleChanged, this, &mainWindow::updateTabTitle);
	connect(w, &searchTab::changed, this, &mainWindow::updateTabs);
	connect(w, &searchTab::closed, this, &mainWindow::tabClosed);

	QString title = w->windowTitle();
	if (title.isEmpty())
	{ title = "New tab"; }

	int pos = m_loaded ? ui->tabWidget->currentIndex() + (!m_tabs.isEmpty()) : m_tabs.count();
	int index = ui->tabWidget->insertTab(pos, w, title);
	m_tabs.append(w);

	QPushButton *closeTab = new QPushButton(QIcon(":/images/close.png"), "", this);
		closeTab->setFlat(true);
		closeTab->resize(QSize(8, 8));
		connect(closeTab, &QPushButton::clicked, w, &searchTab::deleteLater);
		ui->tabWidget->findChild<QTabBar*>()->setTabButton(index, QTabBar::RightSide, closeTab);

	if (!background)
		ui->tabWidget->setCurrentIndex(index);

	if (save)
		saveTabs(m_profile->getPath() + "/tabs.txt");
}

bool mainWindow::saveTabs(const QString &filename)
{
	return TabsLoader::save(filename, m_tabs, reinterpret_cast<searchTab*>(m_currentTab));
}
bool mainWindow::loadTabs(const QString &filename)
{
	QList<searchTab*> tabs;
	int currentTab;

	if (!TabsLoader::load(filename, tabs, currentTab, m_profile, this))
		return false;

	bool preload = m_settings->value("preloadAllTabs", false).toBool();
	for (auto tab : qAsConst(tabs))
	{
		addSearchTab(tab, true, false);
		if (!preload)
			m_tabsWaitingForPreload.append(tab);
	}

	m_forcedTab = currentTab;
	return true;
}
void mainWindow::updateTabTitle(searchTab *tab)
{
	ui->tabWidget->setTabText(ui->tabWidget->indexOf(tab), tab->windowTitle());
}
void mainWindow::updateTabs()
{
	if (m_loaded)
	{
		saveTabs(m_profile->getPath() + "/tabs.txt");
	}
}
void mainWindow::tabClosed(searchTab *tab)
{
	// Store closed tab information
	QJsonObject obj;
	tab->write(obj);
	m_closedTabs.append(obj);
	if (m_closedTabs.count() > CLOSED_TAB_HISTORY_MAX) {
		m_closedTabs.removeFirst();
	}
	ui->actionRestoreLastClosedTab->setEnabled(true);

	m_tabs.removeAll(tab);
}
void mainWindow::restoreLastClosedTab()
{
	if (m_closedTabs.isEmpty())
		return;

	QJsonObject infos = m_closedTabs.takeLast();
	searchTab *tab = TabsLoader::loadTab(infos, m_profile, this, true);
	addSearchTab(tab);

	ui->actionRestoreLastClosedTab->setEnabled(!m_closedTabs.isEmpty());
}
void mainWindow::currentTabChanged(int tab)
{
	if (m_loaded && tab < m_tabs.size())
	{
		if (ui->tabWidget->widget(tab)->maximumWidth() != 16777214)
		{
			searchTab *tb = m_tabs[tab];
			if (m_tabsWaitingForPreload.contains(tb))
			{
				tb->load();
				m_tabsWaitingForPreload.removeAll(tb);
			}
			else if (m_currentTab != ui->tabWidget->currentWidget())
			{
				setTags(tb->results());
				setWiki(tb->wiki());
			}
			m_currentTab = ui->tabWidget->currentWidget();
		}
	}
}

void mainWindow::setTags(const QList<Tag> &tags, searchTab *from)
{
	if (from != nullptr && m_tabs.indexOf(from) != ui->tabWidget->currentIndex())
		return;

	clearLayout(ui->dockInternetScrollLayout);
	m_currentTags = tags;

	QAffiche *taglabel = new QAffiche(QVariant(), 0, QColor(), this);
	taglabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
	connect(taglabel, static_cast<void (QAffiche::*)(const QString &)>(&QAffiche::middleClicked), this, &mainWindow::loadTagTab);
	connect(taglabel, &QAffiche::linkHovered, this, &mainWindow::linkHovered);
	connect(taglabel, &QAffiche::linkActivated, this, &mainWindow::loadTagNoTab);
	taglabel->setText(TagStylist(m_profile).stylished(tags, true, true).join("<br/>"));

	// Context menu
	taglabel->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(taglabel, &QWidget::customContextMenuRequested, this, &mainWindow::contextMenu);

	ui->dockInternetScrollLayout->addWidget(taglabel);
}

void mainWindow::closeCurrentTab()
{
	// Unclosable tabs have a maximum width of 16777214 (default: 16777215)
	auto currentTab = ui->tabWidget->currentWidget();
	if (currentTab->maximumWidth() != 16777214)
	{ currentTab->deleteLater(); }
}

void mainWindow::tabNext()
{
	int index = ui->tabWidget->currentIndex();
	int count = ui->tabWidget->count();
	ui->tabWidget->setCurrentIndex((index + 1) % count);
}
void mainWindow::tabPrev()
{
	int index = ui->tabWidget->currentIndex();
	int count = ui->tabWidget->count();
	ui->tabWidget->setCurrentIndex((index - 1 + count) % count);
}

void mainWindow::addTableItem(QTableWidget *table, int row, int col, const QString &text)
{
	auto *item = new QTableWidgetItem(text);
	item->setToolTip(text);

	table->setItem(row, col, item);
}

void mainWindow::batchAddGroup(const DownloadQueryGroup &values)
{
	// Ignore downloads already present in the list
	if (m_groupBatchs.contains(values))
		return;

	m_groupBatchs.append(values);
	int pos = m_groupBatchs.count();

	ui->tableBatchGroups->setRowCount(ui->tableBatchGroups->rowCount() + 1);
	int row = ui->tableBatchGroups->rowCount() - 1;
	m_allow = false;

	auto *item = new QTableWidgetItem(getIcon(":/images/status/pending.png"), QString::number(pos));
	item->setFlags(item->flags() ^ Qt::ItemIsEditable);
	ui->tableBatchGroups->setItem(row, 0, item);

	addTableItem(ui->tableBatchGroups, row, 1, values.tags);
	addTableItem(ui->tableBatchGroups, row, 2, values.site->url());
	addTableItem(ui->tableBatchGroups, row, 3, QString::number(values.page));
	addTableItem(ui->tableBatchGroups, row, 4, QString::number(values.perpage));
	addTableItem(ui->tableBatchGroups, row, 5, QString::number(values.total));
	addTableItem(ui->tableBatchGroups, row, 6, values.filename);
	addTableItem(ui->tableBatchGroups, row, 7, values.path);
	addTableItem(ui->tableBatchGroups, row, 8, values.postFiltering.join(' '));
	addTableItem(ui->tableBatchGroups, row, 9, values.getBlacklisted ? "true" : "false");

	auto *prog = new QProgressBar(this);
	prog->setTextVisible(false);
	prog->setMaximum(values.total);
	m_progressBars.append(prog);
	ui->tableBatchGroups->setCellWidget(row, 10, prog);

	m_allow = true;
	saveLinkList(m_profile->getPath() + "/restore.igl");
	updateGroupCount();
}
void mainWindow::updateGroupCount()
{
	int groups = 0;
	for (int i = 0; i < ui->tableBatchGroups->rowCount(); i++)
		groups += ui->tableBatchGroups->item(i, 5)->text().toInt();
	ui->labelGroups->setText(tr("Groups (%1/%2)").arg(ui->tableBatchGroups->rowCount()).arg(groups));
}
void mainWindow::batchAddUnique(const DownloadQueryImage &query, bool save)
{
	// Ignore downloads already present in the list
	if (m_batchs.contains(query))
		return;

	log(QStringLiteral("Adding single image: %1").arg(query.values["file_url"]), Logger::Info);

	m_batchs.append(query);
	ui->tableBatchUniques->setRowCount(ui->tableBatchUniques->rowCount() + 1);

	int row = ui->tableBatchUniques->rowCount() - 1;
	addTableItem(ui->tableBatchUniques, row, 0, query.values["id"]);
	addTableItem(ui->tableBatchUniques, row, 1, query.values["md5"]);
	addTableItem(ui->tableBatchUniques, row, 2, query.values["rating"]);
	addTableItem(ui->tableBatchUniques, row, 3, query.values["tags"]);
	addTableItem(ui->tableBatchUniques, row, 4, query.values["file_url"]);
	addTableItem(ui->tableBatchUniques, row, 5, query.values["date"]);
	addTableItem(ui->tableBatchUniques, row, 6, query.site->name());
	addTableItem(ui->tableBatchUniques, row, 7, query.filename);
	addTableItem(ui->tableBatchUniques, row, 8, query.path);

	if (save)
	{ saveLinkList(m_profile->getPath() + "/restore.igl"); }
}
void mainWindow::saveFolder()
{
	QString path = m_settings->value("Save/path").toString().replace("\\", "/");
	if (path.right(1) == "/")
	{ path = path.left(path.length()-1); }
	QDir dir(path);
	if (dir.exists())
	{ showInGraphicalShell(path); }
}
void mainWindow::openSettingsFolder()
{
	QDir dir(savePath(""));
	if (dir.exists())
	{ showInGraphicalShell(dir.absolutePath()); }
}

void mainWindow::batchClear()
{
	// Don't do anything if there's nothing to clear
	if (ui->tableBatchGroups->rowCount() == 0 && ui->tableBatchUniques->rowCount() == 0)
		return;

	// Confirm deletion
	auto reponse = QMessageBox::question(this, tr("Confirmation"), tr("Are you sure you want to clear your download list?"), QMessageBox::Yes | QMessageBox::No);
	if (reponse != QMessageBox::Yes)
		return;

	m_batchs.clear();
	ui->tableBatchUniques->clearContents();
	ui->tableBatchUniques->setRowCount(0);
	m_groupBatchs.clear();
	ui->tableBatchGroups->clearContents();
	ui->tableBatchGroups->setRowCount(0);
	qDeleteAll(m_progressBars);
	m_progressBars.clear();
	updateGroupCount();
}
void mainWindow::batchClearSel()
{
	batchClearSelGroups();
	batchClearSelUniques();
}
void mainWindow::batchClearSelGroups()
{
	QList<int> rows;
	for (QTableWidgetItem *selected : ui->tableBatchGroups->selectedItems())
	{
		int row = selected->row();
		if (!rows.contains(row))
			rows.append(row);
	}

	batchRemoveGroups(rows);
}
void mainWindow::batchClearSelUniques()
{
	QList<int> rows;
	for (QTableWidgetItem *selected : ui->tableBatchUniques->selectedItems())
	{
		int row = selected->row();
		if (!rows.contains(row))
			rows.append(row);
	}

	batchRemoveUniques(rows);
}
void mainWindow::batchRemoveGroups(QList<int> rows)
{
	qSort(rows);

	int rem = 0;
	for (int i : qAsConst(rows))
	{
		int pos = i - rem;
		m_progressBars[pos]->deleteLater();
		m_progressBars.removeAt(pos);
		m_groupBatchs.removeAt(pos);
		ui->tableBatchGroups->removeRow(pos);
		rem++;
	}

	updateGroupCount();
}
void mainWindow::batchRemoveUniques(QList<int> rows)
{
	qSort(rows);

	int rem = 0;
	for (int i : qAsConst(rows))
	{
		int pos = i - rem;
		ui->tableBatchUniques->removeRow(pos);
		m_batchs.removeAt(pos);
		rem++;
	}

	updateGroupCount();
}

void mainWindow::batchMove(int diff)
{
	QList<QTableWidgetItem *> selected = ui->tableBatchGroups->selectedItems();
	if (selected.isEmpty())
		return;

	QSet<int> rows;
	for (QTableWidgetItem *item : selected)
		rows.insert(item->row());

	for (int sourceRow : rows)
	{
		int destRow = sourceRow + diff;
		if (destRow < 0 || destRow >= ui->tableBatchGroups->rowCount())
			return;

		for (int col = 0; col < ui->tableBatchGroups->columnCount(); ++col)
		{
			QTableWidgetItem *sourceItem = ui->tableBatchGroups->takeItem(sourceRow, col);
			QTableWidgetItem *destItem = ui->tableBatchGroups->takeItem(destRow, col);

			ui->tableBatchGroups->setItem(sourceRow, col, destItem);
			ui->tableBatchGroups->setItem(destRow, col, sourceItem);
		}
	}

	QItemSelection selection;
	for (int i = 0; i < selected.count(); i++)
	{
		QModelIndex index = ui->tableBatchGroups->model()->index(selected.at(i)->row(), selected.at(i)->column());
		selection.select(index, index);
	}

	auto* selectionModel = new QItemSelectionModel(ui->tableBatchGroups->model(), this);
	selectionModel->select(selection, QItemSelectionModel::ClearAndSelect);
	ui->tableBatchGroups->setSelectionModel(selectionModel);
}
void mainWindow::batchMoveUp()
{
	batchMove(-1);
}
void mainWindow::batchMoveDown()
{
	batchMove(1);
}

void mainWindow::updateBatchGroups(int y, int x)
{
	if (m_allow && x > 0)
	{
		QString val = ui->tableBatchGroups->item(y, x)->text();
		int toInt = val.toInt();

		switch (x)
		{
			case 1:	m_groupBatchs[y].tags = val;						break;
			case 3:	m_groupBatchs[y].page = toInt;						break;
			case 6:	m_groupBatchs[y].filename = val;					break;
			case 7:	m_groupBatchs[y].path = val;						break;
			case 8:	m_groupBatchs[y].postFiltering = val.split(' ', QString::SkipEmptyParts);	break;
			case 9:	m_groupBatchs[y].getBlacklisted = (val != "false");	break;

			case 2:
				if (!m_profile->getSites().contains(val))
				{
					error(this, tr("This source is not valid."));
					ui->tableBatchGroups->item(y, x)->setText(m_groupBatchs[y].site->url());
				}
				else
				{ m_groupBatchs[y].site = m_profile->getSites().value(val); }
				break;

			case 4:
				if (toInt < 1)
				{
					error(this, tr("The image per page value must be greater or equal to 1."));
					ui->tableBatchGroups->item(y, x)->setText(QString::number(m_groupBatchs[y].perpage));
				}
				else
				{ m_groupBatchs[y].perpage = toInt; }
				break;

			case 5:
				if (toInt < 0)
				{
					error(this, tr("The image limit must be greater or equal to 0."));
					ui->tableBatchGroups->item(y, x)->setText(QString::number(m_groupBatchs[y].total));
				}
				else
				{
					m_groupBatchs[y].total = toInt;
					m_progressBars[y]->setMaximum(toInt);
				}
				break;
		}

		saveLinkList(m_profile->getPath() + "/restore.igl");
	}
}

Site* mainWindow::getSelectedSiteOrDefault()
{
	if (m_selectedSites.isEmpty())
		return m_profile->getSites().first();

	return m_selectedSites.first();
}

void mainWindow::addGroup()
{
	AddGroupWindow *wAddGroup = new AddGroupWindow(getSelectedSiteOrDefault(), m_profile, this);
	connect(wAddGroup, &AddGroupWindow::sendData, this, &mainWindow::batchAddGroup);
	wAddGroup->show();
}
void mainWindow::addUnique()
{
	AddUniqueWindow *wAddUnique = new AddUniqueWindow(getSelectedSiteOrDefault(), m_profile, this);
	connect(wAddUnique, SIGNAL(sendData(DownloadQueryImage)), this, SLOT(batchAddUnique(DownloadQueryImage)));
	wAddUnique->show();
}

void mainWindow::updateFavorites()
{
	clearLayout(ui->layoutFavoritesDock);

	QStringList assoc = QStringList() << "name" << "note" << "lastviewed";
	QString order = assoc[qMax(ui->comboOrderFav->currentIndex(), 0)];
	bool reverse = (ui->comboAscFav->currentIndex() == 1);

	if (order == "note")
	{ std::sort(m_favorites.begin(), m_favorites.end(), Favorite::sortByNote); }
	else if (order == "lastviewed")
	{ std::sort(m_favorites.begin(), m_favorites.end(), Favorite::sortByLastviewed); }
	else
	{ std::sort(m_favorites.begin(), m_favorites.end(), Favorite::sortByName); }
	if (reverse)
	{ m_favorites = reversed(m_favorites); }
	QString format = tr("MM/dd/yyyy");

	for (const Favorite &fav : qAsConst(m_favorites))
	{
		QLabel *lab = new QLabel(QString(R"(<a href="%1" style="color:black;text-decoration:none;">%2</a>)").arg(fav.getName(), fav.getName()), this);
		connect(lab, SIGNAL(linkActivated(QString)), this, SLOT(loadTag(QString)));
		lab->setToolTip("<img src=\""+fav.getImagePath()+"\" /><br/>"+tr("<b>Name:</b> %1<br/><b>Note:</b> %2 %%<br/><b>Last view:</b> %3").arg(fav.getName(), QString::number(fav.getNote()), fav.getLastViewed().toString(format)));
		ui->layoutFavoritesDock->addWidget(lab);
	}
}
void mainWindow::updateKeepForLater()
{
	QStringList kfl = m_profile->getKeptForLater();

	clearLayout(ui->dockKflScrollLayout);

	for (const QString &tag : kfl)
	{
		auto *taglabel = new QAffiche(QString(tag), 0, QColor(), this);
		taglabel->setText(QString(R"(<a href="%1" style="color:black;text-decoration:none;">%1</a>)").arg(tag));
		taglabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
		connect(taglabel, static_cast<void (QAffiche::*)(const QString &)>(&QAffiche::middleClicked), this, &mainWindow::loadTagTab);
		connect(taglabel, &QAffiche::linkActivated, this, &mainWindow::loadTagNoTab);
		ui->dockKflScrollLayout->addWidget(taglabel);
	}
}


void mainWindow::logShow(const QString &msg)
{
	if (!m_showLog)
		return;

	// Find meta delimitations
	QString htmlMsg = msg;
	int timeEnd = msg.indexOf(']');
	int levelEnd = msg.indexOf(']', timeEnd + 1);
	QString level = msg.mid(timeEnd + 2, levelEnd - timeEnd - 2);

	// Level color
	static const QMap<QString, QString> colors
	{
		{ "Debug", "#999" },
		{ "Info", "" },
		{ "Warning", "orange" },
		{ "Error", "red" },
	};
	QString levelColor = colors[level];
	if (!levelColor.isEmpty())
	{
		htmlMsg.insert(msg.size(), "</span>");
		htmlMsg.insert(timeEnd + 1, QString("<span style='color:%1'>").arg(colors[level]));
	}

	// Time color
	htmlMsg.insert(timeEnd + 1, "</span>");
	htmlMsg.insert(0, "<span style='color:darkgreen'>");

	ui->labelLog->appendHtml(htmlMsg);
	ui->labelLog->verticalScrollBar()->setValue(ui->labelLog->verticalScrollBar()->maximum());
}
void mainWindow::logClear()
{
	QFile logFile(m_profile->getPath() + "/main.log");
	if (logFile.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		logFile.resize(0);
		logFile.close();
	}

	if (m_showLog)
	{
		ui->labelLog->clear();
	}
}
void mainWindow::logOpen()
{ QDesktopServices::openUrl("file:///" + m_profile->getPath() + "/main.log"); }

void mainWindow::loadLanguage(const QString& rLanguage, bool quiet)
{
	if (m_currLang != rLanguage)
	{
		m_currLang = rLanguage;
		QLocale locale = QLocale(m_currLang);
		QLocale::setDefault(locale);

		m_translator.load(savePath("languages/"+m_currLang+".qm", true));
		m_qtTranslator.load(savePath("languages/qt/"+m_currLang+".qm", true));

		if (!quiet)
		{
			log(QStringLiteral("Translating texts in %1...").arg(m_currLang), Logger::Info);
			ui->retranslateUi(this);
			DONE();
		}
	}
}

// Update interface language
void mainWindow::changeEvent(QEvent* event)
{
	// Translation
	if (event->type() == QEvent::LocaleChange)
	{
		QString locale = QLocale::system().name();
		locale.truncate(locale.lastIndexOf('_'));
		loadLanguage(locale);
	}

	// Minimize to tray
	else if (event->type() == QEvent::WindowStateChange && (windowState() & Qt::WindowMinimized))
	{
		bool tray = m_settings->value("Monitoring/enableTray", false).toBool();
		bool minimizeToTray = m_settings->value("Monitoring/minimizeToTray", false).toBool();
		if (tray && minimizeToTray && m_trayIcon != nullptr && m_trayIcon->isVisible())
		{
			QTimer::singleShot(250, this, SLOT(hide()));
		}
	}

	QMainWindow::changeEvent(event);
}

// Save tabs and settings on close
void mainWindow::closeEvent(QCloseEvent *e)
{
	// Close to tray
	bool tray = m_settings->value("Monitoring/enableTray", false).toBool();
	bool closeToTray = m_settings->value("Monitoring/closeToTray", false).toBool();
	if (tray && closeToTray && m_trayIcon != nullptr && m_trayIcon->isVisible() && !m_closeFromTray)
	{
		hide();
		e->ignore();
		return;
	}

	// Confirm before closing if there is a batch download or multiple tabs
	if (m_settings->value("confirm_close", true).toBool() && (m_tabs.count() > 1 || m_getAll))
	{
		QMessageBox msgBox(this);
		msgBox.setText(tr("Are you sure you want to quit?"));
		msgBox.setIcon(QMessageBox::Warning);
		QCheckBox dontShowCheckBox(tr("Don't keep for later"));
		dontShowCheckBox.setCheckable(true);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 2, 0))
		msgBox.setCheckBox(&dontShowCheckBox);
#else
		msgBox.addButton(&dontShowCheckBox, QMessageBox::ResetRole);
#endif
		msgBox.addButton(QMessageBox::Yes);
		msgBox.addButton(QMessageBox::Cancel);
		msgBox.setDefaultButton(QMessageBox::Cancel);
		int response = msgBox.exec();

		// Don't close on "cancel"
		if (response != QMessageBox::Yes)
		{
			e->ignore();
			return;
		}

		// Remember checkbox
		if (dontShowCheckBox.checkState() == Qt::Checked)
		{ m_settings->setValue("confirm_close", false); }
	}

	log(QStringLiteral("Saving..."), Logger::Debug);
		saveLinkList(m_profile->getPath() + "/restore.igl");
		saveTabs(m_profile->getPath() + "/tabs.txt");
		m_settings->setValue("state", saveState());
		m_settings->setValue("geometry", saveGeometry());
		QStringList sizes;
		sizes.reserve(ui->tableBatchGroups->columnCount());
		for (int i = 0; i < ui->tableBatchGroups->columnCount(); i++)
		{ sizes.append(QString::number(ui->tableBatchGroups->horizontalHeader()->sectionSize(i))); }
		m_settings->setValue("batch", sizes.join(","));
		m_settings->setValue("crashed", false);
		m_settings->sync();
		QFile(m_settings->fileName()).copy(m_profile->getPath() + "/old/settings."+QString(VERSION)+".ini");
		m_profile->sync();
	DONE();
	m_loaded = false;

	// Ensore the tray icon is hidden quickly on close
	if (m_trayIcon != nullptr && m_trayIcon->isVisible())
		m_trayIcon->hide();

	e->accept();
	qApp->quit();
}

void mainWindow::options()
{
	log(QStringLiteral("Opening options window..."), Logger::Debug);

	auto *options = new optionsWindow(m_profile, this);
	connect(options, SIGNAL(languageChanged(QString)), this, SLOT(loadLanguage(QString)));
	connect(options, &optionsWindow::settingsChanged, this, &mainWindow::on_buttonInitSettings_clicked);
	connect(options, &QDialog::accepted, this, &mainWindow::optionsClosed);
	options->show();

	DONE();
}

void mainWindow::optionsClosed()
{
	for (searchTab* tab : m_tabs)
	{
		tab->optionsChanged();
		tab->updateCheckboxes();
	}
}

void mainWindow::setSource(const QString &source)
{
	if (!m_profile->getSites().contains(source))
		return;

	m_selectedSites.clear();
	m_selectedSites.append(m_profile->getSites().value(source));

	if (m_tabs.isEmpty())
		return;

	m_tabs.first()->saveSources(m_selectedSites);
}

void mainWindow::aboutWebsite()
{
	QDesktopServices::openUrl(QUrl(PROJECT_WEBSITE_URL));
}
void mainWindow::aboutGithub()
{
	QDesktopServices::openUrl(QUrl(PROJECT_GITHUB_URL));
}
void mainWindow::aboutReportBug()
{
	QDesktopServices::openUrl(QUrl(QString(PROJECT_GITHUB_URL) + "/issues/new"));
}

void mainWindow::aboutAuthor()
{
	AboutWindow *aw = new AboutWindow(QString(VERSION), this);
	aw->show();
}

/* Batch download */
void mainWindow::batchSel()
{
	getAll(false);
}
void mainWindow::getAll(bool all)
{
	// Initial checks
	if (m_getAll)
	{
		log(QStringLiteral("Batch download start cancelled because another one is already running."), Logger::Warning);
		return;
	}
	if (m_settings->value("Save/path").toString().isEmpty())
	{
		error(this, tr("You did not specify a save folder!"));
		return;
	}
	else if (m_settings->value("Save/filename").toString().isEmpty())
	{
		error(this, tr("You did not specify a filename!"));
		return;
	}
	log(QStringLiteral("Batch download started."), Logger::Info);

	if (m_progressDialog == nullptr)
	{
		m_progressDialog = new batchWindow(m_profile->getSettings(), this);
		connect(m_progressDialog, &batchWindow::paused, this, &mainWindow::getAllPause);
		connect(m_progressDialog, &batchWindow::rejected, this, &mainWindow::getAllCancel);
		connect(m_progressDialog, &batchWindow::skipped, this, &mainWindow::getAllSkip);
	}

	// Reinitialize variables
	m_getAll = true;
	ui->widgetDownloadButtons->setDisabled(m_getAll);
	m_getAllDownloaded = 0;
	m_getAllExists = 0;
	m_getAllIgnored = 0;
	m_getAllIgnoredPre = 0;
	m_getAll404s = 0;
	m_getAllErrors = 0;
	m_getAllSkipped = 0;
	m_downloaders.clear();
	m_getAllRemaining.clear();
	m_getAllFailed.clear();
	m_getAllDownloading.clear();
	m_getAllSkippedImages.clear();
	m_batchPending.clear();
	m_lastDownloader = nullptr;

	if (!all)
	{
		QList<int> tdl;
		for (QTableWidgetItem *item : ui->tableBatchUniques->selectedItems())
		{
			int row = item->row();
			if (tdl.contains(row))
				continue;
			tdl.append(row);

			DownloadQueryImage batch = m_batchs[row];
			Page *page = new Page(m_profile, batch.site, m_profile->getSites().values(), batch.values.value("tags").split(" "), 1, 1, QStringList(), false, this);

			BatchDownloadImage d;
			d.image = QSharedPointer<Image>(new Image(batch.site, batch.values, m_profile, page));
			d.queryImage = &batch;

			m_getAllRemaining.append(d);
		}
	}
	else
	{
		for (const DownloadQueryImage &batch : qAsConst(m_batchs))
		{
			if (batch.values.value("file_url").isEmpty())
			{
				log(QStringLiteral("No file URL provided in image download query"), Logger::Warning);
				continue;
			}

			QMap<QString, QString> dta = batch.values;
			dta.insert("filename", batch.filename);
			dta.insert("folder", batch.path);

			Page *page = new Page(m_profile, batch.site, m_profile->getSites().values(), batch.values["tags"].split(" "), 1, 1, QStringList(), false, this);

			BatchDownloadImage d;
			d.image = QSharedPointer<Image>(new Image(batch.site, dta, m_profile, page));
			d.queryImage = &batch;

			m_getAllRemaining.append(d);
		}
	}
	m_getAllLimit = m_batchs.size();

	m_allow = false;
	for (int i = 0; i < ui->tableBatchGroups->rowCount(); i++)
	{ ui->tableBatchGroups->item(i, 0)->setIcon(getIcon(":/images/status/pending.png")); }
	m_allow = true;
	m_profile->getCommands().before();
	m_batchDownloading.clear();

	QSet<int> todownload = QSet<int>();
	for (QTableWidgetItem *item : ui->tableBatchGroups->selectedItems())
		if (!todownload.contains(item->row()))
			todownload.insert(item->row());

	if (all || !todownload.isEmpty())
	{
		for (int j = 0; j < m_groupBatchs.count(); ++j)
		{
			if (all || todownload.contains(j))
			{
				if (m_progressBars.length() > j && m_progressBars[j] != nullptr)
				{
					m_progressBars[j]->setValue(0);
					m_progressBars[j]->setMinimum(0);
					// m_progressBars[j]->setMaximum(100);
				}

				DownloadQueryGroup b = m_groupBatchs[j];
				m_batchPending.insert(j, b);
				m_getAllLimit += b.total;
				m_batchDownloading.insert(j);
			}
		}
	}

	if (m_batchPending.isEmpty() && m_getAllRemaining.isEmpty())
	{
		m_getAll = false;
		ui->widgetDownloadButtons->setEnabled(true);
		return;
	}

	m_progressDialog->show();
	getAllLogin();
}

void mainWindow::getAllLogin()
{
	m_progressDialog->clear();
	m_progressDialog->setText(tr("Logging in, please wait..."));

	m_getAllLogins.clear();
	QQueue<Site*> logins;
	for (Downloader *downloader : qAsConst(m_downloaders))
	{
		for (Site *site : downloader->getSites())
		{
			if (!m_getAllLogins.contains(site))
			{
				m_getAllLogins.append(site);
				logins.enqueue(site);
			}
		}
	}

	if (m_getAllLogins.empty())
	{
		getAllFinishedLogins();
		return;
	}

	m_progressDialog->setCurrentValue(0);
	m_progressDialog->setCurrentMax(m_getAllLogins.count());

	while (!logins.isEmpty())
	{
		Site *site = logins.dequeue();
		connect(site, &Site::loggedIn, this, &mainWindow::getAllFinishedLogin);
		site->login();
	}
}
void mainWindow::getAllFinishedLogin(Site *site, Site::LoginResult)
{
	if (m_getAllLogins.empty())
	{ return; }

	m_progressDialog->setCurrentValue(m_progressDialog->currentValue() + 1);
	m_getAllLogins.removeAll(site);

	if (m_getAllLogins.empty())
	{ getAllFinishedLogins(); }
}

void mainWindow::getAllFinishedLogins()
{
	bool usePacking = m_settings->value("packing_enable", true).toBool();
	int realConstImagesPerPack = m_settings->value("packing_size", 1000).toInt();

	int total = 0;
	for (auto j = m_batchPending.begin(); j != m_batchPending.end(); ++j)
	{
		DownloadQueryGroup b = j.value();

		int constImagesPerPack = usePacking ? realConstImagesPerPack : b.total;
		int pagesPerPack = qCeil(static_cast<qreal>(constImagesPerPack) / b.perpage);
		int imagesPerPack = pagesPerPack * b.perpage;
		int packs = qCeil(static_cast<qreal>(b.total) / imagesPerPack);
		total += b.total;

		int lastPageImages = b.total % imagesPerPack;
		if (lastPageImages == 0)
			lastPageImages = imagesPerPack;

		Downloader *previous = nullptr;
		for (int i = 0; i < packs; ++i)
		{
			Downloader *downloader = new Downloader(m_profile,
													b.tags.split(' '),
													b.postFiltering,
													QList<Site*>() << b.site,
													b.page + i * pagesPerPack,
													(i == packs - 1 ? lastPageImages : imagesPerPack),
													b.perpage,
													b.path,
													b.filename,
													nullptr,
													nullptr,
													b.getBlacklisted,
													m_profile->getBlacklist(),
													false,
													0,
													"",
													previous);
			downloader->setData(j.key());
			downloader->setQuit(false);

			connect(downloader, &Downloader::finishedImages, this, &mainWindow::getAllFinishedImages);
			connect(downloader, &Downloader::finishedImagesPage, this, &mainWindow::getAllFinishedPage);

			m_waitingDownloaders.enqueue(downloader);
			previous = downloader;
		}
	}

	m_getAllImagesCount = total;
	getNextPack();
}

void mainWindow::getNextPack()
{
	m_downloaders.clear();

	// If there are pending packs
	if (!m_waitingDownloaders.isEmpty())
	{
		m_downloaders.append(m_waitingDownloaders.dequeue());
		getAllGetPages();
	}

	// Only images to download
	else
	{
		m_batchAutomaticRetries = m_settings->value("Save/automaticretries", 0).toInt();
		getAllImages();
	}
}

void mainWindow::getAllGetPages()
{
	m_progressDialog->clearImages();
	m_progressDialog->setText(tr("Downloading pages, please wait..."));

	int max = 0;
	int packSize = 0;
	for (Downloader *downloader : qAsConst(m_downloaders))
	{
		downloader->getImages();
		max += downloader->pagesCount();
		packSize += downloader->imagesMax();
	}

	m_progressDialog->setCurrentValue(0);
	m_progressDialog->setCurrentMax(max);
	m_batchCurrentPackSize = packSize;
}

/**
 * Called when a page have been loaded and parsed.
 *
 * @param page The loaded page
 */
void mainWindow::getAllFinishedPage(Page *page)
{
	Downloader *d = qobject_cast<Downloader*>(sender());

	int pos = d->getData().toInt();
	m_groupBatchs[pos].unk += (m_groupBatchs[pos].unk == "" ? "" : "¤") + QString::number((quintptr)page);

	m_progressDialog->setCurrentValue(m_progressDialog->currentValue() + 1);
}

/**
 * Called when a page have been loaded and parsed.
 *
 * @param images The images results on this page
 */
void mainWindow::getAllFinishedImages(const QList<QSharedPointer<Image>> &images)
{
	Downloader *downloader = qobject_cast<Downloader*>(sender());
	m_downloaders.removeAll(downloader);
	m_getAllIgnoredPre += downloader->ignoredCount();

	int row = downloader->getData().toInt();

	for (const auto &img : images)
	{
		BatchDownloadImage d;
		d.image = img;
		d.queryGroup = &m_batchPending[row];
		m_getAllRemaining.append(d);
	}

	m_progressBars[row]->setValue(0);
	m_progressBars[row]->setMaximum(images.count());

	if (m_lastDownloader != nullptr)
	{ m_lastDownloader->deleteLater(); }
	m_lastDownloader = downloader;

	// Update image to take into account unlisted images
	int unlisted = m_batchCurrentPackSize - images.count();
	m_getAllImagesCount -= unlisted;

	if (m_downloaders.isEmpty())
	{
		m_batchAutomaticRetries = m_settings->value("Save/automaticretries", 0).toInt();
		getAllImages();
	}
}

/**
 * Called when all pages have been loaded and parsed from all sources.
 */
void mainWindow::getAllImages()
{
	// Si la limite d'images est dépassée, on retire celles en trop
	while (m_getAllRemaining.count() > m_getAllLimit && !m_getAllRemaining.isEmpty())
		m_getAllRemaining.takeLast().image->deleteLater();

	log(QStringLiteral("All images' urls have been received (%1).").arg(m_getAllRemaining.count()), Logger::Info);

	// We add the images to the download dialog
	m_progressDialog->clearImages();
	m_progressDialog->setText(tr("Preparing images, please wait..."));
	m_progressDialog->setCount(m_getAllRemaining.count());
	for (const BatchDownloadImage &download : qAsConst(m_getAllRemaining))
	{
		int siteId = download.siteId(m_groupBatchs);
		QSharedPointer<Image> img = download.image;

		// We add the image
		m_progressDialog->addImage(img->url(), siteId, img->fileSize());
		connect(img.data(), &Image::urlChanged, m_progressDialog, &batchWindow::imageUrlChanged);
		connect(img.data(), &Image::urlChanged, this, &mainWindow::imageUrlChanged);
	}

	// Set some values on the batch window
	m_progressDialog->updateColumns();
	m_progressDialog->setText(tr("Downloading images..."));
	m_progressDialog->setCurrentValue(0);
	m_progressDialog->setCurrentMax(m_getAllRemaining.count());
	m_progressDialog->setTotalValue(m_getAllDownloaded + m_getAllExists + m_getAllIgnored + m_getAllErrors);
	m_progressDialog->setTotalMax(m_getAllImagesCount);

	// Check whether we need to get the tags first (for the filename) or if we can just download the images directly
	// TODO(Bionus): having one batch needing it currently causes all batches to need it, should mae it batch (Downloader) dependent
	m_mustGetTags = needExactTags(m_settings);
	for (int f = 0; f < m_groupBatchs.size() && !m_mustGetTags; f++)
	{
		Filename fn(m_groupBatchs[f].filename);
		Site *site = m_groupBatchs[f].site;
		Api *api = site->firstValidApi();
		QString apiName = api == nullptr ? "" : api->getName();
		int need = fn.needExactTags(site, apiName);
		if (need != 0)
			m_mustGetTags = need;
	}
	for (int f = 0; f < m_batchs.size() && !m_mustGetTags; f++)
	{
		Filename fn(m_batchs[f].filename);
		Site *site = m_batchs[f].site;
		Api *api = site->firstValidApi();
		QString apiName = api == nullptr ? "" : api->getName();
		int need = fn.needExactTags(site, apiName);
		if (need != 0)
			m_mustGetTags = need;
	}

	if (m_mustGetTags)
		log(QStringLiteral("Downloading images details."), Logger::Info);
	else
		log(QStringLiteral("Downloading images directly."), Logger::Info);

	// We start the simultaneous downloads
	int count = qMax(1, qMin(m_settings->value("Save/simultaneous").toInt(), 10));
	m_getAllCurrentlyProcessing.store(count);
	for (int i = 0; i < count; i++)
		_getAll();
}

int mainWindow::needExactTags(QSettings *settings)
{
	auto logFiles = getExternalLogFiles(settings);
	for (auto it = logFiles.begin(); it != logFiles.end(); ++it)
	{
		Filename fn(it.value().value("content").toString());
		int need = fn.needExactTags();
		if (need != 0)
			return need;
	}

	QStringList settingNames = QStringList()
		<< "Exec/tag_before"
		<< "Exec/image"
		<< "Exec/tag_after"
		<< "Exec/SQL/before"
		<< "Exec/SQL/tag_before"
		<< "Exec/SQL/image"
		<< "Exec/SQL/tag_after"
		<< "Exec/SQL/after";
	for (const QString &setting : settingNames)
	{
		QString value = settings->value(setting, "").toString();
		if (value.isEmpty())
			continue;

		Filename fn(value);
		int need = fn.needExactTags();
		if (need != 0)
			return need;
	}

	return 0;
}

void mainWindow::_getAll()
{
	// We quit as soon as the user cancels
	if (m_progressDialog->cancelled())
		return;

	// If there are still images do download
	if (!m_getAllRemaining.empty())
	{
		// We take the first image to download
		BatchDownloadImage download = m_getAllRemaining.takeFirst();
		QSharedPointer<Image> img = download.image;
		m_getAllDownloading.append(download);

		// Get the tags first if necessary
		bool hasUnknownTag = false;
		for (const Tag &tag : img->tags())
		{
			if (tag.type().name() == "unknown")
			{
				hasUnknownTag = true;
				break;
			}
		}
		if (m_mustGetTags == 2 || (m_mustGetTags == 1 && hasUnknownTag))
		{
			connect(img.data(), &Image::finishedLoadingTags, this, &mainWindow::getAllPerformTags);
			img->loadDetails();
		}
		else
		{
			// Row
			int siteId = download.siteId(m_groupBatchs);
			int row = getRowForSite(siteId);

			if (siteId >= 0)
			{ ui->tableBatchGroups->item(row, 0)->setIcon(getIcon(":/images/status/downloading.png")); }

			// Path
			QString filename = download.query()->filename;
			QString path = download.query()->path;
			QStringList paths = img->path(filename, path, m_getAllDownloaded + m_getAllExists + m_getAllIgnored + m_getAllErrors + 1, true, false, true, true, true);

			bool notexists = true;
			for (const QString &p : paths)
			{
				QFile f(p);
				if (f.exists())
				{ notexists = false; }
			}

			// If the file does not already exists
			if (notexists)
			{
				getAllGetImageIfNotBlacklisted(download, siteId);
			}

			// If the file already exists
			else
			{
				m_getAllExists++;
				log(QStringLiteral("File already exists: <a href=\"file:///%1\">%1</a>").arg(paths.at(0)), Logger::Info);
				m_progressDialog->loadedImage(img->url(), Image::SaveResult::AlreadyExists);
				getAllImageOk(download, siteId);
			}
		}
	}

	// When the batch download finishes
	else if (m_getAllCurrentlyProcessing.fetchAndAddRelaxed(-1) == 1 && m_getAll)
	{ getAllFinished(); }
}

void mainWindow::getAllGetImageIfNotBlacklisted(const BatchDownloadImage &download, int siteId)
{
	// Early return if we want to download blacklisted images
	if (download.queryGroup == Q_NULLPTR || download.queryGroup->getBlacklisted)
	{
		getAllGetImage(download, siteId);
		return;
	}

	// Check if image is blacklisted
	const QStringList &detected = PostFilter::blacklisted(download.image->tokens(m_profile), m_profile->getBlacklist());
	if (!detected.isEmpty())
	{
		m_getAllIgnored++;
		log(QStringLiteral("Image ignored for containing blacklisted tags: '%1'").arg(detected.join("', '")), Logger::Info);
		m_progressDialog->loadedImage(download.image->url(), Image::SaveResult::Ignored);
		getAllImageOk(download, siteId);
		return;
	}

	// Image is not blacklisted, proceed as usual
	getAllGetImage(download, siteId);
}

void mainWindow::getAllImageOk(const BatchDownloadImage &download, int siteId, bool retry)
{
	download.image->unload();
	m_downloadTime.remove(download.image->url());
	m_downloadTimeLast.remove(download.image->url());

	if (retry)
		return;

	m_progressDialog->setCurrentValue(m_progressDialog->currentValue() + 1);
	m_progressDialog->setTotalValue(m_getAllDownloaded + m_getAllExists + m_getAllIgnored + m_getAllErrors);

	if (siteId >= 0)
	{
		int row = getRowForSite(siteId);
		m_progressBars[siteId - 1]->setValue(m_progressBars[siteId - 1]->value() + 1);
		if (m_progressBars[siteId - 1]->value() >= m_progressBars[siteId - 1]->maximum())
		{ ui->tableBatchGroups->item(row, 0)->setIcon(getIcon(":/images/status/ok.png")); }
	}

	m_getAllDownloading.removeAll(download);
	_getAll();
}

void mainWindow::imageUrlChanged(const QString &before, const QString &after)
{
	m_downloadTimeLast.insert(after, m_downloadTimeLast[before]);
	m_downloadTimeLast.remove(before);
	m_downloadTime.insert(after, m_downloadTime[before]);
	m_downloadTime.remove(before);
}
void mainWindow::getAllProgress(QSharedPointer<Image> img, qint64 bytesReceived, qint64 bytesTotal)
{
	QString url = img->url();
	if (img->fileSize() == 0)
	{
		img->setFileSize(bytesTotal);
		m_progressDialog->sizeImage(url, bytesTotal);
	}

	if (!m_downloadTimeLast.contains(url))
		return;

	if (m_downloadTimeLast[url].elapsed() >= 1000)
	{
		m_downloadTimeLast[url].restart();
		int elapsed = m_downloadTime[url].elapsed();
		double speed = elapsed != 0 ? (bytesReceived * 1000) / elapsed : 0;
		m_progressDialog->speedImage(url, speed);
	}

	int percent = 0;
	if (bytesTotal> 0)
	{
		qreal pct = static_cast<qreal>(bytesReceived) / static_cast<qreal>(bytesTotal);
		percent = qFloor(pct * 100);
	}

	m_progressDialog->statusImage(url, percent);
}
void mainWindow::getAllPerformTags()
{
	if (m_progressDialog->cancelled())
		return;

	log(QStringLiteral("Tags received"), Logger::Info);

	const BatchDownloadImage *downloadPtr = Q_NULLPTR;
	for (const BatchDownloadImage &i : qAsConst(m_getAllDownloading))
		if (i.image.data() == sender())
			downloadPtr = &i;
	if (downloadPtr == Q_NULLPTR)
	{
		log(QStringLiteral("Tags received from unknown sender"), Logger::Error);
		return;
	}

	BatchDownloadImage download = *downloadPtr;
	QSharedPointer<Image> img = download.image;

	// Row
	int siteId = download.siteId(m_groupBatchs);
	int row = getRowForSite(siteId);

	// Getting path
	QString filename = download.query()->filename;
	QString path = download.query()->path;

	// Save path
	path.replace("\\", "/");
	if (path.right(1) == "/")
	{ path = path.left(path.length() - 1); }

	int cnt = m_getAllDownloaded + m_getAllExists + m_getAllIgnored + m_getAllErrors + 1;
	QStringList paths = img->path(filename, path, cnt, true, false, true, true, true);
	const QString &pth = paths.at(0); // FIXME

	QFile f(pth);
	if (!f.exists())	{ f.setFileName(pth.section('.', 0, -2)+".png");	}
	if (!f.exists())	{ f.setFileName(pth.section('.', 0, -2)+".gif");	}
	if (!f.exists())	{ f.setFileName(pth.section('.', 0, -2)+".jpeg");	}
	if (!f.exists())
	{
		getAllGetImageIfNotBlacklisted(download, siteId);
	}
	else
	{
		m_progressDialog->setCurrentValue(m_progressDialog->currentValue() + 1);
		m_getAllExists++;
		log(QStringLiteral("File already exists: <a href=\"file:///%1\">%1</a>").arg(f.fileName()), Logger::Info);
		m_progressDialog->loadedImage(img->url(), Image::SaveResult::AlreadyExists);
		if (siteId >= 0)
		{
			m_progressBars[siteId - 1]->setValue(m_progressBars[siteId - 1]->value()+1);
			if (m_progressBars[siteId - 1]->value() >= m_progressBars[siteId - 1]->maximum())
			{ ui->tableBatchGroups->item(row, 0)->setIcon(getIcon(":/images/status/ok.png")); }
		}
		m_downloadTimeLast.remove(img->url());
		m_getAllDownloading.removeAll(download);
		m_progressDialog->setTotalValue(m_getAllDownloaded + m_getAllExists + m_getAllIgnored + m_getAllErrors);
		_getAll();
	}
}

int mainWindow::getRowForSite(int siteId)
{
	return siteId - 1;
}

void mainWindow::getAllGetImage(const BatchDownloadImage &download, int siteId)
{
	QSharedPointer<Image> img = download.image;

	// If there is already a downloader for this image, we simply restart it
	if (m_getAllImageDownloaders.contains(img))
	{
		m_getAllImageDownloaders[img]->save();
		return;
	}

	// Row
	int row = getRowForSite(siteId);

	// Path
	QString filename = download.query()->filename;
	QString path = download.query()->path;
	if (siteId >= 0)
	{ ui->tableBatchGroups->item(row, 0)->setIcon(getIcon(":/images/status/downloading.png")); }

	// Track download progress
	m_progressDialog->loadingImage(img->url());
	m_downloadTime.insert(img->url(), QTime());
	m_downloadTime[img->url()].start();
	m_downloadTimeLast.insert(img->url(), QTime());
	m_downloadTimeLast[img->url()].start();

	// Start loading and saving image
	log(QStringLiteral("Loading image from <a href=\"%1\">%1</a> %2").arg(img->fileUrl().toString()).arg(m_getAllDownloading.size()), Logger::Info);
	int count = m_getAllDownloaded + m_getAllExists + m_getAllIgnored + m_getAllErrors + 1;
	auto imgDownloader = new ImageDownloader(img, filename, path, count, true, false, this);
	connect(imgDownloader, &ImageDownloader::saved, this, &mainWindow::getAllGetImageSaved, Qt::UniqueConnection);
	connect(imgDownloader, &ImageDownloader::downloadProgress, this, &mainWindow::getAllProgress, Qt::UniqueConnection);
	imgDownloader->save();
	m_getAllImageDownloaders[img] = imgDownloader;
}

void mainWindow::getAllGetImageSaved(QSharedPointer<Image> img, QMap<QString, Image::SaveResult> result)
{
	// Delete ImageDownloader to prevent leaks
	m_getAllImageDownloaders[img]->deleteLater();
	m_getAllImageDownloaders.remove(img);

	// Find related download query
	const BatchDownloadImage *downloadPtr = Q_NULLPTR;
	for (const BatchDownloadImage &i : qAsConst(m_getAllDownloading))
		if (i.image == img)
			downloadPtr = &i;
	if (downloadPtr == Q_NULLPTR)
	{
		log(QStringLiteral("Saved image signal received from unknown sender"), Logger::Error);
		return;
	}
	BatchDownloadImage download = *downloadPtr;

	// Save error count to compare it later on
	bool diskError = false;
	auto res = result.first();

	// Disk writing errors
	for (auto it = result.begin(); it != result.end(); ++it)
	{
		const QString &path = it.key();
		if (it.value() == Image::SaveResult::Error)
		{
			diskError = true;

			if (!m_progressDialog->isPaused())
			{
				m_progressDialog->pause();

				bool isDriveFull = false;
				QString drive;
				#if (QT_VERSION >= QT_VERSION_CHECK(5, 4, 0))
					QDir destinationDir = QFileInfo(path).absoluteDir();
					QStorageInfo storage(destinationDir);
					isDriveFull = storage.isValid() && (storage.bytesAvailable() < img->fileSize() || storage.bytesAvailable() < 20 * 1024 * 1024);
					QString rootPath = storage.rootPath();
					#ifdef Q_OS_WIN
						drive = QStringLiteral("%1 (%2)").arg(storage.name(), rootPath.endsWith("/") ? rootPath.left(rootPath.length() - 1) : rootPath);
					#else
						drive = rootPath;
					#endif
				#endif

				QString msg;
				if (isDriveFull)
				{ msg = tr("Not enough space on the destination drive \"%1\".\nPlease free some space before resuming the download.").arg(drive); }
				else
				{ msg = tr("An error occured saving the image.\n%1\nPlease solve the issue before resuming the download.").arg(path); }
				QMessageBox::critical(m_progressDialog, tr("Error"), msg);
			}
		}
	}

	if (res == Image::SaveResult::NetworkError)
	{
		m_getAllErrors++;
		m_getAllFailed.append(download);
	}
	else if (res == Image::SaveResult::NotFound)
	{ m_getAll404s++; }
	else if (res == Image::SaveResult::AlreadyExists)
	{ m_getAllExists++; }
	else if (res == Image::SaveResult::Ignored)
	{ m_getAllIgnored++; }
	else if (!diskError)
	{ m_getAllDownloaded++; }

	m_progressDialog->loadedImage(img->url(), res);

	int siteId = download.siteId(m_groupBatchs);
	getAllImageOk(download, siteId, diskError);
}

void mainWindow::getAllCancel()
{
	log(QStringLiteral("Cancelling downloads..."), Logger::Info);
	m_progressDialog->cancel();
	for (const BatchDownloadImage &download : qAsConst(m_getAllDownloading))
	{
		download.image->abortTags();
		download.image->abortImage();
	}
	for (Downloader *downloader : qAsConst(m_downloaders))
	{
		downloader->cancel();
	}
	m_getAll = false;
	ui->widgetDownloadButtons->setEnabled(true);
	DONE();
}

void mainWindow::getAllSkip()
{
	log(QStringLiteral("Skipping downloads..."), Logger::Info);

	int count = m_getAllDownloading.count();
	for (const BatchDownloadImage &download : qAsConst(m_getAllDownloading))
	{
		download.image->abortTags();
		download.image->abortImage();
	}
	m_getAllSkippedImages.append(m_getAllDownloading);
	m_getAllDownloading.clear();

	m_getAllSkipped += count;
	m_progressDialog->setTotalValue(m_getAllDownloaded + m_getAllExists + m_getAllIgnored + m_getAllErrors);
	m_getAllCurrentlyProcessing.store(count);
	for (int i = 0; i < count; ++i)
		_getAll();

	DONE();
}

void mainWindow::getAllFinished()
{
	if (!m_waitingDownloaders.isEmpty())
	{
		getNextPack();
		return;
	}

	log(QStringLiteral("Images download finished."), Logger::Info);
	m_progressDialog->setTotalValue(m_progressDialog->totalMax());

	// Delete objects
	if (m_lastDownloader != nullptr)
	{
		m_lastDownloader->deleteLater();
		m_lastDownloader = nullptr;
	}

	// Retry in case of error
	int failedCount = m_getAllErrors + m_getAllSkipped;
	if (failedCount > 0)
	{
		int reponse;
		if (m_batchAutomaticRetries > 0)
		{
			m_batchAutomaticRetries--;
			reponse = QMessageBox::Yes;
		}
		else
		{
			int totalCount = m_getAllDownloaded + m_getAllIgnored + m_getAllExists + m_getAll404s + m_getAllErrors + m_getAllSkipped;
			reponse = QMessageBox::question(this, tr("Getting images"), tr("Errors occured during the images download. Do you want to restart the download of those images? (%1/%2)").arg(failedCount).arg(totalCount), QMessageBox::Yes | QMessageBox::No);
		}

		if (reponse == QMessageBox::Yes)
		{
			m_getAll = true;
			m_progressDialog->clear();
			m_getAllRemaining.clear();
			m_getAllRemaining.append(m_getAllFailed);
			m_getAllRemaining.append(m_getAllSkippedImages);
			m_getAllImagesCount = m_getAllRemaining.count();
			m_getAllFailed.clear();
			m_getAllSkippedImages.clear();
			m_getAllDownloaded = 0;
			m_getAllExists = 0;
			m_getAllIgnored = 0;
			m_getAllIgnoredPre = 0;
			m_getAll404s = 0;
			m_getAllErrors = 0;
			m_getAllSkipped = 0;
			m_progressDialog->show();
			getAllImages();
			return;
		}
	}

	// Download result
	QMessageBox::information(
		this,
		tr("Getting images"),
		QString(
			tr("%n file(s) downloaded successfully.", "", m_getAllDownloaded)+"\r\n"+
			tr("%n file(s) ignored.", "", m_getAllIgnored + m_getAllIgnoredPre)+"\r\n"+
			tr("%n file(s) already existing.", "", m_getAllExists)+"\r\n"+
			tr("%n file(s) not found on the server.", "", m_getAll404s)+"\r\n"+
			tr("%n file(s) skipped.", "", m_getAllSkipped)+"\r\n"+
			tr("%n error(s).", "", m_getAllErrors)
		)
	);

	// Final action
	switch (m_progressDialog->endAction())
	{
		case 1:	m_progressDialog->close();				break;
		case 2:	openTray();								break;
		case 3:	saveFolder();							break;
		case 4:	QSound::play(":/sounds/finished.wav");	break;
		case 5: shutDown();								break;
	}
	activateWindow();
	m_getAll = false;

	// Remove after download and retries are finished
	if (m_progressDialog->endRemove())
	{ batchRemoveGroups(m_batchDownloading.toList()); }

	// End of batch download
	m_profile->getCommands().after();
	ui->widgetDownloadButtons->setEnabled(true);
	log(QStringLiteral("Batch download finished"), Logger::Info);
}

void mainWindow::getAllPause()
{
	if (m_progressDialog->isPaused())
	{
		log(QStringLiteral("Pausing downloads..."), Logger::Info);
		for (const auto &download : qAsConst(m_getAllDownloading))
		{
			download.image->abortTags();
			download.image->abortImage();
		}
		m_getAll = false;
	}
	else
	{
		log(QStringLiteral("Recovery of downloads..."), Logger::Info);
		for (const auto &download : qAsConst(m_getAllDownloading))
		{
			getAllGetImage(download, download.siteId(m_groupBatchs));
		}
		m_getAll = true;
	}
	DONE();
}

void mainWindow::blacklistFix()
{
	auto *win = new BlacklistFix1(m_profile, this);
	win->show();
}
void mainWindow::emptyDirsFix()
{
	auto *win = new EmptyDirsFix1(m_profile, this);
	win->show();
}
void mainWindow::md5FixOpen()
{
	auto *win = new md5Fix(m_profile, this);
	win->show();
}
void mainWindow::renameExisting()
{
	auto *win = new RenameExisting1(m_profile, this);
	win->show();
}
void mainWindow::utilTagLoader()
{
	auto *win = new TagLoader(m_profile);
	win->show();
}

void mainWindow::on_buttonSaveLinkList_clicked()
{
	QString lastDir = m_settings->value("linksLastDir", "").toString();
	QString save = QFileDialog::getSaveFileName(this, tr("Save link list"), QDir::toNativeSeparators(lastDir), tr("Imageboard-Grabber links (*.igl)"));
	if (save.isEmpty())
	{ return; }

	save = QDir::toNativeSeparators(save);
	m_settings->setValue("linksLastDir", save.section(QDir::separator(), 0, -2));

	if (saveLinkList(save))
	{ QMessageBox::information(this, tr("Save link list"), tr("Link list saved successfully!")); }
	else
	{ QMessageBox::critical(this, tr("Save link list"), tr("Error opening file.")); }
}
bool mainWindow::saveLinkList(const QString &filename)
{
	return DownloadQueryLoader::save(filename, m_batchs, m_groupBatchs);
}

void mainWindow::on_buttonLoadLinkList_clicked()
{
	QString load = QFileDialog::getOpenFileName(this, tr("Load link list"), QString(), tr("Imageboard-Grabber links (*.igl)"));
	if (load.isEmpty())
	{ return; }

	if (loadLinkList(load))
	{ QMessageBox::information(this, tr("Load link list"), tr("Link list loaded successfully!")); }
	else
	{ QMessageBox::critical(this, tr("Load link list"), tr("Error opening file.")); }
}
bool mainWindow::loadLinkList(const QString &filename)
{
	QList<DownloadQueryImage> newBatchs;
	QList<DownloadQueryGroup> newGroupBatchs;

	if (!DownloadQueryLoader::load(filename, newBatchs, newGroupBatchs, m_profile->getSites()))
		return false;

	log(tr("Loading %n download(s)", "", newBatchs.count() + newGroupBatchs.count()), Logger::Info);

	m_allow = false;
	for (const auto &queryImage : qAsConst(newBatchs))
	{
		batchAddUnique(queryImage, false);
	}
	for (auto queryGroup : qAsConst(newGroupBatchs))
	{
		ui->tableBatchGroups->setRowCount(ui->tableBatchGroups->rowCount() + 1);
		QString last = queryGroup.unk;
		int max = last.rightRef(last.indexOf("/")+1).toInt(), val = last.leftRef(last.indexOf("/")).toInt();

		int row = ui->tableBatchGroups->rowCount() - 1;
		addTableItem(ui->tableBatchGroups, row, 1, queryGroup.tags);
		addTableItem(ui->tableBatchGroups, row, 2, queryGroup.site->url());
		addTableItem(ui->tableBatchGroups, row, 3, QString::number(queryGroup.page));
		addTableItem(ui->tableBatchGroups, row, 4, QString::number(queryGroup.perpage));
		addTableItem(ui->tableBatchGroups, row, 5, QString::number(queryGroup.total));
		addTableItem(ui->tableBatchGroups, row, 6, queryGroup.filename);
		addTableItem(ui->tableBatchGroups, row, 7, queryGroup.path);
		addTableItem(ui->tableBatchGroups, row, 8, queryGroup.postFiltering.join(' '));
		addTableItem(ui->tableBatchGroups, row, 9, queryGroup.getBlacklisted ? "true" : "false");

		queryGroup.unk = "true";
		m_groupBatchs.append(queryGroup);
		QTableWidgetItem *it = new QTableWidgetItem(getIcon(":/images/status/"+QString(val == max ? "ok" : (val > 0 ? "downloading" : "pending"))+".png"), "");
		it->setFlags(it->flags() ^ Qt::ItemIsEditable);
		it->setTextAlignment(Qt::AlignCenter);
		ui->tableBatchGroups->setItem(row, 0, it);

		auto *prog = new QProgressBar(this);
		prog->setMaximum(queryGroup.total);
		prog->setValue(val < 0 || val > max ? 0 : val);
		prog->setMinimum(0);
		prog->setTextVisible(false);
		m_progressBars.append(prog);
		ui->tableBatchGroups->setCellWidget(row, 10, prog);
	}
	m_allow = true;
	updateGroupCount();

	return true;
}

void mainWindow::setWiki(const QString &wiki)
{
	ui->labelWiki->setText("<style>.title { font-weight: bold; } ul { margin-left: -30px; }</style>" + wiki);
}

void mainWindow::siteDeleted(Site *site)
{
	QList<int> batchRows;
	for (int i = 0; i < m_groupBatchs.count(); ++i)
	{
		const DownloadQueryGroup &batch = m_groupBatchs[i];
		if (batch.site == site)
			batchRows.append(i);
	}
	batchRemoveGroups(batchRows);

	QList<int> uniquesRows;
	for (int i = 0; i < m_batchs.count(); ++i)
	{
		const DownloadQueryImage &batch = m_batchs[i];
		if (batch.site == site)
			uniquesRows.append(i);
	}
	batchRemoveUniques(uniquesRows);
}

QIcon& mainWindow::getIcon(const QString &path)
{
	if (!m_icons.contains(path))
		m_icons.insert(path, QIcon(path));

	return m_icons[path];
}

void mainWindow::on_buttonFolder_clicked()
{
	QString folder = QFileDialog::getExistingDirectory(this, tr("Choose a save folder"), ui->lineFolder->text());
	if (!folder.isEmpty())
	{
		ui->lineFolder->setText(folder);
		updateCompleters();
		saveSettings();
	}
}
void mainWindow::on_buttonSaveSettings_clicked()
{
	QString folder = fixFilename("", ui->lineFolder->text());
	if (!QDir(folder).exists())
		QDir::root().mkpath(folder);

	m_settings->setValue("Save/path_real", folder);
	m_settings->setValue("Save/filename_real", ui->comboFilename->currentText());
	saveSettings();
}
void mainWindow::on_buttonInitSettings_clicked()
{
	// Reload filename history
	QFile f(m_profile->getPath() + "/filenamehistory.txt");
	QStringList filenames;
	if (f.open(QFile::ReadOnly | QFile::Text))
	{
		QString line;
		while ((line = f.readLine()) > 0)
		{
			QString l = line.trimmed();
			if (!l.isEmpty() && !filenames.contains(l))
			{
				filenames.append(l);
				ui->comboFilename->addItem(l);
			}
		}
		f.close();
	}

	// Update quick settings dock
	ui->lineFolder->setText(m_settings->value("Save/path_real").toString());
	ui->comboFilename->setCurrentText(m_settings->value("Save/filename_real").toString());

	// Save settings
	saveSettings();
}
void mainWindow::updateCompleters()
{
	if (ui->lineFolder->text() != m_settings->value("Save/path").toString())
	{
		m_lineFolder_completer.append(ui->lineFolder->text());
		ui->lineFolder->setCompleter(new QCompleter(m_lineFolder_completer));
	}
	/*if (ui->labelFilename->text() != m_settings->value("Save/filename").toString())
	{
		m_lineFilename_completer.append(ui->lineFilename->text());
		ui->lineFilename->setCompleter(new QCompleter(m_lineFilename_completer));
	}*/
}
void mainWindow::saveSettings()
{
	// Filename combobox
	QString txt = ui->comboFilename->currentText();
	for (int i = ui->comboFilename->count() - 1; i >= 0; --i)
		if (ui->comboFilename->itemText(i) == txt)
			ui->comboFilename->removeItem(i);
	ui->comboFilename->insertItem(0, txt);
	ui->comboFilename->setCurrentIndex(0);
	QString message;
	Filename fn(ui->comboFilename->currentText());
	fn.isValid(m_profile, &message);
	ui->labelFilename->setText(message);

	// Save filename history
	QFile f(m_profile->getPath() + "/filenamehistory.txt");
	if (f.open(QFile::WriteOnly | QFile::Text | QFile::Truncate))
	{
		for (int i = qMax(0, ui->comboFilename->count() - 50); i < ui->comboFilename->count(); ++i)
			f.write(QString(ui->comboFilename->itemText(i) + "\n").toUtf8());
		f.close();
	}

	// Update settings
	QString folder = fixFilename("", ui->lineFolder->text());
	m_settings->setValue("Save/path", folder);
	m_settings->setValue("Save/filename", ui->comboFilename->currentText());
	m_settings->sync();
}



void mainWindow::loadMd5(const QString &path, bool newTab, bool background, bool save)
{
	QFile file(path);
	if (file.open(QFile::ReadOnly))
	{
		QString md5 = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Md5).toHex();
		file.close();

		loadTag("md5:" + md5, newTab, background, save);
	}
}
void mainWindow::loadTag(const QString &tag, bool newTab, bool background, bool save)
{
	if (tag.startsWith("http://") || tag.startsWith("https://"))
	{
		QDesktopServices::openUrl(tag);
		return;
	}

	if (newTab)
		addTab(tag, background, save);
	else if (m_tabs.count() > 0 && ui->tabWidget->currentIndex() < m_tabs.count())
		m_tabs[ui->tabWidget->currentIndex()]->setTags(tag);
}
void mainWindow::loadTagTab(const QString &tag)
{ loadTag(tag.isEmpty() ? m_link : tag, true); }
void mainWindow::loadTagNoTab(const QString &tag)
{ loadTag(tag.isEmpty() ? m_link : tag, false); }
void mainWindow::linkHovered(const QString &tag)
{
	m_link = tag;
}
void mainWindow::contextMenu()
{
	if (m_link.isEmpty())
		return;

	TagContextMenu *menu = new TagContextMenu(m_link, m_currentTags, QUrl(), m_profile, false, this);
	connect(menu, &TagContextMenu::openNewTab, this, &mainWindow::openInNewTab);
	menu->exec(QCursor::pos());
}
void mainWindow::openInNewTab()
{
	addTab(m_link);
}


void mainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
	if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
	{
		showNormal();
	}
}

void mainWindow::trayMessageClicked()
{
	// No op
}

void mainWindow::trayClose()
{
	m_closeFromTray = true;
	close();
	m_closeFromTray = false;
}


void mainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	const QMimeData* mimeData = event->mimeData();

	// Drop a text containing an URL
	if (mimeData->hasText())
	{
		QString url = mimeData->text();
		if (isUrl(url))
		{
			event->acceptProposedAction();
			return;
		}
	}

	// Drop URLs
	if (mimeData->hasUrls())
	{
		QList<QUrl> urlList = mimeData->urls();
		for (int i = 0; i < urlList.size() && i < 32; ++i)
		{
			QString path = urlList.at(i).toLocalFile();
			QFileInfo fileInfo(path);
			if (fileInfo.exists() && fileInfo.isFile())
			{
				event->acceptProposedAction();
				return;
			}
		}
	}
}

void mainWindow::dropEvent(QDropEvent* event)
{
	const QMimeData* mimeData = event->mimeData();

	// Drop a text containing an URL
	if (mimeData->hasText())
	{
		QString url = mimeData->text();
		if (isUrl(url))
		{
			QEventLoop loopLoad;
			QNetworkReply *reply = m_networkAccessManager.get(QNetworkRequest(QUrl(url)));
			connect(reply, &QNetworkReply::finished, &loopLoad, &QEventLoop::quit);
			loopLoad.exec();

			if (reply->error() == QNetworkReply::NoError)
			{
				QString md5 = QCryptographicHash::hash(reply->readAll(), QCryptographicHash::Md5).toHex();
				loadTag("md5:" + md5, true, false);
			}
			return;
		}
	}

	// Drop URLs
	if (mimeData->hasUrls())
	{
		QList<QUrl> urlList = mimeData->urls();
		for (int i = 0; i < urlList.size() && i < 32; ++i)
		{
			loadMd5(urlList.at(i).toLocalFile(), true, false);
		}
	}
}
