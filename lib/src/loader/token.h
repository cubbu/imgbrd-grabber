#ifndef TOKEN_H
#define TOKEN_H

#include <QString>
#include <QVariant>


class Token
{
	public:
		Token() = default;
		explicit Token(const QVariant &value, const QVariant &def = QVariant());
		explicit Token(const QVariant &value, const QString &whatToDoDefault, const QString &emptyDefault, const QString &multipleDefault);

		QVariant value() const;
		template <typename T> T value() const { return m_value.value<T>(); }
		QString toString() const;

		QString whatToDoDefault() const;
		QString emptyDefault() const;
		QString multipleDefault() const;

	private:
		QVariant m_value;
		QString m_whatToDoDefault;
		QString m_emptyDefault;
		QString m_multipleDefault;
};

bool operator==(const Token &lhs, const Token &rhs);
bool operator!=(const Token &lhs, const Token &rhs);

#endif // TOKEN_H
