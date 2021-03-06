#include "models/source.h"
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJSValue>
#include <QJSValueIterator>
#include <QStringList>
#include "functions.h"
#include "models/api/api.h"
#include "models/api/html-api.h"
#include "models/api/javascript-api.h"
#include "models/api/javascript-console-helper.h"
#include "models/api/javascript-grabber-helper.h"
#include "models/api/json-api.h"
#include "models/api/rss-api.h"
#include "models/api/xml-api.h"
#include "models/profile.h"
#include "models/site.h"


QString getUpdaterBaseUrl()
{
	#if defined NIGHTLY || defined QT_DEBUG
		return QStringLiteral("https://raw.githubusercontent.com/Bionus/imgbrd-grabber/develop/release/sites");
	#else
		return QStringLiteral("https://raw.githubusercontent.com/Bionus/imgbrd-grabber/master/release/sites");
	#endif
}

QJSEngine *Source::jsEngine()
{
	static QJSEngine *engine = Q_NULLPTR;

	if (engine == Q_NULLPTR)
	{
		engine = new QJSEngine();
		engine->globalObject().setProperty("Grabber", engine->newQObject(new JavascriptGrabberHelper(*engine)));
		engine->globalObject().setProperty("console", engine->newQObject(new JavascriptConsoleHelper("[JavaScript] ", engine)));

		// JavaScript helper file
		QFile jsHelper(m_dir + "/../helper.js");
		if (jsHelper.open(QFile::ReadOnly | QFile::Text))
		{
			QJSValue helperResult = engine->evaluate(jsHelper.readAll(), jsHelper.fileName());
			jsHelper.close();

			if (helperResult.isError())
			{ log(QStringLiteral("Uncaught exception at line %1: %2").arg(helperResult.property("lineNumber").toInt()).arg(helperResult.toString()), Logger::Error); }
		}
		else
		{ log(QStringLiteral("JavaScript helper file could not be opened"), Logger::Error); }
	}

	return engine;
}
QMutex *Source::jsEngineMutex()
{
	static QMutex *mutex = Q_NULLPTR;

	if (mutex == Q_NULLPTR)
	{ mutex = new QMutex(); }

	return mutex;
}

Source::Source(Profile *profile, const QString &dir)
	: m_dir(dir), m_diskName(QFileInfo(dir).fileName()), m_profile(profile), m_updater(m_diskName, m_dir, getUpdaterBaseUrl())
{
	// Load XML details for this source from its model file
	QFile file(m_dir + "/model.xml");
	if (file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QString fileContents = file.readAll();
		QDomDocument doc;
		QString errorMsg;
		int errorLine, errorColumn;
		if (!doc.setContent(fileContents, false, &errorMsg, &errorLine, &errorColumn))
		{ log(QStringLiteral("Error parsing XML file: %1 (%2 - %3).").arg(errorMsg, QString::number(errorLine), QString::number(errorColumn)), Logger::Error); }
		else
		{
			QDomElement docElem = doc.documentElement();
			QMap<QString, QString> details = domToMap(docElem);

			// Tag format mapper
			static QMap<QString, TagNameFormat::CaseFormat> caseAssoc
			{
				{ "lower", TagNameFormat::Lower },
				{ "upper_first", TagNameFormat::UpperFirst },
				{ "upper", TagNameFormat::Upper },
				{ "caps", TagNameFormat::Caps },
			};

			// Javascript models
			bool enableJs = m_profile->getSettings()->value("enableJsModels", true).toBool();
			QFile js(m_dir + "/model.js");
			if (enableJs && js.exists() && js.open(QIODevice::ReadOnly | QIODevice::Text))
			{
				log(QStringLiteral("Using Javascript model for %1").arg(m_diskName), Logger::Debug);

				QString src = "(function() { var window = {}; " + js.readAll().replace("export var source = ", "return ") + " })()";

				m_jsSource = jsEngine()->evaluate(src, js.fileName());
				if (m_jsSource.isError())
				{ log(QStringLiteral("Uncaught exception at line %1: %2").arg(m_jsSource.property("lineNumber").toInt()).arg(m_jsSource.toString()), Logger::Error); }
				else
				{
					m_name = m_jsSource.property("name").toString();

					// Get the list of APIs for this Source
					QJSValue apis = m_jsSource.property("apis");
					QJSValueIterator it(apis);
					while (it.hasNext())
					{
						it.next();
						m_apis.append(new JavascriptApi(details, m_jsSource, jsEngineMutex(), it.name()));
					}
					if (m_apis.isEmpty())
					{ log(QStringLiteral("No valid source has been found in the model.js file from %1.").arg(m_name)); }

					// Read tag naming format
					const QJSValue &tagFormat = m_jsSource.property("tagFormat");
					if (!tagFormat.isUndefined())
					{
						auto caseFormat = caseAssoc.value(tagFormat.property("case").toString(), TagNameFormat::Lower);
						m_tagNameFormat = TagNameFormat(caseFormat, tagFormat.property("wordSeparator").toString());
					}
				}

				js.close();
			}
			else
			{
				if (enableJs)
				{ log(QStringLiteral("Javascript model not found for %1").arg(m_diskName), Logger::Warning); }

				log(QStringLiteral("Using XML model for %1").arg(m_diskName), Logger::Debug);

				m_name = details.value("Name");

				// Get the list of possible API for this Source
				QStringList possibleApis = QStringList() << "Xml" << "Json" << "Rss" << "Html";
				QStringList availableApis;
				for (const QString &api : possibleApis)
					if (details.contains("Urls/" + api + "/Tags"))
						availableApis.append(api);

				if (!availableApis.isEmpty())
				{
					m_apis.reserve(availableApis.count());
					for (const QString &apiName : availableApis)
					{
						Api *api = nullptr;
						if (apiName == QLatin1String("Html"))
						{ api = new HtmlApi(details); }
						else if (apiName == QLatin1String("Json"))
						{ api = new JsonApi(details); }
						else if (apiName == QLatin1String("Rss"))
						{ api = new RssApi(details); }
						else if (apiName == QLatin1String("Xml"))
						{ api = new XmlApi(details); }

						if (api != nullptr)
						{ m_apis.append(api); }
						else
						{ log(QStringLiteral("Unknown API type '%1'").arg(apiName), Logger::Error); }
					}
				}
				else
				{ log(QStringLiteral("No valid source has been found in the model.xml file from %1.").arg(m_name)); }

				// Read tag naming format
				auto caseFormat = caseAssoc.value(details.value("TagFormat/Case", "lower"), TagNameFormat::Lower);
				m_tagNameFormat = TagNameFormat(caseFormat, details.value("TagFormat/WordSeparator", "_"));
			}
		}

		file.close();
	}
	else
	{ log(QStringLiteral("Impossible to open the model file '%1'").arg(m_dir + "/model.xml"), Logger::Warning); }

	// Get the list of all sites pertaining to this source
	QFile f(m_dir + "/sites.txt");
	if (f.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		while (!f.atEnd())
		{
			QString line = f.readLine().trimmed();
			if (line.isEmpty())
				continue;

			Site *site = new Site(line, this);
			m_sites.append(site);
		}
	}
	if (m_sites.isEmpty())
	{ log(QStringLiteral("No site for source %1").arg(m_name), Logger::Debug); }
}

Source::~Source()
{
	qDeleteAll(m_apis);
	qDeleteAll(m_sites);
}


QString Source::getName() const					{ return m_name;	}
QString Source::getPath() const					{ return m_dir;		}
const QList<Site*> &Source::getSites() const	{ return m_sites;	}
const QList<Api*> &Source::getApis() const		{ return m_apis;	}
Profile *Source::getProfile() const				{ return m_profile;	}
const SourceUpdater &Source::getUpdater() const	{ return m_updater;	}

Api *Source::getApi(const QString &name) const
{
	for (Api *api : this->getApis())
		if (api->getName() == name)
			return api;
	return nullptr;
}
