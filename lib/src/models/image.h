#ifndef IMAGE_H
#define IMAGE_H

#include <QDateTime>
#include <QMap>
#include <QNetworkReply>
#include <QPixmap>
#include <QSettings>
#include "loader/downloadable.h"
#include "models/pool.h"
#include "tags/tag.h"


class ExtensionRotator;
class Page;
class Profile;
class Site;

class Image : public QObject, public Downloadable
{
	Q_OBJECT

	public:
		Image();
		Image(Site *site, QMap<QString, QString> details, Profile *profile, Page *parent = Q_NULLPTR);
		Image(const Image &other);
		int			value() const;
		QStringList	path(QString fn = "", QString pth = "", int counter = 0, bool complex = true, bool simple = false, bool maxLength = true, bool shouldFixFilename = true, bool getFull = false) const;
		QStringList	stylishedTags(Profile *profile) const;
		SaveResult  save(const QString &path, bool force = false, bool basic = false, bool addMd5 = true, bool startCommands = false, int count = 1, bool postSave = true);
		void		postSaving(const QString &path, bool addMd5 = true, bool startCommands = false, int count = 1, bool basic = false);
		QMap<QString, Image::SaveResult> save(const QStringList &paths, bool addMd5 = true, bool startCommands = false, int count = 1, bool force = false);
		QMap<QString, Image::SaveResult> save(const QString &filename, const QString &path, bool addMd5 = true, bool startCommands = false, int count = 1);
		QString		md5() const;
		QString		url() const;
		QString		rating() const;
		QString		site() const;
		QList<Tag>	tags() const;
		QList<Tag>	filteredTags(const QStringList &remove) const;
		QStringList tagsString() const;
		QList<Pool>	pools() const;
		qulonglong	id() const;
		int			fileSize() const;
		int			width() const;
		int			height() const;
		QDateTime	createdAt() const;
		QUrl		pageUrl() const;
		QUrl		fileUrl() const;
		QSize		size() const;
		QPixmap		previewImage() const;
		const QPixmap &previewImage();
		void		setPreviewImage(const QPixmap &preview);
		Page		*page() const;
		const QByteArray &data() const;
		Site		*parentSite() const;
		ExtensionRotator *extensionRotator() const;
		bool		hasTag(QString tag) const;
		bool		hasAnyTag(const QStringList &tags) const;
		bool		hasAllTags(const QStringList &tags) const;
		void		setUrl(const QString &);
		void		setData(const QByteArray &data);
		void		setSize(QSize size);
		void		setFileSize(int size);
		void		setFileExtension(const QString &ext);
		void		setSavePath(const QString &path);
		bool		shouldDisplaySample() const;
		QUrl		getDisplayableUrl() const;
		bool		isVideo() const;
		QString		isAnimated() const;
		void		setTags(const QList<Tag> &tags);
		bool		isGallery() const;

		// Displayable
		QColor color() const override;
		QString tooltip() const override;
		QList<QStrP> detailsData() const override;

		// Downloadable
		QString url(Size size) const override;
		void preload(const Filename &filename) override;
		QStringList paths(const Filename &filename, const QString &folder, int count) const override;
		QMap<QString, Token> generateTokens(Profile *profile) const override;
		SaveResult preSave(const QString &path) override;
		void postSave(QMap<QString, SaveResult> result, bool addMd5, bool startCommands, int count) override;

		// Templates
		template <typename T>
		T token(const QString &name) const
		{
			return tokens(m_profile).value(name).value<T>();
		}

	protected:
		void setRating(const QString &rating);

	public slots:
		void loadDetails(bool rateLimit = false);
		void loadImage(bool inMemory = true, bool force = false);
		void abortTags();
		void abortImage();
		void parseDetails();
		void unload();

	private slots:
		void finishedImageBasic();
		void finishedImageInMemory();
		void finishedImageS(bool inMemory);
		void downloadProgressImageBasic(qint64, qint64);
		void downloadProgressImageInMemory(qint64, qint64);
		void downloadProgressImageS(qint64, qint64, bool inMemory);

	signals:
		void finishedLoadingPreview();
		void finishedLoadingTags();
		void finishedImage(QNetworkReply::NetworkError, const QString &);
		void downloadProgressImage(qint64, qint64);
		void urlChanged(const QString &before, const QString &after);

	private:
		Profile			*m_profile;
		Page			*m_parent;
		qulonglong		m_id;
		int				m_score, m_parentId, m_fileSize, m_authorId;
		bool			m_hasChildren, m_hasNote, m_hasComments, m_hasScore;
		QString			m_url;
		QString	mutable m_md5;
		QString			m_author, m_name, m_status, m_rating, m_source, m_site, m_savePath;
		QUrl			m_pageUrl, m_fileUrl, m_sampleUrl, m_previewUrl;
		QSize			m_size;
		QPixmap			m_imagePreview;
		QDateTime		m_createdAt;
		QByteArray		m_data;
		QNetworkReply	*m_loadDetails, *m_loadImage;
		QList<Tag>		m_tags;
		QList<Pool>		m_pools;
		QTime			m_timer;
		QSettings		*m_settings;
		QStringList		m_search;
		Site			*m_parentSite;
		QNetworkReply::NetworkError m_loadImageError;
		ExtensionRotator *m_extensionRotator;
		bool			m_loadingPreview, m_loadingDetails, m_loadingImage, m_tryingSample, m_loadedDetails, m_loadedImage;
		bool			m_isGallery = false;
};

Q_DECLARE_METATYPE(Image)
Q_DECLARE_METATYPE(Image::SaveResult)

#endif // IMAGE_H
