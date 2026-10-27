#pragma once

#include <QByteArray>
#include <QString>

// Password hashing with PBKDF2-HMAC-SHA256 and a random per-user salt.
// Stored format: "pbkdf2-sha256$<iterations>$<salt base64>$<hash base64>".
namespace PasswordHash {

constexpr int kDefaultIterations = 120000;

QByteArray pbkdf2Sha256(const QByteArray& password, const QByteArray& salt, int iterations, int keyLength = 32);
QString make(const QString& password, int iterations = kDefaultIterations);
bool verify(const QString& password, const QString& stored);

} // namespace PasswordHash
