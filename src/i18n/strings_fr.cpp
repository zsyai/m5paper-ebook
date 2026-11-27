// French interface strings. Keys are the Simplified Chinese strings used in
// the source (see include/i18n/i18n.h).

#include "i18n/translation_table.h"
#include "ui/pages/user_guide_text_fr.h"

namespace reader {
namespace i18n {

namespace {

const TranslationEntry kEntries[] = {
    {nullptr, "取消", "Annuler"},
    {nullptr, "未检测到TF卡，插入后点击屏幕重试", "Insérez une carte SD et touchez"},
    {nullptr, "最近阅读", "Récents"},
    {nullptr, "书名", "Titre"},
    {nullptr, "阅读进度", "Progression"},
    {nullptr, "最近导入", "Ajoutés"},
    {nullptr, "内置", "Intégrée"},
    {nullptr, "正在打开 ", "Ouverture "},
    {nullptr, "正在解析 ", "Analyse "},
    {nullptr, "加载中...", "Chargement..."},
    {nullptr, "文件为空或读取失败", "Fichier vide ou illisible"},
    {nullptr, "图片解析失败", "Image illisible"},
    {nullptr, "系统已进入休眠", "En veille"},
    {nullptr, "按下或拨动滚轮即可唤醒", "Molette pour réveiller"},
    {nullptr, "向内长按滚轮键2秒即可唤醒", "Appui long 2 s sur la molette"},
    {nullptr, "上次读完", "Fini (récent)"},
    {nullptr, "已读完", "Fini"},
    {nullptr, "未读", "Non lu"},
    {nullptr, "上次阅读 · ", "Récent · "},
    {nullptr, "书架", "Livres"},
    {nullptr, "未找到书籍。", "Aucun livre trouvé."},
    {nullptr, "< 上一页", "< Préc."},
    {nullptr, "下一页 >", "Suiv. >"},
    {nullptr, "上一页", "Page préc."},
    {nullptr, "下一页", "Page suiv."},
    {nullptr, "传书", "Transfert"},
    {nullptr, "设置", "Réglages"},
    {nullptr, "休眠", "Veille"},
    {nullptr, "未检测到TF卡", "Carte SD absente"},
    {nullptr, "字体文件缺失，正在使用内置字体", "Police absente : police intégrée"},
    {nullptr, "请通过“传书”上传字体文件", "Envoyez une police (Transfert)"},
    {nullptr, "确定删除《", "Supprimer « "},
    {nullptr, "》？", " » ?"},
    {nullptr, "删除书籍", "Supprimer le livre"},
    {nullptr, "书籍文件和阅读记录都会删除，", "Le livre et sa progression"},
    {nullptr, "此操作不可恢复。", "seront effacés pour de bon."},
    {nullptr, "删除", "Supprimer"},
    {nullptr, "操作失败", "Échec de l'opération"},
    {nullptr, "删除失败", "Échec de la suppression"},
    {nullptr, "清除阅读记录", "Effacer la progression"},
    {nullptr, "排序方式", "Trier par"},
    {nullptr, "无法打开该书籍", "Impossible d'ouvrir ce livre"},
    {nullptr, "恢复默认", "Défaut"},
    {nullptr, "菜单", "Menu"},
    {nullptr, "全刷", "Rafraîchir"},
    {nullptr, "自动翻页", "Page auto"},
    {nullptr, "无", "Aucune"},
    {nullptr, "操作设置", "Commandes"},
    {nullptr, "全刷间隔", "Rafraîchissement"},
    {nullptr, " 页", " p."},
    {nullptr, "秒", " s"},
    {nullptr, "点击区域", "Zones tactiles"},
    {nullptr, "九宫格", "9 zones"},
    {nullptr, "三栏", "3 zones"},
    {nullptr, "滑动操作", "Glissements"},
    {nullptr, "滚轮方向", "Sens molette"},
    {nullptr, "反转", "Inversé"},
    {nullptr, "正常", "Normal"},
    {nullptr, "蓝牙翻页器", "Télécommande BT"},
    {nullptr, "未连接", "Aucune"},
    {nullptr, "返回", "Retour"},
    {nullptr, "排版设置", "Mise en page"},
    {nullptr, "自动翻页秒数", "Page auto (s)"},
    {nullptr, "蓝牙翻页器暂未支持", "Bluetooth non pris en charge"},
    {nullptr, "点区域切换功能", "Touchez une zone pour changer"},
    {nullptr, "向左滑", "Vers la gauche"},
    {nullptr, "向右滑", "Vers la droite"},
    {nullptr, "向上滑", "Vers le haut"},
    {nullptr, "向下滑", "Vers le bas"},
    {nullptr, "无图片文件", "Aucune image"},
    {nullptr, "当前选择", "Sélectionnée"},
    {nullptr, "设为锁屏", "Fond de veille"},
    {nullptr, "正文字体", "Police"},
    {nullptr, "正文字号", "Taille"},
    {nullptr, "固定", "Fixe"},
    {nullptr, "阅读方向", "Orientation"},
    {nullptr, "行间距", "Interligne"},
    {nullptr, "页边距", "Marges"},
    {nullptr, "段首缩进", "Alinéa"},
    {nullptr, "行下虚线", "Réglure"},
    {nullptr, "行间距 1.0-3.0", "Interligne 1.0-3.0"},
    {nullptr, "自动翻页：开（", "Page auto : oui ("},
    {nullptr, "秒）", " s)"},
    {nullptr, "自动翻页：关", "Page auto : non"},
    {nullptr, "目录", "Sommaire"},
    {nullptr, "阅读设置", "Réglages"},
    {nullptr, "按页码", "Page"},
    {nullptr, "按百分比", "Pourcentage"},
    {nullptr, "跳转 ", "Aller à "},
    {nullptr, "系统设置", "Réglages système"},
    {nullptr, "界面字体", "Police menus"},
    {nullptr, "休眠模式", "Mode veille"},
    {nullptr, "浅休眠", "Légère"},
    {nullptr, "深度休眠", "Profonde"},
    {nullptr, "锁屏壁纸", "Fond d'écran"},
    {nullptr, "选择", "Choisir"},
    {nullptr, "未选择", "Aucun"},
    {nullptr, "过期阅读记录", "Données obsolètes"},
    {nullptr, "清理", "Vider"},
    {nullptr, "字体缓存", "Cache polices"},
    {nullptr, "使用说明", "Mode d'emploi"},
    {nullptr, "阅读", "Lire"},
    {nullptr, "清理中...", "Nettoyage..."},
    {nullptr, "清理字体缓存", "Vider le cache"},
    {nullptr, "没有未使用的缓存。", "Aucun cache inutilisé."},
    {nullptr, "正在使用的缓存会保留。", "Le cache actif est conservé."},
    {nullptr, "删除 ", "Supprimer "},
    {nullptr, " 个未使用的缓存，", " caches inutiles,"},
    {nullptr, "共 ", "soit "},
    {nullptr, "。", "."},
    {nullptr, "确定", "OK"},
    {nullptr, "暂无目录", "Pas de sommaire"},
    {nullptr, "跳转", "Aller à"},
    {nullptr, "WiFi 传书", "Transfert WiFi"},
    {nullptr, "【 无线传书指南 】", "Transfert sans fil"},
    {nullptr, "第一步：连接 WiFi 热点", "1. Connectez-vous au WiFi"},
    {nullptr, "      SSID：M5PaperAP", "      SSID : M5PaperAP"},
    {nullptr, "      密码：12345678", "      Mot de passe : 12345678"},
    {nullptr, "第二步：打开浏览器访问", "2. Ouvrez dans un navigateur"},
    {nullptr, "第三步：在网页中上传文件", "3. Envoyez vos fichiers"},
    {nullptr, "结束传书", "Terminer"},
    {nullptr, "开", "Oui"},
    {nullptr, "关", "Non"},
    {nullptr, "...取消", "...Annuler"},
    {nullptr, "使用说明 · %d/%d", "Mode d'emploi · %d/%d"},
    {nullptr, "语言", "Langue"},
    // Context-dependent strings.
    {"keypad", "删除", "Effacer"},
    {"settings", "阅读设置", "Réglages de lecture"},
};

constexpr char kGlyphPreload[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "àâçéèêëîïôûùüœÀÂÇÉÈÊÎÔÛŒ«»’…·:;!?-/%()[].,<> 0123456789";

constexpr char kPreviewSample[] =
    "La pluie avait cessé vers le soir. Élise ouvrit la fenêtre et respira "
    "l'odeur des pavés mouillés ; au loin, une horloge sonnait huit heures. "
    "Elle reprit son livre là où elle l'avait laissé.\n"
    "« Encore un chapitre », se dit-elle, et la nuit passa sans qu'elle y prît "
    "garde.";

}  // namespace

const LanguageTable kFrTable = {
    kEntries, sizeof(kEntries) / sizeof(kEntries[0]), kGlyphPreload,
    kUserGuideTextFr, kPreviewSample,
};

}  // namespace i18n
}  // namespace reader
