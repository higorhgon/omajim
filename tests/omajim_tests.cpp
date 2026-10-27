#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QXmlStreamWriter>
#include <QXmppClient.h>
#include <QXmppMessage.h>
#include <QXmppMucData.h>
#include <QXmppMucManagerV2.h>
#include <QXmppPepBookmarkManager.h>
#include <QXmppResultSet.h>
#include <QXmppRosterIq.h>
#include <QXmppTask.h>

#include <QBuffer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QCryptographicHash>
#include <QImage>

#include "avatarstore.h"
#include "backend.h"
#include "contactmodel.h"
#include "filepicker.h"
#include "files.h"
#include "history.h"
#include "messagemodel.h"
#include "rosterstore.h"
#include "secretstore.h"
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
    void openFile(const QString &title) override { lastTitle = title; }
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
        QCOMPARE(backend.contacts()->rowCount(), 1);
        QCOMPARE(backend.contacts()->data(backend.contacts()->index(0), ContactModel::NameRole).toString(), QString("Bob"));
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

    void contactsListOnlinePeopleFirst() {
        ContactModel model;
        model.upsert("zoe@example.org", "Zoe");
        model.upsert("adam@example.org", "Adam");
        model.upsert("bea@example.org", {});
        QCOMPARE(model.jidAt(0), QString("adam@example.org"));

        model.setPresence("zoe@example.org", "away", "lunch");
        QCOMPARE(model.jidAt(0), QString("zoe@example.org"));
        QCOMPARE(model.data(model.index(0), ContactModel::StatusTextRole).toString(), QString("lunch"));
        QCOMPARE(model.data(model.index(2), ContactModel::NameRole).toString(), QString("bea"));

        model.setFilter("ad");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.jidAt(0), QString("adam@example.org"));
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
