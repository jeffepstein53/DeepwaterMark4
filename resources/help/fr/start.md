# Deepwater

Deepwater est un terminal de marché construit entièrement sur des sources publiques. Il réunit données macro, actualités, positionnement, marchés en direct, graphiques avec flux d'ordres, journal de trading et outils d'IA. Ce premier chapitre décrit ce que toutes les pages ont en commun ; chacun des chapitres suivants est consacré à un onglet de l'en-tête.

## Interface

### L'en-tête

La première ligne porte le logo Deepwater, les onglets principaux, une horloge, l'icône de mise à jour, le **?** qui ouvre cette documentation et l'engrenage des options. La seconde liste les sous-onglets de l'onglet où vous êtes, avec à droite une description en une ligne de la page affichée.

L'horloge donne votre jour, votre date et votre heure locales, et ajoute l'heure UTC quand la place le permet. Quand la fenêtre se resserre, l'en-tête renonce d'abord à l'heure UTC, puis à la date, puis au nom Deepwater, et rapproche les onglets pour qu'ils restent tous visibles.

### Se déplacer

Un clic sur un onglet principal vous ramène au dernier sous-onglet que vous y avez ouvert : vous pouvez ainsi passer d'un graphique au calendrier sans perdre votre place. Au lancement suivant, Deepwater rouvre la page que vous avez quittée.

### Le comportement des pages

Une page est construite à votre première visite, puis conservée telle quelle : y revenir est immédiat, et votre sélection, votre défilement et vos filtres sont toujours là. Tant qu'elle est masquée, elle met en pause ses minuteries et ses animations, qu'elle reprend à votre retour.

Certaines pages ont besoin de dizaines de requêtes avant d'être remplies. Pour vous éviter l'attente, Deepwater les prépare en arrière-plan, l'une après l'autre, environ quatre secondes après le lancement : **Vue d'ensemble**, **Heatmap technique**, **Taux**, **Macro**, **Heatmap fondamentale**, **Récap des sessions**, **Vue des marchés** et **Recherche bancaire**. Si vous en ouvrez une avant son tour, ses requêtes passent en tête de file.

Changer de langue ou de thème reconstruit toutes les pages, puis rouvre celle où vous étiez.

### Petits écrans

Sur ordinateur, la fenêtre descend jusqu'à 1,024 × 600 et les pages réorganisent leurs panneaux en conséquence. Sur téléphone, l'en-tête tient sur une seule ligne : **☰** ouvre un menu avec tous les onglets et sous-onglets, suivi du nom de la page courante, de l'heure, du **?** et de l'engrenage.

## Raccourcis clavier

Quelques raccourcis fonctionnent partout dans l'application ; les pages qui ont les leurs, comme [Graphiques](help:charts), les donnent dans leur chapitre. Sous Windows, Ctrl remplace ⌘ partout où cette documentation l'écrit.

| Touches | Effet |
|---|---|
| ⌘1 … ⌘9 | Ouvre l'onglet principal 1 à 9, sur son premier sous-onglet (⌘0 remet le zoom à sa taille réelle ; sur iPad, il ouvre le dixième onglet) |
| ⌘] et ⌘[ | Sous-onglet suivant et précédent de l'onglet courant |
| ⌘, | Options |
| F1 ou ⌘? | Cette documentation, ouverte au chapitre de l'onglet courant |

## Options

L'engrenage ouvre les options, tout comme ⌘, et, sur macOS, **Deepwater › Réglages…** dans la barre de menus. Chaque changement s'applique tout de suite et est enregistré ; Échap ferme la fenêtre.

### Langue

L'interface, cette documentation comprise, existe en anglais et en français. Quelle que soit la langue, les nombres gardent le format 1,234.56 auquel les traders sont habitués ; seuls les dates et les noms de jours et de mois suivent votre choix.

### Thème

Dix thèmes recolorent toute l'application. Chaque carte montre un aperçu de ses couleurs, et la survoler en donne une courte description.

| Thème | Type | Aspect |
|---|---|---|
| Deepwater | Par défaut | Noir chaud, accents ambrés, cyan à la hausse et corail à la baisse |
| Braise | Sombre | Charbon chaud aux accents dorés |
| Abysse | Sombre | Bleu des grands fonds aux accents aqua |
| Jade | Sombre | Vert sombre aux accents jade |
| Oxyde | Sombre | Bruns rouille avec un accent rouge orangé |
| Nova | Sombre | Violet nuit aux accents lavande |
| Bloomberg | Sombre | Noir pur, texte ambré et accents blancs |
| Glacier | Clair | Blanc glacier aux accents bleus |
| Dune | Clair | Sable aux accents orange brûlé |
| Sakura | Clair | Rose poudré aux accents rose vif |

## Mises à jour

Deepwater cherche lui-même les nouvelles versions : une fois au lancement, puis en restant connecté à un service de notification qui annonce chaque nouvelle publication, si bien qu'une version publiée pendant que l'app est ouverte apparaît en quelques secondes. Il ne vérifie jamais à intervalles réguliers. Les versions viennent des publications du projet sur GitHub, construites pour toutes les plateformes à la fois.

Quand une version est disponible, l'icône de mise à jour de l'en-tête (une flèche vers un plateau, à gauche du **?**) passe à l'ambre avec un point. Un clic ouvre la fenêtre de mise à jour : la version que vous avez, la nouvelle, et ce qu'elle change. **Vérifier maintenant** relance la recherche à tout moment.

Sur Mac, **Installer et redémarrer** télécharge la nouvelle version, la vérifie, la met à la place de celle qui tourne et la relance ; si Deepwater tourne depuis un dossier de compilation, la nouvelle version va dans Applications. Sous Windows, le même bouton télécharge l'installateur, qui se lance une fois Deepwater fermé : Windows demande votre autorisation, puis Deepwater redémarre. Un téléchargement continue si vous fermez la fenêtre, et l'icône en montre la progression.

Une app d'iPhone ou d'iPad ne peut pas installer elle-même une nouvelle version. La fenêtre y indique qu'une version est disponible et **Ouvrir la page de la version** y mène, pour la réinstaller avec Sideloadly ou AltStore ; avec TestFlight, les mises à jour arrivent toutes seules.

La recherche de mises à jour utilise deux sources listées dans l'onglet [Sources](help:sources), GitHub et ntfy.sh ; les couper là l'arrête.

## Lire les écrans

Dans le thème par défaut, le cyan signifie hausse, long, positif ou appétit pour le risque, et le rouge corail baisse, short, négatif ou aversion au risque. L'ambre marque les titres, l'onglet actif et les données en direct.

Un badge **● LIVE** indique que la donnée arrive en continu ou qu'elle est fraîche. Quand il passe au gris et affiche **STALE** ou **OFFLINE**, la source ne répond plus depuis un moment et la page montre la dernière donnée reçue.

Les heures suivent l'horloge de votre ordinateur, sauf quand une page indique autre chose, par exemple l'heure UTC ou l'heure locale d'une place. Un **ⓘ** après le titre d'un panneau l'explique au survol.

## Données et clés

Deepwater lit des sources publiques : banques centrales, instituts statistiques, places de marché et fils d'actualité. La plupart ne vous demandent rien. Quelques-unes réclament une clé gratuite ou un compte, à saisir dans l'onglet [Sources](help:sources), qui montre aussi l'état de chaque source et permet de couper celles que vous voulez.

Tout ce que Deepwater enregistre, de votre journal et des archives de marché aux faits économiques, modèles et réglages, reste sur cet ordinateur.

Un marché fermé affiche simplement son dernier prix. Week-ends, jours fériés et séances de nuit ne sont pas des erreurs.

## Utiliser cette documentation

Les chapitres sont listés à gauche, le texte occupe le centre, et la colonne de droite montre les sections du chapitre en surlignant celle que vous lisez ; un clic sur une section y mène. Les liens du texte ouvrent d'autres chapitres.

Pour chercher, tapez dans le champ en haut à gauche ou appuyez sur ⌘F. La casse et les accents sont ignorés, et la liste des chapitres indique combien de résultats chacun contient. Entrée ou ↓ passe au résultat suivant, et continue dans le chapitre suivant quand celui-ci est épuisé ; Maj+Entrée ou ↑ revient en arrière.

La fenêtre suit la fenêtre principale quand vous la déplacez ou la redimensionnez, et vous pouvez la déplacer par sa ligne de titre. Échap ou × la ferme.

## Les chapitres

[Global](help:global) donne la vue d'ensemble : panorama des marchés, récaps de session et heures de marché. [Actualités](help:news) couvre le calendrier économique, le fil d'actualités, la recherche bancaire et le fil Squawk. [Économie](help:economy) rassemble le globe macro et les taux des banques centrales, et [Positionnement](help:positioning) montre comment se placent les traders particuliers, le rapport COT et les banques, ainsi que le sentiment de risque.

[Marchés](help:market) suit les prix en direct et les interprète : vue des marchés, saisonnalité, heatmaps fondamentale et technique, liquidité. [Graphiques](help:charts) est le terminal graphique, avec indicateurs, flux d'ordres, testeur de stratégies et trading fictif. [Compte](help:account) contient votre track record et le calculateur de risque.

Enfin, [Data](help:data) montre les faits économiques et les barres de marché que Deepwater enregistre, [IA](help:ai) réunit modèles de langage, agents et apprentissage automatique, et [Sources](help:sources) liste chaque source de données avec son état et ses clés.
