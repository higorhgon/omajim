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
#include <QDomDocument>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTcpServer>
#include <QTcpSocket>
#include <QCryptographicHash>
#include <QImage>

#include "avatarstore.h"
#include "backend.h"
#include "contactmodel.h"
#include "emojicatalog.h"
#include "filepicker.h"
#include "files.h"
#include "firewall.h"
#include "gajimimport.h"
#include "history.h"
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
            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x)
                    image.setPixel(x, y, noisy ? qRgba(x * 7 % 256, y * 13 % 256, (x * y) % 256, 255) : qRgba(255, 0, 0, 255));
            const QString path = dir.filePath(name);
            image.save(path, "PNG");
            return path;
        };
        QVERIFY(store.add(":gato:", picture("gato.png", 24, false)).isEmpty());
        QVERIFY(!store.add(":gato:", picture("other.png", 24, false)).isEmpty());
        QVERIFY(!store.add("dois gatos", picture("other.png", 24, false)).isEmpty());
        QVERIFY(!store.add(":x:", dir.filePath("missing.png")).isEmpty());
        // Too big to fetch in one stanza: shrunk.
        QVERIFY(store.add(":big:", picture("big.png", 400, true)).isEmpty());
        QCOMPARE(store.own().size(), 2);

        const QHash<QString, QString> found = store.findOwn("olha :gato: e :big:");
        QCOMPARE(found.size(), 2);
        QVERIFY(found.value(":gato:").startsWith("sha1+"));
        QVERIFY(found.value(":gato:").endsWith("@bob.xmpp.org"));
        QString type;
        const QByteArray big = store.dataOf(found.value(":big:"), &type);
        QCOMPARE(type, QString("image/png"));
        QCOMPARE(QImage::fromData(big).size(), QSize(64, 64));
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

    void backendWontSendWhileOffline() {
        QTemporaryDir dir;
        FakeSecretStore secrets;
        Backend backend(dir.filePath("config"), dir.filePath("data"), nullptr, &secrets);
        backend.setStatus("offline");
        backend.signIn("alice@example.org", "secret", "127.0.0.1:1", false);
        backend.openChat("bob@example.org");
        QVERIFY(!backend.sendMessage("hi"));
        QVERIFY(!backend.errorText().isEmpty());
        QCOMPARE(backend.messages()->rowCount(), 0);
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

        menu.Event(14, "clicked", QDBusVariant(0), 0);
        QCOMPARE(backend.status(), QString("dnd"));
        backend.setStatus("offline");

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
