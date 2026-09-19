#include "logbook.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>

// ---------------------------------------------------------------------------
// QsoRecord
// ---------------------------------------------------------------------------

QsoRecord QsoRecord::normalized() const
{
    QsoRecord r;
    r.call = call.trimmed().toUpper();
    if (r.call.isEmpty() || r.call.size() > 32)
        return QsoRecord();   // invalid

    r.id        = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    r.date      = date.trimmed().left(8);
    r.time      = time.trimmed().left(6);
    r.band      = band.trimmed().left(16);
    r.mode      = mode.trimmed().left(16).toUpper();
    r.grid      = grid.trimmed().left(16).toUpper();
    r.theirGrid = theirGrid.trimmed().left(16).toUpper();
    r.rstSent   = rstSent.trimmed().left(16);
    r.rstRcvd   = rstRcvd.trimmed().left(16);
    r.comment   = comment.trimmed().left(256);
    r.name      = name.trimmed().left(128);
    r.freq      = freq > 0 ? freq : 0;
    r.df        = df;
    return r;
}

QsoRecord QsoRecord::fromJson(const QJsonObject &in, const QString &id)
{
    QsoRecord r;
    r.id        = id;
    r.call      = in.value("call").toString();
    r.date      = in.value("date").toString();
    r.time      = in.value("time").toString();
    r.band      = in.value("band").toString();
    r.mode      = in.value("mode").toString();
    r.grid      = in.value("grid").toString();
    r.theirGrid = in.value("theirGrid").toString();
    r.rstSent   = in.value("rstSent").toString();
    r.rstRcvd   = in.value("rstRcvd").toString();
    r.comment   = in.value("comment").toString();
    r.name      = in.value("name").toString();
    r.freq      = in.value("freq").toVariant().toLongLong();
    if (in.value("df").isDouble())
        r.df = in.value("df").toInt();
    return r.normalized();
}

QJsonObject QsoRecord::toJson() const
{
    QJsonObject o;
    o["id"]   = id;
    o["call"] = call;
    o["freq"] = freq;
    const auto put = [&o](const char *key, const QString &value) {
        if (!value.isEmpty()) o[QLatin1String(key)] = value;
    };
    put("date", date);
    put("time", time);
    put("band", band);
    put("mode", mode);
    put("grid", grid);
    put("theirGrid", theirGrid);
    put("rstSent", rstSent);
    put("rstRcvd", rstRcvd);
    put("comment", comment);
    put("name", name);
    if (df >= 0) o["df"] = df;
    return o;
}

QByteArray QsoRecord::toAdif() const
{
    QByteArray out;
    // ADIF lengths are in bytes, so measure the UTF-8 encoding, not QChars.
    const auto field = [&out](const char *tag, const QString &value) {
        if (value.isEmpty()) return;
        const QByteArray bytes = value.toUtf8();
        out += '<';
        out += tag;
        out += ':';
        out += QByteArray::number(bytes.size());
        out += '>';
        out += bytes;
        out += ' ';
    };
    field("APP_WFWEB_ID", id);
    field("CALL", call);
    field("QSO_DATE", date);
    field("TIME_ON", time);
    if (freq > 0)
        field("FREQ", QString::number(freq / 1e6, 'f', 6));
    field("BAND", band);
    field("MODE", mode);
    field("RST_SENT", rstSent);
    field("RST_RCVD", rstRcvd);
    field("GRIDSQUARE", theirGrid);
    field("MY_GRIDSQUARE", grid);
    field("COMMENT", comment);
    field("NAME", name);
    if (df >= 0)
        field("APP_WFWEB_DF", QString::number(df));
    out += "<EOR>\n";
    return out;
}

// ---------------------------------------------------------------------------
// Logbook — static helpers
// ---------------------------------------------------------------------------

QByteArray Logbook::adifHeader()
{
    return QByteArray("ADIF Export from wfweb\n"
                      "<ADIF_VER:5>3.1.4 <PROGRAMID:5>wfweb <EOH>\n");
}

// Linear byte scanner: <NAME:len[:type]>value ... <EOR>.  Lengths are byte
// counts, tags are case-insensitive, anything outside <> is ignored, and
// whatever precedes <EOH> is discarded.  No regex and no QString copy of the
// whole file, so a 20 MB lifetime log loads in linear time.
QList<QsoRecord> Logbook::parseAdif(const QByteArray &data)
{
    QList<QsoRecord> out;
    const int n = data.size();
    int pos = 0;

    QHash<QByteArray, QByteArray> fields;
    const auto flush = [&fields, &out]() {
        if (fields.contains("CALL")) {
            QsoRecord r;
            r.id        = QString::fromUtf8(fields.value("APP_WFWEB_ID"));
            r.call      = QString::fromUtf8(fields.value("CALL"));
            r.date      = QString::fromUtf8(fields.value("QSO_DATE"));
            r.time      = QString::fromUtf8(fields.value("TIME_ON"));
            r.band      = QString::fromUtf8(fields.value("BAND"));
            r.mode      = QString::fromUtf8(fields.value("MODE"));
            r.rstSent   = QString::fromUtf8(fields.value("RST_SENT"));
            r.rstRcvd   = QString::fromUtf8(fields.value("RST_RCVD"));
            r.theirGrid = QString::fromUtf8(fields.value("GRIDSQUARE"));
            r.grid      = QString::fromUtf8(fields.value("MY_GRIDSQUARE"));
            r.comment   = QString::fromUtf8(fields.value("COMMENT"));
            r.name      = QString::fromUtf8(fields.value("NAME"));
            r.freq      = qRound64(fields.value("FREQ").toDouble() * 1e6);
            if (fields.contains("APP_WFWEB_DF"))
                r.df = fields.value("APP_WFWEB_DF").toInt();
            r = r.normalized();
            if (r.isValid()) out.append(r);
        }
        fields.clear();
    };

    while (pos < n) {
        const int lt = data.indexOf('<', pos);
        if (lt < 0) break;
        const int gt = data.indexOf('>', lt);
        if (gt < 0) break;
        const QByteArray tag = data.mid(lt + 1, gt - lt - 1);
        pos = gt + 1;

        if (tag.compare("EOR", Qt::CaseInsensitive) == 0) { flush(); continue; }
        if (tag.compare("EOH", Qt::CaseInsensitive) == 0) { fields.clear(); continue; }
        const int c1 = tag.indexOf(':');
        if (c1 < 0) continue;   // <EOH>, stray markup
        const int c2 = tag.indexOf(':', c1 + 1);
        int len = tag.mid(c1 + 1, c2 < 0 ? -1 : c2 - c1 - 1).toInt();
        if (len < 0) len = 0;
        if (pos + len > n) len = n - pos;
        fields[tag.left(c1).toUpper()] = data.mid(pos, len);
        pos += len;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Logbook — file
// ---------------------------------------------------------------------------

bool Logbook::open(const QString &path)
{
    path_ = path;
    records_.clear();
    QDir().mkpath(QFileInfo(path_).absolutePath());

    QFile file(path_);
    if (!file.exists())
        return rewrite();   // header only
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray data = file.readAll();
    file.close();
    records_ = parseAdif(data);
    std::sort(records_.begin(), records_.end(), [](const QsoRecord &a, const QsoRecord &b) {
        const QString ka = a.sortKey(), kb = b.sortKey();
        return ka != kb ? ka < kb : a.id < b.id;
    });

    // Records that came from another program have no APP_WFWEB_ID; fromJson
    // minted one, so persist it now or the ids change on every restart.
    // (Only true for freshly imported files — a normal reload rewrites nothing.)
    if (!records_.isEmpty() && !data.contains("APP_WFWEB_ID"))
        return rewrite();
    return true;
}

bool Logbook::rewrite() const
{
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(adifHeader());
    for (const QsoRecord &r : records_)
        file.write(r.toAdif());
    return file.commit();
}

bool Logbook::append(const QsoRecord &r) const
{
    if (!QFile::exists(path_))
        return rewrite();   // file was removed underneath us: rebuild with header
    QFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    const QByteArray rec = r.toAdif();
    return file.write(rec) == rec.size();
}

// ---------------------------------------------------------------------------
// Logbook — index
// ---------------------------------------------------------------------------

int Logbook::lowerBound(const QString &sortKey, const QString &id) const
{
    auto it = std::lower_bound(records_.cbegin(), records_.cend(), std::make_pair(sortKey, id),
        [](const QsoRecord &r, const std::pair<QString, QString> &key) {
            const QString k = r.sortKey();
            return k != key.first ? k < key.first : r.id < key.second;
        });
    return int(it - records_.cbegin());
}

int Logbook::insertSorted(const QsoRecord &r)
{
    const int idx = lowerBound(r.sortKey(), r.id);
    records_.insert(idx, r);
    return idx;
}

int Logbook::indexOf(const QString &id) const
{
    for (int i = 0; i < records_.size(); ++i)
        if (records_[i].id == id) return i;
    return -1;
}

const QsoRecord *Logbook::find(const QString &id) const
{
    const int idx = indexOf(id);
    return idx < 0 ? nullptr : &records_[idx];
}

bool Logbook::add(QsoRecord &r)
{
    if (!r.isValid()) return false;
    if (r.id.isEmpty()) r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int idx = insertSorted(r);
    if (append(r)) return true;
    records_.removeAt(idx);
    return false;
}

bool Logbook::update(const QString &id, QsoRecord r)
{
    const int idx = indexOf(id);
    if (idx < 0 || !r.isValid()) return false;
    const QList<QsoRecord> backup = records_;   // implicitly shared, cheap
    records_.removeAt(idx);
    r.id = id;
    insertSorted(r);
    if (rewrite()) return true;
    records_ = backup;
    return false;
}

bool Logbook::remove(const QString &id)
{
    const int idx = indexOf(id);
    if (idx < 0) return false;
    const QList<QsoRecord> backup = records_;
    records_.removeAt(idx);
    if (rewrite()) return true;
    records_ = backup;
    return false;
}

bool Logbook::clear()
{
    const QList<QsoRecord> backup = records_;
    records_.clear();
    if (rewrite()) return true;
    records_ = backup;
    return false;
}

int Logbook::merge(const QList<QsoRecord> &incoming)
{
    const auto dupKey = [](const QsoRecord &r) {
        return r.date + '|' + r.time + '|' + r.call + '|' + QString::number(r.freq) + '|' + r.mode;
    };
    QSet<QString> seen;
    for (const QsoRecord &r : records_) seen.insert(dupKey(r));
    const QList<QsoRecord> backup = records_;

    int added = 0;
    for (QsoRecord r : incoming) {
        if (!r.isValid()) continue;
        const QString key = dupKey(r);
        if (seen.contains(key)) continue;
        seen.insert(key);
        if (r.id.isEmpty()) r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        insertSorted(r);
        ++added;
    }
    if (added && !rewrite()) {
        records_ = backup;   // never leave memory and disk disagreeing
        return 0;
    }
    return added;
}

Logbook::Page Logbook::page(int limit, const QString &before, const QString &call) const
{
    Page result;
    if (limit <= 0) limit = 100;
    if (limit > 1000) limit = 1000;

    // Start just past the newest record, or at the cursor's own position so
    // the page holds strictly older entries.  A cursor whose record has since
    // been deleted still resolves to the right place via lower_bound.
    int end = records_.size();
    const int bar = before.indexOf('|');
    if (bar > 0)
        end = lowerBound(before.left(bar), before.mid(bar + 1));

    const QString wanted = call.trimmed().toUpper();
    int i = end;
    while (i > 0 && result.entries.size() < limit) {
        --i;
        if (!wanted.isEmpty() && records_[i].call != wanted) continue;
        result.entries.append(records_[i]);
    }
    if (i > 0 && !result.entries.isEmpty())
        result.next = cursorFor(result.entries.last());
    return result;
}
