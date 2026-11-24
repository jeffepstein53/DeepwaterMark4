# Data

L'onglet Data montre ce que Deepwater enregistre lui-même, sur cet ordinateur. **Macro data** conserve chaque fait économique que l'application lit, sous forme de chaînes liées ; **Technical data** conserve les bougies des paires que vous choisissez, avec leurs indicateurs et leur flux d'ordres.

## Macro data

Cette page répond à une question simple : qu'a dit chaque indicateur, pour quelle période, et a-t-il été révisé depuis ? Les chaînes sont listées à gauche ; la partie droite dessine celles que vous cochez, en 2D par défaut ou dans l'une des deux vues 3D, avec un cadre de détails sous la vue 2D.

### Faits, chaînes et révisions

Un **fait** est une valeur d'un indicateur pour une période, réelle ou prévue. Une **chaîne** relie les faits d'un indicateur, d'un même type et d'une même source, chacun au précédent. Une valeur révisée n'écrase jamais l'ancienne : la révision est un nouveau fait, posé sur le bloc qu'elle corrige, si bien que vous voyez toujours ce que l'on savait auparavant.

Chaque chaîne affiche son type : **RÉEL**, **PRÉVISION**, **CONSENSUS**, **IMPLICITE** ou **PRIX**. Les chaînes se répartissent en six familles, chacune avec sa couleur. La famille macro reprend chaque série de chaque économie de l'onglet [Économie](help:economy).

| Famille | Ce qu'elle enregistre |
|---|---|
| TAUX | Taux directeurs de chaque économie (tout l'historique, un fait par changement), taux actuel des banques centrales suivies, leurs décisions (tirées de leurs communiqués, ou déduites de la série des taux : deux chaînes distinctes) et le chemin implicite du marché |
| MACRO | Chaque série macro de chaque économie de l'onglet Économie, tout l'historique |
| CALENDRIER | Publications du calendrier enregistrées semaine après semaine : valeurs réelles, consensus Forex Factory, et révisions lues dans le chiffre « précédent » |
| PRIX | Ouverture, plus haut, plus bas et clôture quotidiens de chaque instrument, depuis Yahoo |
| PRÉVISIONS | Prévisions publiées par les banques, par trimestre (ING), reliées à la chaîne du marché qu'elles visent |
| POSITIONS | COT de la CFTC depuis 1986 (open interest et positions nettes par catégorie), enregistrements du sentiment retail et indice de risque |

### Import et mise à jour

Le premier import démarre la première fois que vous ouvrez cette page. Il télécharge l'historique de chaque famille, et ces faits sont marqués « importés de l'historique ». Ensuite, une passe a lieu au lancement pour chaque famille dont la dernière passe date d'au moins 6 heures, puis toutes les 6 heures tant que Deepwater tourne, et aussitôt pour chaque nouvelle valeur réelle du calendrier. Ces passes n'ajoutent que ce qui est nouveau ou révisé, et leurs faits sont marqués « vus en direct ».

L'en-tête indique où en est la passe en cours (`2 sur 6 · Prix de marché…`), ou **À jour · prochaine passe dans …**, avec les totaux de chaînes, de faits et de révisions. Si la source d'une famille n'est pas prête, ce qui a été récupéré est stocké et la famille est repassée plus tard. Tout est gardé dans une base sur cet ordinateur ; rien n'est envoyé nulle part.

### Choisir les chaînes

Chaque mot tapé dans le champ de recherche du panneau de gauche doit figurer dans le pays ou le marché d'une chaîne, son indicateur, son type, sa source ou sa clé. Les boutons de famille, sur deux rangées, restreignent la liste à une famille, tandis que **TOUT** les montre toutes ; chacun affiche son nombre de chaînes, et un seul est actif à la fois. **✓ TOUT COCHER** et **✕ TOUT DÉCOCHER** cochent ou décochent toutes les chaînes que la famille et la recherche en cours laissent passer, et une ligne en dessous donne le nombre de chaînes affichées.

Chaque ligne indique le sujet (un pays, une banque ou un marché), l'indicateur et la source, le type, et le nombre de faits et de révisions (`1,204 · 3R`). Un clic sur une ligne affiche ou masque sa chaîne, et les coches sont retenues. Quand rien ne correspond, la liste affiche **Aucune chaîne ne correspond.**

### La vue 2D

Les boutons du haut passent de la **2D**, vue par défaut, aux deux vues 3D. En 2D, chaque chaîne affichée occupe une bande sur un même axe du temps ; les dates du haut restent en place pendant le défilement, et un trait pointillé marque aujourd'hui. **Période** règle l'étendue, de 1M à 20Y en passant par 6M, 1Y et 5Y, ou TOUT, qui va du premier au dernier fait des chaînes affichées. L'étendue est retenue, et chaque période déborde un peu au-delà d'aujourd'hui pour laisser la place aux prévisions.

Un bloc occupe sa période, et sa luminosité situe la valeur entre le plus bas et le plus haut de la chaîne. Les blocs d'une chaîne sont reliés par un trait, et la valeur s'inscrit dedans quand le bloc est assez large. Les révisions forment de fines couches orange empilées sur le bloc, trois au plus dessinées. Les prévisions sont des blocs pointillés sur la période qu'elles visent, avec un trait en pointillé vers la valeur réelle quand sa chaîne est aussi affichée. Un petit point vert signale un fait vu en direct, et non importé de l'historique.

La molette zoome autour du curseur, de 5 jours à 200 ans, et glisser déplace dans le temps. Le survol d'un bloc donne sa chaîne, sa période, sa valeur et son nombre de révisions ; un clic affiche ses détails en dessous. Les bandes sont lues dans la base à mesure qu'elles apparaissent, si bien que des milliers de chaînes peuvent être cochées. Sans rien de coché, la vue affiche **Cochez des chaînes à gauche pour les afficher ici.**

### Le cadre des détails

Sans sélection, le cadre explique blocs, chaînes, révisions et prévisions. Dès que vous cliquez sur un bloc, il donne la chaîne et la source, la période et la valeur (pour une prévision, la période visée, la valeur et le jour où elle a été faite), la date d'enregistrement du fait et s'il a été vu en direct ou importé, sa date de publication ou d'observation, et la note de la source.

Pour une période révisée, il liste toutes ses valeurs successives, de la plus ancienne à la plus récente, avec leur jour d'enregistrement. Il montre aussi les voisins, c'est-à-dire les valeurs précédente et suivante avec leurs périodes, et la variation. **◀ PRÉCÉDENT** et **SUIVANT ▶** parcourent les blocs de la chaîne, et la vue suit.

### PAYSAGE 3D

Le paysage étale le temps en largeur, une boîte par mois, et les chaînes affichées d'avant en arrière, groupées en plateaux par famille ; une chaîne de prévision se tient juste derrière celle qu'elle vise. Hauteur et luminosité traduisent le rang du mois dans l'historique de sa propre chaîne, de 0 pour le plus bas jamais vu à 100 pour le plus haut. Des chaînes d'unités différentes deviennent ainsi comparables, et les mouvements simultanés dessinent des crêtes.

Les mois révisés portent une fine dalle orange, les prévisions sont translucides, et des plans translucides marquent aujourd'hui et la date de coupe. Le curseur **Coupe** place ce plan de date, **✕** l'enlève et **RECENTRER** remet la caméra en place. Glissez pour tourner, glissez avec le clic droit (ou Maj + glisser) pour déplacer, et zoomez à la molette. Le survol montre la valeur, un clic garde le détail d'un bloc, et un double-clic ouvre la chaîne en 2D autour de ce mois, en la cochant si elle ne l'était pas.

Le paysage suit les coches. Sans rien de coché, il affiche **Cochez des chaînes à gauche (ou TOUT COCHER) pour élever le paysage.**

### CUBE MACRO 3D

Le cube croise pays, indicateurs et temps. Il est construit à partir de toutes les chaînes macro, quelles que soient les coches, avec une cellule par mois (ou par trimestre, ou par année). Sa couleur donne le rang de la valeur dans sa propre série : bleu pour le plus bas jamais vu, gris pour le milieu, orange pour le plus haut.

**Couper par** tranche le cube par date, par pays ou par indicateur, puis le curseur choisit laquelle, dont le nom s'affiche à côté ; le reste du cube demeure en fantôme pâle. **RECENTRER** tourne la caméra face à la coupe. La souris fonctionne comme dans le paysage : glisser pour tourner, clic droit glissé (ou Maj + glisser) pour déplacer, molette pour zoomer, survol d'une cellule pour sa valeur et son rang, double-clic pour l'ouvrir en 2D.

Tant que l'import n'a rempli aucune chaîne macro, le cube l'indique : c'est l'import qui les remplit. Les deux vues 3D ne sont créées qu'à leur première ouverture, et celle-ci commence par lire les cellules mensuelles.

## Technical data

Cette page enregistre les bougies clôturées des paires que vous choisissez et montre où en est leur collecte. Le panneau de gauche suit la collecte de la paire sélectionnée, celui de droite montre ses dernières bougies telles qu'elles sont stockées, et un panneau de flux d'ordres apparaît quand la paire en enregistre.

### Ce qui est enregistré

Chaque paire garde ses bougies clôturées dans les unités de temps choisies parmi 1m, 5m, 15m, 1h, 4h, 1D et 1W, depuis son premier jour ou depuis un jour que vous fixez, avec les colonnes d'indicateurs que vous retenez. Celles-ci sont calculées par le moteur d'indicateurs de Deepwater sur tout l'historique de chaque série. BTCUSDT est enregistrée par défaut, dans les sept unités de temps, sur tout son historique et avec 23 colonnes d'indicateurs ; si vous la supprimez, elle le reste.

Les paires crypto viennent de Binance spot, des données publiques qui ne demandent pas de clé. Elles sont rattrapées au lancement puis tenues à jour chaque minute ; un trou est redemandé une fois, et ce qui manque encore ensuite correspond à une panne de la plateforme elle-même. Le forex vient des prix bid de Dukascopy, sans clé lui non plus : les sept majeures (EURUSD, GBPUSD, USDJPY, USDCHF, AUDUSD, USDCAD, NZDUSD), avec un historique depuis 2003. La journée forex va de 17 h à 17 h heure de New York et n'est publiée qu'une fois terminée : les bougies les plus récentes sont donc celles de la veille, et il n'y en a pas le week-end.

Les paires Binance peuvent aussi enregistrer du flux d'ordres : l'historique footprint (spot, perpétuel USD-M ou les deux), la profondeur du carnet et l'open interest du perpétuel, et un carnet en direct gardé en mémoire seulement. Tout ce qui est écrit sur disque va dans une seule base, sur cet ordinateur, dont la taille s'affiche en haut à droite.

### Suivre la collecte

Les pastilles en haut du panneau de gauche choisissent la paire affichée, et ce choix est retenu. Dessous figurent le marché et la source, le premier jour, et le nombre d'unités de temps et de colonnes d'indicateurs.

Le tableau compte ensuite une ligne par unité de temps. **UT**, **BOUGIES** et **DU → AU** donnent l'unité de temps, son nombre de bougies et ses première et dernière bougies. **INDICATEURS** compte les lignes calculées et passe au vert quand toutes les bougies en ont. **TROUS** donne le nombre de trous et de bougies manquantes, en orange tant que certains n'ont pas encore été redemandés. **ÉTAT** dit où en est l'unité de temps, et une barre montre l'avancée de l'historique, verte une fois complet.

L'état peut être : téléchargement de l'historique, mise à jour, en attente, complet (tenu à jour, ou chaque jour pour le forex), en pause, annulé, pas démarré, rien encore, historique incomplet, une erreur, ou « Dukascopy a refusé … : redemandé dans 10 min ».

Quand la paire enregistre du flux d'ordres, un second tableau liste ses **ARCHIVES** (footprint spot, footprint perp., profondeur perp., intérêt ouvert), avec les **JOURS** stockés sur ceux voulus, **DU → AU**, les jours **ABSENTS**, la **TAILLE** par jour et l'**ÉTAT**. Sous les tableaux, une ligne dit ce qui se télécharge ou se calcule en ce moment, avec une note quand la collecte est en pause, annulée, pas encore démarrée ou ralentie par la lenteur de Dukascopy.

La collecte démarre 20 secondes après l'ouverture de Deepwater, sauf si Binance ou Dukascopy est désactivé dans [Sources](help:sources), et les paires passent à tour de rôle.

### Mettre en pause, annuler, ajouter une paire

**⏸ TOUT EN PAUSE**, en haut, arrête toutes les paires, crypto et forex, jusqu'à ce que vous appuyiez sur **▶ TOUT REPRENDRE** ; ce choix est retenu d'un lancement à l'autre. **⏸ PAUSE** et **▶ REPRENDRE** font de même pour cette paire seulement : ce qui est stocké reste, et la collecte repart là où elle s'était arrêtée.

**ANNULER** va plus loin. Il arrête la paire et efface tout ce qu'elle a téléchargé, après vous avoir demandé de choisir entre **Arrêter et effacer** et **Garder**. La paire reste listée, en pause et vide, et **▶ RELANCER** reprend sa collecte depuis son premier jour.

**RÉGLAGES** ouvre la fenêtre de la paire, décrite plus bas, et **+ PAIRE** en ajoute une nouvelle. Sans aucune paire, la page affiche **Aucune paire enregistrée : ajoutez-en une avec + PAIRE.**

### La fenêtre d'une paire

À l'ajout d'une paire, vous choisissez d'abord **CRYPTO · BINANCE** ou **FOREX · DUKASCOPY**. En crypto, tapez un symbole spot Binance dans **Paire**, par exemple `ETHUSDT`. Binance est interrogé aussitôt et répond si la paire est toujours cotée ou ne l'est plus (historique seulement) et quand date sa première bougie, ou la fenêtre indique « Binance n'a pas de paire spot … ». En forex, choisissez une majeure pas encore enregistrée. Une paire déjà enregistrée est refusée.

**Historique** part du premier jour de la paire ou, avec **Depuis le**, d'une date que vous fixez. Sous **Unités de temps**, cochez celles que vous voulez. En forex, 4h, 1D et 1W sont construites à partir de 1h, gardée avec elles, tandis que 1m, 5m et 15m demandent un fichier minute par jour : comptez des jours de téléchargement.

La section **Footprint** (crypto seulement) enregistre le **Spot**, le **Perpétuel USD-M** ou les deux, depuis la première archive (spot 2017-08-17, perpétuel 2019-12-31) ou depuis une date, trois mois en arrière par défaut. Elle garde acheteurs et vendeurs à chaque niveau de prix, minute par minute, par niveaux d'un point de base (0.01 % du prix), soit environ 130 Ko par jour et par marché.

**Carnet d'ordres et positions** (crypto seulement) propose trois enregistrements. **Profondeur du carnet** prend le carnet du perpétuel jusqu'à ±0.2, 1, 2, 3, 4 et 5 % toutes les 30 secondes, depuis 2023. **Intérêt ouvert** s'accompagne des ratios long/short toutes les 5 minutes, depuis 2020. **Carnet en direct (mémoire)** garde le carnet et les transactions spot et perpétuel en mémoire tant que Deepwater tourne, sans jamais les écrire sur disque.

**Indicateurs** liste les colonnes, chacune supprimable par ✕. **LISTE PAR DÉFAUT** rétablit les 23 colonnes par défaut (moyennes 20, 50 et 200, Bollinger, RSI 14, MACD, ATR, DMI, Stochastique, OBV, VWAP), et **AUCUN** ne garde que les bougies. Avec **AJOUTER UN INDICATEUR**, vous choisissez un indicateur de la bibliothèque de Deepwater et le réglez, puis Deepwater le vérifie sur des bougies de test : il n'est accepté que s'il ne regarde jamais en avant et si, recalculé sur de nouvelles bougies, il redonne les valeurs calculées sur tout l'historique. Les sorties qui regardent en avant sont refusées, et leur bouton dit pourquoi. **AJOUTER** place l'indicateur dans la liste ; les doublons sont refusés.

**Taille** estime les bougies et la place disque pour chaque unité de temps. La fenêtre se ferme par **AJOUTER LA PAIRE** (ou **ENREGISTRER**), par **ANNULER**, ou par **SUPPRIMER LA PAIRE**, qui efface toutes ses bougies et tous ses indicateurs après confirmation. Tout changement qui effacerait des données stockées, comme une unité de temps retirée, un premier jour plus tardif ou un footprint raccourci, liste ce qui disparaîtrait et demande **Supprimer et enregistrer**.

### Les dernières bougies

Le panneau de droite montre les 300 dernières bougies stockées, la plus récente en haut, dans l'unité de temps choisie par ses pastilles, qui sont celles de la paire (1h par défaut). Les colonnes sont **heure (UTC)**, **ouverture**, **haut**, **bas**, **clôture** (verte ou rouge), **volume**, **transactions** et **achat agressif %**, la part du volume achetée par des ordres au marché, puis chaque colonne d'indicateur. Le forex n'a ni nombre de transactions ni part agressive, affichés —.

Les valeurs d'indicateurs grisées sont encore dans leur période de chauffe : sur les premières bougies, les longues moyennes ne sont pas encore stabilisées, comme l'explique l'infobulle. Un `…` signifie que les indicateurs de cette bougie ne sont pas encore calculés.

### Le panneau Flux d'ordres

Ce panneau apparaît quand la paire enregistre du flux d'ordres, avec un onglet par type : **Footprint**, **Profondeur du carnet**, **Intérêt ouvert** et **Carnet en direct**. Il s'ouvre sur le dernier jour stocké et suit les nouveaux ; **‹** et **›** passent au jour d'avant ou d'après. Si aucun jour n'est encore stocké, patientez : la collecte télécharge d'abord les jours les plus récents.

L'onglet footprint bascule entre **Spot** et **Perpétuel USD-M** et montre une ligne par minute : heure, OHLC, volume, acheté, vendu, delta, transactions, niveaux, POC, zone de valeur, déséquilibres acheteurs et vendeurs, et séries, chaque colonne expliquée par son infobulle. Un clic sur une minute affiche ses niveaux à droite. Les déséquilibres utilisent un ratio de 3.

L'onglet de profondeur liste les instantanés du jour : la part des bids jusqu'à ±0.2, ±1 et ±5 %, la profondeur cumulée par distance (bids −, asks +), le prix médian et la portée de l'instantané. L'onglet d'intérêt ouvert présente des lignes de 5 minutes : contrats ouverts, variation, ratios long/short des meilleurs traders (par comptes et par positions), ratio de tous les comptes, et ratio achats/ventes agressifs.

Le carnet en direct montre le spot ou le perpétuel, rafraîchi chaque seconde : les 10 dernières minutes à l'écran et 24 h gardées en mémoire, jamais sur disque. Il démarre avec la collecte et ne montre rien tant que la paire est en pause.
