#ifndef SEARCH_TAB_H
#define SEARCH_TAB_H

#include <QCheckBox>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalMapper>
#include <QSpinBox>
#include <QStackedWidget>
#include <QWidget>
#include "models/image.h"


class DownloadQueryGroup;
class DownloadQueryImage;
class Favorite;
class mainWindow;
class Profile;
class QBouton;
class FixedSizeGridLayout;
class TextEdit;
class VerticalScrollArea;
class ZoomWindow;

class searchTab : public QWidget
{
	Q_OBJECT

	public:
		searchTab(Profile *profile, mainWindow *parent);
		~searchTab() override;
		void init();
		void mouseReleaseEvent(QMouseEvent *e) override;
		virtual QList<Site*> sources();
		virtual QString tags() const = 0;
		QList<Tag> results();
		QString wiki();
		int imagesPerPage();
		int columns();
		QString postFilter();
		virtual void setTags(const QString &tags, bool preload = true) = 0;
		virtual bool validateImage(const QSharedPointer<Image> &img, QString &error);
		QStringList selectedImages();
		void setSources(const QList<Site*> &sources);
		void setImagesPerPage(int ipp);
		void setColumns(int columns);
		void setPostFilter(const QString &postfilter);
		virtual QList<Site*> loadSites() const;
		virtual void onLoad();
		virtual void write(QJsonObject &json) const = 0;

	protected:
		void setSelectedSources(QSettings *settings);
		void setTagsFromPages(const QMap<QString, QList<QSharedPointer<Page> > > &pages);
		void addHistory(const QString &tags, int page, int ipp, int cols);
		QStringList reasonsToFail(Page *page, const QStringList &complete = QStringList(), QString *meant = nullptr);
		void clear();
		TextEdit *createAutocomplete();
		void loadImageThumbnails(Page *page, const QList<QSharedPointer<Image>> &imgs);
		void loadImageThumbnail(Page *page, QSharedPointer<Image> img, const QString &url);
		QBouton *createImageThumbnail(int position, QSharedPointer<Image> img);
		FixedSizeGridLayout *createImagesLayout(QSettings *settings);
		void thumbnailContextMenu(int position, QSharedPointer<Image> img);

	protected slots:
		void contextSaveImage(int position);
		void contextSaveImageAs(int position);
		void contextSaveSelected();
		void setMergeResultsMode(bool merged);
		void setEndlessLoadingMode(bool enabled);
		void toggleSource(const QString &url);
		void setFavoriteImage(const QString &name);

	private:
		void addLayout(QLayout *layout, int row, int column);

	public slots:
		// Sources
		void openSourcesWindow();
		void saveSources(const QList<Site *> &sel, bool canLoad = true);
		void updateCheckboxes();
		// Zooms
		void webZoom(int);
		void openImage(QSharedPointer<Image> image);
		// Pagination
		void firstPage();
		void previousPage();
		void nextPage();
		void lastPage();
		// Focus search field
		virtual void focusSearch() = 0;
		// Batch
		void getSel();
		// History
		void historyBack();
		void historyNext();
		// Results
		virtual void load() = 0;
		virtual void updateTitle() = 0;
		void loadTags(QStringList tags);
		void endlessLoad();
		void loadPage();
		virtual void addResultsPage(Page *page, const QList<QSharedPointer<Image>> &imgs, bool merged, const QString &noResultsMessage = nullptr);
		void setMergedLabelText(QLabel *txt, const QList<QSharedPointer<Image>> &imgs);
		virtual void setPageLabelText(QLabel *txt, Page *page, const QList<QSharedPointer<Image>> &imgs, const QString &noResultsMessage = nullptr);
		void addResultsImage(QSharedPointer<Image> img, Page *page, bool merge = false);
		void finishedLoadingPreview();
		// Merged
		QList<QSharedPointer<Image>> mergeResults(int page, const QList<QSharedPointer<Image> > &results);
		void addMergedMd5(int page, const QString &md5);
		bool containsMergedMd5(int page, const QString &md5);
		// Loading
		void finishedLoading(Page *page);
		void failedLoading(Page *page);
		void httpsRedirect(Page *page);
		void postLoading(Page *page, const QList<QSharedPointer<Image> > &imgs);
		void finishedLoadingTags(Page *page);
		// Image selection
		void selectImage(QSharedPointer<Image> img);
		void unselectImage(QSharedPointer<Image> img);
		void toggleImage(QSharedPointer<Image> img);
		void toggleImage(int id, bool toggle, bool range);
		// Others
		void optionsChanged();

	signals:
		// Tab events
		void titleChanged(searchTab*);
		void changed(searchTab*);
		void closed(searchTab*);

		// Batch
		void batchAddGroup(const DownloadQueryGroup &);
		void batchAddUnique(const DownloadQueryImage &);

	protected:
		Profile				*m_profile;
		int					m_lastPage;
		qulonglong			m_lastPageMaxId, m_lastPageMinId;
		const QMap<QString, Site*> &m_sites;
		QMap<Image*, QBouton*>	m_boutons;
		QStringList			m_selectedImages;
		QList<QSharedPointer<Image>>	m_selectedImagesPtrs;
		QList<Site*>		m_selectedSources;
		QSignalMapper		*m_checkboxesSignalMapper;
		QList<QCheckBox*>	m_checkboxes;
		QList<Favorite>		&m_favorites;
		QList<Tag>			m_tags;
		mainWindow			*m_parent;
		QSettings			*m_settings;
		QString				m_wiki;
		QMap<Page*, QList<QSharedPointer<Image>>> m_validImages;

		QStringList m_completion;
		QMap<QNetworkReply*, QSharedPointer<Image>> m_thumbnailsLoading;
		QList<QSharedPointer<Image>> m_images;
		QMap<QString, QList<QSharedPointer<Page>>> m_pages;
		QMap<QString, QSharedPointer<Page>> m_lastPages;
		QMap<Site*, QLabel*> m_siteLabels;
		QMap<Site*, QVBoxLayout*> m_siteLayouts;
		QMap<Page*, FixedSizeGridLayout*> m_layouts;
		int m_page;
		int m_pagemax;
		bool m_stop;
		int m_lastToggle;
		bool m_endlessLoadingEnabled, m_endlessLoadingEnabledPast;
		int m_endlessLoadOffset;
		bool m_pageMergedMode;
		QPointer<ZoomWindow> m_lastZoomWindow;

		// History
		bool m_from_history;
		int m_history_cursor;
		QList<QMap<QString, QString>> m_history;
		QString m_lastTags;
		QList<QPair<int, QSet<QString>>> m_mergedMd5s;

		// UI stuff
		TextEdit *m_postFiltering;
		QCheckBox *ui_checkMergeResults;
		QProgressBar *ui_progressMergeResults;
		QStackedWidget *ui_stackedMergeResults;
		QSpinBox *ui_spinPage;
		QSpinBox *ui_spinImagesPerPage;
		QSpinBox *ui_spinColumns;
		QWidget *ui_widgetMeant;
		QLabel *ui_labelMeant;
		QGridLayout *ui_layoutResults;
		QLayout *ui_layoutSourcesList;
		QPushButton *ui_buttonHistoryBack;
		QPushButton *ui_buttonHistoryNext;
		QPushButton *ui_buttonNextPage;
		QPushButton *ui_buttonLastPage;
		QPushButton *ui_buttonGetAll;
		QPushButton *ui_buttonGetPage;
		QPushButton *ui_buttonGetSel;
		QPushButton *ui_buttonFirstPage;
		QPushButton *ui_buttonPreviousPage;
		QPushButton *ui_buttonEndlessLoad;
		VerticalScrollArea *ui_scrollAreaResults;
};

#endif // SEARCH_TAB_H
