#ifndef LOGBOOK_H
#define LOGBOOK_H

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

// One QSO as the server stores it.  JSON only at the edges (REST/WebSocket);
// ADIF on disk.  Kept as a plain struct rather than a QJsonObject so a
// lifetime log (100k+ records) stays small in memory.
struct QsoRecord {
    QString id;         // stable UUID, persisted as APP_WFWEB_ID
    QString date;       // YYYYMMDD (UTC)
    QString time;       // HHMMSS (UTC)
    QString call;
    qint64  freq = 0;   // Hz
    QString band;
    QString mode;
    QString grid;       // MY_GRIDSQUARE
    QString theirGrid;  // GRIDSQUARE
    QString rstSent;
    QString rstRcvd;
    QString comment;
    QString name;
    int     df = -1;    // digi audio offset in Hz, <0 = not set

    bool isValid() const { return !call.isEmpty(); }
    // Chronological sort key.  Entries without date/time sort first (oldest).
    QString sortKey() const { return date + time; }
    QJsonObject toJson() const;
    QByteArray toAdif() const;   // one <...> record terminated by <EOR>\n
    // Whitelists and length-caps untrusted input; empty call => invalid record.
    static QsoRecord fromJson(const QJsonObject &in, const QString &id = QString());
    // Trim / upper-case / cap every field and mint an id; the single
    // normalisation path for JSON input and ADIF parsing alike.
    QsoRecord normalized() const;
};

// The station logbook: a plain ADIF file on disk plus a chronological
// in-memory index.  Adds append one record; edits and deletes rewrite the
// file atomically.  Never hands out the whole log — callers page through it.
class Logbook {
public:
    struct Page {
        QList<QsoRecord> entries;   // newest first
        QString next;               // cursor for the following page, empty at the end
    };

    static QByteArray adifHeader();
    static QList<QsoRecord> parseAdif(const QByteArray &data);
    // False when the process runs in a container and `dir` sits on the
    // container's own root filesystem rather than a mounted volume, i.e. the
    // file dies with the container.  Linux only; elsewhere always true.
    static bool isPersistentLocation(const QString &dir);
    // Testable core of the above: `mountinfo` is /proc/self/mountinfo text.
    static bool isPersistentLocation(const QString &dir, const QString &mountinfo, bool inContainer);
    static QString cursorFor(const QsoRecord &r) { return r.sortKey() + QLatin1Char('|') + r.id; }

    // Sets the path, creates the file with a header if missing, otherwise
    // loads it.  Returns false if the file could not be read or created.
    bool open(const QString &path);
    const QString &path() const { return path_; }
    int count() const { return records_.size(); }

    bool add(QsoRecord &r);                       // assigns r.id if empty
    bool update(const QString &id, QsoRecord r);  // r.id is replaced by id
    bool remove(const QString &id);
    // Never discards data: a non-empty file is renamed to
    // <path>.<yyyyMMdd-HHmmss>.bak before the header-only file is written.
    bool clear(QString *backupPath = nullptr);
    // Adds entries not already present (same date, time, call, freq, mode).
    // Returns the number added; one file rewrite for the whole batch.
    int merge(const QList<QsoRecord> &incoming);
    const QsoRecord *find(const QString &id) const;

    // Up to `limit` entries older than `before` (a cursor from a previous
    // page, empty = start from the newest), optionally only those with the
    // given callsign.
    Page page(int limit, const QString &before, const QString &call) const;

private:
    int insertSorted(const QsoRecord &r);
    int indexOf(const QString &id) const;
    int lowerBound(const QString &sortKey, const QString &id) const;
    bool rewrite() const;
    bool append(const QsoRecord &r) const;

    QString path_;
    QList<QsoRecord> records_;   // ascending by (sortKey, id)
};

#endif // LOGBOOK_H
