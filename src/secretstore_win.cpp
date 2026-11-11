// SecretStore no Windows: a senha fica no Gerenciador de Credenciais (uma
// credencial genérica "omajim/<conta>"), protegida pela conta do Windows.
// Mesma interface da versão Linux (secretstore.cpp, via secret-tool).
#include "secretstore.h"

#include <windows.h>
#include <wincred.h>

static QString targetFor(const QString &account) {
    return QStringLiteral("omajim/") + account;
}

static LPCWSTR wide(const QString &text) {
    return reinterpret_cast<LPCWSTR>(text.utf16());
}

// O código do erro, lido antes de qualquer outra chamada sobrescrevê-lo.
static QString lastError() {
    return QStringLiteral("0x%1").arg(quint32(GetLastError()), 8, 16, QLatin1Char('0'));
}

SecretStore::SecretStore(QObject *parent, const QString &secretTool) : QObject(parent), m_secretTool(secretTool) {}

// As chamadas respondem depois, como as do secret-tool no Linux, e não no
// meio de quem pediu.
void SecretStore::lookup(const QString &account, LookupDone done) {
    QMetaObject::invokeMethod(this, [account, done] {
        const QString target = targetFor(account);
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(wide(target), CRED_TYPE_GENERIC, 0, &credential)) {
            if (GetLastError() == ERROR_NOT_FOUND)
                done({}, {});
            else
                done({}, tr("Couldn't read the password from the Windows Credential Manager (%1).").arg(lastError()));
            return;
        }
        const QString password = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                                                   qsizetype(credential->CredentialBlobSize));
        CredFree(credential);
        done(password, {});
    }, Qt::QueuedConnection);
}

// O Gajim não guarda nada aqui que dê para reaproveitar.
void SecretStore::lookupGajim(const QString &, LookupDone done) {
    QMetaObject::invokeMethod(this, [done] { done({}, {}); }, Qt::QueuedConnection);
}

void SecretStore::store(const QString &account, const QString &password, Done done) {
    QMetaObject::invokeMethod(this, [account, password, done] {
        const QString target = targetFor(account);
        QByteArray secret = password.toUtf8();
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(wide(target));
        credential.UserName = const_cast<LPWSTR>(wide(account));
        credential.CredentialBlobSize = DWORD(secret.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(secret.data());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        const bool written = CredWriteW(&credential, 0);
        secret.fill('\0');
        done(written ? QString()
                     : tr("The Windows Credential Manager refused to save the password (%1).").arg(lastError()));
    }, Qt::QueuedConnection);
}

void SecretStore::clear(const QString &account, Done done) {
    QMetaObject::invokeMethod(this, [account, done] {
        const QString target = targetFor(account);
        if (CredDeleteW(wide(target), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND)
            done({});
        else
            done(tr("Couldn't remove the password from the Windows Credential Manager (%1).").arg(lastError()));
    }, Qt::QueuedConnection);
}
