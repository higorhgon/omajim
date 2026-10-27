#pragma once

#include <QAbstractListModel>
#include <QHash>

#include "history.h"

// The open conversation, oldest first. File transfers keep their live
// progress here, beside what the history stores.
class MessageModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        BodyRole,
        OutgoingRole,
        StampRole,
        KindRole,
        FilePathRole,
        FileSizeRole,
        StateRole,
        ProgressRole,
        DeliveredRole,
        SenderRole,
        UrlRole,
        EditedRole,
        PreviewRole,
    };

    explicit MessageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(const QList<HistoryEntry> &entries);
    void append(const HistoryEntry &entry);
    void updateFile(qint64 id, const QString &state, const QString &filePath);
    void setProgress(qint64 id, double progress);
    void markDelivered(const QString &xmppId);
    void setLink(qint64 id, const QString &url, const QString &xmppId);
    void applyCorrection(qint64 id, const QString &body);
    // A picture to show for a file line, as a local path; not kept in the
    // history, since it's rebuilt from the files on disk.
    void setPreview(qint64 id, const QString &path);
    const QList<HistoryEntry> &entries() const { return m_entries; }
    // What the model knows of a line, for lines on screen; empty otherwise.
    QString url(qint64 id) const;
    QString filePath(qint64 id) const;

signals:
    void countChanged();

private:
    int rowOf(qint64 id) const;

    QList<HistoryEntry> m_entries;
    QHash<qint64, double> m_progress;
    QHash<qint64, QString> m_previews;
};
