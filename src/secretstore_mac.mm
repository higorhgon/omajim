#include "secretstore.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

static const QString OmajimService = QStringLiteral("io.github.higorhgon.Omajim");

static NSMutableDictionary *keychainQuery(const QString &service, const QString &account) {
    return [@{(__bridge NSString *)kSecClass: (__bridge NSString *)kSecClassGenericPassword,
              (__bridge NSString *)kSecAttrService: service.toNSString(),
              (__bridge NSString *)kSecAttrAccount: account.toNSString()} mutableCopy];
}

static QString keychainError(OSStatus status) {
    NSString *description = CFBridgingRelease(SecCopyErrorMessageString(status, nullptr));
    return description ? QString::fromNSString(description) : QString::number(status);
}

static QString readPassword(NSMutableDictionary *query, OSStatus &status) {
    query[(__bridge NSString *)kSecReturnData] = @YES;
    CFTypeRef result = nullptr;
    status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
    if (status != errSecSuccess)
        return {};
    NSData *data = CFBridgingRelease(result);
    return QString::fromUtf8(static_cast<const char *>(data.bytes), qsizetype(data.length));
}

SecretStore::SecretStore(QObject *parent, const QString &) : QObject(parent) {
    m_keychain.setMaxThreadCount(1);
}

void SecretStore::lookup(const QString &account, LookupDone done) {
    lookupService(OmajimService, account, std::move(done));
}

void SecretStore::lookupGajim(const QString &account, LookupDone done) {
    lookupService(QStringLiteral("gajim"), account, std::move(done));
}

void SecretStore::lookupService(const QString &service, const QString &account, LookupDone done) {
    m_keychain.start([this, service, account, done] {
        @autoreleasepool {
            OSStatus status;
            const QString password = readPassword(keychainQuery(service, account), status);
            const QString error = status == errSecSuccess || status == errSecItemNotFound
                ? QString() : tr("Couldn't read the password from the macOS Keychain (%1).").arg(keychainError(status));
            QMetaObject::invokeMethod(this, [done, password, error] { done(password, error); }, Qt::QueuedConnection);
        }
    });
}

void SecretStore::store(const QString &account, const QString &password, Done done) {
    m_keychain.start([this, account, password, done] {
        @autoreleasepool {
            NSMutableDictionary *query = keychainQuery(OmajimService, account);
            const QByteArray utf8 = password.toUtf8();
            NSData *data = [NSData dataWithBytes:utf8.constData() length:utf8.size()];
            NSDictionary *value = @{(__bridge NSString *)kSecValueData: data};
            [query addEntriesFromDictionary:value];
            OSStatus status = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
            if (status == errSecDuplicateItem) {
                [query removeObjectForKey:(__bridge NSString *)kSecValueData];
                status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)value);
            }
            const QString error = status == errSecSuccess
                ? QString() : tr("Couldn't save the password in the macOS Keychain (%1).").arg(keychainError(status));
            QMetaObject::invokeMethod(this, [done, error] { done(error); }, Qt::QueuedConnection);
        }
    });
}

void SecretStore::clear(const QString &account, Done done) {
    m_keychain.start([this, account, done] {
        @autoreleasepool {
            const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)keychainQuery(OmajimService, account));
            const QString error = status == errSecSuccess || status == errSecItemNotFound
                ? QString() : tr("Couldn't remove the password from the macOS Keychain (%1).").arg(keychainError(status));
            QMetaObject::invokeMethod(this, [done, error] { done(error); }, Qt::QueuedConnection);
        }
    });
}
