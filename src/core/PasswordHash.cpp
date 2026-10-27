#include "PasswordHash.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QStringList>

namespace PasswordHash {

QByteArray pbkdf2Sha256(const QByteArray& password, const QByteArray& salt, int iterations, int keyLength)
{
    // RFC 8018, section 5.2
    QByteArray key;
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, password);
    for (quint32 block = 1; key.size() < keyLength; ++block) {
        QByteArray index(4, '\0');
        index[0] = char((block >> 24) & 0xff);
        index[1] = char((block >> 16) & 0xff);
        index[2] = char((block >> 8) & 0xff);
        index[3] = char(block & 0xff);
        mac.reset();
        mac.addData(salt);
        mac.addData(index);
        QByteArray u = mac.result();
        QByteArray t = u;
        for (int i = 1; i < iterations; ++i) {
            mac.reset();
            mac.addData(u);
            u = mac.result();
            for (int j = 0; j < t.size(); ++j)
                t[j] = char(t[j] ^ u[j]);
        }
        key.append(t);
    }
    return key.left(keyLength);
}

QString make(const QString& password, int iterations)
{
    QByteArray salt(16, '\0');
    for (int i = 0; i < salt.size(); i += 4) {
        const quint32 r = QRandomGenerator::system()->generate();
        for (int k = 0; k < 4 && i + k < salt.size(); ++k)
            salt[i + k] = char((r >> (8 * k)) & 0xff);
    }
    const QByteArray hash = pbkdf2Sha256(password.toUtf8(), salt, iterations);
    return QStringLiteral("pbkdf2-sha256$%1$%2$%3")
        .arg(iterations)
        .arg(QString::fromLatin1(salt.toBase64()), QString::fromLatin1(hash.toBase64()));
}

bool verify(const QString& password, const QString& stored)
{
    const QStringList parts = stored.split(QLatin1Char('$'));
    if (parts.size() != 4 || parts[0] != QLatin1String("pbkdf2-sha256"))
        return false;
    bool ok = false;
    const int iterations = parts[1].toInt(&ok);
    if (!ok || iterations < 1 || iterations > 10000000)
        return false;
    const QByteArray salt = QByteArray::fromBase64(parts[2].toLatin1());
    const QByteArray expected = QByteArray::fromBase64(parts[3].toLatin1());
    if (salt.isEmpty() || expected.isEmpty())
        return false;
    const QByteArray actual = pbkdf2Sha256(password.toUtf8(), salt, iterations, expected.size());
    // Constant-time comparison.
    unsigned char diff = 0;
    for (int i = 0; i < expected.size(); ++i)
        diff |= static_cast<unsigned char>(expected[i] ^ actual[i]);
    return diff == 0;
}

} // namespace PasswordHash
