#ifndef IMAGE_DOWNLOADER_H
#define IMAGE_DOWNLOADER_H

#include <QObject>
#include "downloader/file-downloader.h"
#include "models/image.h"


class ImageDownloader : public QObject
{
	Q_OBJECT

	public:
		ImageDownloader(QSharedPointer<Image> img, const QString &filename, const QString &path, int count, bool addMd5, bool startCommands, QObject *parent = Q_NULLPTR, bool loadTags = false, bool rotate = true);
		ImageDownloader(QSharedPointer<Image> img, const QStringList &paths, int count, bool addMd5, bool startCommands, QObject *parent = Q_NULLPTR, bool rotate = true);

	public slots:
		void save();

	protected:
		QMap<QString, Image::SaveResult> makeMap(const QStringList &keys, Image::SaveResult value);
		QMap<QString, Downloadable::SaveResult> postSaving(QMap<QString, Downloadable::SaveResult> result = QMap<QString, Downloadable::SaveResult>());

	signals:
		void downloadProgress(QSharedPointer<Image> img, qint64 v1, qint64 v2);
		void saved(QSharedPointer<Image> img, const QMap<QString, Image::SaveResult> &result);

	private slots:
		void loadedSave();
		void loadImage();
		void downloadProgressImage(qint64 v1, qint64 v2);
		void writeError();
		void networkError(QNetworkReply::NetworkError error, const QString &msg);
		void success();

	private:
		QSharedPointer<Image> m_image;
		FileDownloader m_fileDownloader;
		QString m_filename;
		QString m_path;
		bool m_loadTags;
		QStringList m_paths;
		QString m_temporaryPath;
		int m_count;
		bool m_addMd5;
		bool m_startCommands;
		bool m_writeError;
		bool m_rotate;

		QNetworkReply *m_reply = Q_NULLPTR;
		QString m_url = "";
		bool m_tryingSample = false;
};

#endif // IMAGE_DOWNLOADER_H
