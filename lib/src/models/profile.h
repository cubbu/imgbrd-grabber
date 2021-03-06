#ifndef PROFILE_H
#define PROFILE_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QSettings>
#include <QString>
#include "models/favorite.h"


class Commands;
class Site;
class Source;

class Profile : public QObject
{
	Q_OBJECT

	public:
		Profile();
		Profile(QSettings *settings, const QList<Favorite> &favorites, const QStringList &keptForLater = QStringList(), const QString &path = QString());
		explicit Profile(const QString &path);
		~Profile() override;
		void sync();

		// Temporary path
		QString tempPath() const;

		// Favorite management
		void addFavorite(const Favorite &fav);
		void removeFavorite(const Favorite &fav);
		void emitFavorite();

		// KFL management
		void addKeptForLater(const QString &tag);
		void removeKeptForLater(const QString &tag);

		// Ignore management
		void addIgnored(const QString &tag);
		void removeIgnored(const QString &tag);

		// MD5 management
		QPair<QString, QString> md5Action(const QString &md5);
		QString md5Exists(const QString &md5);
		void addMd5(const QString &md5, const QString &path);
		void setMd5(const QString &md5, const QString &path);
		void removeMd5(const QString &md5);

		// Auto-completion
		void addAutoComplete(const QString &tag);

		// Sites management
		void addSite(Site *site);
		void removeSite(Site *site);

		// Blacklist management
		void setBlacklistedTags(const QList<QStringList> &tags);
		void addBlacklistedTag(const QString &tag);
		void removeBlacklistedTag(const QString &tag);

		// Getters
		QString getPath() const;
		QSettings *getSettings() const;
		QList<Favorite> &getFavorites();
		QStringList &getKeptForLater();
		QStringList &getIgnored();
		Commands &getCommands();
		QStringList &getAutoComplete();
		QStringList &getCustomAutoComplete();
		QList<QStringList> &getBlacklist();
		const QMap<QString, Source*> &getSources() const;
		const QMap<QString, Site*> &getSites() const;
		QList<Site*> getFilteredSites(const QStringList &urls) const;

	private:
		void syncFavorites();
		void syncKeptForLater();
		void syncIgnored();

	signals:
		void favoritesChanged();
		void keptForLaterChanged();
		void ignoredChanged();
		void sitesChanged();
		void siteDeleted(Site *site);
		void blacklistChanged();

	private:
		QString 		m_path;
		QSettings		*m_settings;
		QList<Favorite>	m_favorites;
		QStringList		m_keptForLater;
		QStringList		m_ignored;
		Commands		*m_commands;
		QStringList		m_autoComplete;
		QStringList		m_customAutoComplete;
		QList<QStringList>		m_blacklistedTags;
		QHash<QString, QString>	m_md5s;
		QMap<QString, Source*>	m_sources;
		QMap<QString, Site*>	m_sites;
};

#endif // PROFILE_H
