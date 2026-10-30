#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QXmlStreamWriter>
#include <QXmppBitsOfBinaryContentId.h>
#include <QXmppBitsOfBinaryIq.h>
#include <QXmppClient.h>
#include <QXmppXmlElement.h>
#include <QXmppFallback.h>
#include <QXmppMessage.h>
#include <QXmppMessageReaction.h>
#include <QXmppMucData.h>
#include <QXmppMucManagerV2.h>
#include <QXmppPepBookmarkManager.h>
#include <QXmppResultSet.h>
#include <QXmppDiscoveryManager.h>
#include <QXmppRosterIq.h>
#include <QXmppVersionManager.h>
#include <QXmppRosterManager.h>
#include <QXmppTask.h>

#include <QBuffer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QDomDocument>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTcpServer>
#include <QTcpSocket>
#include <QCryptographicHash>
#include <QImage>
#include <QImageReader>
#include <QRandomGenerator>

#include "accounts.h"
#include "avatarstore.h"
#include "backend.h"
#include "contactmodel.h"
#include "emojicatalog.h"
#include "filepicker.h"
#include "files.h"
#include "firewall.h"
#include "gajimimport.h"
#include "history.h"
#include "richtext.h"
#include "composer.h"
#include "smileys.h"
#include "messagemodel.h"
#include "rosterstore.h"
#include "secretstore.h"
#include "textformat.h"
#include "theme.h"
#include "translator.h"
#include "tray.h"

// Hands out whatever the test put in, without touching the real keyring.
class FakeSecretStore : public SecretStore {
public:
    QHash<QString, QString> passwords;

    void lookup(const QString &account, LookupDone done) override { done(passwords.value(account), {}); }
    void store(const QString &account, const QString &password, Done done) override {
        passwords.insert(account, password);
        done({});
    }
    void clear(const QString &account, Done done) override {
        passwords.remove(account);
        done({});
    }
    QHash<QString, QString> gajimPasswords;
    void lookupGajim(const QString &account, LookupDone done) override { done(gajimPasswords.value(account), {}); }
};

class FakeFilePicker : public FilePicker {
public:
    QString lastTitle;
    QString saveTo;
    void openFile(const QString &title) override { lastTitle = title; }
    void saveFile(const QString &title, const QString &) override {
        lastTitle = title;
        emit savePicked(saveTo);
    }
};

class OmajimTests : public QObject {
    Q_OBJECT

private slots:
    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeBuildsThePaletteFromTheTheme() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml",
              "accent = \"#89b4fa\"\nbackground = \"#1e1e2e\"\nforeground = '#cdd6f4' # text\nred = \"#f38ba8\"\n");
        Theme theme(dir.path());
        const QVariantMap colors = theme.colors();
        QCOMPARE(colors.value("background").value<QColor>(), QColor("#1e1e2e"));
        QCOMPARE(colors.value("error").value<QColor>(), QColor("#f38ba8"));
        // Text on the accent stays readable: dark on this light blue.
        QCOMPARE(colors.value("accentText").value<QColor>(), QColor("#111111"));
        // Surfaces sit between the background and the text.
        const QColor panel = colors.value("panel").value<QColor>();
        QVERIFY(panel != QColor("#1e1e2e") && panel.lightness() < QColor("#cdd6f4").lightness());

        // Without Omarchy, a full palette still comes out.
        QTemporaryDir empty;
        Theme fallback(empty.path());
        QCOMPARE(fallback.colors().value("accent").value<QColor>(), QColor("#FFD60A"));
        QVERIFY(fallback.colors().contains("selectionText"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void bundledCatalogTranslatesTheApp() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        Translator translator(dir.path() + "/language", QFINDTESTDATA("../src/i18n"));
        QCOMPARE(translator.translate("Main", "Sign in"), QString("Entrar"));
    }

    void fileNamesFromContactsStayInTheFolder() {
        QCOMPARE(Files::safeFileName("../../.bashrc"), QString("bashrc"));
        QCOMPARE(Files::safeFileName("C:\\Users\\x\\photo.jpg"), QString("photo.jpg"));
        QCOMPARE(Files::safeFileName("..."), QString("file"));
        QCOMPARE(Files::safeFileName("report.pdf"), QString("report.pdf"));
    }

    void receivedFilesNeverReplaceExistingOnes() {
        QTemporaryDir dir;
        QCOMPARE(Files::uniquePath(dir.path(), "a.txt"), dir.filePath("a.txt"));
        write(dir.filePath("a.txt"), "x");
        QCOMPARE(Files::uniquePath(dir.path(), "a.txt"), dir.filePath("a (2).txt"));
        write(dir.filePath("a (2).txt.part"), "x");
        QCOMPARE(Files::uniquePath(dir.path(), "a.txt"), dir.filePath("a (3).txt"));
        write(dir.filePath("README"), "x");
        QCOMPARE(Files::uniquePath(dir.path(), "README"), dir.filePath("README (2)"));
    }

    void historyKeepsConversationsApart() {
        QTemporaryDir dir;
        History history;
        QVERIFY(history.open(dir.filePath("a/history.sqlite")));

        for (int i = 0; i < 5; ++i) {
            HistoryEntry entry;
            entry.peer = "bob@example.org";
            entry.body = QString::number(i);
            entry.stamp = QDateTime::fromMSecsSinceEpoch(1000 + i, QTimeZone::UTC);
            entry.xmppId = "id" + QString::number(i);
            QVERIFY(history.add(entry) > 0);
        }
        HistoryEntry other;
        other.peer = "carol@example.org";
        other.body = "hi";
        history.add(other);

        const auto recent = history.recent("bob@example.org", 3);
        QCOMPARE(recent.size(), 3);
        QCOMPARE(recent.first().body, QString("2"));
        QCOMPARE(recent.last().body, QString("4"));
        QVERIFY(history.contains("bob@example.org", "id1"));
        QVERIFY(!history.contains("carol@example.org", "id1"));
        QVERIFY(!history.contains("bob@example.org", ""));
    }

    void historyRecognizesArchivedCopies() {
        QTemporaryDir dir;
        History history;
        QVERIFY(history.open(dir.filePath("history.sqlite")));
        HistoryEntry entry;
        entry.peer = "bob@example.org";
        entry.body = "hi";
        entry.archiveId = "arch-1";
        history.add(entry);
        // The same message back from the archive, under a different id.
        QVERIFY(history.contains("bob@example.org", "other-id", "arch-1"));
        QVERIFY(!history.contains("bob@example.org", "other-id", "arch-2"));
        QVERIFY(!history.contains("bob@example.org", "", ""));
        QCOMPARE(history.recent("bob@example.org", 1).first().archiveId, QString("arch-1"));
    }

    void historyUpgradesOlderDatabases() {
        QTemporaryDir dir;
        const QString path = dir.filePath("history.sqlite");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "old");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE messages (id INTEGER PRIMARY KEY, peer TEXT NOT NULL,"
                               " outgoing INTEGER NOT NULL, kind TEXT NOT NULL, body TEXT NOT NULL,"
                               " stamp INTEGER NOT NULL, file_path TEXT, file_size INTEGER, state TEXT,"
                               " xmpp_id TEXT)"));
            QVERIFY(query.exec("INSERT INTO messages (peer, outgoing, kind, body, stamp, xmpp_id)"
                               " VALUES ('bob@example.org', 0, 'text', 'old', 1, 'x1')"));
            db.close();
        }
        QSqlDatabase::removeDatabase("old");

        History history;
        QVERIFY2(history.open(path), qPrintable(history.errorString()));
        QCOMPARE(history.recent("bob@example.org", 5).first().body, QString("old"));
        HistoryEntry entry;
        entry.peer = "bob@example.org";
        entry.body = "new";
        entry.archiveId = "a";
        QVERIFY(history.add(entry) > 0);
    }

    void emptyBeforeAsksTheArchiveForItsNewestPage() {
        // syncArchive relies on QXmpp writing an empty <before/>, which RSM
        // reads as "the last page".
        QXmppResultSetQuery page;
        page.setMax(100);
        page.setBefore(QStringLiteral(""));
        QString xml;
        QXmlStreamWriter writer(&xml);
        page.toXml(&writer);
        QVERIFY2(xml.contains("<before/>") || xml.contains("<before></before>"), qPrintable(xml));
    }

    void avatarsAreStoredOnceAndRound() {
        QTemporaryDir dir;
        AvatarStore store(dir.filePath("avatars"));
        QImage image(300, 200, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");

        const QString hash = store.save(png);
        QCOMPARE(hash, QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha1).toHex()));
        QVERIFY(store.has(hash));
        QVERIFY(store.pathFor("bob@example.org").isEmpty());

        store.assign("bob@example.org", hash);
        const QImage saved(store.pathFor("bob@example.org"));
        QCOMPARE(saved.size(), QSize(128, 128));
        // Round: the corner is see-through, the middle is the picture.
        QCOMPARE(qAlpha(saved.pixel(0, 0)), 0);
        QCOMPARE(QColor(saved.pixel(64, 64)), QColor(Qt::red));

        // The index outlives the store, like the files do.
        AvatarStore again(dir.filePath("avatars"));
        QCOMPARE(again.hashFor("bob@example.org"), hash);
        again.clear("bob@example.org");
        QVERIFY(again.pathFor("bob@example.org").isEmpty());

        QVERIFY(store.save("not an image").isEmpty());
    }

    void ownPicturesAreSquaredAndSmall() {
        // A wide photo: its middle square, shrunk to 256, as JPEG.
        QImage photo(1200, 800, QImage::Format_RGB32);
        photo.fill(Qt::blue);
        QString type;
        const QByteArray jpeg = AvatarStore::photoFrom(photo, &type);
        QCOMPARE(type, QString("image/jpeg"));
        QImage read;
        QVERIFY(read.loadFromData(jpeg));
        QCOMPARE(read.size(), QSize(256, 256));

        // See-through and small: PNG, not made bigger.
        QImage icon(100, 140, QImage::Format_ARGB32);
        icon.fill(Qt::transparent);
        const QByteArray png = AvatarStore::photoFrom(icon, &type);
        QCOMPARE(type, QString("image/png"));
        QVERIFY(read.loadFromData(png));
        QCOMPARE(read.size(), QSize(100, 100));

        QVERIFY(AvatarStore::photoFrom(QImage(), &type).isEmpty());
    }

    void ownAvatarNeedsAConnection() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        QVERIFY(backend.ownAvatar().isEmpty());
        backend.setAvatar(dir.filePath("nothing.png"));
        QVERIFY(backend.errorText().contains("offline"));
        backend.dismissError();
        QVERIFY(backend.ownNickname().isEmpty());
        backend.setNickname("Alice");
        QVERIFY(backend.errorText().contains("offline"));
        QVERIFY(backend.ownNickname().isEmpty());
    }

    void avatarHashesAreReadEitherWay() {
        const QByteArray raw = QCryptographicHash::hash("x", QCryptographicHash::Sha1);
        const QString hex = QString::fromLatin1(raw.toHex());
        QCOMPARE(AvatarStore::normalizedHash(raw), hex);
        QCOMPARE(AvatarStore::normalizedHash(raw.toHex().toUpper()), hex);
        QVERIFY(AvatarStore::normalizedHash("short").isEmpty());
    }

    void deliveryReceiptsMarkOnlyOurMessages() {
        QTemporaryDir dir;
        History history;
        QVERIFY(history.open(dir.filePath("history.sqlite")));
        HistoryEntry sent;
        sent.peer = "bob@example.org";
        sent.outgoing = true;
        sent.body = "hi";
        sent.xmppId = "m1";
        history.add(sent);
        HistoryEntry received = sent;
        received.outgoing = false;
        received.xmppId = "m2";
        history.add(received);

        QVERIFY(history.markDelivered("bob@example.org", "m1"));
        QVERIFY(!history.markDelivered("bob@example.org", "m2"));
        QVERIFY(!history.markDelivered("carol@example.org", "m1"));
        QVERIFY(history.recent("bob@example.org", 2).first().delivered);

        MessageModel model;
        model.reset(history.recent("bob@example.org", 2));
        model.markDelivered("m2");
        QVERIFY(!model.data(model.index(1), MessageModel::DeliveredRole).toBool());
    }

    void typingShowsForTheOpenChat() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");
        QSignalSpy typing(&backend, &Backend::typingChanged);

        auto inject = [&](QXmppMessage::State state, const QString &body = {}) {
            QXmppMessage message("bob@example.org/phone", "alice@example.org", body);
            message.setType(QXmppMessage::Chat);
            message.setState(state);
            message.setId(QString::number(typing.count()) + body);
            emit backend.client()->messageReceived(message);
        };
        inject(QXmppMessage::Composing);
        QVERIFY(backend.currentChatTyping());
        // Typing notices are no messages.
        QCOMPARE(backend.messages()->rowCount(), 0);

        inject(QXmppMessage::Active, "done");
        QVERIFY(!backend.currentChatTyping());
        QCOMPARE(backend.messages()->rowCount(), 1);

        inject(QXmppMessage::Composing);
        backend.openChat("carol@example.org");
        QVERIFY(!backend.currentChatTyping());
        backend.openChat("bob@example.org");
        QVERIFY(backend.currentChatTyping());
    }

    void roomsListFirstAndKeepWhoWrote() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.contacts()->upsert("adam@example.org", "Adam");

        // A bookmark from the server brings the room into the list, on top.
        auto *bookmarks = backend.client()->findExtension<QXmppPepBookmarkManager>();
        emit bookmarks->bookmarksAdded({QXmppMucBookmark("team@rooms.example.org", "Team", false, {}, {})});
        QCOMPARE(backend.contacts()->jidAt(0), QString("team@rooms.example.org"));
        QVERIFY(backend.contacts()->data(backend.contacts()->index(0), ContactModel::IsRoomRole).toBool());

        backend.openChat("team@rooms.example.org");
        QVERIFY(backend.currentChatIsRoom());
        auto *muc = backend.client()->findExtension<QXmppMucManagerV2>();
        auto roomMessage = [](const QString &nick, const QString &id, const QString &body) {
            QXmppMessage message("team@rooms.example.org/" + nick, "alice@example.org", body);
            message.setType(QXmppMessage::GroupChat);
            message.setId(id);
            return message;
        };
        emit muc->messageReceived("team@rooms.example.org", roomMessage("bob", "r1", "hello all"));
        // Our own message, echoed back by the room.
        emit muc->messageReceived("team@rooms.example.org", roomMessage("alice", "r2", "hi"));
        // The same line again (a rejoin's history) isn't stored twice.
        emit muc->roomHistoryReceived("team@rooms.example.org", {roomMessage("bob", "r1", "hello all")});

        auto *messages = backend.messages();
        QCOMPARE(messages->rowCount(), 2);
        QCOMPARE(messages->data(messages->index(0), MessageModel::SenderRole).toString(), QString("bob"));
        QVERIFY(!messages->data(messages->index(0), MessageModel::OutgoingRole).toBool());
        QVERIFY(messages->data(messages->index(1), MessageModel::OutgoingRole).toBool());

        // A private message from someone in the room goes with the room.
        QXmppMessage whisper("team@rooms.example.org/bob", "alice@example.org", "psst");
        whisper.setType(QXmppMessage::Chat);
        whisper.setId("p1");
        emit backend.client()->messageReceived(whisper);
        QCOMPARE(messages->rowCount(), 3);
        QVERIFY(messages->data(messages->index(2), MessageModel::SenderRole).toString().startsWith("bob"));
        QVERIFY(!backend.currentChatTyping());

        // No files to rooms, and no sending while not in the room.
        QVERIFY(!backend.sendMessage("anyone?"));
    }

    void invitationsCanBeDeclined() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        auto *muc = backend.client()->findExtension<QXmppMucManagerV2>();

        QXmpp::Muc::DirectInvitation invitation;
        invitation.setJid("Team@rooms.example.org");
        invitation.setReason("standup");
        emit muc->directInvitationReceived(invitation, "bob@example.org/phone", {});
        // The same room twice is one invitation.
        emit muc->directInvitationReceived(invitation, "carol@example.org", {});
        QCOMPARE(backend.invitations().size(), 1);
        const QVariantMap first = backend.invitations().first().toMap();
        QCOMPARE(first.value("room").toString(), QString("team@rooms.example.org"));
        QCOMPARE(first.value("from").toString(), QString("bob@example.org"));

        backend.declineInvitation(0);
        QVERIFY(backend.invitations().isEmpty());
    }

    void historyKnowsTheNewestLine() {
        QTemporaryDir dir;
        History history;
        QVERIFY(history.open(dir.filePath("history.sqlite")));
        QVERIFY(!history.lastStamp("team@rooms.example.org").isValid());
        HistoryEntry entry;
        entry.peer = "team@rooms.example.org";
        entry.body = "hi";
        entry.sender = "bob";
        entry.stamp = QDateTime::fromMSecsSinceEpoch(5000, QTimeZone::UTC);
        history.add(entry);
        QCOMPARE(history.lastStamp("team@rooms.example.org"), entry.stamp);
        QCOMPARE(history.recent("team@rooms.example.org", 1).first().sender, QString("bob"));
    }

    void sharedLinksBecomeFiles() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");

        auto receive = [&](const QString &id, const QString &body, const QString &oob) {
            QXmppMessage message("bob@example.org/phone", "alice@example.org", body);
            message.setType(QXmppMessage::Chat);
            message.setId(id);
            message.setOutOfBandUrl(oob);
            emit backend.client()->messageReceived(message);
        };
        const QString link = "https://upload.example.org/abc/Relat%C3%B3rio%20final.pdf";
        receive("f1", link, link);
        // A link mentioned in a sentence stays text.
        receive("f2", "look: " + link, link);

        auto *messages = backend.messages();
        QCOMPARE(messages->rowCount(), 2);
        QCOMPARE(messages->data(messages->index(0), MessageModel::KindRole).toString(), QString("file"));
        QCOMPARE(messages->data(messages->index(0), MessageModel::BodyRole).toString(), QString("Relatório final.pdf"));
        QCOMPARE(messages->data(messages->index(0), MessageModel::StateRole).toString(), QString("remote"));
        QCOMPARE(messages->data(messages->index(1), MessageModel::KindRole).toString(), QString("text"));
    }

    void filesComeDownFromTheServer() {
        QTemporaryDir dir;
        // Downloads land in a folder of the test's own.
        QDir(dir.path()).mkpath("xdg");
        QDir(dir.path()).mkpath("downloads");
        write(dir.filePath("xdg/user-dirs.dirs"), "XDG_DOWNLOAD_DIR=\"" + dir.filePath("downloads").toUtf8() + "\"\n");
        const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", dir.filePath("xdg").toUtf8());
        auto restore = qScopeGuard([&] { qputenv("XDG_CONFIG_HOME", oldConfig); });

        // A one-file HTTP server.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray content(100000, 'x');
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, content] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(content.size()) +
                              "\r\nConnection: close\r\n\r\n" + content);
                socket->disconnectFromHost();
            });
        });

        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");
        write(dir.filePath("downloads/notes.txt"), "already here");

        const QString link = QStringLiteral("http://127.0.0.1:%1/notes.txt").arg(server.serverPort());
        QXmppMessage message("bob@example.org/phone", "alice@example.org", link);
        message.setType(QXmppMessage::Chat);
        message.setId("d1");
        message.setOutOfBandUrl(link);
        emit backend.client()->messageReceived(message);

        auto *messages = backend.messages();
        const qint64 id = messages->data(messages->index(0), MessageModel::IdRole).toLongLong();
        backend.downloadFile(id);
        QTRY_COMPARE(messages->data(messages->index(0), MessageModel::StateRole).toString(), QString("done"));
        // The file that was there stays; the new one takes the next name.
        const QString saved = messages->data(messages->index(0), MessageModel::FilePathRole).toString();
        QCOMPARE(saved, dir.filePath("downloads/notes (2).txt"));
        QFile file(saved);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
        QFile original(dir.filePath("downloads/notes.txt"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("already here"));
    }

    void interruptedDownloadsCanStartOver() {
        QTemporaryDir dir;
        History history;
        QVERIFY(history.open(dir.filePath("history.sqlite")));
        HistoryEntry entry;
        entry.peer = "bob@example.org";
        entry.kind = "file";
        entry.body = "a.pdf";
        entry.url = "https://upload.example.org/a.pdf";
        entry.state = "downloading";
        entry.filePath = "/tmp/a.pdf";
        history.add(entry);
        HistoryEntry upload = entry;
        upload.outgoing = true;
        upload.state = "uploading";
        history.add(upload);

        history.failInterruptedTransfers();
        const auto entries = history.recent("bob@example.org", 5);
        QCOMPARE(entries.at(0).state, QString("remote"));
        QVERIFY(entries.at(0).filePath.isEmpty());
        QCOMPARE(entries.at(0).url, entry.url);
        QCOMPARE(entries.at(1).state, QString("failed"));
    }

    void correctionsReplaceTheirLine() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");

        auto receive = [&](const QString &from, const QString &id, const QString &body, const QString &replaces = {}) {
            QXmppMessage message(from, "alice@example.org", body);
            message.setType(QXmppMessage::Chat);
            message.setId(id);
            message.setReplaceId(replaces);
            emit backend.client()->messageReceived(message);
        };
        receive("bob@example.org/phone", "m1", "see you at 10");
        receive("bob@example.org/phone", "m2", "see you at 11", "m1");
        // A second correction may point at the first one.
        receive("bob@example.org/phone", "m3", "see you at 11:30", "m2");

        auto *messages = backend.messages();
        QCOMPARE(messages->rowCount(), 1);
        QCOMPARE(messages->data(messages->index(0), MessageModel::BodyRole).toString(), QString("see you at 11:30"));
        QVERIFY(messages->data(messages->index(0), MessageModel::EditedRole).toBool());

        // Nobody can rewrite someone else's line: our carbon "correcting"
        // Bob's message stays a line of its own.
        QXmppMessage forged("alice@example.org/laptop", "bob@example.org", "hijacked");
        forged.setType(QXmppMessage::Chat);
        forged.setId("m4");
        forged.setReplaceId("m1");
        emit backend.client()->messageReceived(forged);
        QCOMPARE(messages->data(messages->index(0), MessageModel::BodyRole).toString(), QString("see you at 11:30"));
        QCOMPARE(messages->rowCount(), 2);

        // That carbon is our last line now, so Up offers it for correcting.
        QCOMPARE(backend.startEditingLast(), QString("hijacked"));
        QVERIFY(backend.editing());
        backend.cancelEditing();
        QVERIFY(!backend.editing());

        // Bob's line last again: nothing of ours to correct.
        receive("bob@example.org/phone", "m5", "ok");
        QVERIFY(backend.startEditingLast().isEmpty());
    }

    void anyRecentLineOfOursCanBeEdited() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");
        auto send = [&](const QString &from, const QString &to, const QString &id, const QString &body,
                        const QDateTime &stamp = {}) {
            QXmppMessage message(from, to, body);
            message.setType(QXmppMessage::Chat);
            message.setId(id);
            if (stamp.isValid())
                message.setStamp(stamp);
            emit backend.client()->messageReceived(message);
        };
        // Ours from another device two hours ago, ours just now, then Bob's.
        send("alice@example.org/laptop", "bob@example.org", "old", "too late", QDateTime::currentDateTimeUtc().addSecs(-7200));
        send("alice@example.org/laptop", "bob@example.org", "mine", "typo hree");
        send("bob@example.org/phone", "alice@example.org", "his", "ok");
        auto *messages = backend.messages();
        QCOMPARE(messages->rowCount(), 3);
        auto id = [&](int row) { return messages->data(messages->index(row), MessageModel::IdRole).toLongLong(); };

        // Not the last line, yet ours and recent: it can be edited.
        QVERIFY(backend.canEdit(id(1)));
        QVERIFY(!backend.canEdit(id(0)));
        QVERIFY(!backend.canEdit(id(2)));
        QVERIFY(backend.startEditing(id(2)).isEmpty());
        QVERIFY(!backend.editing());

        QCOMPARE(backend.startEditing(id(1)), QString("typo hree"));
        QVERIFY(backend.editing());
        QCOMPARE(backend.editingText(), QString("typo hree"));
        // Another conversation drops it.
        backend.openChat("carol@example.org");
        QVERIFY(!backend.editing());
    }

    void destinationsFollowTheSidebarOrder() {
        ContactModel model;
        model.upsert("zoe@example.org", "Zoe");
        model.upsert("adam@example.org", "Adam");
        model.upsert("bea@example.org", "Bea");
        const QDateTime now = QDateTime::currentDateTimeUtc();
        model.setActivity("adam@example.org", now.addSecs(-60), "older", false);
        model.setActivity("zoe@example.org", now, "newer", true);
        auto jids = [](const QVariantList &list) {
            QStringList out;
            for (const QVariant &item : list)
                out << item.toMap().value("jid").toString();
            return out;
        };
        // Nothing typed: the open conversations, newest first.
        QCOMPARE(jids(model.destinations("")), (QStringList{"zoe@example.org", "adam@example.org"}));
        // Typed: anyone, the sidebar's own search untouched.
        QCOMPARE(jids(model.destinations("bea")), QStringList{"bea@example.org"});
        QCOMPARE(model.filter(), QString());
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.destinations("adam").first().toMap().value("name").toString(), QString("Adam"));
    }

    void rosterIsKeptBetweenRuns() {
        QTemporaryDir dir;
        History history;
        QVERIFY(history.open(dir.filePath("history.sqlite")));
        RosterStore store(&history);

        QXmppRosterIq::Item bob;
        bob.setBareJid("Bob@example.org");
        bob.setName("Bob");
        bob.setGroups({"Team", "Friends"});
        bob.setSubscriptionType(QXmppRosterIq::Item::Both);
        QXmppRosterIq::Item carol;
        carol.setBareJid("carol@example.org");
        carol.setSubscriptionType(QXmppRosterIq::Item::To);
        store.replaceAll("v1", {bob, carol});
        store.removeItem("v2", "carol@example.org");
        bob.setName("Bobby");
        store.upsertItem("v3", bob);

        QXmppRosterStorage::RosterCache cache;
        store.load().then(&m_taskContext, [&](QXmppRosterStorage::RosterCache &&loaded) { cache = loaded; });
        QCOMPARE(cache.version, QString("v3"));
        QCOMPARE(cache.items.size(), size_t(1));
        QCOMPARE(cache.items.front().bareJid(), QString("bob@example.org"));
        QCOMPARE(cache.items.front().name(), QString("Bobby"));
        QCOMPARE(cache.items.front().subscriptionType(), QXmppRosterIq::Item::Both);
        QCOMPARE(cache.items.front().groups(), QSet<QString>({"Team", "Friends"}));

        store.clear();
        QVERIFY(history.rosterVersion().isEmpty());
        QVERIFY(history.roster().isEmpty());
    }

    void cachedContactsShowBeforeConnecting() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        secrets.passwords.insert("alice@example.org", "secret");
        {
            History history;
            QVERIFY(history.open(dir.filePath("data/accounts/alice@example.org/history.sqlite")));
            history.replaceRoster("v1", {RosterRow{"bob@example.org", "Bob", {}, "both", false}});
        }
        QDir().mkpath(dir.filePath("config"));
        write(dir.filePath("config/omajim.conf"), "[account]\njid=alice@example.org\nhost=127.0.0.1:1\nstatus=offline\n");

        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.start();
        QVERIFY(backend.signedIn());
        QCOMPARE(backend.connectionState(), QString("offline"));
        // No conversation with Bob yet, so he shows in a search.
        backend.contacts()->setFilter("bob");
        QCOMPARE(backend.contacts()->rowCount(), 1);
        QCOMPARE(backend.contacts()->data(backend.contacts()->index(0), ContactModel::NameRole).toString(), QString("Bob"));
    }

    void messagesGetSafeClickableLinks() {
        const QString html = TextFormat::toHtml("see https://example.org/a?b=1&c=2, or www.omarchy.org.\n<b>hi</b>", "#123456");
        QVERIFY(html.contains("<a href=\"https://example.org/a?b=1&amp;c=2\" style=\"color:#123456\">"));
        // The comma and the full stop end the sentence, not the link.
        QVERIFY(html.contains(">https://example.org/a?b=1&amp;c=2</a>, or"));
        QVERIFY(html.contains("<a href=\"https://www.omarchy.org\""));
        QVERIFY(html.contains("&lt;b&gt;hi&lt;/b&gt;"));
        QVERIFY(html.contains("<br>"));
        QVERIFY(!TextFormat::toHtml("javascript:alert(1)", "#000").contains("<a"));

        QVERIFY(TextFormat::isOpenableLink("https://example.org"));
        QVERIFY(TextFormat::isOpenableLink("xmpp:bob@example.org"));
        QVERIFY(!TextFormat::isOpenableLink("file:///etc/passwd"));
        QVERIFY(!TextFormat::isOpenableLink("javascript:alert(1)"));
    }

    void messagesShowTheirStyling() {
        auto html = [](const QString &text) { return TextFormat::toHtml(text, "#1", "#2"); };
        QCOMPARE(html("*bold* _italic_ ~gone~ `a*b*c`"), QString("<b>bold</b> <i>italic</i> <s>gone</s> <code>a*b*c</code>"));
        QCOMPARE(html("*bold _and italic_*"), QString("<b>bold <i>and italic</i></b>"));
        // Not styling: inside words, links, and loose directives.
        QCOMPARE(html("file_name_here and 2*3*4"), QString("file_name_here and 2*3*4"));
        QVERIFY(html("https://example.org/a_b_c").contains(">https://example.org/a_b_c</a>"));
        QCOMPARE(html("* not bold *"), QString("* not bold *"));
        // Blocks: code as it is, quotes in the quote color.
        QCOMPARE(html("```\n*x* <y>\n```\nafter"), QString("<pre>*x* &lt;y&gt;</pre>after"));
        QCOMPARE(html("> said\nreply"), QString("<span style=\"color:#2\">│ said</span><br>reply"));
        // Still escaped inside styling.
        QCOMPARE(html("*<b>*"), QString("<b>&lt;b&gt;</b>"));
    }

    void accountsRunTogetherEachWithItsOwnHistory() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        // One account from before several: it becomes the list, with its server.
        {
            QSettings old(dir.filePath("config/omajim.conf"), QSettings::IniFormat);
            old.setValue("account/jid", "alice@example.org");
            old.setValue("account/host", "127.0.0.1:1");
            old.setValue("account/status", "offline");
        }
        secrets.passwords.insert("alice@example.org", "secret");
        secrets.passwords.insert("carol@example.org", "other");
        {
            Accounts accounts(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
            accounts.start();
            QCOMPARE(accounts.list().size(), 1);
            QCOMPARE(accounts.current()->jid(), QString("alice@example.org"));
            QCOMPARE(accounts.current()->host(), QString("127.0.0.1:1"));
            QVERIFY(accounts.current()->signedIn());
            // Alone, it can't be turned off.
            accounts.setEnabled("alice@example.org", false);
            QVERIFY(accounts.current()->enabled());
        }
        {
            QSettings settings(dir.filePath("config/omajim.conf"), QSettings::IniFormat);
            settings.setValue("accounts/list", QStringList{"alice@example.org", "carol@example.org"});
            settings.setValue("hosts/carol@example.org", "127.0.0.1:2");
        }

        // Both signed in at once, each with its own conversations.
        Accounts accounts(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        accounts.start();
        QCOMPARE(accounts.backends().size(), 2);
        Backend *alice = accounts.backends().at(0);
        Backend *carol = accounts.backends().at(1);
        QVERIFY(alice->signedIn() && carol->signedIn());
        QCOMPARE(accounts.current(), alice);
        QXmppMessage message("bob@example.org/phone", "alice@example.org", "for alice");
        message.setType(QXmppMessage::Chat);
        message.setId("a1");
        emit alice->client()->messageReceived(message);
        QVERIFY(alice->contacts()->contains("bob@example.org"));
        QVERIFY(!carol->contacts()->contains("bob@example.org"));
        QCOMPARE(accounts.totalUnread(), 1);
        QCOMPARE(accounts.list().at(0).toMap().value("unread").toInt(), 1);

        // Moving between them, by name or by place (Ctrl+2).
        QSignalSpy current(&accounts, &Accounts::currentChanged);
        accounts.selectIndex(1);
        QCOMPARE(accounts.current(), carol);
        accounts.select("alice@example.org");
        QCOMPARE(accounts.current(), alice);
        QCOMPARE(current.count(), 2);

        // A notification brings up its own account and conversation.
        accounts.select("alice@example.org");
        emit carol->raiseRequested();
        QCOMPARE(accounts.current(), carol);
        accounts.select("alice@example.org");

        // One off: kept, offline, remembered; the last one on stays on.
        accounts.setEnabled("carol@example.org", false);
        QVERIFY(!carol->enabled());
        accounts.setEnabled("alice@example.org", false);
        QVERIFY(alice->enabled());
        accounts.setEnabled("carol@example.org", true);

        // A password not kept: asked for again once it's turned back on.
        accounts.configure("carol@example.org", "127.0.0.1:3", false);
        QCOMPARE(carol->host(), QString("127.0.0.1:3"));
        QVERIFY(!secrets.passwords.contains("carol@example.org"));
        accounts.setEnabled("carol@example.org", false);
        QVERIFY(carol->signedIn());
        accounts.setEnabled("carol@example.org", true);
        QVERIFY(!carol->signedIn());
        QCOMPARE(accounts.current(), carol);
        carol->signIn("carol@example.org", "other", "127.0.0.1:3", true);
        QVERIFY(carol->signedIn() && carol->savePassword());
        QCOMPARE(secrets.passwords.value("carol@example.org"), QString("other"));
        accounts.select("alice@example.org");

        // Our status goes to every account.
        accounts.setStatus("dnd");
        QCOMPARE(alice->status(), QString("dnd"));
        QCOMPARE(carol->status(), QString("dnd"));
        accounts.setStatus("offline");

        // Adding one: a sign-in of its own, and back if called off.
        accounts.addAccount();
        QVERIFY(accounts.adding());
        QVERIFY(accounts.current()->jid().isEmpty());
        QVERIFY(!accounts.current()->signedIn());
        accounts.cancelAdding();
        QVERIFY(!accounts.adding());
        QCOMPARE(accounts.current(), alice);

        // Signing out: off the list and the keyring, the other one shows.
        accounts.signOut("alice@example.org");
        QCOMPARE(accounts.backends().size(), 1);
        QCOMPARE(accounts.current(), carol);
        QVERIFY(!secrets.passwords.contains("alice@example.org"));
        QSettings settings(dir.filePath("config/omajim.conf"), QSettings::IniFormat);
        QCOMPARE(settings.value("accounts/list").toStringList(), QStringList{"carol@example.org"});
        // The last one too: signing in again.
        accounts.signOut("carol@example.org");
        QVERIFY(accounts.current());
        QVERIFY(!accounts.current()->signedIn());
        QVERIFY(accounts.list().isEmpty());
    }

    void preferencesAreKept() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        {
            Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
            QCOMPARE(backend.awayMinutes(), 5);
            QCOMPARE(backend.xaMinutes(), 15);
            QVERIFY(backend.notifications());
            QVERIFY(backend.textSmileys());
            QSignalSpy idle(&backend, &Backend::idleTimesChanged);
            backend.setAwayMinutes(10);
            backend.setXaMinutes(0);
            QCOMPARE(idle.count(), 2);
            backend.setNotifications(false);
            backend.setTextSmileys(false);
            QCOMPARE(backend.closeAction(), QString("tray"));
            backend.setCloseAction("sideways");
            QCOMPARE(backend.closeAction(), QString("tray"));
            backend.setCloseAction("minimize");
            // Nothing synced yet: no period is here already.
            QVERIFY(!backend.historyReaches(30));
            QCOMPARE(backend.formatMessage(":) oi", "#1", "#2"), QString(":) oi"));
        }
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        QCOMPARE(backend.awayMinutes(), 10);
        QCOMPARE(backend.xaMinutes(), 0);
        QVERIFY(!backend.notifications());
        QVERIFY(!backend.textSmileys());
        QCOMPARE(backend.closeAction(), QString("minimize"));
    }

    void pidginTextSmileysBecomeEmoji() {
        auto html = [](const QString &text) { return TextFormat::toHtml(text, "#1", "#2"); };
        QCOMPARE(html(":) oi :-D, <3 e :beer:"), QString("🙂 oi 😃, ❤️ e 🍺"));
        QCOMPARE(html("O:-) \\m/"), QString("😇 🤘"));
        // Only standing apart, and never in code or links.
        QCOMPARE(html("a:) e (x:P) 10:30"), QString("a:) e (x:P) 10:30"));
        QCOMPARE(html("`:)`"), QString("<code>:)</code>"));
        QVERIFY(!html("https://example.org/:-D").contains("😃"));
        // Addresses with many parameters, where Pidgin found smileys.
        const QString address = "https://example.org/busca?q=a;)&b=:D&c=8-)&d=:P&e=:-*";
        QVERIFY(!html(address + " :)").contains("😃"));
        QVERIFY(html(address + " :)").endsWith(" 🙂"));
        QVERIFY(html(address).contains(QString(address).replace("&", "&amp;") + "</a>"));
        // Nor a custom smiley inside an address.
        const QString custom = TextFormat::toHtml("https://example.org/?x=:gato: :gato:", "#1", "#2", {{":gato:", "<img>"}});
        QCOMPARE(custom.count("<img>"), 1);
    }

    void formattedMessagesGoOutForGajimAndPidgin() {
        using RichText::Run;
        auto run = [](const QString &text, auto setup) {
            Run r;
            r.text = text;
            setup(r);
            return r;
        };
        auto none = [](Run &) {};
        // Plain text: no XHTML-IM.
        auto plain = RichText::compose({run("just text", none)});
        QCOMPARE(plain.body, QString("just text"));
        QVERIFY(plain.xhtml.isEmpty());

        // Markers for Gajim, at the words' edges; Pidgin's tags and spans.
        auto message = RichText::compose({run("a ", none), run("bold ", [](Run &r) { r.bold = true; }),
                                          run("red", [](Run &r) { r.bold = true; r.color = "#ff0000"; }),
                                          run(" and ", none),
                                          run("marked", [](Run &r) { r.background = "#ffff00"; r.underline = true; }),
                                          run(" f(x)", [](Run &r) { r.code = true; })});
        QCOMPARE(message.body, QString("a *bold red* and marked `f(x)`"));
        QCOMPARE(message.xhtml, QString("<p>a <strong>bold </strong><span style=\"color: #ff0000;\"><strong>red</strong></span> and "
                                        "<span style=\"background: #ffff00; text-decoration: underline;\">marked</span>"
                                        "<span style=\"font-family: monospace;\"> f(x)</span></p>"));

        // A code block keeps its lines, fenced; custom smileys as pictures, not in links.
        auto block = RichText::compose({run("veja :gato: https://x.org/:gato:\n", [](Run &r) { r.italic = true; }),
                                        run("int a;\nint b;", [](Run &r) { r.block = true; })},
                                       {{":gato:", "sha1+1@bob.xmpp.org"}});
        QCOMPARE(block.body, QString("_veja :gato: https://x.org/:gato:_\n```\nint a;\nint b;\n```"));
        QVERIFY(block.xhtml.contains("<em>veja <img alt=\":gato:\" src=\"cid:sha1+1@bob.xmpp.org\"/> https://x.org/:gato:</em>"));
        QVERIFY(block.xhtml.endsWith("<br/><span style=\"font-family: monospace;\">int a;</span><br/>"
                                     "<span style=\"font-family: monospace;\">int b;</span></p>"));
    }

    void theMessageFieldFormatsAsPidginsToolbar() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nTextEdit { textFormat: TextEdit.RichText }", QUrl());
        std::unique_ptr<QObject> edit(component.create());
        QVERIFY2(edit, qPrintable(component.errorString()));
        auto *document = edit->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(document);
        Composer composer;
        // Text goes in as text, even what looks like markup.
        composer.setPlainText(document, "ola <b>mundo</b>");
        QCOMPARE(composer.plainText(document), QString("ola <b>mundo</b>"));
        composer.setPlainText(document, "ola mundo bonito");
        composer.toggle(document, 4, 9, "bold");
        QVERIFY(composer.formatOf(document, 4, 9).value("bold").toBool());
        QVERIFY(!composer.formatOf(document, 0, 9).value("bold").toBool());
        composer.setColor(document, 4, 9, "#e01b24", false);
        composer.setColor(document, 0, 3, "#fff3a0", true);
        composer.toggle(document, 10, 16, "underline");

        QVariantList runs = composer.runs(document);
        QList<RichText::Run> parts;
        for (const QVariant &value : runs) {
            const QVariantMap map = value.toMap();
            RichText::Run run;
            run.text = map.value("text").toString();
            run.bold = map.value("bold").toBool();
            run.underline = map.value("underline").toBool();
            run.color = map.value("color").toString();
            run.background = map.value("background").toString();
            parts << run;
        }
        const auto message = RichText::compose(parts);
        QCOMPARE(message.body, QString("ola *mundo* bonito"));
        QVERIFY2(message.xhtml.contains("<span style=\"color: #000000; background: #fff3a0;\">ola</span>"), qPrintable(message.xhtml));
        QVERIFY(message.xhtml.contains("<span style=\"color: #e01b24;\"><strong>mundo</strong></span>"));
        QVERIFY(message.xhtml.contains("<span style=\"text-decoration: underline;\">bonito</span>"));

        // Taking the background off takes the dark letters it brought; again
        // on bold, bold comes off.
        composer.setColor(document, 0, 3, "", true);
        composer.toggle(document, 4, 9, "bold");
        runs = composer.runs(document);
        QCOMPARE(runs.first().toMap().value("color").toString(), QString());
        QVERIFY(!composer.formatOf(document, 4, 9).value("bold").toBool());
        // A code block, and back to plain.
        composer.setPlainText(document, "veja\nint a;");
        composer.toggleBlock(document, 6, 6);
        runs = composer.runs(document);
        QVERIFY(!runs.first().toMap().value("block").toBool());
        QVERIFY(runs.last().toMap().value("block").toBool());
        composer.clearFormat(document, 0, 11);
        QVERIFY(!composer.runs(document).last().toMap().value("block").toBool());
    }

    void pidginFormattingShowsReadably() {
        const QColor bubble("#2a2b3d"), foreground("#e4e4e7");
        auto html = [&](const QString &xhtml) {
            return RichText::toHtml(xhtml, "#88f", {{"sha1+1@bob.xmpp.org", "<img src=\"file:///s.png\">"}}, true, bubble, foreground);
        };
        // As Pidgin sends it: spans with CSS, <br/>, text smileys still turned.
        const QString pidgin = html("<p><span style='font-weight: bold;'>oi</span> <span style='color: #ff0000;'>:)</span>"
                                    "<br/><em>it</em> <span style='text-decoration: underline;'>u</span></p>");
        QVERIFY2(pidgin.contains("<span style=\"font-weight:600\">oi</span>"), qPrintable(pidgin));
        QVERIFY(pidgin.contains("🙂"));
        QVERIFY(pidgin.contains("<br><span style=\"font-style:italic\">it</span>"));
        QVERIFY(pidgin.contains("text-decoration:underline"));
        // Black on the dark bubble comes up; a background brings dark text.
        const QString dark = html("<p><span style='color: #000000;'>escuro</span></p>");
        const QColor shown(QRegularExpression("color:(#[0-9a-f]{6})").match(dark).captured(1));
        QVERIFY(shown.lightness() > 120);
        const QString marked = html("<p><span style='background: #ffff00;'>marcado</span></p>");
        QVERIFY2(marked.contains("background-color:#ffff00"), qPrintable(marked));
        QVERIFY2(QColor(QRegularExpression("color:(#[0-9a-f]{6})").match(marked).captured(1)).lightness() < 100, qPrintable(marked));
        // Nothing else gets through: no script, no local links, no outside pictures.
        const QString hostile = html("<p><script>alert(1)</script><a href='file:///etc/passwd'>x</a>"
                                     "<img src='https://evil/x.png' alt='pic'/><img src='cid:sha1+1@bob.xmpp.org' alt=':g:'/>"
                                     "<span style='position:fixed' onclick='y'>&lt;b&gt;</span></p>");
        QVERIFY(!hostile.contains("alert"));
        QVERIFY(!hostile.contains("/etc/passwd"));
        QVERIFY(!hostile.contains("evil"));
        QVERIFY(hostile.contains("pic"));
        QVERIFY(hostile.contains("<img src=\"file:///s.png\">"));
        QVERIFY(!hostile.contains("onclick"));
        QVERIFY(hostile.contains("&lt;b&gt;"));
        // Links are still links, and unreadable or empty XHTML gives way to the body.
        QVERIFY(html("<p>see https://example.org</p>").contains("<a href=\"https://example.org\""));
        QVERIFY(html("<p>broken <b></p>").isEmpty());
        QVERIFY(html("<p> </p>").isEmpty());
    }

    void forwardedLinesShowWhoWroteThem() {
        // The header, up to the line break (or space) after it.
        const QString line = "[28/10/2026 10:20] Karen Fernanda de Moraes:\nele na intranet https://example.org/?a=1";
        QCOMPARE(TextFormat::forwardedHeaderLength(line), qsizetype(QString("[28/10/2026 10:20] Karen Fernanda de Moraes:\n").size()));
        QVERIFY(TextFormat::forwardedHeaderLength("[10/28/26, 10:20 AM] Bob: hi") > 0);
        QCOMPARE(TextFormat::forwardedHeaderLength("[nota] Bob: hi"), qsizetype(0));
        // Only a forwarded line gets the header set apart: a pasted copy is text.
        QVERIFY(TextFormat::toHtml(line, "#1", "#2", {}, true, true)
                    .startsWith("<span style=\"font-weight:600\">[28/10/2026 10:20] Karen Fernanda de Moraes:</span><br>ele na intranet <a"));
        QVERIFY(!TextFormat::toHtml(line, "#1", "#2").contains("font-weight"));
        // Forwarded before the line break: the header still on its own line.
        QVERIFY(TextFormat::toHtml("[28/10/2026 10:20] Karen: oi", "#1", "#2", {}, true, true).endsWith("Karen:</span><br>oi"));

        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");
        // Omajim's mark says so, not the text.
        QXmppMessage forwarded("bob@example.org/pc", "alice@example.org", line);
        forwarded.setType(QXmppMessage::Chat);
        forwarded.setId("f1");
        forwarded.extensions().add(QXmpp::Xml::Element("forwarded", "https://github.com/higorhgon/omajim/forwarded"));
        // Through the wire and back, as another Omajim gets it.
        QByteArray xml;
        QXmlStreamWriter writer(&xml);
        forwarded.toXml(&writer);
        QDomDocument document;
        QVERIFY(bool(document.setContent(xml, QDomDocument::ParseOption::UseNamespaceProcessing)));
        QXmppMessage received;
        received.parse(document.documentElement());
        emit backend.client()->messageReceived(received);
        QXmppMessage pasted("bob@example.org/pc", "alice@example.org", line);
        pasted.setType(QXmppMessage::Chat);
        pasted.setId("f2");
        emit backend.client()->messageReceived(pasted);
        QCOMPARE(backend.messages()->rowCount(), 2);
        QVERIFY(backend.messages()->data(backend.messages()->index(0), MessageModel::ForwardedRole).toBool());
        QVERIFY(!backend.messages()->data(backend.messages()->index(1), MessageModel::ForwardedRole).toBool());
        // Kept in the history.
        backend.openChat("carol@example.org");
        backend.openChat("bob@example.org");
        QVERIFY(backend.messages()->data(backend.messages()->index(0), MessageModel::ForwardedRole).toBool());
    }

    void customSmileysTravelAsPidginSendsThem() {
        QTemporaryDir dir;
        SmileyStore store(dir.filePath("smileys"));
        auto picture = [&](const QString &name, int side, bool noisy) {
            QImage image(side, side, QImage::Format_ARGB32);
            QRandomGenerator noise(7);
            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x)
                    image.setPixel(x, y, noisy ? (noise.generate() | 0xff000000) : qRgba(255, 0, 0, 255));
            const QString path = dir.filePath(name);
            image.save(path, "PNG");
            return path;
        };
        QVERIFY(store.add(":gato:", picture("gato.png", 24, false)).isEmpty());
        QVERIFY(!store.add(":gato:", picture("other.png", 24, false)).isEmpty());
        QVERIFY(!store.add("dois gatos", picture("other.png", 24, false)).isEmpty());
        QVERIFY(!store.add(":x:", dir.filePath("missing.png")).isEmpty());
        // Too big to fetch in one stanza (Prosody takes 256 KB): a still
        // picture is shrunk to a sticker's size...
        QVERIFY(QFileInfo(picture("big.png", 400, true)).size() > 150 * 1024);
        QVERIFY(store.add(":big:", dir.filePath("big.png")).isEmpty());
        QCOMPARE(store.own().size(), 2);
        // ...an animation, which would stop, is turned down; a smaller one
        // goes as it is, still moving.
        auto animated = [&](const QString &name, int padding) {
            // Two frames of one pixel, padded with a comment to the size wanted.
            QByteArray data = QByteArray("GIF89a\x01\x00\x01\x00\x80\x00\x00\x00\x00\x00\xff\xff\xff", 19);
            data += QByteArray("\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00", 19);
            const QByteArray frame("\x21\xf9\x04\x00\x0a\x00\x00\x00\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02\x44\x01\x00", 23);
            data += frame + frame;
            data += QByteArray("\x21\xfe", 2);
            for (int left = padding; left > 0; left -= 255)
                data += char(std::min(left, 255)) + QByteArray(std::min(left, 255), 'x');
            data += QByteArray("\x00\x3b", 2);
            QFile file(dir.filePath(name));
            file.open(QIODevice::WriteOnly);
            file.write(data);
            return file.fileName();
        };
        const QString heavy = animated("heavy.gif", 160 * 1024);
        QImageReader moving(heavy);
        QVERIFY(moving.supportsAnimation() && moving.imageCount() == 2);
        QVERIFY(store.add(":pesado:", heavy).contains("150"));
        const QString light = animated("light.gif", 1000);
        QVERIFY(store.add(":leve:", light).isEmpty());
        QFile lightFile(light);
        lightFile.open(QIODevice::ReadOnly);
        QCOMPARE(store.dataOf(store.findOwn(":leve:").value(":leve:"), nullptr), lightFile.readAll());
        store.remove(":leve:");
        QCOMPARE(store.own().size(), 2);

        const QHash<QString, QString> found = store.findOwn("olha :gato: e :big:");
        QCOMPARE(found.size(), 2);
        QVERIFY(found.value(":gato:").startsWith("sha1+"));
        QVERIFY(found.value(":gato:").endsWith("@bob.xmpp.org"));
        QString type;
        const QByteArray big = store.dataOf(found.value(":big:"), &type);
        QCOMPARE(type, QString("image/png"));
        QCOMPARE(QImage::fromData(big).size(), QSize(160, 160));
        QVERIFY(store.isOwn(found.value(":big:")));

        // In an address it's part of it, not a smiley.
        QVERIFY(store.findOwn("https://example.org/?a=:gato:&b=1").isEmpty());
        QCOMPARE(SmileyStore::xhtmlFor("https://example.org/?a=:gato: :gato:", found).count("<img"), 1);

        // What goes out, and what comes back from Pidgin: the same pictures.
        const QString xhtml = SmileyStore::xhtmlFor("a<b :gato:\nfim", {{":gato:", found.value(":gato:")}});
        QCOMPARE(xhtml, QString("<p>a&lt;b <img alt=\":gato:\" src=\"cid:%1\"/><br/>fim</p>").arg(found.value(":gato:")));
        QXmppMessage sent({}, "bob@example.org", "a<b :gato:\nfim");
        sent.setXhtml(xhtml);
        QByteArray xml;
        QXmlStreamWriter writer(&xml);
        sent.toXml(&writer);
        QDomDocument document;
        QVERIFY(bool(document.setContent(xml, QDomDocument::ParseOption::UseNamespaceProcessing)));
        QXmppMessage received;
        received.parse(document.documentElement());
        QCOMPARE(SmileyStore::imagesIn(received.xhtml()), (QHash<QString, QString>{{":gato:", found.value(":gato:")}}));

        // The request for a picture, and the answer, as Pidgin reads them.
        QXmppBitsOfBinaryIq request;
        request.setType(QXmppIq::Get);
        request.setCid(QXmppBitsOfBinaryContentId::fromContentId(found.value(":gato:")));
        QByteArray requestXml;
        QXmlStreamWriter requestWriter(&requestXml);
        request.toXml(&requestWriter);
        QVERIFY2(requestXml.contains("<data xmlns=\"urn:xmpp:bob\" cid=\"" + found.value(":gato:").toUtf8() + "\"></data>"), requestXml);

        // A picture must match its cid's hash.
        const QByteArray gif = "GIF89a not really";
        QVERIFY(!store.store(SmileyStore::cidOf("something else"), gif));
        QVERIFY(store.store(SmileyStore::cidOf(gif), gif));

        store.remove(":big:");
        QCOMPARE(store.own().size(), 1);
        QVERIFY(!store.isOwn(found.value(":big:")));
        SmileyStore again(dir.filePath("smileys"));
        QCOMPARE(again.own().size(), 1);
    }

    void receivedCustomSmileysShowOnceThePictureIsHere() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");

        QImage image(20, 20, QImage::Format_RGB32);
        image.fill(Qt::green);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        const QString cid = SmileyStore::cidOf(png);

        // As Pidgin sends it: the text keeps the shortcut.
        QXmppMessage message("bob@example.org/pidgin", "alice@example.org", "oi :sapo:");
        message.setType(QXmppMessage::Chat);
        message.setId("p1");
        message.setXhtml(QString("<p>oi <img alt=':sapo:' src='cid:%1'/></p>").arg(cid));
        emit backend.client()->messageReceived(message);
        QCOMPARE(backend.messages()->rowCount(), 1);
        const QVariantMap smileys = backend.messages()->data(backend.messages()->index(0), MessageModel::SmileysRole).toMap();
        QCOMPARE(smileys.value(":sapo:").toString(), cid);
        // Until the picture comes, the text.
        QVERIFY(!backend.formatMessage("oi :sapo:", "#1", "#2", smileys).contains("<img"));
        QVERIFY(backend.smileys()->store(cid, png));
        const QString html = backend.formatMessage("oi :sapo:", "#1", "#2", smileys);
        QVERIFY(html.contains("<img"));
        QVERIFY(html.contains("width=\"20\""));

        // The history keeps them.
        backend.openChat("carol@example.org");
        backend.openChat("bob@example.org");
        QCOMPARE(backend.messages()->data(backend.messages()->index(0), MessageModel::SmileysRole).toMap(), smileys);

        // Pidgin only sends them to clients that say they take them.
        auto *disco = backend.client()->findExtension<QXmppDiscoveryManager>();
        const QStringList features = disco->capabilities().features();
        QVERIFY(features.contains("urn:xmpp:bob"));
        QVERIFY(features.contains("http://jabber.org/protocol/xhtml-im"));
        // Each once: Pidgin hashes the list as it comes, and a feature twice
        // gives a hash other than ours, so it drops all of our capabilities.
        QCOMPARE(QSet<QString>(features.cbegin(), features.cend()).size(), features.size());
    }

    void imagesFromKnownContactsGetPreviews() {
        QTemporaryDir dir;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        int requests = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, png, &requests] {
                socket->readAll();
                ++requests;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(png.size()) +
                              "\r\nConnection: close\r\n\r\n" + png);
                socket->disconnectFromHost();
            });
        });

        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        // Bob is in the roster; Mallory isn't.
        backend.contacts()->upsert("bob@example.org", "Bob");
        emit backend.client()->findExtension<QXmppRosterManager>()->itemAdded("bob@example.org");

        auto share = [&](const QString &from, const QString &id, const QString &name) {
            const QString link = QStringLiteral("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(name);
            QXmppMessage message(from + "/phone", "alice@example.org", link);
            message.setType(QXmppMessage::Chat);
            message.setId(id);
            message.setOutOfBandUrl(link);
            emit backend.client()->messageReceived(message);
        };
        backend.openChat("bob@example.org");
        share("bob@example.org", "i1", "photo.png");
        auto *messages = backend.messages();
        QTRY_VERIFY(!messages->data(messages->index(0), MessageModel::PreviewRole).toString().isEmpty());
        QCOMPARE(QImage(QUrl(messages->data(messages->index(0), MessageModel::PreviewRole).toString()).toLocalFile()).size(),
                 QSize(40, 30));

        backend.openChat("mallory@example.org");
        share("mallory@example.org", "i2", "tracker.png");
        QTest::qWait(300);
        QCOMPARE(requests, 1);
        QVERIFY(messages->data(messages->index(0), MessageModel::PreviewRole).toString().isEmpty());
    }

    void gajimHistoryComesOverOnce() {
        QTemporaryDir dir;
        const QString source = dir.filePath("logs.db");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "gajim");
            db.setDatabaseName(source);
            QVERIFY(db.open());
            QSqlQuery q(db);
            for (const char *statement : {
                     "CREATE TABLE account (pk INTEGER PRIMARY KEY, jid TEXT NOT NULL)",
                     "CREATE TABLE remote (pk INTEGER PRIMARY KEY, jid TEXT NOT NULL)",
                     "CREATE TABLE message (pk INTEGER PRIMARY KEY, fk_account_pk INTEGER, fk_remote_pk INTEGER,"
                     " resource TEXT, type INTEGER, direction INTEGER, timestamp FLOAT, state INTEGER, id TEXT,"
                     " stanza_id TEXT, text TEXT, correction_id TEXT)",
                     "CREATE TABLE oob (pk INTEGER PRIMARY KEY, fk_message_pk INTEGER, url TEXT, description TEXT)",
                     "CREATE TABLE filetransfer (pk INTEGER PRIMARY KEY, fk_message_pk INTEGER, name TEXT, size INTEGER)",
                     "INSERT INTO account VALUES (1, 'alice@example.org'), (2, 'other@example.org')",
                     "INSERT INTO remote VALUES (1, 'bob@example.org'), (2, 'team@rooms.example.org')",
                     "INSERT INTO message VALUES (1, 1, 1, 'phone', 1, 1, 100.5, 0, 'g1', 's1', 'hi alice', NULL)",
                     "INSERT INTO message VALUES (2, 1, 1, NULL, 1, 2, 101, 0, 'g2', NULL, 'see you at 10', NULL)",
                     "INSERT INTO message VALUES (3, 1, 1, NULL, 1, 2, 102, 0, 'g3', NULL, 'see you at 11', 'g2')",
                     "INSERT INTO message VALUES (4, 1, 2, 'carol', 2, 1, 103, 0, NULL, NULL, 'standup now', NULL)",
                     "INSERT INTO message VALUES (5, 1, 2, 'dave', 3, 1, 104, 0, 'g5', NULL, 'psst', NULL)",
                     "INSERT INTO message VALUES (6, 1, 1, NULL, 1, 1, 105, 0, 'g6', NULL, 'https://up.example.org/a/plan.pdf', NULL)",
                     "INSERT INTO oob VALUES (1, 6, 'https://up.example.org/a/plan.pdf', NULL)",
                     "INSERT INTO message VALUES (7, 1, 1, NULL, 1, 1, 106, 0, 'g7', NULL, NULL, NULL)",
                     "INSERT INTO filetransfer VALUES (1, 7, 'photo.jpg', 2048)",
                     "INSERT INTO message VALUES (8, 2, 1, NULL, 1, 1, 107, 0, 'x1', NULL, 'not ours', NULL)"})
                QVERIFY2(q.exec(statement), qPrintable(q.lastError().text()));
            db.close();
        }
        QSqlDatabase::removeDatabase("gajim");
        QFile original(source);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray before = QCryptographicHash::hash(original.readAll(), QCryptographicHash::Sha256);
        original.close();

        QCOMPARE(GajimImport::count(source, "Alice@example.org").found, 6);

        History history;
        QVERIFY(history.open(dir.filePath("ours.sqlite")));
        // Already here from the server's archive: skipped by its id.
        HistoryEntry archived;
        archived.peer = "bob@example.org";
        archived.body = "hi alice";
        archived.archiveId = "s1";
        archived.stamp = QDateTime::fromMSecsSinceEpoch(100500, QTimeZone::UTC);
        history.add(archived);

        auto result = GajimImport::import(source, "alice@example.org", history);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.found, 6);
        QCOMPARE(result.imported, 5);

        const auto bob = history.recent("bob@example.org", 10);
        QCOMPARE(bob.size(), 4);
        QCOMPARE(bob.at(1).body, QString("see you at 11"));
        QVERIFY(bob.at(1).edited && bob.at(1).outgoing);
        QCOMPARE(bob.at(2).kind, QString("file"));
        QCOMPARE(bob.at(2).body, QString("plan.pdf"));
        QCOMPARE(bob.at(2).state, QString("remote"));
        QCOMPARE(bob.at(3).body, QString("photo.jpg"));
        QCOMPARE(bob.at(3).fileSize, qint64(2048));

        const auto room = history.recent("team@rooms.example.org", 10);
        QCOMPARE(room.size(), 2);
        QCOMPARE(room.at(0).sender, QString("carol"));
        QVERIFY(room.at(1).sender.startsWith("dave"));

        // Again: nothing new, and Gajim's file is as it was.
        QCOMPARE(GajimImport::import(source, "alice@example.org", history).imported, 0);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(original.readAll(), QCryptographicHash::Sha256), before);
    }

    void gajimComesOverOnTheFirstRun() {
        QTemporaryDir dir;
        // Gajim's settings, only what differs from its defaults, and its history.
        const QString settings = dir.filePath("gajim/settings.sqlite");
        const QString logs = dir.filePath("gajim/logs.db");
        QDir().mkpath(dir.filePath("gajim"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "gajim-settings");
            db.setDatabaseName(settings);
            QVERIFY(db.open());
            QSqlQuery q(db);
            for (const char *statement : {
                     "CREATE TABLE settings (name TEXT UNIQUE, settings TEXT)",
                     "CREATE TABLE account_settings (account TEXT UNIQUE, settings TEXT)",
                     "INSERT INTO settings VALUES ('app', '{\"autoawaytime\": 10, \"autoxa\": false,"
                     " \"show_notifications\": false, \"action_on_close\": \"minimize\"}')",
                     "INSERT INTO account_settings VALUES ('old', '{\"account\": {\"address\": \"Old@example.org\","
                     " \"active\": false}}')",
                     "INSERT INTO account_settings VALUES ('work', '{\"account\": {\"address\": \"alice@example.org\","
                     " \"use_custom_host\": true, \"custom_host\": \"127.0.0.1\", \"custom_port\": 1, \"password\": \"\"},"
                     " \"contact\": {}}')"})
                QVERIFY2(q.exec(statement), qPrintable(q.lastError().text()));
            db.close();
        }
        QSqlDatabase::removeDatabase("gajim-settings");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "gajim-logs");
            db.setDatabaseName(logs);
            QVERIFY(db.open());
            QSqlQuery q(db);
            for (const char *statement : {
                     "CREATE TABLE account (pk INTEGER PRIMARY KEY, jid TEXT NOT NULL)",
                     "CREATE TABLE remote (pk INTEGER PRIMARY KEY, jid TEXT NOT NULL)",
                     "CREATE TABLE message (pk INTEGER PRIMARY KEY, fk_account_pk INTEGER, fk_remote_pk INTEGER,"
                     " resource TEXT, type INTEGER, direction INTEGER, timestamp FLOAT, state INTEGER, id TEXT,"
                     " stanza_id TEXT, text TEXT, correction_id TEXT)",
                     "CREATE TABLE oob (pk INTEGER PRIMARY KEY, fk_message_pk INTEGER, url TEXT, description TEXT)",
                     "CREATE TABLE filetransfer (pk INTEGER PRIMARY KEY, fk_message_pk INTEGER, name TEXT, size INTEGER)",
                     "INSERT INTO account VALUES (1, 'alice@example.org')",
                     "INSERT INTO remote VALUES (1, 'bob@example.org')",
                     "INSERT INTO message VALUES (1, 1, 1, 'phone', 1, 1, 100.5, 0, 'g1', 's1', 'from gajim', NULL)"})
                QVERIFY2(q.exec(statement), qPrintable(q.lastError().text()));
            db.close();
        }
        QSqlDatabase::removeDatabase("gajim-logs");

        const auto read = GajimImport::readSettings(settings);
        QCOMPARE(read.accounts.size(), 2);
        QCOMPARE(read.accounts.at(0).jid, QString("old@example.org"));
        QVERIFY(!read.accounts.at(0).active);
        QCOMPARE(read.accounts.at(1).host, QString("127.0.0.1:1"));
        QCOMPARE(*read.awayMinutes, 10);
        QCOMPARE(*read.xaMinutes, 0);
        QVERIFY(!*read.notifications);
        QCOMPARE(*read.closeAction, QString("minimize"));

        FakeSecretStore secrets;
        secrets.gajimPasswords.insert("alice@example.org", "from-gajim");
        {
            Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
            backend.setStatus("offline");
            backend.setGajimSources({settings}, {logs});
            backend.start();
            // The active account, with its server, Gajim's password kept as ours.
            QCOMPARE(backend.jid(), QString("alice@example.org"));
            QCOMPARE(backend.host(), QString("127.0.0.1:1"));
            QSettings kept(dir.filePath("config/omajim.conf"), QSettings::IniFormat);
            QCOMPARE(kept.value("accounts/list").toStringList(), (QStringList{"old@example.org", "alice@example.org"}));
            // Off in Gajim, off here.
            QVERIFY(kept.value("disabled/old@example.org").toBool());
            QVERIFY(backend.signedIn());
            QCOMPARE(secrets.passwords.value("alice@example.org"), QString("from-gajim"));
            QCOMPARE(backend.awayMinutes(), 10);
            QCOMPARE(backend.xaMinutes(), 0);
            QVERIFY(!backend.notifications());
            QCOMPARE(backend.closeAction(), QString("minimize"));
            backend.openChat("bob@example.org");
            QCOMPARE(backend.messages()->rowCount(), 1);
            QCOMPARE(backend.messages()->data(backend.messages()->index(0), MessageModel::BodyRole).toString(),
                     QString("from gajim"));
        }
        // Only the first run: what changes here afterwards stays.
        {
            Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
            backend.setCloseAction("quit");
            backend.signOut();
        }
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.setGajimSources({settings}, {logs});
        backend.start();
        QCOMPARE(backend.closeAction(), QString("quit"));
    }

    void ufwRulesDecideTheDirectPort() {
        QTemporaryDir dir;
        const QString conf = dir.filePath("ufw.conf"), defaults = dir.filePath("default"), rules = dir.filePath("user.rules");
        Firewall firewall(conf, defaults, rules);
        const QStringList lan{"192.168.0.5"};

        // No ufw at all: nothing in the way.
        QCOMPARE(firewall.isIncomingPortBlocked(28011, "tcp", lan), std::optional<bool>(false));

        write(conf, "ENABLED=no\n");
        QCOMPARE(firewall.isIncomingPortBlocked(28011, "tcp", lan), std::optional<bool>(false));

        write(conf, "ENABLED=yes\n");
        write(defaults, "DEFAULT_INPUT_POLICY=\"DROP\"\n");
        // The rules file can't be read: say so rather than guess.
        QCOMPARE(firewall.isIncomingPortBlocked(28011, "tcp", lan), std::nullopt);

        write(rules, "*filter\n### tuple ### allow tcp 22 0.0.0.0/0 any 0.0.0.0/0 in\n");
        QCOMPARE(firewall.isIncomingPortBlocked(28011, "tcp", lan), std::optional<bool>(true));

        // The rule the README suggests: the local network may connect.
        write(rules, "### tuple ### allow tcp 28011 0.0.0.0/0 any 192.168.0.0/24 in comment=omajim\n");
        QCOMPARE(firewall.isIncomingPortBlocked(28011, "tcp", lan), std::optional<bool>(false));
        QCOMPARE(firewall.isIncomingPortBlocked(28011, "tcp", {"10.1.2.3"}), std::optional<bool>(true));
        QCOMPARE(firewall.isIncomingPortBlocked(28011, "udp", lan), std::optional<bool>(true));

        // The first rule that fits wins, ranges and logging variants count.
        write(rules, "### tuple ### deny tcp 28011 0.0.0.0/0 any 0.0.0.0/0 in\n"
                     "### tuple ### allow tcp 28011 0.0.0.0/0 any 0.0.0.0/0 in\n");
        QCOMPARE(firewall.isIncomingPortBlocked(28011, "tcp", lan), std::optional<bool>(true));
        write(rules, "### tuple ### allow_log any 28000:28100 0.0.0.0/0 any 0.0.0.0/0 in\n");
        QCOMPARE(firewall.isIncomingPortBlocked(28011, "tcp", lan), std::optional<bool>(false));

        // An open default lets everything in.
        write(rules, "");
        write(defaults, "DEFAULT_INPUT_POLICY=\"ACCEPT\"\n");
        QCOMPARE(firewall.isIncomingPortBlocked(28011, "tcp", lan), std::optional<bool>(false));
    }

    void nicknamesStayLocal() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        {
            Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
            backend.setStatus("offline");
            backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
            auto *contacts = backend.contacts();
            contacts->upsert("jcp@example.org", "Jean Carlos Pereira");
            backend.setAlias("JCP@example.org", "  Jean  ");
            backend.openChat("jcp@example.org");
            QCOMPARE(backend.currentChatName(), QString("Jean"));

            // Both names find the contact.
            contacts->setFilter("pereira");
            QCOMPARE(contacts->rowCount(), 1);
            QCOMPARE(contacts->data(contacts->index(0), ContactModel::NameRole).toString(), QString("Jean"));
            QCOMPARE(contacts->data(contacts->index(0), ContactModel::RosterNameRole).toString(), QString("Jean Carlos Pereira"));
        }
        // Back on the next start, until cleared.
        Backend again(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        again.setStatus("offline");
        again.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        again.contacts()->upsert("jcp@example.org", "Jean Carlos Pereira");
        QCOMPARE(again.contacts()->nameOf("jcp@example.org"), QString("Jean"));
        again.setAlias("jcp@example.org", "");
        QCOMPARE(again.contacts()->nameOf("jcp@example.org"), QString("Jean Carlos Pereira"));
    }

    void searchIsFuzzyAndAccentBlind() {
        QVERIFY(ContactModel::fuzzyScore("jcp", "Jean Carlos Pereira") > 0);
        QVERIFY(ContactModel::fuzzyScore("joao", "João Silva") > 0);
        QCOMPARE(ContactModel::fuzzyScore("xyz", "Jean Carlos Pereira"), -1);
        // Out of order isn't a match.
        QCOMPARE(ContactModel::fuzzyScore("pj", "Jean Pereira"), -1);
        // A word's start and a plain substring rank higher than scattered letters.
        QVERIFY(ContactModel::fuzzyScore("car", "Carla Souza") > ContactModel::fuzzyScore("car", "Oscar Ramos"));
        QVERIFY(ContactModel::fuzzyScore("car", "Oscar Ramos") > ContactModel::fuzzyScore("car", "Clara Rocha"));

        ContactModel model;
        model.upsert("clara@example.org", "Clara Rocha");
        model.upsert("carla@example.org", "Carla Souza");
        model.upsert("bob@example.org", "Bob");
        model.setFilter("car");
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.jidAt(0), QString("carla@example.org"));
    }

    void wholeMessagesCanBeSelectedAndCopied() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.contacts()->upsert("bob@example.org", "Bob");
        backend.openChat("bob@example.org");
        auto receive = [&](const QString &id, const QString &body) {
            QXmppMessage message("bob@example.org/phone", "alice@example.org", body);
            message.setType(QXmppMessage::Chat);
            message.setId(id);
            emit backend.client()->messageReceived(message);
        };
        receive("s1", "first");
        receive("s2", "second");
        receive("s3", "third");
        auto *messages = backend.messages();
        auto id = [&](int row) { return messages->data(messages->index(row), MessageModel::IdRole).toLongLong(); };

        // From the third back to the first: all three, in order.
        messages->selectRange(id(2), id(0));
        QCOMPARE(messages->selectedCount(), 3);
        messages->toggleSelected(id(1));
        QCOMPARE(messages->selectedCount(), 2);
        QVERIFY(!messages->data(messages->index(1), MessageModel::SelectedRole).toBool());

        const QStringList lines = backend.selectedMessagesText().split('\n');
        QCOMPARE(lines.size(), 2);
        QVERIFY(lines.at(0).endsWith("] Bob: first"));
        QVERIFY(lines.at(1).endsWith("] Bob: third"));

        // Shift+drag from the second line: the third stays picked, and going
        // back over a line takes it out again.
        messages->toggleSelected(id(0));
        messages->holdSelection();
        messages->dragRange(id(1), id(0));
        QCOMPARE(messages->selectedCount(), 3);
        messages->dragRange(id(1), id(1));
        QCOMPARE(messages->selectedCount(), 2);
        QVERIFY(!messages->data(messages->index(0), MessageModel::SelectedRole).toBool());
        QVERIFY(messages->data(messages->index(2), MessageModel::SelectedRole).toBool());

        // Another conversation starts with nothing selected.
        backend.openChat("carol@example.org");
        QCOMPARE(messages->selectedCount(), 0);
    }

    void contactsSeeOmajim() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        QCOMPARE(backend.client()->findExtension<QXmppVersionManager>()->clientName(), QString("Omajim"));
        auto *disco = backend.client()->findExtension<QXmppDiscoveryManager>();
        QCOMPARE(disco->clientCapabilitiesNode(), QString("https://github.com/higorhgon/omajim"));
    }

    void repliesAndReactionsShowOnTheirLine() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.contacts()->upsert("bob@example.org", "Bob");
        backend.openChat("bob@example.org");
        auto *client = backend.client();

        QXmppMessage first("bob@example.org/phone", "alice@example.org", "see you at 10");
        first.setType(QXmppMessage::Chat);
        first.setId("m1");
        emit client->messageReceived(first);

        // A reply whose body starts with the quote other clients show.
        const QString quote = "> Bob:\n> see you at 10\n";
        QXmppMessage answer("bob@example.org/phone", "alice@example.org", quote + "ok, 10 it is");
        answer.setType(QXmppMessage::Chat);
        answer.setId("m2");
        answer.setReply(QXmpp::Reply{"bob@example.org", "m1"});
        answer.setFallbackMarkers({QXmppFallback("urn:xmpp:reply:0",
            {QXmppFallback::Reference{QXmppFallback::Body, QXmppFallback::Range{0, uint32_t(quote.size())}}})});
        emit client->messageReceived(answer);

        auto *messages = backend.messages();
        QCOMPARE(messages->rowCount(), 2);
        QCOMPARE(messages->data(messages->index(1), MessageModel::BodyRole).toString(), QString("ok, 10 it is"));
        const QVariantMap reply = messages->data(messages->index(1), MessageModel::ReplyRole).toMap();
        QCOMPARE(reply.value("name").toString(), QString("Bob"));
        QCOMPARE(reply.value("text").toString(), QString("see you at 10"));
        QCOMPARE(messages->rowOfMessage("m1"), 0);

        auto react = [&](const QString &from, const QString &to, const QStringList &emojis) {
            QXmppMessageReaction reaction;
            reaction.setMessageId("m1");
            reaction.setEmojis(emojis);
            QXmppMessage message(from, to);
            message.setType(QXmppMessage::Chat);
            message.setReaction(reaction);
            emit client->messageReceived(message);
        };
        react("bob@example.org/phone", "alice@example.org", {"👍", "❤️"});
        // A new set replaces the old one.
        react("bob@example.org/phone", "alice@example.org", {"👍"});
        // Ours, from another of our devices.
        react("alice@example.org/laptop", "bob@example.org", {"👍"});

        const QVariantList reactions = messages->data(messages->index(0), MessageModel::ReactionsRole).toList();
        QCOMPARE(reactions.size(), 1);
        const QVariantMap thumbs = reactions.first().toMap();
        QCOMPARE(thumbs.value("emoji").toString(), QString("👍"));
        QCOMPARE(thumbs.value("count").toInt(), 2);
        QVERIFY(thumbs.value("mine").toBool());
        // Reactions aren't messages.
        QCOMPARE(messages->rowCount(), 2);
    }

    void idleTimeSetsAwayAndBack() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("online");
        backend.setIdleLevel(1);
        QCOMPARE(backend.status(), QString("away"));
        backend.setIdleLevel(2);
        QCOMPARE(backend.status(), QString("xa"));
        backend.setIdleLevel(0);
        QCOMPARE(backend.status(), QString("online"));

        // The saved status stays the chosen one, not the automatic away.
        backend.setIdleLevel(2);
        {
            QSettings saved(dir.filePath("config/omajim.conf"), QSettings::IniFormat);
            const QString status = saved.value("account/status", "online").toString();
            QVERIFY2(status == "online", qPrintable(status));
        }
        // A status picked meanwhile wins, and idle time leaves it alone.
        backend.setStatus("dnd");
        backend.setIdleLevel(0);
        QCOMPARE(backend.status(), QString("dnd"));
        backend.setIdleLevel(1);
        QCOMPARE(backend.status(), QString("dnd"));
    }

    void textAcrossMessagesCopiesAsATranscript() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.contacts()->upsert("bob@example.org", "Bob");
        backend.openChat("bob@example.org");
        for (const auto &[id, body] : {std::pair{"t1", "first line"}, {"t2", "second one"}, {"t3", "third here"}}) {
            QXmppMessage message("bob@example.org/phone", "alice@example.org", body);
            message.setType(QXmppMessage::Chat);
            message.setId(id);
            emit backend.client()->messageReceived(message);
        }

        // Within one message: just the text.
        QCOMPARE(backend.transcriptText({QVariantMap{{"row", 1}, {"text", "cond"}}}), QString("cond"));

        // Across three: the tail of the first, the middle whole (scrolled
        // away, so taken from the history), the head of the last.
        const QStringList lines = backend.transcriptText({QVariantMap{{"row", 0}, {"text", "line"}},
                                                          QVariantMap{{"row", 1}, {"text", QVariant()}},
                                                          QVariantMap{{"row", 2}, {"text", "third"}}}).split('\n');
        QCOMPARE(lines.size(), 3);
        QVERIFY(lines.at(0).startsWith("[") && lines.at(0).endsWith("] Bob: line"));
        QVERIFY(lines.at(1).endsWith("] Bob: second one"));
        QVERIFY(lines.at(2).endsWith("] Bob: third"));
    }

    void conversationSearchReachesOldLines() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        {
            History history;
            QVERIFY(history.open(dir.filePath("data/accounts/alice@example.org/history.sqlite")));
            history.beginBatch();
            for (int i = 0; i < 400; ++i) {
                HistoryEntry entry;
                entry.peer = "bob@example.org";
                entry.body = i == 10 ? "A REUNIÃO foi adiada" : i == 390 ? "reuniao amanhã" : QString("line %1").arg(i);
                entry.stamp = QDateTime::fromMSecsSinceEpoch(1000LL * (i + 1), QTimeZone::UTC);
                history.add(entry);
            }
            history.commitBatch();
        }
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");
        QCOMPARE(backend.messages()->rowCount(), 300);

        // Case and accents don't matter; oldest first.
        const QVariantList hits = backend.searchConversation("reunião");
        QCOMPARE(hits.size(), 2);

        // The newest is on screen already; the oldest comes in with the
        // lines after it.
        QVERIFY(backend.revealMessage(hits.last().toLongLong()) >= 0);
        const int row = backend.revealMessage(hits.first().toLongLong());
        QCOMPARE(row, 0);
        QCOMPARE(backend.messages()->data(backend.messages()->index(row), MessageModel::BodyRole).toString(),
                 QString("A REUNIÃO foi adiada"));
        QCOMPARE(backend.messages()->rowCount(), 390);
        QVERIFY(backend.searchConversation("  ").isEmpty());
    }

    void pastedPicturesAndFilesAreOfferedToSend() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        QClipboard *clipboard = QGuiApplication::clipboard();

        // Files copied in a file manager.
        write(dir.filePath("video.mp4"), "x");
        auto *files = new QMimeData;
        files->setUrls({QUrl::fromLocalFile(dir.filePath("video.mp4")), QUrl::fromLocalFile(dir.filePath("gone.pdf"))});
        clipboard->setMimeData(files);
        QVariantMap content = backend.clipboardContent();
        QCOMPARE(content.value("kind").toString(), QString("files"));
        QCOMPARE(content.value("paths").toStringList(), QStringList{dir.filePath("video.mp4")});

        // A screenshot: saved as a PNG to send.
        QImage shot(30, 20, QImage::Format_RGB32);
        shot.fill(Qt::green);
        clipboard->setImage(shot);
        QCOMPARE(backend.clipboardContent().value("kind").toString(), QString("image"));
        const QString path = backend.savePastedImage();
        QVERIFY(path.endsWith(".png"));
        QCOMPARE(QImage(path).size(), QSize(30, 20));

        // Text stays text.
        clipboard->setText("just words");
        QVERIFY(backend.clipboardContent().isEmpty());
    }

    void filesCanBeSavedElsewhere() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        FakeFilePicker picker;
        Backend backend(dir.filePath("config"), dir.filePath("data"), &picker, &secrets);
        const QByteArray content(300000, 'z');
        write(dir.filePath("report.pdf"), content);
        QDir(dir.path()).mkpath("elsewhere");
        picker.saveTo = dir.filePath("elsewhere/copy.pdf");

        backend.saveFileAs(dir.filePath("report.pdf"), "report.pdf");
        QVERIFY(picker.lastTitle.contains("report.pdf"));
        QFile copy(picker.saveTo);
        QVERIFY(copy.open(QIODevice::ReadOnly));
        QCOMPARE(copy.readAll(), content);

        // Closing the dialog saves nothing; a file that's gone says so.
        picker.saveTo.clear();
        backend.saveFileAs(dir.filePath("report.pdf"), "report.pdf");
        backend.saveFileAs(dir.filePath("gone.pdf"), "gone.pdf");
        QVERIFY(backend.errorText().contains("gone.pdf"));
    }

    void historyMarksInterruptedTransfersAsFailed() {
        QTemporaryDir dir;
        History history;
        QVERIFY(history.open(dir.filePath("history.sqlite")));
        HistoryEntry file;
        file.peer = "bob@example.org";
        file.kind = "file";
        file.body = "a.zip";
        file.state = "active";
        history.add(file);
        HistoryEntry done = file;
        done.state = "done";
        history.add(done);

        history.failInterruptedTransfers();
        const auto entries = history.recent("bob@example.org", 10);
        QCOMPARE(entries.at(0).state, QString("failed"));
        QCOMPARE(entries.at(1).state, QString("done"));
    }

    void searchListsOnlinePeopleFirst() {
        ContactModel model;
        model.upsert("zoe@example.org", "Zoe");
        model.upsert("adam@example.org", "Adam");
        model.upsert("bea@example.org", {});
        // Without a search only open conversations show, and none is open.
        QCOMPARE(model.rowCount(), 0);
        model.setFilter("example");
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.jidAt(0), QString("adam@example.org"));

        model.setPresence("zoe@example.org", "away", "lunch");
        QCOMPARE(model.jidAt(0), QString("zoe@example.org"));
        QCOMPARE(model.data(model.index(0), ContactModel::StatusTextRole).toString(), QString("lunch"));
        QCOMPARE(model.data(model.index(2), ContactModel::NameRole).toString(), QString("bea"));

        model.setFilter("ad");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.jidAt(0), QString("adam@example.org"));
    }

    void openConversationsListNewestFirst() {
        ContactModel model;
        model.upsert("zoe@example.org", "Zoe");
        model.upsert("adam@example.org", "Adam");
        const QDateTime now = QDateTime::currentDateTimeUtc();
        model.setActivity("adam@example.org", now.addSecs(-60), "older", false);
        model.setActivity("zoe@example.org", now, "newer", true);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.jidAt(0), QString("zoe@example.org"));
        QCOMPARE(model.data(model.index(0), ContactModel::LastMessageRole).toString(), QString("newer"));
        QVERIFY(model.data(model.index(0), ContactModel::LastOutgoingRole).toBool());

        // An older line arriving late doesn't push a conversation up.
        model.setActivity("adam@example.org", now.addSecs(-120), "oldest", false);
        QCOMPARE(model.jidAt(0), QString("zoe@example.org"));
        QCOMPARE(model.data(model.index(1), ContactModel::LastMessageRole).toString(), QString("older"));

        model.setOpen("zoe@example.org", false);
        QCOMPARE(model.rowCount(), 1);
    }

    void conversationsStayOpenOrClosed() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        {
            History history;
            QVERIFY(history.open(dir.filePath("data/accounts/alice@example.org/history.sqlite")));
            HistoryEntry old;
            old.peer = "bob@example.org";
            old.body = "from before";
            history.add(old);
        }
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        // The first run opens the conversations that have history.
        QCOMPARE(backend.contacts()->rowCount(), 1);
        QCOMPARE(backend.contacts()->data(backend.contacts()->index(0), ContactModel::LastMessageRole).toString(),
                 QString("from before"));

        backend.closeConversation("bob@example.org");
        QCOMPARE(backend.contacts()->rowCount(), 0);

        // A message opens it again.
        QXmppMessage message("bob@example.org/phone", "alice@example.org", "hello again");
        message.setType(QXmppMessage::Chat);
        message.setId("o1");
        emit backend.client()->messageReceived(message);
        QCOMPARE(backend.contacts()->rowCount(), 1);
        QCOMPARE(backend.contacts()->data(backend.contacts()->index(0), ContactModel::LastMessageRole).toString(),
                 QString("hello again"));
        backend.closeConversation("bob@example.org");

        // Closed stays closed on the next start.
        Backend again(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        again.setStatus("offline");
        again.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        QCOMPARE(again.contacts()->rowCount(), 0);
        again.contacts()->setFilter("bob");
        QCOMPARE(again.contacts()->rowCount(), 1);
    }

    void browsingListsTheRosterByGroup() {
        ContactModel model;
        model.upsert("zoe@example.org", "Zoe");
        model.upsert("adam@example.org", "Adam");
        model.upsert("bea@example.org", "Bea");
        model.upsertRoom("team@rooms.example.org", "Team");
        model.setGroups("zoe@example.org", {"Work"});
        model.setGroups("bea@example.org", {"Family"});
        model.setActivity("adam@example.org", QDateTime::currentDateTimeUtc(), "hi", false);

        // Not browsing: only the open conversation.
        QCOMPARE(model.rowCount(), 1);

        // Browsing: rooms, then the groups in order, then people without one.
        model.setBrowsing(true);
        QStringList order;
        for (int row = 0; row < model.rowCount(); ++row)
            order << model.jidAt(row);
        QCOMPARE(order, (QStringList{"team@rooms.example.org", "bea@example.org", "zoe@example.org", "adam@example.org"}));
        QCOMPARE(model.data(model.index(0), ContactModel::GroupRole).toString(), QString("#rooms"));
        QCOMPARE(model.data(model.index(3), ContactModel::GroupRole).toString(), QString());

        // A group's name finds its people.
        model.setFilter("work");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.jidAt(0), QString("zoe@example.org"));
        QCOMPARE(model.allGroups(), (QStringList{"Family", "Work"}));
    }

    void theSidebarModeIsRemembered() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        {
            Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
            QVERIFY(!backend.showAllContacts());
            backend.setShowAllContacts(true);
            QVERIFY(backend.contacts()->browsing());
            QVERIFY(!backend.sidebarCollapsed());
            QSignalSpy collapsed(&backend, &Backend::sidebarCollapsedChanged);
            backend.setSidebarCollapsed(true);
            backend.setSidebarCollapsed(true);
            QCOMPARE(collapsed.count(), 1);
        }
        Backend again(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        QVERIFY(again.showAllContacts());
        QVERIFY(again.contacts()->browsing());
        QVERIFY(again.sidebarCollapsed());
    }

    void contactsCountUnread() {
        ContactModel model;
        model.upsert("bob@example.org", "Bob");
        QSignalSpy spy(&model, &ContactModel::totalUnreadChanged);
        model.addUnread("bob@example.org");
        model.addUnread("bob@example.org");
        QCOMPARE(model.totalUnread(), 2);
        model.clearUnread("bob@example.org");
        QCOMPARE(model.totalUnread(), 0);
        QCOMPARE(spy.count(), 3);
    }

    void messagesTrackTransferProgress() {
        MessageModel model;
        HistoryEntry entry;
        entry.id = 7;
        entry.kind = "file";
        entry.state = "offered";
        model.append(entry);
        model.setProgress(7, 0.5);
        QCOMPARE(model.data(model.index(0), MessageModel::ProgressRole).toDouble(), 0.5);
        model.updateFile(7, "done", "/tmp/x");
        QCOMPARE(model.data(model.index(0), MessageModel::StateRole).toString(), QString("done"));
        QCOMPARE(model.data(model.index(0), MessageModel::FilePathRole).toString(), QString("/tmp/x"));
    }

    void backendStartsSignedOutWithoutAnAccount() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.start();
        QVERIFY(!backend.signedIn());
        QVERIFY(!backend.starting());
        QCOMPARE(backend.connectionState(), QString("offline"));
    }

    void backendRejectsAMalformedAddress() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.signIn("not-an-address", "secret", {}, true);
        QVERIFY(!backend.signedIn());
        QVERIFY(!backend.errorText().isEmpty());
        QVERIFY(secrets.passwords.isEmpty());
    }

    void backendRemembersTheAccount() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        {
            Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
            backend.setStatus("offline");
            backend.signIn(" Alice@Example.org/laptop ", "secret", "127.0.0.1:1", true);
            QVERIFY(backend.signedIn());
            QCOMPARE(backend.jid(), QString("alice@example.org"));
            QCOMPARE(secrets.passwords.value("alice@example.org"), QString("secret"));
            backend.setStatus("offline");
        }
        QVERIFY(QFile::exists(dir.filePath("data/accounts/alice@example.org/history.sqlite")));

        Backend again(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        again.setStatus("offline");
        again.start();
        QVERIFY(again.signedIn());
        QCOMPARE(again.host(), QString("127.0.0.1:1"));

        again.signOut();
        QVERIFY(!again.signedIn());
        QVERIFY(secrets.passwords.isEmpty());
        // The address stays, so signing in again only needs the password.
        QCOMPARE(again.jid(), QString("alice@example.org"));
    }

    void backendOnlySendsFilesToSomeoneOnline() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        FakeFilePicker picker;
        Backend backend(dir.filePath("config"), dir.filePath("data"), &picker, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");
        QCOMPARE(backend.currentChat(), QString("bob@example.org"));

        backend.chooseFileToSend("direct");
        QVERIFY(picker.lastTitle.contains("bob"));

        write(dir.filePath("a.txt"), "hello");
        emit picker.picked(dir.filePath("a.txt"));
        QVERIFY(backend.errorText().contains("bob"));
        QCOMPARE(backend.messages()->rowCount(), 0);
    }

    void backendListensForDirectTransfersOnAFixedPort() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        QVERIFY(backend.directTransferPort() != 0);

        // With the port taken, it still listens, on whatever port is free.
        Backend second(dir.filePath("config2"), dir.filePath("data2"), nullptr, &secrets);
        QVERIFY(second.directTransferPort() != 0);
        QVERIFY(second.directTransferPort() != backend.directTransferPort());
        if (backend.directTransferPort() != 28011)
            QSKIP("Port 28011 is in use on this machine");
    }

    void repeatedIdsFromARestartAreNotDropped() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("jean@example.org");
        // QXmpp numbers ids per run: after Jean restarts, "qx23" again.
        auto incoming = [&](const QString &id, const QString &body, const QString &stanzaId = {}) {
            QXmppMessage message("jean@example.org/omajim", "alice@example.org", body);
            message.setType(QXmppMessage::Chat);
            message.setId(id);
            if (!stanzaId.isEmpty())
                message.setStanzaIds({QXmppStanzaId{stanzaId, "alice@example.org"}});
            emit backend.client()->messageReceived(message);
        };
        incoming("qx23", "oi", "a1");
        incoming("qx23", "tudo bem?", "a2");
        QCOMPARE(backend.messages()->rowCount(), 2);
        // The same message twice (live, then from the archive) still once.
        incoming("qx23", "tudo bem?", "a2");
        incoming("qx23", "tudo bem?");
        QCOMPARE(backend.messages()->rowCount(), 2);

        // Ours go out with an id of their own, not QXmpp's numbering.
        QVERIFY(backend.sendMessage("ok"));
        const auto *messages = backend.messages();
        const QString ours = messages->entry(messages->data(messages->index(2), MessageModel::IdRole).toLongLong()).xmppId;
        QVERIFY2(!ours.startsWith("qx") && ours.size() >= 32, qPrintable(ours));
    }

    void roomsAreCreatedWithAnAddressFromTheirName() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        // Letters without accents, digits, dashes.
        QCOMPARE(backend.roomAddressFor("Suporte TI"), QString("suporte-ti"));
        QCOMPARE(backend.roomAddressFor("  Ação Ágil — 2026!  "), QString("acao-agil-2026"));
        QCOMPARE(backend.roomAddressFor("devs.backend_01"), QString("devs.backend_01"));
        // Offline, or with no room service found, nothing is created.
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        QVERIFY(!backend.createRoom({{"name", "Suporte"}, {"address", "suporte"}, {"access", "open"}, {"persistent", true}}));
        QVERIFY(!backend.errorText().isEmpty());
        // Not in a room: no people, no powers.
        QVERIFY(backend.roomParticipants().isEmpty());
        QVERIFY(backend.roomPowers().isEmpty());
    }

    void offlineMessagesWaitAndShowHowFarTheyGot() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");
        // Offline, it waits to go out (a clock), kept for the next connection.
        QVERIFY(backend.sendMessage("first"));
        QVERIFY(backend.sendMessage("second"));
        QVERIFY(backend.errorText().isEmpty());
        auto *messages = backend.messages();
        QCOMPARE(messages->rowCount(), 2);
        QCOMPARE(messages->data(messages->index(0), MessageModel::SendStateRole).toInt(), int(HistoryEntry::Pending));
        const QString firstId = messages->entry(messages->data(messages->index(0), MessageModel::IdRole).toLongLong()).xmppId;
        const QString secondId = messages->entry(messages->data(messages->index(1), MessageModel::IdRole).toLongLong()).xmppId;

        // Sent (as if the server took them), then their receipt, then read:
        // a read marker covers every one before it.
        History history;
        QVERIFY(history.open(dir.filePath("data/accounts/alice@example.org/history.sqlite")));
        QCOMPARE(history.pending().size(), 2);
        for (const HistoryEntry &entry : history.pending())
            history.setSendState(entry.id, HistoryEntry::Sent);
        QVERIFY(history.pending().isEmpty());
        backend.openChat("carol@example.org");
        backend.openChat("bob@example.org");
        QCOMPARE(messages->data(messages->index(1), MessageModel::SendStateRole).toInt(), int(HistoryEntry::Sent));

        QXmppMessage received("bob@example.org/pc", "alice@example.org");
        received.setType(QXmppMessage::Chat);
        received.setMarker(QXmppMessage::Received);
        received.setMarkerId(firstId);
        emit backend.client()->messageReceived(received);
        QCOMPARE(messages->data(messages->index(0), MessageModel::SendStateRole).toInt(), int(HistoryEntry::Delivered));
        QCOMPARE(messages->data(messages->index(1), MessageModel::SendStateRole).toInt(), int(HistoryEntry::Sent));

        QXmppMessage read("bob@example.org/pc", "alice@example.org");
        read.setType(QXmppMessage::Chat);
        read.setMarker(QXmppMessage::Displayed);
        read.setMarkerId(secondId);
        emit backend.client()->messageReceived(read);
        QCOMPARE(messages->data(messages->index(0), MessageModel::SendStateRole).toInt(), int(HistoryEntry::Read));
        QCOMPARE(messages->data(messages->index(1), MessageModel::SendStateRole).toInt(), int(HistoryEntry::Read));
        // Markers make no lines, and the history keeps the states.
        QCOMPARE(messages->rowCount(), 2);
        backend.openChat("carol@example.org");
        backend.openChat("bob@example.org");
        QCOMPARE(messages->data(messages->index(1), MessageModel::SendStateRole).toInt(), int(HistoryEntry::Read));

        // Telling them when we read theirs can be turned off.
        QVERIFY(backend.sendReadMarkers());
        backend.setSendReadMarkers(false);
        QVERIFY(!backend.sendReadMarkers());
    }

    void trayIconShowsNewMessagesFirst() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        Tray tray(&backend, nullptr, false);
        QCOMPARE(tray.iconName(), QString("omajim-status-offline-symbolic"));

        QSignalSpy newIcon(&tray, &Tray::NewIcon);
        backend.contacts()->upsert("bob@example.org", "Bob");
        backend.contacts()->addUnread("bob@example.org");
        QCOMPARE(tray.state(), QString("message-new"));
        QCOMPARE(newIcon.count(), 1);

        backend.contacts()->clearUnread("bob@example.org");
        QCOMPARE(tray.state(), QString("offline"));
    }

    void trayMenuSetsTheStatus() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.setStatus("offline");
        TrayMenu menu(&backend);

        DBusMenuLayoutItem layout;
        menu.GetLayout(0, -1, {}, layout);
        QCOMPARE(layout.children.size(), 4);
        const DBusMenuLayoutItem status = layout.children.at(1);
        QCOMPARE(status.properties.value("children-display").toString(), QString("submenu"));
        QCOMPARE(status.children.size(), 6);
        QCOMPARE(status.children.last().properties.value("toggle-state").toInt(), 1);

        // Only the top level, when asked for depth 0.
        menu.GetLayout(0, 0, {}, layout);
        QVERIFY(layout.children.isEmpty());

        // A status picked there is for every account: it's asked for.
        QSignalSpy picked(&menu, &TrayMenu::statusRequested);
        menu.Event(14, "clicked", QDBusVariant(0), 0);
        QCOMPARE(picked.count(), 1);
        QCOMPARE(picked.first().first().toString(), QString("dnd"));

        QSignalSpy quit(&menu, &TrayMenu::quitRequested);
        QVERIFY(menu.EventGroup({DBusMenuEvent{4, "clicked", QDBusVariant(0), 0}}).isEmpty());
        QCOMPARE(quit.count(), 1);
        QCOMPARE(menu.EventGroup({DBusMenuEvent{99, "clicked", QDBusVariant(0), 0}}), QList<int>{99});
    }

    void emojiCatalogFindsTonesAndRemembers() {
        QTemporaryDir dir;
        const QString settings = dir.filePath("omajim.conf");
        EmojiCatalog catalog(settings, QFINDTESTDATA("../src/emoji.tsv"));
        auto emojis = [](const QVariantList &items) {
            QStringList list;
            for (const QVariant &item : items)
                list << item.toMap().value("emoji").toString();
            return list;
        };

        // Every section has emoji; the first starts with the smileys.
        for (int section = 0; section < EmojiCatalog::sectionCount; ++section)
            QVERIFY(!catalog.section(section).isEmpty());
        QCOMPARE(emojis(catalog.section(0)).first(), QString::fromUtf8("😀"));

        // Found in Portuguese without accents, and in English.
        QVERIFY(emojis(catalog.search("coracao vermelho")).contains(QString::fromUtf8("❤️")));
        QCOMPARE(emojis(catalog.search("thumbs up")).first(), QString::fromUtf8("👍"));
        QVERIFY(catalog.search("nada-disso-existe").isEmpty());

        // A hand takes five tones, found again from any of them; a face none.
        const QStringList tones = catalog.tones(QString::fromUtf8("👍"));
        QCOMPARE(tones.size(), 5);
        QCOMPARE(tones.first(), QString::fromUtf8("👍🏻"));
        QCOMPARE(catalog.tones(tones.last()), tones);
        QVERIFY(catalog.tones(QString::fromUtf8("😀")).isEmpty());
        // Where the tone replaces the presentation selector, the plain one keeps it.
        QCOMPARE(catalog.tones(QString::fromUtf8("✌️")).first(), QString::fromUtf8("✌🏻"));

        // The last used first, with its tone, and kept for the next run.
        catalog.use(QString::fromUtf8("😀"));
        catalog.use(tones.at(2));
        catalog.use(QString::fromUtf8("😀"));
        QCOMPARE(emojis(catalog.recent()), (QStringList{QString::fromUtf8("😀"), tones.at(2)}));
        EmojiCatalog again(settings, QFINDTESTDATA("../src/emoji.tsv"));
        QCOMPARE(emojis(again.recent()), emojis(catalog.recent()));
    }

private:
    QObject m_taskContext;

    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(OmajimTests)
#include "omajim_tests.moc"
