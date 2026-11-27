"""Build the French Wi-Fi transfer page from the Chinese one.

Usage: python tools/translate_html_fr.py
Reads assets/transfer_client.html and writes assets/transfer_client_fr.html.
Run it again after changing the Chinese page; it lists the lines it left in
Chinese so new strings can be added to R.
"""
import re

SRC = 'assets/transfer_client.html'
DST = 'assets/transfer_client_fr.html'
R = [
    ('lang="zh-CN"', 'lang="fr"'), ("lang='zh-CN'", "lang='fr'"), ('lang="zh"', 'lang="fr"'),
    ('M5Paper 资源传输', 'M5Paper – Transfert'),
    ('>资源传输<', '>Transfert<'),
    ('>使用说明<', ">Mode d'emploi<"),
    ('>文件传输<', '>Fichiers<'),
    ('>字体转换<', '>Polices<'),
    ('点击选择文件（可多选）', 'Touchez pour choisir des fichiers (plusieurs possibles)'),
    ("'开始发送 ('", "'Envoyer ('"),
    ('开始发送', 'Envoyer'),
    ("'正在传输至设备...'", "'Envoi vers l\\'appareil...'"),
    ('正在传输...', 'Envoi en cours...'),
    ('当前文件...', 'Fichier en cours...'),
    ('部分传输失败', 'Envoi partiellement échoué'),
    ('已跳过传输', 'Envoi ignoré'),
    ('传输完成', 'Envoi terminé'),
    ('重试失败的文件', 'Réessayer les fichiers en échec'),
    ('新的传输', 'Nouvel envoi'),
    ('点击选择 .ttf 字体', 'Touchez pour choisir une police .ttf'),
    ('目标字号 (px)', 'Taille cible (px)'),
    ('清晰：高精细度，但体积较大；流畅：高流畅度（体积极小），但边缘有较明显锯齿。',
     'Nette : plus fine mais plus lourde ; Fluide : très légère et rapide, mais bords plus crénelés.'),
    ('字体版本', 'Version'),
    ('>清晰<', '>Nette<'), ('>流畅<', '>Fluide<'),
    ('开始转换', 'Convertir'),
    ('正在解析字体...', 'Analyse de la police...'),
    ('转换完成', 'Conversion terminée'),
    ('下载到本地', 'Télécharger'),
    ('传输至设备', "Envoyer à l'appareil"),
    ('继续转换其他', 'Convertir une autre police'),
    ('设备文件', "Fichiers de l'appareil"),
    ('⬆️ 返回', '⬆️ Remonter'),
    ('加载中...', 'Chargement...'),
    ('加载失败', 'Échec du chargement'),
    ('空文件夹', 'Dossier vide'),
    ("'文件夹'", "'Dossier'"),
    ('确认删除', 'Confirmer la suppression'),
    ('你确定要删除该文件吗？', 'Voulez-vous vraiment supprimer ce fichier ?'),
    ('>取消<', '>Annuler<'),
    ('>删除<', '>Supprimer<'),
    ('1. 文件存放位置：', '1. Où ranger les fichiers :'),
    ('• 电子书（.txt / .epub）请存放在 <code>/books</code> 目录下。',
     '• Livres (.txt / .epub) : dossier <code>/books</code>.'),
    ('• 字体文件（.bin）请存放在 <code>/fonts</code> 目录下。',
     '• Polices (.ttf / .otf / .bin) : dossier <code>/fonts</code>.'),
    ('2. 传输注意事项：', '2. Pendant un envoi :'),
    ('<strong>重要：</strong>在传输文件前，请先在下方的“设备文件”浏览器中<strong>进入对应的文件夹</strong>（例如进入 <code>/books</code> 文件夹），然后再点击“开始发送”。文件将会上传到当前显示的路径下。',
     "<strong>Important :</strong> avant d'envoyer, <strong>ouvrez le bon dossier</strong> dans « Fichiers de l'appareil » plus bas (par exemple <code>/books</code>), puis touchez « Envoyer ». Les fichiers vont dans le dossier affiché."),
    ('• 可以一次选择多个文件，它们会按顺序逐个上传。如果当前文件夹里已有同名文件，会先询问是否覆盖，选择取消就跳过这个文件。',
     "• Vous pouvez choisir plusieurs fichiers : ils sont envoyés l'un après l'autre. Si un fichier du même nom existe déjà, la page demande s'il faut le remplacer ; Annuler le saute."),
    ('3. 字体生成功能：', '3. Conversion de polices :'),
    ('• 切换到“字体转换”标签，可以选择本地的 <code>.ttf</code> 字体文件，设置字号后转换为设备支持的 <code>.bin</code> 格式。',
     "• L'onglet « Polices » convertit une police <code>.ttf</code> en <code>.bin</code> à la taille choisie (facultatif : les .ttf sont lues directement)."),
    ('• 转换完成后，可以直接“传输至设备”，默认会保存到 <code>/fonts</code> 目录。',
     "• Une fois convertie, « Envoyer à l'appareil » la range dans <code>/fonts</code>."),
    ('知道了', 'Compris'),
    ("'已选择 ' + files.length + ' 个文件'", "files.length + ' fichiers sélectionnés'"),
    ("'总进度（' + (index + 1) + ' / ' + total + '）'", "'Progression (' + (index + 1) + ' / ' + total + ')'"),
    ("'当前：'", "'En cours : '"),
    ("'正在传输：'", "'Envoi : '"),
    ("'「' + file.name + '」已存在，是否覆盖？\\n点「取消」跳过这个文件。'",
     "'« ' + file.name + ' » existe déjà. Le remplacer ?\\nAnnuler pour sauter ce fichier.'"),
    ("'已跳过 ' + skipped + ' 个文件'", "skipped + ' fichier(s) ignoré(s)'"),
    ("'成功 ' + done + ' 个'", "done + ' réussi(s)'"),
    ("'，跳过 ' + skipped + ' 个'", "', ' + skipped + ' ignoré(s)'"),
    ("'，失败 ' + failedFiles.length + ' 个：'", "', ' + failedFiles.length + ' en échec : '"),
    (".join('、')", ".join(', ')"),
    ('仅支持 .ttf 格式文件', 'Seuls les fichiers .ttf sont acceptés'),
    ('正在渲染字符 ${current}/${total}', 'Rendu des caractères ${current}/${total}'),
    ('字体转换失败，请检查文件是否损坏。', 'Échec de la conversion : le fichier est peut-être endommagé.'),
    ('字体 [${fileName}] 已成功保存至设备 /fonts 目录！', "Police [${fileName}] enregistrée dans /fonts sur l'appareil !"),
    ("'传输失败'", "'Échec de l\\'envoi'"),
    ("'删除失败'", "'Échec de la suppression'"),
    ('确定要删除文件<br><strong style="color:var(--text-main)">${fileName}</strong> 吗？<br>此操作不可恢复。',
     'Supprimer le fichier<br><strong style="color:var(--text-main)">${fileName}</strong> ?<br>Cette action est irréversible.'),
]

t = open(SRC, encoding='utf-8').read()
for old, new in sorted(R, key=lambda p: -len(p[0])):
    t = t.replace(old, new)
open(DST, 'w', encoding='utf-8').write(t)
left = [(n, l.strip()) for n, l in enumerate(t.splitlines(), 1)
        if re.search(r'[一-鿿]', l) and not re.match(r'\s*(//|/\*|<!--)', l)]
for n, l in left:
    print('untranslated', n, l[:160])
print(len(left), 'untranslated lines')
