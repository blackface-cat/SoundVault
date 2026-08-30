#include "agent/AgentService.h"
#include "db/MetadataStore.h"
#include "audio/AudioEngine.h"

#include <QDateTime>
#include <QFileInfo>

namespace {
QVariantMap param(const QString &type, const QString &title)
{
    return {{QStringLiteral("type"), type}, {QStringLiteral("title"), title}};
}

QVariantMap fileMap(const FileRecord &f)
{
    return QVariantMap{{QStringLiteral("path"), f.path},
                       {QStringLiteral("name"), f.name},
                       {QStringLiteral("format"), f.format},
                       {QStringLiteral("duration"), f.duration},
                       {QStringLiteral("sample_rate"), f.sampleRate},
                       {QStringLiteral("channels"), f.channels},
                       {QStringLiteral("bit_depth"), f.bitDepth},
                       {QStringLiteral("rating"), f.rating},
                       {QStringLiteral("favorite"), f.favorite},
                       {QStringLiteral("tags"), f.tags},
                       {QStringLiteral("facets"), f.facetRefs},
                       {QStringLiteral("rel_path"), f.relPath}};
}
}

AgentService::AgentService(MetadataStore *db, AudioEngine *engine, QObject *parent)
    : QObject(parent)
    , db_(db)
    , engine_(engine)
{
    using P = AgentPerm::Permission;
    const auto reg = [this](const QString &name, const QString &desc, P perm,
                            const QString &category, QVariantMap schema) {
        tools_.insert(name, {name, desc, perm, schema, category});
    };
    const auto obj = [](const QVariantMap &props, const QStringList &req = {}) {
        return QVariantMap{{QStringLiteral("type"), QStringLiteral("object")},
                           {QStringLiteral("properties"), props},
                           {QStringLiteral("required"), req}};
    };

    reg(QStringLiteral("agent.capabilities"), QStringLiteral("能力发现：版本/工具/授权范围"),
        P::ReadLibrary, QStringLiteral("read"), {});
    reg(QStringLiteral("agent.status"), QStringLiteral("软件与素材库状态"),
        P::ReadLibrary, QStringLiteral("read"), {});

    reg(QStringLiteral("library.list"), QStringLiteral("列出素材根文件夹（多库）"),
        P::ReadLibrary, QStringLiteral("read"), {});
    reg(QStringLiteral("library.add"), QStringLiteral("添加素材根文件夹"),
        P::ManageLibrary, QStringLiteral("write"),
        obj({{QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("文件夹路径"))}},
            {QStringLiteral("path")}));
    reg(QStringLiteral("library.remove"), QStringLiteral("移除素材根文件夹（不动文件）"),
        P::ManageLibrary, QStringLiteral("write"),
        obj({{QStringLiteral("id"), param(QStringLiteral("integer"), QStringLiteral("资料库 ID"))}},
            {QStringLiteral("id")}));

    reg(QStringLiteral("audio.search"),
        QStringLiteral("搜索素材：关键词/标签/多维 facet/评分/收藏"),
        P::ReadLibrary, QStringLiteral("read"),
        obj({{QStringLiteral("q"), param(QStringLiteral("string"), QStringLiteral("关键词"))},
             {QStringLiteral("tag"), param(QStringLiteral("string"), QStringLiteral("标签名"))},
             {QStringLiteral("facet"), param(QStringLiteral("string"), QStringLiteral("维度:标签id，如 atmosphere:dark"))},
             {QStringLiteral("favorite"), param(QStringLiteral("boolean"), QStringLiteral("仅收藏"))},
             {QStringLiteral("rating_min"), param(QStringLiteral("integer"), QStringLiteral("最低评分"))},
             {QStringLiteral("limit"), param(QStringLiteral("integer"), QStringLiteral("条数上限"))}}));
    reg(QStringLiteral("audio.get_details"), QStringLiteral("素材详情（属性/标签/facet/备注）"),
        P::ReadLibrary, QStringLiteral("read"),
        obj({{QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("素材绝对路径"))}},
            {QStringLiteral("path")}));
    reg(QStringLiteral("audio.get_current"), QStringLiteral("当前播放素材"),
        P::ReadLibrary, QStringLiteral("read"), {});

    reg(QStringLiteral("library.browse"), QStringLiteral("让软件浏览并显示某文件夹的音频"),
        P::ReadLibrary, QStringLiteral("read"),
        obj({{QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("文件夹路径"))}},
            {QStringLiteral("path")}));
    reg(QStringLiteral("facet.list"), QStringLiteral("多维标签维度与标签（含使用计数）"),
        P::ReadLibrary, QStringLiteral("read"), {});
    reg(QStringLiteral("tag.list"), QStringLiteral("自定义标签列表（含计数）"),
        P::ReadLibrary, QStringLiteral("read"), {});
    reg(QStringLiteral("fav_group.list"), QStringLiteral("收藏分组列表"),
        P::ReadLibrary, QStringLiteral("read"), {});
    reg(QStringLiteral("history.recent"), QStringLiteral("最近播放 / 最近搜索"),
        P::ReadLibrary, QStringLiteral("read"),
        obj({{QStringLiteral("type"), param(QStringLiteral("string"), QStringLiteral("play|search"))},
             {QStringLiteral("limit"), param(QStringLiteral("integer"), QStringLiteral("条数"))}}));

    reg(QStringLiteral("player.get_state"), QStringLiteral("播放器状态"),
        P::ReadLibrary, QStringLiteral("read"), {});
    reg(QStringLiteral("player.play"), QStringLiteral("播放（可指定素材路径）"),
        P::ControlPlayer, QStringLiteral("playback"),
        obj({{QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("素材路径（可选）"))}}));
    reg(QStringLiteral("player.pause"), QStringLiteral("暂停"), P::ControlPlayer,
        QStringLiteral("playback"), {});
    reg(QStringLiteral("player.stop"), QStringLiteral("停止"), P::ControlPlayer,
        QStringLiteral("playback"), {});
    reg(QStringLiteral("player.seek"), QStringLiteral("定位（比例 0-1）"), P::ControlPlayer,
        QStringLiteral("playback"),
        obj({{QStringLiteral("ratio"), param(QStringLiteral("number"), QStringLiteral("0.0-1.0"))}},
            {QStringLiteral("ratio")}));
    reg(QStringLiteral("player.set_volume"), QStringLiteral("音量 0-1"), P::ControlPlayer,
        QStringLiteral("playback"),
        obj({{QStringLiteral("volume"), param(QStringLiteral("number"), QStringLiteral("0.0-1.0"))}},
            {QStringLiteral("volume")}));
    reg(QStringLiteral("player.set_speed"), QStringLiteral("倍速 0.5-2.0"), P::ControlPlayer,
        QStringLiteral("playback"),
        obj({{QStringLiteral("speed"), param(QStringLiteral("number"), QStringLiteral("0.5-2.0"))}},
            {QStringLiteral("speed")}));
    reg(QStringLiteral("player.set_loop"), QStringLiteral("循环开关"), P::ControlPlayer,
        QStringLiteral("playback"),
        obj({{QStringLiteral("on"), param(QStringLiteral("boolean"), QStringLiteral("是否循环"))}},
            {QStringLiteral("on")}));

    reg(QStringLiteral("favorite.set"), QStringLiteral("设置收藏"), P::EditMetadata,
        QStringLiteral("write"),
        obj({{QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("素材路径"))},
             {QStringLiteral("favorite"), param(QStringLiteral("boolean"), QStringLiteral("是否收藏"))}},
            {QStringLiteral("path"), QStringLiteral("favorite")}));
    reg(QStringLiteral("rating.set"), QStringLiteral("设置评分 0-5"), P::EditMetadata,
        QStringLiteral("write"),
        obj({{QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("素材路径"))},
             {QStringLiteral("rating"), param(QStringLiteral("integer"), QStringLiteral("0-5"))}},
            {QStringLiteral("path"), QStringLiteral("rating")}));
    reg(QStringLiteral("notes.set"), QStringLiteral("设置备注"), P::EditMetadata,
        QStringLiteral("write"),
        obj({{QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("素材路径"))},
             {QStringLiteral("notes"), param(QStringLiteral("string"), QStringLiteral("备注内容"))}},
            {QStringLiteral("path")}));
    reg(QStringLiteral("tag.add_to_files"), QStringLiteral("给素材加标签（不存在则创建）"),
        P::EditMetadata, QStringLiteral("write"),
        obj({{QStringLiteral("paths"), param(QStringLiteral("array"), QStringLiteral("素材路径列表"))},
             {QStringLiteral("tag"), param(QStringLiteral("string"), QStringLiteral("标签名"))}},
            {QStringLiteral("paths"), QStringLiteral("tag")}));
    reg(QStringLiteral("tag.remove_from_files"), QStringLiteral("从素材移除标签"),
        P::EditMetadata, QStringLiteral("write"),
        obj({{QStringLiteral("paths"), param(QStringLiteral("array"), QStringLiteral("素材路径列表"))},
             {QStringLiteral("tag"), param(QStringLiteral("string"), QStringLiteral("标签名"))}},
            {QStringLiteral("paths"), QStringLiteral("tag")}));
    reg(QStringLiteral("facet.set"), QStringLiteral("设置多维 facet 标签"),
        P::EditMetadata, QStringLiteral("write"),
        obj({{QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("素材路径"))},
             {QStringLiteral("dim"), param(QStringLiteral("string"), QStringLiteral("维度"))},
             {QStringLiteral("tag_id"), param(QStringLiteral("string"), QStringLiteral("标签 id"))},
             {QStringLiteral("on"), param(QStringLiteral("boolean"), QStringLiteral("开/关"))}},
            {QStringLiteral("path"), QStringLiteral("dim"), QStringLiteral("tag_id"), QStringLiteral("on")}));
    reg(QStringLiteral("ucs.list"), QStringLiteral("UCS 分类列表（含计数）"),
        P::ReadLibrary, QStringLiteral("read"), {});
    reg(QStringLiteral("ucs.set"), QStringLiteral("设置 UCS 分类（空清除）"),
        P::EditMetadata, QStringLiteral("write"),
        obj({{QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("素材路径"))},
             {QStringLiteral("cat_id"), param(QStringLiteral("string"), QStringLiteral("UCS CatID（如 FOOTSTEPS）"))}},
            {QStringLiteral("path"), QStringLiteral("cat_id")}));
    reg(QStringLiteral("ucs.detect"), QStringLiteral("从文件名识别 UCS CatID"),
        P::ReadLibrary, QStringLiteral("read"),
        obj({{QStringLiteral("filename"), param(QStringLiteral("string"), QStringLiteral("文件名"))}},
            {QStringLiteral("filename")}));
    reg(QStringLiteral("kind.set"), QStringLiteral("设置业务大类（environment/foley/special）"),
        P::EditMetadata, QStringLiteral("write"),
        obj({{QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("素材路径"))},
             {QStringLiteral("kind"), param(QStringLiteral("string"), QStringLiteral("大类"))}},
            {QStringLiteral("path"), QStringLiteral("kind")}));
    reg(QStringLiteral("fav_group.create"), QStringLiteral("新建收藏分组"),
        P::EditMetadata, QStringLiteral("write"),
        obj({{QStringLiteral("name"), param(QStringLiteral("string"), QStringLiteral("分组名"))}},
            {QStringLiteral("name")}));
    reg(QStringLiteral("fav_group.add"), QStringLiteral("素材加入收藏分组"),
        P::EditMetadata, QStringLiteral("write"),
        obj({{QStringLiteral("group_id"), param(QStringLiteral("integer"), QStringLiteral("分组 ID"))},
             {QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("素材路径"))}},
            {QStringLiteral("group_id"), QStringLiteral("path")}));
    reg(QStringLiteral("fav_group.remove"), QStringLiteral("素材移出收藏分组"),
        P::EditMetadata, QStringLiteral("write"),
        obj({{QStringLiteral("group_id"), param(QStringLiteral("integer"), QStringLiteral("分组 ID"))},
             {QStringLiteral("path"), param(QStringLiteral("string"), QStringLiteral("素材路径"))}},
            {QStringLiteral("group_id"), QStringLiteral("path")}));

    reg(QStringLiteral("action.preview"), QStringLiteral("预览高风险操作（签发一次性确认令牌）"),
        P::ManageLibrary, QStringLiteral("preview"),
        obj({{QStringLiteral("op"), param(QStringLiteral("string"), QStringLiteral("操作名"))},
             {QStringLiteral("params"), param(QStringLiteral("object"), QStringLiteral("参数"))}},
            {QStringLiteral("op")}));
    reg(QStringLiteral("action.execute"), QStringLiteral("执行已确认的高风险操作（需令牌）"),
        P::ManageLibrary, QStringLiteral("preview"),
        obj({{QStringLiteral("token"), param(QStringLiteral("string"), QStringLiteral("确认令牌"))}},
            {QStringLiteral("token")}));
}

QStringList AgentService::toolNames() const
{
    return tools_.keys();
}

QVariantMap AgentService::toolInfo(const QString &name) const
{
    if (!tools_.contains(name))
        return {};
    const AgentTool &t = tools_[name];
    return {{QStringLiteral("name"), t.name},
            {QStringLiteral("description"), t.description},
            {QStringLiteral("category"), t.category},
            {QStringLiteral("permission"), int(t.minPermission)},
            {QStringLiteral("params"), t.paramsSchema}};
}

QVariantMap AgentService::capabilities() const
{
    QVariantList list;
    for (const QString &n : toolNames())
        list.append(toolInfo(n));
    return {{QStringLiteral("api"), QStringLiteral("shengku.agent.v1")},
            {QStringLiteral("app"), QStringLiteral("Sound Vault")},
            {QStringLiteral("version"), QStringLiteral("1.4.0")},
            {QStringLiteral("identity"), QStringLiteral("path")},
            {QStringLiteral("tools"), list}};
}

QVariantMap AgentService::status() const
{
    return {{QStringLiteral("app"), QStringLiteral("Sound Vault")},
            {QStringLiteral("version"), QStringLiteral("1.4.0")},
            {QStringLiteral("library_files"), db_ ? db_->fileCount() : 0},
            {QStringLiteral("libraries"), db_ ? int(db_->libraries().size()) : 0},
            {QStringLiteral("facets"), db_ ? int(db_->facets().size()) : 0},
            {QStringLiteral("tags"), db_ ? int(db_->tags().size()) : 0},
            {QStringLiteral("write_enabled"), scope_.allowWrite},
            {QStringLiteral("current_device"), engine_ ? engine_->currentDeviceName() : QString()}};
}

QVariantMap AgentService::call(const QString &tool, const QVariantMap &args,
                               const QString &source)
{
    if (!tools_.contains(tool))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("unknown_tool:%1").arg(tool)}};

    const AgentTool &t = tools_[tool];
    if (t.minPermission == AgentPerm::ReadLibrary && !scope_.allowRead)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("permission_denied:read_disabled")}};
    if (t.category == QStringLiteral("playback") && !scope_.allowPlayback)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("permission_denied:playback_disabled")}};
    const bool needWrite = t.minPermission >= AgentPerm::EditMetadata;
    if (needWrite && !scope_.allowWrite)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("permission_denied:write_disabled")}};

    if (t.category == QStringLiteral("write") || t.category == QStringLiteral("playback")
        || t.category == QStringLiteral("preview"))
        return execWrite(tool, args, source);
    return execRead(tool, args);
}

QVariantMap AgentService::execRead(const QString &tool, const QVariantMap &args)
{
    if (tool == QStringLiteral("agent.capabilities"))
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), capabilities()}};
    if (tool == QStringLiteral("agent.status"))
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), status()}};

    if (tool == QStringLiteral("library.list")) {
        QVariantList out;
        if (db_)
            for (const auto &l : db_->libraries())
                out.append(QVariantMap{{QStringLiteral("id"), l.id},
                                       {QStringLiteral("name"), l.name},
                                       {QStringLiteral("path"), l.path}});
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), out}};
    }
    if (tool == QStringLiteral("audio.search")) {
        if (!db_)
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no_db")}};
        const QString q = args.value(QStringLiteral("q")).toString();
        const QString tag = args.value(QStringLiteral("tag")).toString();
        const QString facet = args.value(QStringLiteral("facet")).toString();
        const bool onlyFav = args.value(QStringLiteral("favorite")).toBool();
        const int minRating = args.value(QStringLiteral("rating_min")).toInt();
        int limit = qBound(1, args.value(QStringLiteral("limit"), 50).toInt(), 200);

        QVector<FileRecord> files;
        if (!facet.isEmpty() && facet.contains(QLatin1Char(':'))) {
            const QString dim = facet.section(QLatin1Char(':'), 0, 0);
            const QString tid = facet.section(QLatin1Char(':'), 1, 1);
            files = db_->filesWithFacet(dim, tid);
        } else if (!tag.isEmpty()) {
            qint64 tagId = 0;
            for (const auto &t : db_->tags())
                if (t.name == tag) { tagId = t.id; break; }
            if (tagId > 0)
                files = db_->filesWithTag(tagId);
        } else {
            files = db_->favorites().isEmpty() ? QVector<FileRecord>() : db_->favorites();
            // 无筛选时按最近播放兜底
            if (q.isEmpty() && tag.isEmpty() && !onlyFav)
                files = db_->recentlyPlayed(limit * 2);
        }
        Q_UNUSED(onlyFav);

        QVariantList out;
        int shown = 0;
        for (const FileRecord &f : files) {
            if (minRating > 0 && f.rating < minRating)
                continue;
            if (!q.isEmpty() && !f.name.contains(q, Qt::CaseInsensitive)
                && !f.relPath.contains(q, Qt::CaseInsensitive))
                continue;
            if (onlyFav && !f.favorite)
                continue;
            out.append(fileMap(f));
            if (++shown >= limit)
                break;
        }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), out}};
    }
    if (tool == QStringLiteral("audio.get_details")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        if (!db_ || path.isEmpty())
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("bad_path")}};
        FileRecord f;
        f.path = path;
        f.name = QFileInfo(path).fileName();
        db_->loadInto(f);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), fileMap(f)}};
    }
    if (tool == QStringLiteral("audio.get_current")) {
        if (!engine_ || engine_->currentPath().isEmpty())
            return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{}}};
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("result"), QVariantMap{
                    {QStringLiteral("path"), engine_->currentPath()},
                    {QStringLiteral("state"), int(engine_->state())},
                    {QStringLiteral("position"), engine_->position()},
                    {QStringLiteral("duration"), engine_->duration()}}}};
    }
    if (tool == QStringLiteral("library.browse")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        if (path.isEmpty() || !QFileInfo::exists(path))
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("bad_path")}};
        emit browseRequested(path);
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("result"), QVariantMap{{QStringLiteral("path"), path}}}};
    }
    if (tool == QStringLiteral("facet.list")) {
        QVariantList out;
        if (db_)
            for (const FacetDim &d : db_->facets()) {
                QVariantList tags;
                for (const FacetTag &t : d.tags)
                    tags.append(QVariantMap{{QStringLiteral("id"), t.id},
                                            {QStringLiteral("cn"), t.cn},
                                            {QStringLiteral("en"), t.en},
                                            {QStringLiteral("group"), t.group},
                                            {QStringLiteral("count"), t.count}});
                out.append(QVariantMap{{QStringLiteral("dim"), d.dim},
                                       {QStringLiteral("cn"), d.cn},
                                       {QStringLiteral("multi"), d.multi},
                                       {QStringLiteral("tags"), tags}});
            }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), out}};
    }
    if (tool == QStringLiteral("tag.list")) {
        QVariantList out;
        if (db_)
            for (const auto &t : db_->tags())
                out.append(QVariantMap{{QStringLiteral("id"), t.id},
                                       {QStringLiteral("name"), t.name},
                                       {QStringLiteral("count"), t.count}});
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), out}};
    }
    if (tool == QStringLiteral("fav_group.list")) {
        QVariantList out;
        if (db_)
            for (const auto &g : db_->favGroups())
                out.append(QVariantMap{{QStringLiteral("id"), g.id},
                                       {QStringLiteral("name"), g.name},
                                       {QStringLiteral("count"), g.count}});
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), out}};
    }
    if (tool == QStringLiteral("ucs.list")) {
        QVariantList out;
        if (db_)
            for (const UcsCat &u : db_->ucsCategories())
                out.append(QVariantMap{{QStringLiteral("cat_id"), u.id},
                                       {QStringLiteral("zh"), u.zh},
                                       {QStringLiteral("count"), u.count}});
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), out}};
    }
    if (tool == QStringLiteral("ucs.detect")) {
        if (!db_)
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no_db")}};
        const QString fn = args.value(QStringLiteral("filename")).toString();
        const QString catId = db_->detectUcs(fn);
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("result"), QVariantMap{{QStringLiteral("filename"), fn},
                                                       {QStringLiteral("cat_id"), catId},
                                                       {QStringLiteral("matched"), !catId.isEmpty()}}}};
    }
    if (tool == QStringLiteral("history.recent")) {
        const QString type = args.value(QStringLiteral("type"), QStringLiteral("play")).toString();
        const int limit = qBound(1, args.value(QStringLiteral("limit"), 50).toInt(), 200);
        QVariantList out;
        if (type == QStringLiteral("search")) {
            if (db_)
                for (const QString &s : db_->recentSearches(limit))
                    out.append(s);
        } else {
            if (db_)
                for (const FileRecord &f : db_->recentlyPlayed(limit))
                    out.append(fileMap(f));
        }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), out}};
    }
    if (tool == QStringLiteral("player.get_state")) {
        if (!engine_)
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no_engine")}};
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("result"), QVariantMap{
                    {QStringLiteral("state"), int(engine_->state())},
                    {QStringLiteral("position"), engine_->position()},
                    {QStringLiteral("duration"), engine_->duration()}}}};
    }
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("unsupported")}};
}

QVariantMap AgentService::execWrite(const QString &tool, const QVariantMap &args,
                                    const QString &source)
{
    if (tool == QStringLiteral("player.play")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            engine_->loadFile(path);
            db_->recordPlay(path);
        }
        engine_->play();
        audit(tool, args, source, QStringLiteral("play"));
        return {{QStringLiteral("ok"), true}};
    }
    if (tool == QStringLiteral("player.pause")) { engine_->pause(); return {{QStringLiteral("ok"), true}}; }
    if (tool == QStringLiteral("player.stop")) { engine_->stop(); return {{QStringLiteral("ok"), true}}; }
    if (tool == QStringLiteral("player.seek")) { engine_->seekByRatio(args.value(QStringLiteral("ratio")).toDouble()); return {{QStringLiteral("ok"), true}}; }
    if (tool == QStringLiteral("player.set_volume")) { engine_->setVolume(args.value(QStringLiteral("volume")).toDouble()); return {{QStringLiteral("ok"), true}}; }
    if (tool == QStringLiteral("player.set_speed")) { engine_->setSpeed(args.value(QStringLiteral("speed")).toDouble()); return {{QStringLiteral("ok"), true}}; }
    if (tool == QStringLiteral("player.set_loop")) { engine_->setLoop(args.value(QStringLiteral("on")).toBool()); return {{QStringLiteral("ok"), true}}; }

    if (tool == QStringLiteral("library.add")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        if (path.isEmpty() || !QFileInfo::exists(path))
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("bad_path")}};
        const qint64 id = db_->addLibrary(path);
        audit(tool, args, source, QStringLiteral("id=%1").arg(id));
        return {{QStringLiteral("ok"), id > 0}, {QStringLiteral("result"), QVariantMap{{QStringLiteral("id"), id}}}};
    }
    if (tool == QStringLiteral("library.remove")) {
        db_->removeLibrary(args.value(QStringLiteral("id")).toLongLong());
        audit(tool, args, source, QStringLiteral("ok"));
        return {{QStringLiteral("ok"), true}};
    }
    if (tool == QStringLiteral("favorite.set")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        db_->setFavorite(path, args.value(QStringLiteral("favorite")).toBool());
        audit(tool, args, source, QStringLiteral("ok"));
        return {{QStringLiteral("ok"), true}};
    }
    if (tool == QStringLiteral("rating.set")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        db_->setRating(path, args.value(QStringLiteral("rating")).toInt());
        audit(tool, args, source, QStringLiteral("ok"));
        return {{QStringLiteral("ok"), true}};
    }
    if (tool == QStringLiteral("notes.set")) {
        db_->setNotes(args.value(QStringLiteral("path")).toString(),
                      args.value(QStringLiteral("notes")).toString());
        audit(tool, args, source, QStringLiteral("ok"));
        return {{QStringLiteral("ok"), true}};
    }
    if (tool == QStringLiteral("tag.add_to_files") || tool == QStringLiteral("tag.remove_from_files")) {
        const QVariantList paths = args.value(QStringLiteral("paths")).toList();
        const QString tagName = args.value(QStringLiteral("tag")).toString();
        if (paths.isEmpty() || tagName.isEmpty())
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("bad_args")}};
        qint64 tagId = 0;
        for (const auto &t : db_->tags())
            if (t.name == tagName) { tagId = t.id; break; }
        if (tagId == 0)
            tagId = db_->addTag(tagName);
        int n = 0;
        for (const QVariant &v : paths)
            if (!v.toString().isEmpty()) {
                if (tool.endsWith(QStringLiteral("add_to_files")))
                    db_->assignTag(v.toString(), tagId);
                else
                    db_->unassignTag(v.toString(), tagId);
                n++;
            }
        audit(tool, args, source, QStringLiteral("files=%1 tag=%2").arg(n).arg(tagName));
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{{QStringLiteral("affected"), n}}}};
    }
    if (tool == QStringLiteral("facet.set")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        const QString dim = args.value(QStringLiteral("dim")).toString();
        const QString tid = args.value(QStringLiteral("tag_id")).toString();
        db_->setFacetTag(path, dim, tid, args.value(QStringLiteral("on")).toBool());
        audit(tool, args, source, QStringLiteral("ok"));
        return {{QStringLiteral("ok"), true}};
    }
    if (tool == QStringLiteral("ucs.set")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        const QString catId = args.value(QStringLiteral("cat_id")).toString();
        db_->setUcs(path, catId);
        audit(tool, args, source, QStringLiteral("ok"));
        return {{QStringLiteral("ok"), true}};
    }
    if (tool == QStringLiteral("kind.set")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        db_->setKind(path, args.value(QStringLiteral("kind")).toString());
        audit(tool, args, source, QStringLiteral("ok"));
        return {{QStringLiteral("ok"), true}};
    }
    if (tool == QStringLiteral("fav_group.create")) {
        const qint64 id = db_->createFavGroup(args.value(QStringLiteral("name")).toString());
        return {{QStringLiteral("ok"), id > 0}, {QStringLiteral("result"), QVariantMap{{QStringLiteral("id"), id}}}};
    }
    if (tool == QStringLiteral("fav_group.add")) {
        db_->addToFavGroup(args.value(QStringLiteral("group_id")).toLongLong(),
                           args.value(QStringLiteral("path")).toString());
        return {{QStringLiteral("ok"), true}};
    }
    if (tool == QStringLiteral("fav_group.remove")) {
        db_->removeFromFavGroup(args.value(QStringLiteral("group_id")).toLongLong(),
                                args.value(QStringLiteral("path")).toString());
        return {{QStringLiteral("ok"), true}};
    }
    if (tool == QStringLiteral("action.preview")) {
        const QString token = issuePreviewToken(args.value(QStringLiteral("op")).toString(),
                                                QStringLiteral("高风险操作待确认"));
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("result"), QVariantMap{{QStringLiteral("token"), token},
                                                       {QStringLiteral("ttl_seconds"), 120},
                                                       {QStringLiteral("one_shot"), true}}}};
    }
    if (tool == QStringLiteral("action.execute")) {
        QString op, summary;
        if (!consumePreviewToken(args.value(QStringLiteral("token")).toString(), &op, &summary))
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("invalid_or_expired_token")}};
        audit(tool, args, source, QStringLiteral("executed:%1").arg(op));
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("result"), QVariantMap{{QStringLiteral("op"), op},
                                                       {QStringLiteral("summary"), summary}}}};
    }
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("unsupported")}};
}

QString AgentService::issuePreviewToken(const QString &tool, const QString &summary)
{
    const QString token = QStringLiteral("tk-") + QString::number(QDateTime::currentMSecsSinceEpoch());
    previewTokens_.insert(token, {tool, summary, QDateTime::currentMSecsSinceEpoch()});
    return token;
}

bool AgentService::consumePreviewToken(const QString &token, QString *tool, QString *summary)
{
    if (!previewTokens_.contains(token))
        return false;
    const PreviewToken t = previewTokens_.take(token);
    if (QDateTime::currentMSecsSinceEpoch() - t.ts > 120 * 1000)
        return false;
    if (tool)
        *tool = t.tool;
    if (summary)
        *summary = t.summary;
    return true;
}

void AgentService::audit(const QString &tool, const QVariantMap &args, const QString &source,
                         const QString &result)
{
    if (!db_)
        return;
    QStringList summary;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        if (it.key() == QStringLiteral("token"))
            continue;
        summary << it.key() + QStringLiteral("=") + it.value().toString();
    }
    db_->audit(source, tool, summary.join(QLatin1Char(' ')), result);
}
