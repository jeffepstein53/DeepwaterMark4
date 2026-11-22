# IA

Cet onglet met les modèles de langage et l'apprentissage automatique au service des données de Deepwater. Vous téléchargez des modèles locaux, ou utilisez Claude Code, vous les chargez dans quatre environnements isolés, et vous accordez à chacun les compétences qu'il a le droit d'utiliser. Vous pouvez aussi entraîner vos propres modèles dans un environnement Python privé.

## LLM

C'est d'ici que viennent les modèles. La barre bascule entre **MODÈLES LOCAUX**, ceux que vous téléchargez et faites tourner sur cet ordinateur, et **INTÉGRATION**, où se règle Claude Code ; le choix est retenu.

### Chercher un modèle sur le Hub

Tapez un nom dans le champ de recherche, par exemple `qwen`, `llama`, `mistral` ou `phi` : les résultats arrivent du Hub Hugging Face pendant la frappe, et les 50 premiers s'affichent. Par défaut, la recherche se limite à **GGUF SEULEMENT**, le format que font tourner les environnements ; **TOUS FORMATS** lève cette limite. Les résultats se trient par **LES PLUS TÉLÉCHARGÉS**, **LES PLUS AIMÉS** ou **MIS À JOUR RÉCEMMENT**.

Chaque résultat donne le nom, l'auteur, la licence, la tâche, les téléchargements (↓) et les likes (♥), et porte la mention **RESTREINT** quand le modèle demande d'accepter ses conditions. Une ligne d'état indique si la recherche est en cours, combien de modèles ont été trouvés, ou que le Hub est injoignable.

### Télécharger un modèle

Un clic sur un résultat affiche sa licence, ses téléchargements et sa dernière mise à jour, avec **OUVRIR SUR HUGGING FACE** pour aller sur sa page. Lisez la licence avant d'utiliser un modèle : certains interdisent l'usage commercial ou exigent une attribution. Pour un modèle restreint, acceptez ses conditions sur huggingface.co avec votre compte, puis enregistrez votre jeton comme expliqué plus bas.

Les fichiers du modèle sont listés avec leur nom (l'infobulle donne le chemin et le SHA-256), leur quantification et leur taille, et un bouton **TÉLÉCHARGER**, ou **✓ INSTALLÉ** si vous l'avez déjà. En vue GGUF, les petits fichiers comme le README ou la config sont masqués. **Espace insuffisant** apparaît quand un fichier dépasse la place libre sur le disque. Pour choisir parmi les quantifications, Q4_K_M est un bon compromis entre taille et qualité, et un modèle demande à peu près sa taille de fichier en mémoire libre.

Les téléchargements passent l'un après l'autre. Chacun affiche **EN ATTENTE**, puis la quantité reçue sur le total, la vitesse et les minutes restantes, avec **PAUSE**, **REPRENDRE** et **ANNULER**. À la fin, il affiche **VÉRIFICATION SHA-256…**, ou **ÉCHEC : …** en cas d'erreur. Un téléchargement interrompu reprend là où il s'était arrêté.

### Modèles installés et jeton

Chaque modèle installé montre son fichier, son dépôt, sa taille et sa licence, et **SHA-256 vérifié** (ou « aucune empreinte publiée »). **AFFICHER** ouvre son dossier et **SUPPRIMER** l'efface, après confirmation. L'en-tête porte **OUVRIR LE DOSSIER**, qui ouvre le dossier des modèles, et donne le nombre de modèles ainsi que la place occupée et libre.

Le champ **JETON** reçoit un jeton d'accès Hugging Face facultatif, utile seulement pour les modèles restreints. Appuyez sur **ENREGISTRER LE JETON** (ou Entrée) ; l'enregistrer vide le supprime. Le jeton est gardé sur cet ordinateur, jamais réaffiché, et envoyé uniquement à huggingface.co. Le même jeton peut s'enregistrer dans [Sources](help:sources).

### Claude Code

La partie **INTÉGRATION** permet d'utiliser Claude Code comme modèle. Le panneau de gauche montre le **PROGRAMME** (où Claude Code est installé, ou « introuvable sur cet ordinateur »), sa **VERSION** et le **COMPTE**, connecté ou non ; l'e-mail n'est jamais affiché. Tout cela est revérifié à chaque ouverture de la page, et **REVÉRIFIER** le refait à la demande.

Si Claude Code n'est pas installé, installez-le depuis claude.com/claude-code, connectez-vous une fois en tapant `claude` dans le Terminal, puis appuyez sur **REVÉRIFIER**. S'il est installé mais déconnecté, tapez `claude auth login` dans le Terminal.

**MODÈLE** choisit entre Opus, Sonnet, Fable et Haiku, et **EFFORT** va de faible à max, en passant par moyen, élevé et très élevé. Pour l'un comme pour l'autre, **PAR DÉFAUT** garde le choix de Claude Code, et un changement s'applique dès la question suivante. Le panneau de droite, **Charger dans un environnement**, montre ce que contient chaque environnement, avec **CHARGER**, **REMPLACER** (qui retire le modèle en place) ou **ÉJECTER**. Vous pouvez aussi glisser Claude Code depuis la liste des **Agents** sur un cube.

### Ce que Claude Code peut et ne peut pas faire

Dans un cube, Claude Code répond dans la discussion du cube, une exécution par question, et la conversation se poursuit d'une question à l'autre jusqu'à ce que vous appuyiez sur **EFFACER**. Ses propres outils sont coupés : pas de shell, pas d'accès à vos fichiers, pas de recherche web, et aucun de vos réglages, hooks ou serveurs Claude Code n'est utilisé.

Il ne lit les données de Deepwater que si l'une des cases de compétence du cube est cochée. Il passe alors par les outils de Deepwater, servis sur cet ordinateur seulement, avec une clé par cube, et fait au plus 20 appels d'outils par question.

Contrairement à un modèle local, Claude Code tourne sur les serveurs d'Anthropic avec votre propre connexion Claude. Vos questions, et ce que lisent ses outils, partent chez Anthropic et comptent sur votre abonnement Claude ou votre usage de l'API.

## Agents

C'est ici que les modèles se mettent au travail. Le panneau de gauche, **Modèles et intégrations**, liste vos modèles téléchargés et **Claude Code** (intégration · serveurs d'Anthropic) ; s'il est vide, prenez un modèle dans **LLM › Modèles locaux**. La partie droite porte quatre cubes, **ENVIRONNEMENT 1** à **4**, en grille 2 × 2 ou en rangée de quatre, selon ce qui donne les plus grands cubes. L'en-tête compte les environnements chargés (`2 / 4`) et la mémoire que demandent les modèles chargés face à la RAM de cet ordinateur, avec une infobulle d'alerte au-delà des trois quarts.

### Charger et déplacer des modèles

Glissez un modèle de la liste sur un cube : il s'y charge, et le cube s'allume. Pendant le glisser, le cube affiche **DÉPOSEZ UN MODÈLE ICI**, puis **RELÂCHEZ POUR CHARGER**. Glisser un cube allumé sur un autre y déplace son modèle, et **ÉJECTER** éteint le cube et arrête son modèle.

La carte d'un cube montre le nom du modèle, puis sa quantification, sa taille et sa part de la RAM (ou « INTÉGRATION · tourne sur les serveurs d'Anthropic » pour Claude Code), et son état : **EN VEILLE**, **CHARGEMENT**, **PRÊT**, **RÉPOND**, **ÉCHEC** ou **ÉTEINT**. Les affectations sont retenues : au lancement, les cubes reviennent en veille et ne chargent leur modèle qu'à l'usage. **DISCUTER**, ou un double-clic sur le cube, ouvre sa conversation et lance le chargement de son modèle.

### Isolement et bac à sable

Chaque cube a son propre dossier et, pour un modèle local, son propre processus, sans internet, sans accès à vos fichiers et sans rien partager avec les autres cubes. Sur macOS, ce processus tourne dans un bac à sable qui ne l'autorise qu'à lire l'application et son fichier de modèle, son propre dossier, et à utiliser le GPU. Sur les autres systèmes il n'est pas isolé ; la discussion le signale, et mieux vaut alors s'en tenir à des modèles de confiance.

Un modèle local sait qu'il est hors ligne, et il ne voit aucune donnée tant qu'aucune compétence n'est cochée. Les modèles d'image et les autres fichiers que le moteur ne sait pas exécuter échouent avec un message dans la discussion.

### Compétences : ce qu'un modèle peut lire

Un modèle démarre sans aucune donnée. Tableaux et graphiques ne lui parviennent jamais collés dans son invite : il ne peut les lire qu'en appelant les outils d'une compétence que vous lui accordez. Les cases de compétence se trouvent sur chaque carte et dans l'en-tête de la discussion. Cocher une case accorde la compétence aussitôt, et chaque appel d'outil est revérifié : une compétence décochée en cours de réponse fait refuser l'appel suivant.

**Lire les données de Deepwater** ouvre ce que montrent les pages : cours et historiques de prix, calendrier économique, actualités et Squawk, séries d'Économie, banques centrales, COT, sentiment retail et de risque, prévisions et recherche des banques, horaires des marchés, lectures techniques, poches de liquidité, saisonnalité, scores fondamentaux, et votre track record sans courtier ni identifiants. Elle couvre aussi les chaînes et les faits de **Data › Macro data**, ainsi que les bougies, le footprint, le carnet d'ordres et l'open interest de **Data › Technical data**. Elle n'atteint jamais les pages Sources et IA, ni aucune clé, ni aucun mot de passe.

**Apprentissage automatique** donne accès au sous-onglet **ML** : le modèle peut charger des jeux de données (prix, macro, COT, technical data), les lire, entraîner des modèles des cinq familles dans le même bac à sable (un à la fois, nommé d'après l'environnement), puis lire et comparer les résultats, dont les chiffres sont calculés par Deepwater. Il ne peut rien supprimer, arrêter ni installer, ni lire de fichier hors des jeux de données.

**Backtester des stratégies** donne le testeur de stratégies des [Graphiques](help:charts), avec des stratégies à règles ou des stratégies Pine que le modèle écrit, exécutées dans un bac à sable, ainsi que les contrôles quant, sur l'historique de Technical data ou les sources des Graphiques. Rien n'est enregistré et aucun ordre n'est passé. Ses essais sont comptés, et le ratio de Sharpe du meilleur est déflaté en fonction de leur nombre.

Un modèle local peut faire jusqu'à 5 appels d'outils par tour et 6 tours par question. Chaque appel s'affiche dans la discussion et s'écrit dans le journal de l'environnement. Les textes d'actualités, de fils et de recherche sont transmis au modèle marqués comme données de tiers, et non comme des instructions.

### La discussion

L'en-tête de la discussion porte **← ENVIRONNEMENTS** pour revenir aux cubes, puis le cube, le modèle, l'état, l'appareil (GPU ou CPU), la taille du contexte et la vitesse. Suivent **TOILE**, les boutons **1** à **4** pour changer d'environnement, et **EFFACER**, qui vide la conversation. Entrée envoie et Maj+Entrée va à la ligne ; **ENVOYER** devient **ARRÊTER** pendant la réponse. Si le moteur échoue, son erreur s'affiche avec **RÉESSAYER**.

Les blocs de réflexion sont atténués sous **RÉFLEXION**, que vous pouvez déplier en entier ou replier, et ne sont pas renvoyés au modèle. Chaque appel d'outil apparaît comme une ligne **OUTIL** avec le nombre de caractères lus, ou **OUTIL ✕** quand il a été refusé ou a échoué.

Quand une réponse est écrite après des résultats d'outils, un contrôle des chiffres la suit. Il affiche « ✓ n chiffres vérifiés » quand chaque chiffre figure dans ce que les outils ont renvoyé ; sinon il liste en orange les chiffres introuvables. Les détails d'une réponse donnent les jetons, les jetons par seconde et les secondes, et signalent si elle a été arrêtée, coupée par la limite de longueur ou par le contexte, ou si des messages anciens ont été écartés. La conversation est gardée dans le dossier de l'environnement, sur cet ordinateur.

### Le panneau d'activité

Le panneau à droite de la conversation montre ce que fait l'agent ; glissez son bord pour l'élargir ou le réduire, et sa largeur est retenue. **EN CE MOMENT** indique l'outil appelé et depuis combien de temps, ou dit que l'agent réfléchit, écrit, charge son modèle, est en erreur ou attend votre question.

**ENTRAÎNEMENTS ML** présente une tuile par entraînement lancé par cet agent : ses données, son avancement et son étape, une courbe en direct (score par pli, perte par époque ou rendement par épisode), puis le résultat avec un petit graphique. **ARRÊTER** interrompt un entraînement en cours. La tuile la plus récente glisse au-dessus, et **‹** et **›** (ou ← et → après un clic dans le panneau) font aller en arrière et en avant. **DERNIÈRES ACTIONS** liste les derniers appels d'outils de l'agent.

### La vue TOILE

Cocher **TOILE** dans l'en-tête de la discussion place la conversation au milieu, reliée à trois nœuds. **COLLECTE** liste les données que l'agent a lues, les plus récentes d'abord, avec ce que demandait chaque appel et le décompte des lectures et des refus. **ENTRAÎNEMENT** montre ses entraînements, comme dans le panneau d'activité. **COMPARAISON** reprend sa dernière comparaison d'entraînements, refaite par Deepwater, avec la question de chaque groupe, le meilleur réglage et le verdict. Un lien s'allume et porte des points mobiles pendant que son travail tourne.

### Évaluation

Que vaut le jugement de marché d'un modèle, une fois écarté ce dont il se souvient simplement ? **ÉVALUATION**, sur une carte chargée, ouvre la fenêtre qui permet de le mesurer.

Avec **Mode évaluation activé** et une date (UTC), les outils de l'environnement répondent comme à ce moment-là : bougies clôturées à cette heure, flux d'ordres stocké jusque-là, faits macro publiés d'ici là, et backtests sur l'historique d'avant. Une date plus tardive est refusée, et les outils qui ne connaissent que le présent (prix, actualités, carnet en direct, pages) sont coupés. Le modèle repart à neuf et connaît la date, mais sa propre mémoire des événements postérieurs n'est pas effacée. **À l'aveugle** va plus loin, en décalant les dates et en masquant le nom et le niveau de prix du marché. La carte affiche alors **ÉVALUATION · date UTC**, et la discussion marque où le mode commence et où il finit.

Le **Protocole d'évaluation** porte sur BINANCE:BTCUSDT, en bougies d'une heure, avec 15 pb de frais par côté. À chaque moment de la période **Du**–**Au**, le modèle décide LONG, SHORT ou FLAT pour l'**Horizon** choisi (1, 4, 12 ou 24 h), dans une conversation neuve, avec ses outils. Vous réglez les **Décisions** (de 2 à 60) et les **Répétitions** (de 1 à 5), et choisissez des **Essais** avec le marché nommé, à l'aveugle, ou les deux. Chaque décision est notée sur les bougies stockées, après frais, face au buy & hold, à la règle de tendance, au flat et à un modèle ML sans LLM. Le protocole demande **Lire les données de Deepwater** cochée, et une douzaine de décisions prend plusieurs minutes.

Commencez par **Sonder sa mémoire** : seules comptent alors les décisions postérieures au dernier mois dont le modèle se souvient (niveau A). Le **Test en avance (niveau C)** pose la même question au présent, puis toutes les 1 à 72 heures tant que Deepwater tourne, et note chaque décision une fois son horizon passé ; un moment manqué compte comme manqué. Gardez le même modèle et les mêmes compétences tout du long. Le rapport s'affiche dans la discussion.

## ML

Ce sous-onglet entraîne vos propres modèles sur des données de marché et macro, de bout en bout : vous choisissez une famille, chargez un jeu de données, dites quoi prédire et à partir de quoi, choisissez un algorithme, puis lisez et comparez les entraînements.

### Environnement Python

L'entraînement se fait sur Mac : chaque entraînement tourne dans un bac à sable de macOS qui le coupe du réseau et de vos fichiers, et Deepwater n'a pas encore d'équivalent sous Windows ; un iPhone ou un iPad ne peut pas lancer Python du tout. Sur ces appareils, l'en-tête le dit et rien n'est installé. Sur Mac, l'entraînement demande Python 3.11 ou plus récent, depuis python.org ou Homebrew. L'en-tête indique si Python a été trouvé et dans quel état se trouve l'environnement. **INSTALLER**, après confirmation, crée l'environnement Python propre à Deepwater dans son dossier de données et télécharge scikit-learn, pandas, NumPy, SciPy, LightGBM et XGBoost, en paquets précompilés uniquement (environ 300 Mo) ; **ANNULER** l'interrompt pendant l'installation.

**INSTALLER PYTORCH** apparaît quand une famille en a besoin et ajoute PyTorch, sur le processeur seulement (environ 700 Mo). Un algorithme qui ne peut pas tourner ici est marqué **indisponible** ou **demande PyTorch**, avec la raison dans son infobulle.

### Familles

| Famille | Ce qu'elle apprend |
|---|---|
| SUPERVISÉ | Prédire une colonne à partir des autres, sur un historique étiqueté (une classe ou une valeur) |
| NON SUPERVISÉ | Trouver des groupes de périodes semblables, des régimes, des jours inhabituels, ou réduire les dimensions ; sans cible |
| SEMI-SUPERVISÉ | Apprendre de quelques lignes étiquetées et de beaucoup d'autres qui ne le sont pas (classes seulement) |
| AUTO-SUPERVISÉ | Apprendre des données elles-mêmes (parties cachées ou suivantes), puis réutiliser l'acquis (PyTorch) |
| PAR RENFORCEMENT | Un agent apprend à être long, short ou flat sur l'historique d'un marché, récompensé par son gain ou sa perte simulés ; aucun ordre n'est jamais envoyé |

### Données

Cinq sources peuvent alimenter un entraînement. Changer de source, de marché ou d'intervalle charge les données aussitôt, et **CHARGER** les recharge ; la page affiche ensuite leur titre, leurs lignes, leur période et leur source.

| Source | Ce qu'elle charge |
|---|---|
| PRIX | Les bougies **1D**, **1W** ou **1H** d'un marché depuis Yahoo, avec des variables tirées des seules bougies passées : rendements, volatilité, RSI, distance aux moyennes mobiles, amplitude. Quotidien et hebdomadaire depuis 1990, horaire sur les 729 derniers jours |
| MACRO | Le tableau mensuel d'une économie ; les valeurs trimestrielles sont reportées sur leurs mois, et les lignes datées par période de référence, pas par publication |
| COT | Un marché CFTC : positions nettes par catégorie, open interest et clôture du future |
| CSV | Tout CSV avec une ligne d'en-tête, choisi par **CHOISIR UN FICHIER…** ; une colonne de date est reconnue à son nom |
| TECHNICAL DATA | Une paire de Data › Technical data : une unité de temps et ses 50,000 à 1,000,000 dernières bougies ; **flux d'ordres** ajoute ses colonnes de footprint, profondeur et open interest, en ne gardant que les bougies qu'elles couvrent |

Les données techniques viennent des paires que vous enregistrez dans [Data](help:data). Les jeux de données restent sous 5 Go au total ; au-delà, les plus anciens sont supprimés quand un nouveau est écrit.

### Cible et variables

**CIBLE : CE QUI EST PRÉDIT** prend une transformation, une colonne et le nombre de lignes à regarder en avant (de 0 à 250). La transformation peut être la **valeur**, sa **variation** ou sa **variation en %**, sa **direction (hausse / pas hausse)**, la **volatilité réalisée**, l'**amplitude (haut − bas)**, la **taille du mouvement**, ou un **gros mouvement (oui / non)**. Une phrase en dessous dit en clair ce qui sera prédit. Le libellé change avec la famille : il devient une suite facultative en non supervisé, des classes en semi-supervisé, une sonde facultative en auto-supervisé, et le prix tradé en renforcement.

**VARIABLES : CE DONT IL APPREND** liste les colonnes à cocher, avec **TOUT** et **AUCUNE**. La cible elle-même est signalée, car un modèle qui la lirait lirait tout simplement la réponse. **Aussi les valeurs passées de chaque variable** ajoute de 0 à 10 retards.

### Modèle

**ALGORITHME** propose, pour chaque famille, les méthodes ci-dessous ; l'infobulle de chacune dit ce qu'elle fait.

| Famille | Algorithmes |
|---|---|
| Supervisé | Linéaire / logistique, Forêt aléatoire, Gradient boosting, LightGBM, XGBoost, Plus proches voisins, Machine à vecteurs de support, Réseau de neurones (MLP) |
| Non supervisé | k-means (avec un balayage de k), Mélange gaussien, Hiérarchique (8,000 dernières lignes), DBSCAN, Isolation forest, Facteur local d'anomalie, Composantes principales (ACP), t-SNE (5,000 dernières lignes) |
| Semi-supervisé | Auto-apprentissage, Diffusion des étiquettes, Propagation des étiquettes |
| Auto-supervisé | Autoencodeur masqué, Contrastif (SimCLR), Pas suivant |
| Renforcement | Q-learning (table), Réseau Q profond (DQN), Acteur-critique (A2C) ; frais en pb par changement de position, long-short ou long seulement |

**PARAMÈTRES** regroupe les réglages de l'algorithme, dans des bornes fixes. En supervisé, **VALIDATION** règle la **Part de test (dernières lignes)** de 5 à 50 % (20 % par défaut), les **Plis de validation croisée temporelle** de 0 à 10, la **Tâche** (automatique, classification ou régression), et l'option **Partie test prédite par** : une seule coupure, ou du walk-forward réentraîné 5 ou 10 fois. **NOM** est facultatif ; laissé vide, l'entraînement prend le nom de l'algorithme et des données.

**ENTRAÎNER**, qui devient **ARRÊTER** pendant qu'il tourne, lance un entraînement à la fois ; s'il ne peut pas démarrer, la raison s'affiche. Les entraînements tournent dans un bac à sable, sans réseau et sans accès à vos fichiers ni à vos clés.

### Entraînements

Chaque entraînement a sa ligne, avec son nom, son état (un pourcentage pendant qu'il tourne), sa date et son score principal face au « sans compétence ». Un clic sur une ligne affiche son résultat. **DOSSIER** ouvre son dossier, qui contient sa copie des données, ses prédictions et les fichiers du modèle, et **SUPPRIMER** l'efface avec son modèle, après confirmation, mais pas pendant qu'il tourne.

Un résultat supervisé donne les périodes d'entraînement et de test et les mesures en **TEST**, **ENTRAÎN.**, **SANS COMPÉTENCE** et **PERSISTANCE** (ce qui a été vu en dernier se répète), puis les scores walk-forward ou de validation croisée, la prédiction pour la dernière ligne, un graphique de la période de test, ce sur quoi le modèle s'appuie, et la matrice de confusion. Pour une direction ou un prix, les prédictions de test sont tradées après frais face au buy & hold, à une règle de tendance et à une exposition constante. Les cibles de volatilité sont jugées face à HAR et EWMA.

Un résultat non supervisé décrit les groupes (lignes, durée, ce qui a suivi, ce qui les distingue), avec des scores de qualité, les groupes dans le temps, le balayage de k, les lignes inhabituelles et pourquoi, des cartes et les poids de l'ACP. Un résultat semi-supervisé montre les étiquettes gardées et cachées, et le compare, sur les mêmes lignes de test, au même modèle entraîné avec les seules étiquettes gardées puis avec toutes. Un résultat auto-supervisé donne la perte par époque, les fenêtres les plus difficiles, une carte des représentations, et la sonde facultative face aux variables brutes et au « sans compétence ».

Un résultat par renforcement donne le rendement, total et par an, le ratio de Sharpe, la perte maximale, les transactions et le temps en position, face au buy & hold, au flat et à 50 agents aléatoires, ainsi que le dernier choix de l'agent. Ces sorties ne sont pas des conseils.

### COMPARER

Le meilleur réglage est-il vraiment meilleur, ou seulement le plus chanceux de nombreux essais ? Cochez la case à gauche de plusieurs lignes (ou ⌘-cliquez-les) et appuyez sur **COMPARER (n)**. Sans rien de coché, **COMPARER** prend l'entraînement sélectionné et ceux qui lui ressemblent : même question sur le même marché, ou même réglage sur d'autres marchés.

**MÊME MARCHÉ** classe les réglages par leur avance sur le « sans compétence », avec des p-values ajustées au nombre de réglages essayés, les confronte aussi à la persistance, compare le meilleur au second sur leurs lignes de test communes, et rend un verdict en clair. **PLUSIEURS MARCHÉS** prend un réglage et mesure chaque marché face à sa propre référence. Les entraînements par renforcement se comparent sur le rendement de test, le buy & hold, l'excédent et les agents aléatoires battus ; les regroupements sur le nombre de groupes et la silhouette, à titre descriptif seulement.

Chaque chiffre est calculé par Deepwater à partir des fichiers des entraînements, les mêmes que citent les agents. Un clic sur une ligne ouvre cet entraînement, et **‹ RETOUR À LA COMPARAISON** ramène à la comparaison.
