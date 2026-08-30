#include "db/MetadataStore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QDebug>

#include <algorithm>

MetadataStore::MetadataStore(QObject *parent)
    : QObject(parent)
{
}

MetadataStore::~MetadataStore()
{
    if (open_) {
        QSqlDatabase::database(QStringLiteral("shengku_meta")).close();
        QSqlDatabase::removeDatabase(QStringLiteral("shengku_meta"));
    }
}

QString MetadataStore::normalizePath(const QString &path)
{
    QString p = QDir::cleanPath(path);
    // Windows 大小写不敏感 + 统一斜杠方向
#ifdef Q_OS_WIN
    p = p.replace(QLatin1Char('/'), QLatin1Char('\\'));
#endif
    return p;
}

bool MetadataStore::open(const QString &cacheDir)
{
    if (open_) {
        QSqlDatabase::database(QStringLiteral("shengku_meta")).close();
        QSqlDatabase::removeDatabase(QStringLiteral("shengku_meta"));
        open_ = false;
    }
    cacheDir_ = cacheDir;
    QDir().mkpath(cacheDir_);
    QDir().mkpath(cacheDir_ + QStringLiteral("/waveforms"));
    QDir().mkpath(cacheDir_ + QStringLiteral("/spectro"));
    dbPath_ = cacheDir_ + QStringLiteral("/metadata.db");

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                QStringLiteral("shengku_meta"));
    db.setDatabaseName(dbPath_);
    if (!db.open()) {
        qWarning() << "元数据数据库打开失败:" << dbPath_ << db.lastError().text();
        return false;
    }
    db.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    db.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    open_ = ensureSchema();
    return open_;
}

bool MetadataStore::ensureSchema()
{
    static const char *kSchema[] = {
        "CREATE TABLE IF NOT EXISTS files("
        "  path TEXT PRIMARY KEY,"
        "  size INTEGER DEFAULT 0,"
        "  mtime INTEGER DEFAULT 0,"
        "  duration REAL DEFAULT 0,"
        "  sampleRate INTEGER DEFAULT 0,"
        "  channels INTEGER DEFAULT 0,"
        "  bitDepth INTEGER DEFAULT 0,"
        "  format TEXT DEFAULT '',"
        "  rating INTEGER DEFAULT 0,"
        "  notes TEXT DEFAULT '',"
        "  favorite INTEGER DEFAULT 0,"
        "  lastPlayed INTEGER DEFAULT 0,"
        "  waveCache TEXT DEFAULT '',"
        "  specCache TEXT DEFAULT '',"
        "  ucsCat TEXT DEFAULT '',"
        "  ucsAuto INTEGER DEFAULT 0,"
        "  kind TEXT DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS tags("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT UNIQUE NOT NULL,"
        "  color TEXT DEFAULT '#5B9DB8')",
        "CREATE TABLE IF NOT EXISTS file_tags("
        "  path TEXT NOT NULL,"
        "  tagId INTEGER NOT NULL,"
        "  PRIMARY KEY(path, tagId))",
        "CREATE TABLE IF NOT EXISTS dim_tags("
        "  dim TEXT NOT NULL,"
        "  dim_cn TEXT NOT NULL,"
        "  tag_id TEXT NOT NULL,"
        "  cn TEXT NOT NULL,"
        "  en TEXT DEFAULT '',"
        "  grp TEXT DEFAULT '',"
        "  multi INTEGER DEFAULT 1,"
        "  PRIMARY KEY(dim, tag_id))",
        "CREATE TABLE IF NOT EXISTS file_dim_tags("
        "  path TEXT NOT NULL,"
        "  dim TEXT NOT NULL,"
        "  tag_id TEXT NOT NULL,"
        "  PRIMARY KEY(path, dim, tag_id))",
        "CREATE TABLE IF NOT EXISTS fav_groups("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  ord INTEGER DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS fav_group_items("
        "  groupId INTEGER NOT NULL,"
        "  path TEXT NOT NULL,"
        "  PRIMARY KEY(groupId, path))",
        "CREATE TABLE IF NOT EXISTS libraries("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  path TEXT UNIQUE NOT NULL,"
        "  name TEXT DEFAULT '',"
        "  ord INTEGER DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS history_search("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  text TEXT NOT NULL,"
        "  at INTEGER DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS audit_log("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts INTEGER DEFAULT 0,"
        "  source TEXT DEFAULT '',"
        "  tool TEXT DEFAULT '',"
        "  summary TEXT DEFAULT '',"
        "  result TEXT DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS ucs_cats("
        "  cat_id TEXT PRIMARY KEY,"
        "  zh TEXT DEFAULT '',"
        "  grp TEXT DEFAULT '')",
    };
    for (const char *sql : kSchema) {
        if (!exec(QString::fromLatin1(sql)))
            return false;
    }
    // 旧库升级：CREATE TABLE IF NOT EXISTS 不会改已存在的表，缺列必须靠 ALTER 补
    ensureColumn(QStringLiteral("files"), QStringLiteral("waveCache"),
                 QStringLiteral("waveCache TEXT DEFAULT ''"));
    ensureColumn(QStringLiteral("files"), QStringLiteral("specCache"),
                 QStringLiteral("specCache TEXT DEFAULT ''"));
    ensureColumn(QStringLiteral("files"), QStringLiteral("ucsCat"),
                 QStringLiteral("ucsCat TEXT DEFAULT ''"));
    ensureColumn(QStringLiteral("files"), QStringLiteral("ucsAuto"),
                 QStringLiteral("ucsAuto INTEGER DEFAULT 0"));
    ensureColumn(QStringLiteral("files"), QStringLiteral("kind"),
                 QStringLiteral("kind TEXT DEFAULT ''"));
    return true;
}

bool MetadataStore::ensureColumn(const QString &table, const QString &column,
                                 const QString &decl)
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        qWarning() << "读取表结构失败:" << table;
        return false;
    }
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0)
            return true;    // 列已存在
    }
    const bool ok = exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(table, decl));
    if (ok)
        qDebug() << "已为旧库补充列:" << table << column;
    return ok;
}

bool MetadataStore::exec(const QString &sql)
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (!q.exec(sql)) {
        qWarning() << "SQL 失败:" << sql << q.lastError().text();
        return false;
    }
    return true;
}

QString MetadataStore::waveCachePathFor(const QString &path) const
{
    const QString h = QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
    return cacheDir_ + QStringLiteral("/waveforms/") + h + QStringLiteral(".svp");
}

QString MetadataStore::specCachePathFor(const QString &path) const
{
    const QString h = QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
    return cacheDir_ + QStringLiteral("/spectro/") + h + QStringLiteral(".svp2");
}

// 前置声明（定义在"用户元数据"段）
static void updateUserColumn(const QString &path, const QString &column, const QVariant &value);

// ---------------------------------------------------------------- UCS

void MetadataStore::ensureUcsCatalog(const QByteArray &ucsJson)
{
    if (!open_ || ucsJson.isEmpty())
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(ucsJson);
    if (!doc.isArray())
        return;
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("shengku_meta"));
    db.transaction();
    // 组 → 父分类 → 子分类，全部拍平进 ucs_cats（cat_id 唯一）
    for (const QJsonValue &gv : doc.array()) {
        const QJsonObject g = gv.toObject();
        const QString grp = g.value(QStringLiteral("name")).toString();
        for (const QJsonValue &pv : g.value(QStringLiteral("cats")).toArray()) {
            const QJsonObject p = pv.toObject();
            const QString pid = p.value(QStringLiteral("id")).toString();
            const QString pzh = p.value(QStringLiteral("zh")).toString();
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO ucs_cats(cat_id, zh, grp) VALUES(?,?,?)"));
            q.addBindValue(pid);
            q.addBindValue(pzh);
            q.addBindValue(grp);
            q.exec();
            for (const QJsonValue &sv : p.value(QStringLiteral("subs")).toArray()) {
                const QJsonObject s = sv.toObject();
                QSqlQuery sq(db);
                sq.prepare(QStringLiteral(
                    "INSERT OR REPLACE INTO ucs_cats(cat_id, zh, grp) VALUES(?,?,?)"));
                sq.addBindValue(s.value(QStringLiteral("id")).toString());
                sq.addBindValue(s.value(QStringLiteral("zh")).toString());
                sq.addBindValue(grp);
                sq.exec();
            }
        }
    }
    db.commit();
}

QVector<UcsCat> MetadataStore::ucsCategories() const
{
    QVector<UcsCat> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (!q.exec(QStringLiteral(
            "SELECT c.cat_id, c.zh, "
            "(SELECT COUNT(*) FROM files f WHERE f.ucsCat = c.cat_id) AS cnt "
            "FROM ucs_cats c ORDER BY c.zh")))
        return out;
    while (q.next()) {
        UcsCat u;
        u.id = q.value(0).toString();
        u.zh = q.value(1).toString();
        u.count = q.value(2).toInt();
        out.append(u);
    }
    return out;
}

QString MetadataStore::detectUcs(const QString &fileName) const
{
    if (!open_ || fileName.isEmpty())
        return QString();
    // 文件名转大写，按分隔符拆 token，匹配 CatID
    const QString base = QFileInfo(fileName).completeBaseName();
    const QString upper = base.toUpper();
    // 1) 直接匹配完整 CatID token（如 FOOTSTEPS_GRASS）
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (!q.exec(QStringLiteral("SELECT cat_id, zh FROM ucs_cats")))
        return QString();
    // 收集所有 cat_id，按长度降序匹配（优先更长、更具体的 CatID）
    QVector<QPair<QString, QString>> cats;
    while (q.next())
        cats.append({q.value(0).toString(), q.value(1).toString()});
    std::sort(cats.begin(), cats.end(),
              [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });

    const QStringList tokens = upper.split(QRegularExpression(QStringLiteral("[_\\-\\s\\.]+")),
                                           Qt::SkipEmptyParts);
    // 1) 完整 base name 包含较长 CatID（含下划线更具体，如 FOOTSTEPS_GRASS），按长度降序
    for (const auto &c : cats) {
        if (c.first.size() < 5)
            continue;
        if (upper.contains(c.first))
            return c.first;
    }
    // 2) token 精确等于 CatID（短 CatID 兜底）
    for (const QString &tk : tokens) {
        if (tk.size() < 3)
            continue;
        for (const auto &c : cats) {
            if (c.first == tk)
                return c.first;
        }
    }
    return QString();
}

void MetadataStore::setUcs(const QString &path, const QString &catId, bool autoDetected)
{
    if (!open_)
        return;
    ensureRow(path);
    updateUserColumn(path, QStringLiteral("ucsCat"), catId);
    updateUserColumn(path, QStringLiteral("ucsAuto"), autoDetected ? 1 : 0);
    emit dataChanged();
}

void MetadataStore::setSpecCache(const QString &path, const QString &cachePath)
{
    if (!open_)
        return;
    ensureRow(path);
    updateUserColumn(path, QStringLiteral("specCache"), cachePath);
}

void MetadataStore::clearAnalysisCache(const QString &path)
{
    if (!open_)
        return;
    const QString p = normalizePath(path);
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("SELECT waveCache, specCache FROM files WHERE path = ?"));
    q.addBindValue(p);
    if (q.exec() && q.next()) {
        for (int i = 0; i < 2; ++i) {
            const QString f = q.value(i).toString();
            if (!f.isEmpty())
                QFile::remove(f);
        }
    }
    QSqlQuery q2(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q2.prepare(QStringLiteral(
        "UPDATE files SET waveCache='', specCache='', duration=0 WHERE path = ?"));
    q2.addBindValue(p);
    q2.exec();
}

QVector<FileRecord> MetadataStore::filesWithUcs(const QString &catId) const
{
    QVector<FileRecord> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("SELECT path FROM files WHERE ucsCat = ?"));
    q.addBindValue(catId);
    if (!q.exec())
        return out;
    while (q.next()) {
        FileRecord r;
        r.path = q.value(0).toString();
        r.name = QFileInfo(r.path).fileName();
        out.append(r);
    }
    loadInto(out);
    return out;
}

// ---------------------------------------------------------------- 业务大类

void MetadataStore::setKind(const QString &path, const QString &kind)
{
    if (!open_)
        return;
    ensureRow(path);
    updateUserColumn(path, QStringLiteral("kind"), kind);
    emit dataChanged();
}

QVector<FileRecord> MetadataStore::filesWithKind(const QString &kind) const
{
    QVector<FileRecord> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("SELECT path FROM files WHERE kind = ?"));
    q.addBindValue(kind);
    if (!q.exec())
        return out;
    while (q.next()) {
        FileRecord r;
        r.path = q.value(0).toString();
        r.name = QFileInfo(r.path).fileName();
        out.append(r);
    }
    loadInto(out);
    return out;
}

int MetadataStore::kindCount(const QString &kind) const
{
    if (!open_)
        return 0;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM files WHERE kind = ?"));
    q.addBindValue(kind);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}

// ---------------------------------------------------------------- 属性缓存

void MetadataStore::upsertAudioInfo(const QString &path, qint64 size, qint64 mtime,
                                    double duration, int sampleRate, int channels,
                                    int bitDepth, const QString &waveCache)
{
    if (!open_)
        return;
    const QString p = normalizePath(path);
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "INSERT INTO files(path,size,mtime,duration,sampleRate,channels,bitDepth,waveCache) "
        "VALUES(?,?,?,?,?,?,?,?) "
        "ON CONFLICT(path) DO UPDATE SET size=excluded.size, mtime=excluded.mtime, "
        "duration=excluded.duration, sampleRate=excluded.sampleRate, "
        "channels=excluded.channels, bitDepth=excluded.bitDepth, waveCache=excluded.waveCache"));
    q.addBindValue(p);
    q.addBindValue(size);
    q.addBindValue(mtime);
    q.addBindValue(duration);
    q.addBindValue(sampleRate);
    q.addBindValue(channels);
    q.addBindValue(bitDepth);
    q.addBindValue(waveCache);
    if (!q.exec())
        qWarning() << "upsertAudioInfo 失败:" << q.lastError().text();
}

void MetadataStore::ensureRow(const QString &path, qint64 size, qint64 mtime)
{
    if (!open_)
        return;
    const QString p = normalizePath(path);
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "INSERT INTO files(path,size,mtime) VALUES(?,?,?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "size=CASE WHEN excluded.size>0 THEN excluded.size ELSE size END, "
        "mtime=CASE WHEN excluded.mtime>0 THEN excluded.mtime ELSE mtime END"));
    q.addBindValue(p);
    q.addBindValue(size);
    q.addBindValue(mtime);
    if (!q.exec())
        qWarning() << "ensureRow 失败:" << q.lastError().text();
}

bool MetadataStore::audioInfoFresh(const QString &path, qint64 size, qint64 mtime) const
{
    if (!open_)
        return false;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "SELECT size, mtime, waveCache, duration FROM files WHERE path = ?"));
    q.addBindValue(normalizePath(path));
    if (!q.exec() || !q.next())
        return false;
    const QString wc = q.value(2).toString();
    return q.value(0).toLongLong() == size
        && q.value(1).toLongLong() == mtime
        && !wc.isEmpty()
        && QFileInfo::exists(wc)
        && q.value(3).toDouble() > 0;
}

// ---------------------------------------------------------------- 读取合并

void MetadataStore::loadInto(FileRecord &rec) const
{
    QVector<FileRecord> v;
    v.append(rec);
    loadInto(v);
    rec = v.first();
}

void MetadataStore::loadInto(QVector<FileRecord> &recs) const
{
    if (!open_ || recs.isEmpty())
        return;
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("shengku_meta"));
    db.transaction();

    // 1) 属性 + 标注
    for (FileRecord &rec : recs) {
        const QString p = normalizePath(rec.path);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT duration,sampleRate,channels,bitDepth,rating,notes,favorite,"
            "lastPlayed,waveCache,specCache,ucsCat,ucsAuto,kind FROM files WHERE path = ?"));
        q.addBindValue(p);
        if (q.exec() && q.next()) {
            if (rec.duration <= 0)
                rec.duration = q.value(0).toDouble();
            if (rec.sampleRate <= 0)
                rec.sampleRate = q.value(1).toInt();
            if (rec.channels <= 0)
                rec.channels = q.value(2).toInt();
            if (rec.bitDepth <= 0)
                rec.bitDepth = q.value(3).toInt();
            rec.rating = q.value(4).toInt();
            rec.notes = q.value(5).toString();
            rec.favorite = q.value(6).toBool();
            rec.lastPlayed = QDateTime::fromMSecsSinceEpoch(q.value(7).toLongLong() * 1000);
            const QString wc = q.value(8).toString();
            if (!wc.isEmpty() && QFileInfo::exists(wc))
                rec.waveCache = wc;
            const QString sc = q.value(9).toString();
            if (!sc.isEmpty() && QFileInfo::exists(sc))
                rec.specCache = sc;
            rec.ucsCat = q.value(10).toString();
            rec.ucsAuto = q.value(11).toInt() != 0;
            rec.kind = q.value(12).toString();
            // UCS 中文名
            if (!rec.ucsCat.isEmpty()) {
                QSqlQuery uq(db);
                uq.prepare(QStringLiteral("SELECT zh FROM ucs_cats WHERE cat_id = ?"));
                uq.addBindValue(rec.ucsCat);
                if (uq.exec() && uq.next())
                    rec.ucsNameZh = uq.value(0).toString();
            }
        }
        rec.tags.clear();
        rec.facetRefs.clear();
    }

    // 2) 标签（一次性取全量 tag 名 + 当前文件夹的关联）
    QHash<QString, QString> tagNames;   // id -> name
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT id, name FROM tags"));
        if (q.exec())
            while (q.next())
                tagNames.insert(q.value(0).toString(), q.value(1).toString());
    }
    for (FileRecord &rec : recs) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT tagId FROM file_tags WHERE path = ?"));
        q.addBindValue(normalizePath(rec.path));
        if (q.exec())
            while (q.next()) {
                const QString n = tagNames.value(q.value(0).toString());
                if (!n.isEmpty())
                    rec.tags.append(n);
            }
    }

    // 3) 多维 facet 标签
    for (FileRecord &rec : recs) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT dim, tag_id FROM file_dim_tags WHERE path = ?"));
        q.addBindValue(normalizePath(rec.path));
        if (q.exec())
            while (q.next())
                rec.facetRefs.append(q.value(0).toString() + QLatin1Char(':')
                                     + q.value(1).toString());
    }
    db.commit();
}

// ---------------------------------------------------------------- 用户元数据

static void updateUserColumn(const QString &path, const QString &column,
                             const QVariant &value)
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("UPDATE files SET %1 = ? WHERE path = ?").arg(column));
    q.addBindValue(value);
    q.addBindValue(MetadataStore::normalizePath(path));
    if (!q.exec())
        qWarning() << "更新失败:" << column << q.lastError().text();
}

void MetadataStore::setRating(const QString &path, int rating)
{
    if (!open_)
        return;
    ensureRow(path);
    updateUserColumn(path, QStringLiteral("rating"), qBound(0, rating, 5));
    emit dataChanged();
}

void MetadataStore::setNotes(const QString &path, const QString &notes)
{
    if (!open_)
        return;
    ensureRow(path);
    updateUserColumn(path, QStringLiteral("notes"), notes);
    emit dataChanged();
}

void MetadataStore::setFavorite(const QString &path, bool favorite)
{
    if (!open_)
        return;
    ensureRow(path);
    updateUserColumn(path, QStringLiteral("favorite"), favorite ? 1 : 0);
    emit dataChanged();
}

void MetadataStore::recordPlay(const QString &path)
{
    if (!open_)
        return;
    ensureRow(path);
    updateUserColumn(path, QStringLiteral("lastPlayed"),
                     QDateTime::currentSecsSinceEpoch());
}

// ---------------------------------------------------------------- 标签

QVector<TagInfo> MetadataStore::tags() const
{
    QVector<TagInfo> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (!q.exec(QStringLiteral(
            "SELECT t.id, t.name, t.color, COUNT(ft.path) AS c "
            "FROM tags t LEFT JOIN file_tags ft ON ft.tagId = t.id "
            "GROUP BY t.id ORDER BY c DESC, t.name")))
        return out;
    while (q.next()) {
        TagInfo t;
        t.id = q.value(0).toLongLong();
        t.name = q.value(1).toString();
        t.color = q.value(2).toString();
        t.count = q.value(3).toInt();
        out.append(t);
    }
    return out;
}

qint64 MetadataStore::addTag(const QString &name, const QString &color)
{
    if (!open_ || name.trimmed().isEmpty())
        return 0;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "INSERT INTO tags(name, color) VALUES(?, ?) "
        "ON CONFLICT(name) DO UPDATE SET color = excluded.color"));
    q.addBindValue(name.trimmed());
    q.addBindValue(color);
    if (!q.exec()) {
        qWarning() << "addTag 失败:" << q.lastError().text();
        return 0;
    }
    QSqlQuery q2(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q2.prepare(QStringLiteral("SELECT id FROM tags WHERE name = ?"));
    q2.addBindValue(name.trimmed());
    if (q2.exec() && q2.next())
        return q2.value(0).toLongLong();
    return 0;
}

void MetadataStore::renameTag(qint64 id, const QString &newName)
{
    if (!open_ || newName.trimmed().isEmpty())
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("UPDATE tags SET name = ? WHERE id = ?"));
    q.addBindValue(newName.trimmed());
    q.addBindValue(id);
    q.exec();
    emit dataChanged();
}

void MetadataStore::deleteTag(qint64 id)
{
    if (!open_)
        return;
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("shengku_meta"));
    db.transaction();
    QSqlQuery q1(db);
    q1.prepare(QStringLiteral("DELETE FROM file_tags WHERE tagId = ?"));
    q1.addBindValue(id);
    q1.exec();
    QSqlQuery q2(db);
    q2.prepare(QStringLiteral("DELETE FROM tags WHERE id = ?"));
    q2.addBindValue(id);
    q2.exec();
    db.commit();
    emit dataChanged();
}

void MetadataStore::assignTag(const QString &path, qint64 tagId)
{
    if (!open_ || tagId <= 0)
        return;
    ensureRow(path);
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO file_tags(path, tagId) VALUES(?, ?)"));
    q.addBindValue(normalizePath(path));
    q.addBindValue(tagId);
    q.exec();
    emit dataChanged();
}

void MetadataStore::unassignTag(const QString &path, qint64 tagId)
{
    if (!open_)
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "DELETE FROM file_tags WHERE path = ? AND tagId = ?"));
    q.addBindValue(normalizePath(path));
    q.addBindValue(tagId);
    q.exec();
    emit dataChanged();
}

void MetadataStore::setTagColor(qint64 id, const QString &color)
{
    if (!open_)
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("UPDATE tags SET color = ? WHERE id = ?"));
    q.addBindValue(color);
    q.addBindValue(id);
    q.exec();
    emit dataChanged();
}

bool MetadataStore::mergeTags(qint64 fromId, qint64 toId)
{
    if (!open_ || fromId <= 0 || toId <= 0 || fromId == toId)
        return false;
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("shengku_meta"));
    db.transaction();
    // 转移关联（已带 toId 的不重复插入）
    QSqlQuery q1(db);
    q1.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO file_tags(path, tagId) "
        "SELECT path, ? FROM file_tags WHERE tagId = ?"));
    q1.addBindValue(toId);
    q1.addBindValue(fromId);
    q1.exec();
    QSqlQuery q2(db);
    q2.prepare(QStringLiteral("DELETE FROM file_tags WHERE tagId = ?"));
    q2.addBindValue(fromId);
    q2.exec();
    QSqlQuery q3(db);
    q3.prepare(QStringLiteral("DELETE FROM tags WHERE id = ?"));
    q3.addBindValue(fromId);
    q3.exec();
    db.commit();
    emit dataChanged();
    return true;
}

int MetadataStore::assignTagByNames(const QStringList &paths, const QString &tagName)
{
    if (!open_ || tagName.trimmed().isEmpty() || paths.isEmpty())
        return 0;
    const qint64 tagId = addTag(tagName.trimmed());
    if (tagId <= 0)
        return 0;
    int n = 0;
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("shengku_meta"));
    db.transaction();
    for (const QString &p : paths) {
        if (p.isEmpty())
            continue;
        ensureRow(p);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO file_tags(path, tagId) VALUES(?, ?)"));
        q.addBindValue(normalizePath(p));
        q.addBindValue(tagId);
        if (q.exec())
            ++n;
    }
    db.commit();
    emit dataChanged();
    return n;
}

QStringList MetadataStore::tagNames(const QString &path) const
{
    QStringList out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "SELECT t.name FROM tags t JOIN file_tags ft ON ft.tagId = t.id "
        "WHERE ft.path = ?"));
    q.addBindValue(normalizePath(path));
    if (q.exec())
        while (q.next())
            out << q.value(0).toString();
    return out;
}

// ---------------------------------------------------------------- 多维 facet

void MetadataStore::ensureFacetCatalog(const QByteArray &facetsJson)
{
    if (!open_ || facetsJson.isEmpty())
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(facetsJson);
    if (!doc.isArray())
        return;
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("shengku_meta"));
    db.transaction();
    for (const QJsonValue &dv : doc.array()) {
        const QJsonObject d = dv.toObject();
        const QString dim = d.value(QStringLiteral("dim")).toString();
        const QString dimCn = d.value(QStringLiteral("cn")).toString();
        const bool multi = d.value(QStringLiteral("multi")).toBool(true);
        for (const QJsonValue &tv : d.value(QStringLiteral("tags")).toArray()) {
            const QJsonObject t = tv.toObject();
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO dim_tags(dim, dim_cn, tag_id, cn, en, grp, multi) "
                "VALUES(?,?,?,?,?,?,?)"));
            q.addBindValue(dim);
            q.addBindValue(dimCn);
            q.addBindValue(t.value(QStringLiteral("id")).toString());
            q.addBindValue(t.value(QStringLiteral("cn")).toString());
            q.addBindValue(t.value(QStringLiteral("en")).toString());
            q.addBindValue(t.value(QStringLiteral("group")).toString());
            q.addBindValue(multi ? 1 : 0);
            q.exec();
        }
    }
    db.commit();
}

QVector<FacetDim> MetadataStore::facets() const
{
    QVector<FacetDim> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (!q.exec(QStringLiteral(
            "SELECT dim, dim_cn, multi, tag_id, cn, en, grp FROM dim_tags "
            "ORDER BY dim, tag_id")))
        return out;
    FacetDim *cur = nullptr;
    while (q.next()) {
        const QString dim = q.value(0).toString();
        if (!cur || cur->dim != dim) {
            FacetDim d;
            d.dim = dim;
            d.cn = q.value(1).toString();
            d.multi = q.value(2).toInt() != 0;
            out.append(d);
            cur = &out.last();
        }
        FacetTag t;
        t.id = q.value(3).toString();
        t.cn = q.value(4).toString();
        t.en = q.value(5).toString();
        t.group = q.value(6).toString();
        cur->tags.append(t);
    }
    // 使用计数
    QHash<QString, int> counts;
    QSqlQuery cq(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (cq.exec(QStringLiteral("SELECT dim, tag_id, COUNT(*) FROM file_dim_tags GROUP BY dim, tag_id")))
        while (cq.next())
            counts.insert(cq.value(0).toString() + ':' + cq.value(1).toString(), cq.value(2).toInt());
    for (FacetDim &d : out)
        for (FacetTag &t : d.tags)
            t.count = counts.value(d.dim + ':' + t.id);
    return out;
}

void MetadataStore::setFacetTag(const QString &path, const QString &dim,
                                const QString &tagId, bool on)
{
    if (!open_)
        return;
    ensureRow(path);
    const QString p = normalizePath(path);
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (on) {
        q.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO file_dim_tags(path, dim, tag_id) VALUES(?,?,?)"));
    } else {
        q.prepare(QStringLiteral(
            "DELETE FROM file_dim_tags WHERE path = ? AND dim = ? AND tag_id = ?"));
    }
    q.addBindValue(p);
    q.addBindValue(dim);
    q.addBindValue(tagId);
    q.exec();
    emit dataChanged();
}

QHash<QString, QString> MetadataStore::facetTagNames(const QString &dim) const
{
    QHash<QString, QString> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("SELECT tag_id, cn FROM dim_tags WHERE dim = ?"));
    q.addBindValue(dim);
    if (q.exec())
        while (q.next())
            out.insert(q.value(0).toString(), q.value(1).toString());
    return out;
}

void MetadataStore::addFacetTag(const QString &dim, const QString &dimCn,
                                const QString &tagId, const QString &cn)
{
    if (!open_)
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO dim_tags(dim, dim_cn, tag_id, cn, en, grp, multi) "
        "VALUES(?,?,?,?,?,?,1)"));
    q.addBindValue(dim);
    q.addBindValue(dimCn);
    q.addBindValue(tagId);
    q.addBindValue(cn);
    q.addBindValue(QString());
    q.addBindValue(QString());
    q.exec();
    emit dataChanged();
}

void MetadataStore::renameFacetTag(const QString &dim, const QString &tagId,
                                   const QString &newCn)
{
    if (!open_)
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("UPDATE dim_tags SET cn = ? WHERE dim = ? AND tag_id = ?"));
    q.addBindValue(newCn);
    q.addBindValue(dim);
    q.addBindValue(tagId);
    q.exec();
    emit dataChanged();
}

void MetadataStore::removeFacetTag(const QString &dim, const QString &tagId)
{
    if (!open_)
        return;
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("shengku_meta"));
    db.transaction();
    QSqlQuery q1(db);
    q1.prepare(QStringLiteral("DELETE FROM dim_tags WHERE dim = ? AND tag_id = ?"));
    q1.addBindValue(dim);
    q1.addBindValue(tagId);
    q1.exec();
    QSqlQuery q2(db);
    q2.prepare(QStringLiteral("DELETE FROM file_dim_tags WHERE dim = ? AND tag_id = ?"));
    q2.addBindValue(dim);
    q2.addBindValue(tagId);
    q2.exec();
    db.commit();
    emit dataChanged();
}

void MetadataStore::renameFacetDim(const QString &dim, const QString &newCn)
{
    if (!open_)
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("UPDATE dim_tags SET dim_cn = ? WHERE dim = ?"));
    q.addBindValue(newCn);
    q.addBindValue(dim);
    q.exec();
    emit dataChanged();
}

void MetadataStore::removeFacetDim(const QString &dim)
{
    if (!open_)
        return;
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("shengku_meta"));
    db.transaction();
    QSqlQuery q1(db);
    q1.prepare(QStringLiteral("DELETE FROM dim_tags WHERE dim = ?"));
    q1.addBindValue(dim);
    q1.exec();
    QSqlQuery q2(db);
    q2.prepare(QStringLiteral("DELETE FROM file_dim_tags WHERE dim = ?"));
    q2.addBindValue(dim);
    q2.exec();
    db.commit();
    emit dataChanged();
}

// ---------------------------------------------------------------- 收藏分组

QVector<FavGroup> MetadataStore::favGroups() const
{
    QVector<FavGroup> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (!q.exec(QStringLiteral(
            "SELECT g.id, g.name, COUNT(i.path) FROM fav_groups g "
            "LEFT JOIN fav_group_items i ON i.groupId = g.id "
            "GROUP BY g.id ORDER BY g.ord, g.id")))
        return out;
    while (q.next()) {
        FavGroup g;
        g.id = q.value(0).toLongLong();
        g.name = q.value(1).toString();
        g.count = q.value(2).toInt();
        out.append(g);
    }
    return out;
}

qint64 MetadataStore::createFavGroup(const QString &name)
{
    if (!open_ || name.trimmed().isEmpty())
        return 0;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("INSERT INTO fav_groups(name) VALUES(?)"));
    q.addBindValue(name.trimmed());
    if (!q.exec())
        return 0;
    emit dataChanged();
    return q.lastInsertId().toLongLong();
}

void MetadataStore::renameFavGroup(qint64 id, const QString &name)
{
    if (!open_ || name.trimmed().isEmpty())
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("UPDATE fav_groups SET name = ? WHERE id = ?"));
    q.addBindValue(name.trimmed());
    q.addBindValue(id);
    q.exec();
    emit dataChanged();
}

void MetadataStore::deleteFavGroup(qint64 id)
{
    if (!open_)
        return;
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("shengku_meta"));
    db.transaction();
    QSqlQuery q1(db);
    q1.prepare(QStringLiteral("DELETE FROM fav_group_items WHERE groupId = ?"));
    q1.addBindValue(id);
    q1.exec();
    QSqlQuery q2(db);
    q2.prepare(QStringLiteral("DELETE FROM fav_groups WHERE id = ?"));
    q2.addBindValue(id);
    q2.exec();
    db.commit();
    emit dataChanged();
}

void MetadataStore::addToFavGroup(qint64 groupId, const QString &path)
{
    if (!open_ || groupId <= 0)
        return;
    ensureRow(path);
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO fav_group_items(groupId, path) VALUES(?, ?)"));
    q.addBindValue(groupId);
    q.addBindValue(normalizePath(path));
    q.exec();
    emit dataChanged();
}

void MetadataStore::removeFromFavGroup(qint64 groupId, const QString &path)
{
    if (!open_)
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "DELETE FROM fav_group_items WHERE groupId = ? AND path = ?"));
    q.addBindValue(groupId);
    q.addBindValue(normalizePath(path));
    q.exec();
    emit dataChanged();
}

QVector<FileRecord> MetadataStore::favGroupItems(qint64 groupId) const
{
    QVector<FileRecord> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "SELECT path FROM fav_group_items WHERE groupId = ?"));
    q.addBindValue(groupId);
    if (!q.exec())
        return out;
    while (q.next()) {
        FileRecord r;
        r.path = q.value(0).toString();
        r.name = QFileInfo(r.path).fileName();
        out.append(r);
    }
    loadInto(out);
    return out;
}

// ---------------------------------------------------------------- 多库

QVector<LibraryRoot> MetadataStore::libraries() const
{
    QVector<LibraryRoot> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (!q.exec(QStringLiteral("SELECT id, path, name FROM libraries ORDER BY ord, id")))
        return out;
    while (q.next()) {
        LibraryRoot l;
        l.id = q.value(0).toLongLong();
        l.path = q.value(1).toString();
        l.name = q.value(2).toString();
        out.append(l);
    }
    return out;
}

qint64 MetadataStore::addLibrary(const QString &path, const QString &name)
{
    if (!open_)
        return 0;
    const QString p = normalizePath(path);
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO libraries(path, name) VALUES(?, ?)"));
    q.addBindValue(p);
    q.addBindValue(name.isEmpty() ? QFileInfo(p).fileName() : name);
    if (!q.exec())
        return 0;
    QSqlQuery q2(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q2.prepare(QStringLiteral("SELECT id FROM libraries WHERE path = ?"));
    q2.addBindValue(p);
    if (q2.exec() && q2.next())
        return q2.value(0).toLongLong();
    return 0;
}

void MetadataStore::renameLibrary(qint64 id, const QString &name)
{
    if (!open_)
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("UPDATE libraries SET name = ? WHERE id = ?"));
    q.addBindValue(name);
    q.addBindValue(id);
    q.exec();
    emit dataChanged();
}

void MetadataStore::removeLibrary(qint64 id)
{
    if (!open_)
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("DELETE FROM libraries WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
    emit dataChanged();
}

// ---------------------------------------------------------------- 历史 / 审计

void MetadataStore::recordSearch(const QString &text)
{
    if (!open_ || text.trimmed().isEmpty())
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "INSERT INTO history_search(text, at) VALUES(?, ?)"));
    q.addBindValue(text.trimmed());
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
}

QStringList MetadataStore::recentSearches(int limit) const
{
    QStringList out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "SELECT text, MAX(at) FROM history_search GROUP BY text "
        "ORDER BY MAX(at) DESC LIMIT ?"));
    q.addBindValue(limit);
    if (q.exec())
        while (q.next())
            out << q.value(0).toString();
    return out;
}

void MetadataStore::audit(const QString &source, const QString &tool,
                          const QString &summary, const QString &result)
{
    if (!open_)
        return;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "INSERT INTO audit_log(ts, source, tool, summary, result) VALUES(?,?,?,?,?)"));
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(source);
    q.addBindValue(tool);
    q.addBindValue(summary);
    q.addBindValue(result);
    q.exec();
}

qint64 MetadataStore::fileCount() const
{
    if (!open_)
        return 0;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM files")) && q.next())
        return q.value(0).toLongLong();
    return 0;
}

// ---------------------------------------------------------------- 全库视图

QVector<FileRecord> MetadataStore::favorites() const
{
    QVector<FileRecord> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    if (!q.exec(QStringLiteral("SELECT path FROM files WHERE favorite = 1")))
        return out;
    while (q.next()) {
        FileRecord r;
        r.path = q.value(0).toString();
        r.name = QFileInfo(r.path).fileName();
        out.append(r);
    }
    loadInto(out);
    return out;
}

QVector<FileRecord> MetadataStore::filesWithTag(qint64 tagId) const
{
    QVector<FileRecord> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral("SELECT path FROM file_tags WHERE tagId = ?"));
    q.addBindValue(tagId);
    if (!q.exec())
        return out;
    while (q.next()) {
        FileRecord r;
        r.path = q.value(0).toString();
        r.name = QFileInfo(r.path).fileName();
        out.append(r);
    }
    loadInto(out);
    return out;
}

QVector<FileRecord> MetadataStore::filesWithFacet(const QString &dim, const QString &tagId) const
{
    QVector<FileRecord> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "SELECT path FROM file_dim_tags WHERE dim = ? AND tag_id = ?"));
    q.addBindValue(dim);
    q.addBindValue(tagId);
    if (!q.exec())
        return out;
    while (q.next()) {
        FileRecord r;
        r.path = q.value(0).toString();
        r.name = QFileInfo(r.path).fileName();
        out.append(r);
    }
    loadInto(out);
    return out;
}

QVector<FileRecord> MetadataStore::recentlyPlayed(int limit) const
{
    QVector<FileRecord> out;
    if (!open_)
        return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("shengku_meta")));
    q.prepare(QStringLiteral(
        "SELECT path FROM files WHERE lastPlayed > 0 "
        "ORDER BY lastPlayed DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec())
        return out;
    while (q.next()) {
        FileRecord r;
        r.path = q.value(0).toString();
        r.name = QFileInfo(r.path).fileName();
        out.append(r);
    }
    loadInto(out);
    return out;
}
