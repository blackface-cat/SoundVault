#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QDateTime>

class QSqlDatabase;

/**
 * 元数据存储（v0.9）
 *
 * 设计原则：文件系统是唯一事实来源，本库只做「缓存 + 用户标注」。
 *  - 键 = 规范化后的绝对路径（换素材根文件夹不丢标注）
 *  - 所有数据（metadata.db + 波形峰值缓存）都放在用户指定的缓存文件夹里
 *  - 音频属性（时长/采样率/声道/位深/波形）按 size+mtime 判断是否过期
 */
struct TagInfo {
    qint64 id = 0;
    QString name;
    QString color;
    int count = 0;
};

struct FileRecord {
    QString path;        // 绝对路径（规范化，主键）
    QString name;        // 文件名
    QString relPath;     // 相对素材根目录（仅显示用）
    qint64 size = 0;
    QDateTime mtime;
    double duration = 0;
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0;
    QString format;
    int rating = 0;
    QString notes;
    bool favorite = false;
    QDateTime lastPlayed;
    QString waveCache;   // 峰值缓存文件路径（空 = 尚未分析）
    QString specCache;   // 频谱图缓存文件路径（空 = 尚未分析）
    QStringList tags;          // 自定义扁平标签（名称）
    QStringList facetRefs;     // 多维 facet 标签引用："dim:tagid"
    QString ucsCat;            // UCS 主分类 CatID（如 FOOTSTEPS）
    QString ucsNameZh;         // UCS 中文名
    bool ucsAuto = false;      // true=自动识别（暗色显示）；false=手动选择
    QString kind;              // 业务大类：environment/foley/special（环境/拟声/特殊）
};

// 多维 facet 标签（参考 MATRIX WAVE 的 metadata 维度体系）
struct FacetTag {
    QString id;
    QString cn;
    QString en;
    QString group;      // 乐器维度下的分组名（如"键盘"）
    int count = 0;
};
struct FacetDim {
    QString dim;        // 维度 key（emotion/atmosphere/...）
    QString cn;         // 中文名（情绪/氛围/...）
    bool multi = true;
    QVector<FacetTag> tags;
};

// UCS 分类（参考 MATRIX WAVE ucs_tree.json）
struct UcsCat {
    QString id;         // CatID
    QString zh;         // 中文名
    int count = 0;      // 使用计数
};

// 素材根目录（多库，参考 MATRIX WAVE library roots）
struct LibraryRoot {
    qint64 id = 0;
    QString path;
    QString name;
    int ord = 0;
};

// 收藏分组
struct FavGroup {
    qint64 id = 0;
    QString name;
    int count = 0;
};

class MetadataStore : public QObject
{
    Q_OBJECT
public:
    explicit MetadataStore(QObject *parent = nullptr);
    ~MetadataStore() override;

    /// 打开（或创建）缓存目录：<cacheDir>/metadata.db + <cacheDir>/waveforms/
    bool open(const QString &cacheDir);
    bool isOpen() const { return open_; }
    QString cacheDir() const { return cacheDir_; }
    QString dbPath() const { return dbPath_; }

    // ---- 音频属性缓存（后台分析完成后写入；size/mtime 记录分析时状态） ----
    void upsertAudioInfo(const QString &path, qint64 size, qint64 mtime,
                         double duration, int sampleRate, int channels, int bitDepth,
                         const QString &waveCache);
    /// 确保行存在（首次收藏/打标签时）
    void ensureRow(const QString &path, qint64 size = 0, qint64 mtime = 0);
    /// 判断缓存属性是否仍有效（size+mtime 匹配且已有波形缓存）
    bool audioInfoFresh(const QString &path, qint64 size, qint64 mtime) const;

    /// 把缓存属性 + 用户标注合并进记录（单条 / 批量事务）
    void loadInto(FileRecord &rec) const;
    void loadInto(QVector<FileRecord> &recs) const;

    // ---- 用户元数据 ----
    void setRating(const QString &path, int rating);
    void setNotes(const QString &path, const QString &notes);
    void setFavorite(const QString &path, bool favorite);
    void recordPlay(const QString &path);

    // ---- 标签 ----
    QVector<TagInfo> tags() const;
    qint64 addTag(const QString &name, const QString &color = QStringLiteral("#5B9DB8"));
    void renameTag(qint64 id, const QString &newName);
    void deleteTag(qint64 id);
    void assignTag(const QString &path, qint64 tagId);
    void unassignTag(const QString &path, qint64 tagId);
    QStringList tagNames(const QString &path) const;
    void setTagColor(qint64 id, const QString &color);
    /// 合并标签：把 fromId 的素材关联转到 toId，然后删除 fromId
    bool mergeTags(qint64 fromId, qint64 toId);
    /// 从文件名批量打标（Agent/批量用）：返回影响的条数
    int assignTagByNames(const QStringList &paths, const QString &tagName);

    // ---- 多维 facet 标签 ----
    void ensureFacetCatalog(const QByteArray &facetsJson);   // 幂等导入内置维度
    QVector<FacetDim> facets() const;                        // 含每标签使用计数
    void setFacetTag(const QString &path, const QString &dim, const QString &tagId, bool on);
    QHash<QString, QString> facetTagNames(const QString &dim) const;  // id → cn

    // ---- 多维 facet 目录增删改（用户可自定义维度/标签） ----
    void addFacetTag(const QString &dim, const QString &dimCn,
                     const QString &tagId, const QString &cn);   // 新建标签（维度不存在则自动建）
    void renameFacetTag(const QString &dim, const QString &tagId, const QString &newCn);
    void removeFacetTag(const QString &dim, const QString &tagId);
    void renameFacetDim(const QString &dim, const QString &newCn);
    void removeFacetDim(const QString &dim);

    // ---- UCS 分类 ----
    void ensureUcsCatalog(const QByteArray &ucsJson);        // 幂等导入内置 UCS 树
    QVector<UcsCat> ucsCategories() const;                   // 含使用计数
    /// 从文件名识别 UCS CatID（匹配 CatID/英文名/同义词），返回空=未识别
    QString detectUcs(const QString &fileName) const;
    /// 设置 UCS 主分类；空=清除。autoDetected=true 表示自动识别（暗色显示）
    void setUcs(const QString &path, const QString &catId, bool autoDetected = false);
    void setSpecCache(const QString &path, const QString &cachePath);
    /// 清除某文件的波形/频谱缓存（记录 + 缓存文件），下次浏览会重新分析
    void clearAnalysisCache(const QString &path);
    QVector<FileRecord> filesWithUcs(const QString &catId) const;

    // ---- 业务大类（环境/拟声/特殊） ----
    void setKind(const QString &path, const QString &kind);  // environment/foley/special/空
    QVector<FileRecord> filesWithKind(const QString &kind) const;
    int kindCount(const QString &kind) const;

    // ---- 收藏分组 ----
    QVector<FavGroup> favGroups() const;
    qint64 createFavGroup(const QString &name);
    void renameFavGroup(qint64 id, const QString &name);
    void deleteFavGroup(qint64 id);
    void addToFavGroup(qint64 groupId, const QString &path);
    void removeFromFavGroup(qint64 groupId, const QString &path);
    QVector<FileRecord> favGroupItems(qint64 groupId) const;

    // ---- 素材根目录（多库） ----
    QVector<LibraryRoot> libraries() const;
    qint64 addLibrary(const QString &path, const QString &name = QString());
    void renameLibrary(qint64 id, const QString &name);
    void removeLibrary(qint64 id);

    // ---- 历史 ----
    void recordSearch(const QString &text);
    QStringList recentSearches(int limit = 50) const;

    // ---- 审计（Agent 写操作） ----
    void audit(const QString &source, const QString &tool, const QString &summary,
               const QString &result);

    // ---- 全库视图（跨文件夹） ----
    QVector<FileRecord> favorites() const;
    QVector<FileRecord> filesWithTag(qint64 tagId) const;
    QVector<FileRecord> filesWithFacet(const QString &dim, const QString &tagId) const;
    QVector<FileRecord> recentlyPlayed(int limit = 200) const;
    qint64 fileCount() const;

    // ---- 工具 ----
    static QString normalizePath(const QString &path);
    /// 波形缓存的确定性路径：<cacheDir>/waveforms/<sha1(path)>.svp
    QString waveCachePathFor(const QString &path) const;
    /// 频谱图缓存的确定性路径：<cacheDir>/spectro/<sha1(path)>.svp2
    QString specCachePathFor(const QString &path) const;

signals:
    void dataChanged();

private:
    bool exec(const QString &sql);
    bool ensureSchema();
    /// 列迁移：旧库缺列时 ALTER TABLE 补上
    /// （CREATE TABLE IF NOT EXISTS 不会修改已存在的表，升级必须靠这个）
    bool ensureColumn(const QString &table, const QString &column, const QString &decl);

    QString cacheDir_;
    QString dbPath_;
    bool open_ = false;
};
