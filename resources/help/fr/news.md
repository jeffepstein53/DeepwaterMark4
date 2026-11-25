# Actualités

Ce que lisent les marchés : le calendrier économique avec ses résultats et l'historique des publications, un fil d'actualités visuel, les notes de recherche des banques elles-mêmes et un fil en temps réel accompagné de la télévision en direct. Aucune page de cet onglet ne demande de clé ni de compte.

## Calendrier

Le calendrier répond à trois questions : ce qui sort cette semaine, ce que le marché attend et ce qui a réellement été publié. Les filtres occupent le haut de la page : une recherche, la navigation par semaine, les pastilles de pays et les segments d'impact. En dessous, le tableau groupe les événements de la semaine par jour, et ses en-têtes de colonnes et de jour restent visibles pendant le défilement. Un clic sur une valeur fait glisser le panneau de fourchette depuis la droite. À partir de 1300 px de large, le tableau se resserre pour lui faire de la place ; plus étroit, le panneau passe par-dessus le tableau. Le pied, **Données terminal · HH:MM:SS**, donne l'heure de la dernière actualisation du calendrier.

### Filtres

Le champ **Rechercher un événement, un pays ou une devise...** cherche dans le nom de l'événement, le code de la devise et le nom du pays. Les flèches **‹ ›** de part et d'autre des dates passent à la semaine précédente ou suivante. Une semaine va du dimanche au samedi, heure de New York.

**PAYS** propose une pastille par devise majeure (USD, EUR, GBP, JPY, CHF, CAD, AUD et NZD), et un clic sur une pastille affiche ou masque la devise. **Autres...** ouvre un menu avec les autres devises de la semaine à cocher, comme CNY ou **ALL — événements mondiaux**. Quand certaines devises sont masquées, la pastille devient **Autres (n)**, n étant le nombre de devises encore affichées. Deepwater mémorise la sélection. **IMPACT** restreint enfin le tableau à **Tous**, **Fort**, **Moyen** ou **Faible**, et chaque segment indique son nombre d'événements.

### Tableau

| Colonne | Contenu |
|---|---|
| **HEURE** | Votre heure locale, ou **Journée** pour un jour férié |
| **DEVISE** | Drapeau et code de la devise |
| **ÉVÉNEMENT** | Le nom, précédé de points d'impact (3 = fort, 2 = moyen, 1 = faible) et suivi d'une étiquette de période |
| **RÉEL** | Le chiffre publié, avec une flèche qui indique s'il est en hausse ou en baisse par rapport au précédent |
| **PRÉVISION** | Le consensus |
| **PRÉCÉDENT** | La publication précédente |
| **SURPRISE** | Réel moins prévision, colorée selon le signe |
| **MIN** / **MAX** | La fourchette des économistes. Aucune source gratuite ne la publie : elle s'affiche **—** |

Les étiquettes de période sont **ANNUEL** (a/a), **MENSUEL** (m/m), **TRIMESTRIEL** (t/t) et **3 MOIS**. Quand le tableau se rétrécit, il abandonne d'abord **MIN** / **MAX**, puis **SURPRISE**. Leurs valeurs passent alors dans l'infobulle de la ligne.

Le survol d'une cellule en dit davantage. Sur **RÉEL**, l'infobulle indique d'où vient le résultat et quand il a été enregistré. Sur **PRÉVISION**, elle donne la source du consensus. Sur un nom tronqué, elle affiche le titre complet et l'impact. La molette fait défiler le tableau en douceur.

### Détail d'un événement

Un clic sur le nom d'un événement le déplie, un second clic le replie. La première partie explique ce que mesure l'indicateur, et comment un chiffre au-dessus ou en dessous de la prévision se lit d'ordinaire pour la devise. Vient ensuite l'historique, intitulé du nom du pays suivi de **· HISTORIQUE**. C'est un graphique des publications passées, avec la dernière valeur, la moyenne, le plus bas et le plus haut.

Le graphique s'affiche en **BARRES** ou en **LIGNE**, sur **5A**, **10A**, **20A** (par défaut) ou **MAX**. Survolez-le pour lire la valeur d'une période. Faites glisser la souris sur le graphique pour zoomer sur une plage, et double-cliquez pour réinitialiser le zoom.

Quand une série officielle existe, l'historique s'appuie dessus, daté par période de référence. Ces séries officielles sont les prix à la consommation, le chômage et le PIB de l'OCDE, les mesures de l'inflation sous-jacente de la Banque du Canada, et l'IPC, l'IPP et les emplois non agricoles du BLS américain. Pour les autres indicateurs, le graphique montre les publications que Deepwater a lui-même enregistrées.

### Panneau de fourchette

Cliquez sur une cellule de valeur, de **RÉEL** à **MAX**, pour ouvrir le panneau. Pour le fermer, cliquez de nouveau sur la même cellule, sur le ✕, ou appuyez sur Échap. Le panneau est conçu autour de la fourchette des économistes. **FOURCHETTE DES ÉCONOMISTES** dessine une échelle avec les repères du consensus, du précédent et du réel. La fourchette elle-même reste vide, faute de source gratuite. **PRÉVISIONS DES BANQUES** est dans le même cas : Deepwater ne collecte pour l'instant aucune prévision de banque pour les événements du calendrier. La section affiche donc **Aucune prévision de banque pour cette publication**.

Plus bas, les cases **CONSENSUS**, **FOURCHETTE**, **PRÉCÉDENT** et **RÉEL** reprennent les chiffres, avec une légende. **SOURCES** renvoie au calendrier ForexFactory et à l'origine du réel ; un clic sur un lien l'ouvre dans le navigateur. La molette fait défiler le panneau.

### D'où viennent les données

Les événements, l'impact, les prévisions et les chiffres précédents viennent du calendrier hebdomadaire de ForexFactory. Deepwater le recharge toutes les 30 minutes tant que la page est affichée. Les résultats sont enregistrés à leur sortie, à partir de trois sources :

- le flux RSS du calendrier Myfxbook, consulté au plus toutes les 5 minutes tant que Deepwater tourne ; la consultation est placée environ une minute après chaque publication d'impact moyen ou fort ;
- les titres chiffrés du fil (FinancialJuice, investingLive), retenus seulement si la devise, l'heure de publication, les mots de l'indicateur, l'unité et la prévision ou le précédent cités concordent ;
- les séries officielles (OCDE, BLS, Banque du Canada) pour les publications passées.

Le même calendrier fournit les publications de [Squawk](help:news), de [Global › Récap des sessions](help:global) et du fil de la vue d'ensemble.

ForexFactory ne publie que la semaine en cours et la suivante, et la semaine suivante apparaît pendant celle qui la précède. Une semaine antérieure n'existe que si Deepwater l'a enregistrée à l'époque ; sinon, le tableau affiche **Aucun enregistrement pour cette semaine**. Si ForexFactory est injoignable, vous lisez **Calendrier ForexFactory indisponible (…)**. Deepwater n'enregistre les résultats que lorsqu'il tourne. Un événement publié pendant que l'application était fermée peut donc rester à **—**, sauf si une série officielle le complète.

## Fil d'actualités

Le fil d'actualités est le versant visuel de l'information : des articles avec leur image, classés selon leur capacité à faire bouger les marchés. L'en-tête **FIL MARCHÉS** porte les pastilles des sources, l'état **DIRECT** et l'horloge, la pastille de langue et le nombre d'articles. Un bandeau de titres défile en dessous. La colonne principale s'ouvre sur **À LA UNE**, suivie de la recherche, des pastilles de thème et des cartes d'articles. La colonne **DERNIERS TITRES** se trouve à droite. Le pied, **Données terminal · HH:MM:SS**, donne l'heure de la dernière actualisation. Sur petit écran, tout tient dans une seule colonne qui défile : la une, les cinq premiers derniers titres, les filtres, puis les cartes.

### En-tête

Les pastilles **SOURCES** déterminent les flux que vous lisez. Un clic sur une pastille affiche ou masque la source, et une source masquée n'est pas téléchargée du tout. Les pastilles qui ne tiennent pas se replient dans un menu **+n**. Survolez une pastille pour voir son site et sa dernière erreur. L'état **DIRECT** devient **ANCIEN** quand aucun flux ne s'est actualisé depuis 20 minutes. **n ARTICLES** compte les articles qui correspondent aux filtres en cours.

La pastille de langue passe d'**EN** (l'original) à **FR**, une traduction française faite sur ce Mac par Apple Translation. Elle affiche **FR…** tant que des traductions sont en attente. Sa mention indique **TRADUCTION AUTO**, **ORIGINAL** ou **FR INDISPONIBLE**. Au départ, la pastille suit la langue de l'interface, puis elle retient votre choix ; survolez-la pour connaître l'état du traducteur.

### Bandeau, une et cartes

Le bandeau fait défiler de droite à gauche tous les articles des sources actives, avec leur ancienneté. Le survol le met en pause, et un clic sur un titre ouvre l'article. **À LA UNE** présente l'article récent le plus susceptible de faire bouger les marchés, de préférence avec une image. Les banques centrales et la macro pèsent le plus dans ce choix. Un clic ouvre l'article.

Pour resserrer les cartes, tapez dans **Rechercher un titre...**. La recherche porte sur les mots du titre, original ou traduit, et sur le nom de la source. Vous pouvez aussi cliquer sur une pastille de thème ; un second clic retire le filtre.

- **FORT IMPACT** : garde les articles importants et classe les 48 dernières heures par impact
- **BANQUE CENTRALE**, **INFLATION**, **EMPLOI**, **PIB**, **PMI**, **VENTES AU DÉTAIL**, **RÉSULTATS** : un thème chacune

Chaque carte montre l'image, le thème quand un filtre est actif, le titre, la source et l'ancienneté. Un clic sur la carte ouvre l'article. Le **+** à sa droite l'ajoute à la file de lecture. Les articles de finances personnelles et de style de vie sont écartés de la une et des listes.

La colonne **DERNIERS TITRES** retient les articles importants et à fort impact des 36 dernières heures. Les articles à fort impact viennent d'abord, puis les autres, du plus récent au plus ancien. Chacun est marqué **FORT IMPACT** ou **IMPORTANT**, avec sa source et son ancienneté. Un clic ouvre l'article. La molette fait défiler la colonne.

### Lecteur

Ouvrir un article remplace le fil par le lecteur et ajoute l'article à la file de lecture. La barre du haut commence par **RETOUR AU FIL**, suivi des pastilles de la **FILE**. Un clic sur une pastille ouvre son article, son ✕ la retire, et **+n** donne accès au reste de la file.

L'article affiche la source et l'heure, une pastille de langue qui bascule entre EN et FR, le titre et l'image. Le texte suit, tel que le flux le fournit : l'article complet ou un résumé. **LIRE SUR**, suivi du nom du site et de ↗, ouvre l'original dans le navigateur. Les liens du texte s'y ouvrent aussi. **Chercher dans cet article...** surligne un mot dans le texte.

À côté du texte, le panneau **LECTURE AUTOMATIQUE** résume l'article à l'aide de règles fixes, sans modèle de langage. Il donne les **POINTS CLÉS** et les banques et marchés **CITÉS**, avec les **COURS EN DIRECT** des marchés nommés. Son **TON** est hawkish / fort, dovish / faible ou neutre. Il indique aussi le temps de lecture et le nombre de mots.

### D'où viennent les données

| Source | Consultée toutes les | Par défaut |
|---|---|---|
| investingLive | 2 min | active |
| Yahoo Finance | 5 min | active |
| CNBC | 2 min | active |
| MarketWatch | 2 min | active |
| Banques centrales (Fed, BCE, Banque d'Angleterre) | 5 min | active |
| FinancialJuice | 2 min | masquée |
| Nasdaq | 10 min | masquée |
| Investing.com | 5 min | masquée |

Les flux sont consultés tant que la page est affichée, et en arrière-plan quand **Options › Données** garde les données fraîches (activé par défaut). Les articles sont gardés 4 jours, 900 au plus, et les images restent 7 jours en cache. Beaucoup de flux n'ont pas d'image : une vignette générée la remplace.

## Recherche bancaire

Les notes de recherche et les PDF des grandes banques, leurs sièges sur un globe et le registre des banques systémiques avec leur total de bilan. La colonne de gauche réunit les onglets **ÉTUDES**, **ACTUS**, **DONNÉES** et **DÉPÔTS**, la liste et un pied consacré aux sources. Le globe occupe le centre jusqu'à ce que vous ouvriez une note : la visionneuse PDF prend alors sa place. À droite se trouve le **REGISTRE — ÉTABLISSEMENTS SYSTÉMIQUES**, et le badge du haut compte les établissements et les régions. Sur petit écran, notes, globe et registre s'empilent dans une colonne qui défile, et une note ouverte les recouvre.

### Notes

**ÉTUDES** montre toutes les notes, **ACTUS** seulement les lectures rapides (flashs et opinions). Les notes sont groupées par jour (**AUJOURD'HUI**, **HIER**, puis les dates). L'en-tête indique combien de notes ont été publiées aujourd'hui et la taille de la bibliothèque. Chaque ligne donne le logo et le nom de la banque, la catégorie, l'ancienneté, le titre, et **PDF · OUVRIR DANS LE TERMINAL** ou **WEB · OUVRIR DANS LE TERMINAL**. Un clic ouvre la note.

Deux menus restreignent la liste. **TOUTES BANQUES** permet de choisir une banque ; celles marquées **(LIEN)** n'ont qu'un lien. **TOUTES PUBLICATIONS** permet de choisir un type : articles, flashs, opinions, rapports, dossiers ou prévisions. Si vous choisissez une banque que Deepwater ne lit pas, la liste affiche **AUCUNE NOTE DE …** avec le nom de la banque et la raison. Un lien **OUVRIR … ↗** mène à son site de recherche.

### Données et dépôts

L'onglet **DONNÉES** explique quelles banques Deepwater lit, et pourquoi. Chaque banque relève de l'un de trois statuts :

| Statut | Signification |
|---|---|
| **LU** | Notes en cache et prévisions extraites : les conditions de la banque autorisent la reprise avec attribution |
| **LIEN** | Les conditions interdisent la reproduction, seul un lien est proposé |
| **AUCUN** | Les conditions interdisent l'accès automatisé, les liens profonds ou la citation |

L'onglet précise ensuite l'origine des actifs du registre et des indicateurs G-SIB. **DÉPÔTS** liste les derniers dépôts SEC EDGAR des banques du registre, avec la date, le formulaire, la banque et la description. Chaque dépôt est un lien.

### Visionneuse de note

L'en-tête de la visionneuse donne la catégorie et la date, la banque et le titre. À côté, trois boutons : **⤓ PDF** enregistre une copie, **⧉ SOURCE** ouvre la note sur le site de la banque, et **✕** ferme la visionneuse, tout comme Échap.

La barre d'outils offre les commandes habituelles. **≡** affiche les vignettes des pages ; un clic sur une vignette mène à sa page. Viennent ensuite un champ du numéro de page avec le nombre de pages, le zoom **−** / **+**, l'ajustement à la largeur, la rotation, l'enregistrement et l'impression. Le menu **⋮** reprend l'enregistrement et l'impression. Il permet aussi d'ouvrir la note dans le lecteur PDF par défaut, de copier le lien du PDF, d'ouvrir la source et de voir la taille du document. ⌘ + molette (Ctrl + molette sous Windows) zoome de 25 % à 400 %, et la molette seule fait défiler. Une note sans PDF affiche un message qui vous renvoie vers **⧉ SOURCE**.

### Globe et registre

Le globe tourne avec une balise sur le siège de chaque banque. Survolez l'étiquette d'une balise pour voir la banque et la ville ; un clic sélectionne la banque. Chaque ligne du registre donne le rang, le logo, le nom, le total de bilan et le ticker. Au survol s'ajoutent la raison sociale, la ville, le statut G-SIB, et les actifs avec les dates du bilan et du dépôt. Un clic sur une ligne sélectionne la banque. Sur les fenêtres basses, la molette fait défiler le registre.

Sélectionner une banque, sur le globe ou dans le registre, filtre les notes sur cette banque. Si vous étiez sur **DONNÉES** ou **DÉPÔTS**, la colonne repasse sur **ÉTUDES**.

### D'où viennent les données

Les notes, les PDF et les tableaux de prévisions viennent d'ING THINK, par son flux RSS et ses listes d'articles. ING est la seule banque dont les conditions autorisent la reprise avec attribution. Deepwater relit les listes au plus une fois par heure, et les tableaux de prévisions toutes les 6 heures. Un PDF se télécharge à l'ouverture et reste en cache. Parmi les autres banques, 19 sont en lien seul et 9 sont exclues par leurs conditions ; l'onglet **DONNÉES** donne la raison de chacune.

Le total de bilan est le dernier fait *Assets* des dépôts XBRL SEC EDGAR de chaque banque, dans la devise du dépôt, gardé un jour en cache. Les indicateurs G-SIB suivent la liste du FSB de novembre 2024. Les prévisions d'ING alimentent aussi [Positionnement › Positionnement bancaire](help:positioning).

## Squawk

Le fil en temps réel : titres, publications économiques et variations de prix horodatés à la seconde, à côté de la télévision en direct et d'un calendrier compact. Le fil occupe la gauche, avec son en-tête, un bandeau des mouvements de prix, les onglets de filtre, la liste et un pied. À droite, **TV TERMINAL** surmonte le calendrier économique compact. Quand la chaîne n'a pas de direct, la télévision se réduit à une bande de message et le calendrier prend la hauteur libérée. Dans une fenêtre petite ou très basse, les blocs défilent comme une seule page.

### Fil

L'en-tête affiche **SQUAWK DIRECT**, une description, le bouton de traduction **EN** / **FR** et le nombre d'**ÉLÉMENTS**. En dessous, le bandeau fait défiler de droite à gauche les marchés qui dépassent en ce moment leur seuil de mouvement.

Les onglets **TOUT**, **INFOS** (les titres, par défaut), **MOVERS** et **PUBLICATIONS** affichent chacun leur nombre d'éléments, et Deepwater mémorise celui que vous choisissez. Chaque ligne donne l'heure à la seconde, l'ancienneté, le badge de la source et le titre. Survolez une ligne pour la source, la date et l'heure, et pour le texte anglais d'origine quand le titre est traduit. Un clic ouvre la source dans le navigateur. La molette fait défiler en douceur, et les nouveaux éléments qui arrivent en haut ne déplacent pas votre lecture.

**EN** / **FR** traduit automatiquement les titres en français sur ce Mac, jusqu'à la fermeture de l'application. Au départ, le bouton suit la langue de l'interface. Survolez-le pour savoir si la traduction est disponible. Le pied, **Données Terminal · HH:MM:SS**, donne l'heure de la dernière récupération, à côté d'un badge **DIRECT**.

### Contenu du fil

Le fil mêle trois sortes d'éléments. Les titres viennent de FinancialJuice (consulté chaque minute), d'investingLive (toutes les 2 minutes) et des communiqués de la Fed, de la BCE et de la Banque d'Angleterre (toutes les 10 minutes). Quand aucune page du fil n'est ouverte, ces consultations s'espacent, à 10 à 30 minutes. Les publications sont les événements du [Calendrier](help:news) dont le résultat est sorti.

Deepwater calcule les variations de prix à partir des cours Yahoo Finance, par rapport à la clôture précédente. Un élément s'ajoute chaque fois qu'un marché franchit son seuil, puis à chaque nouveau multiple de ce seuil :

| Classe | Seuil |
|---|---|
| Indices (hors VIX) | 1 % |
| Actions | 2 % |
| Matières premières | 2 % |
| Crypto | 3 % |
| Majeures FX | 0.5 % |

La liste couvre les dernières 24 heures, avec toujours au moins les 150 éléments les plus récents. Les titres sont archivés 70 jours pour [Global › Récap des sessions](help:global).

### TV Terminal

La télévision diffuse **Bloomberg Television**, **Yahoo Finance** ou **CNBC Television** : le direct YouTube de chaque chaîne dans le lecteur officiel, le son coupé au départ. Deepwater mémorise la chaîne choisie. **Ouvrir sur YouTube** ouvre la page du direct de la chaîne dans le navigateur.

Si une chaîne ne diffuse pas, la bande affiche **… n'a pas de direct sur YouTube en ce moment**, avec le nom de la chaîne ; essayez-en une autre. Le lecteur s'arrête quand vous quittez la page. Il a besoin de la vue web du système, c'est-à-dire de macOS pour l'instant.

### Calendrier compact

Le calendrier compact montre les mêmes événements que le [Calendrier](help:news), du dimanche au samedi. Une fois la semaine écoulée, il passe à la suivante, sauf si vous avez navigué vous-même.

Ses commandes reprennent celles de la page complète : une recherche, les flèches **‹ ›** pour changer de semaine, les pastilles des 8 devises majeures et les pastilles d'impact **Tous**, **Fort**, **Moyen** et **Faible**. **Plus...** propose **Toutes les devises**, **Les 8 majeures seulement**, ou d'autres devises à cocher. Deepwater mémorise les pays et l'impact choisis.

Chaque ligne donne l'heure, le drapeau et la devise, l'impact, l'événement, **RÉEL**, **PRÉVU** et **PRÉCÉDENT**. Les événements à venir affichent un compte à rebours à la seconde (HH:MM:SS, puis jours et heures au-delà de 100 heures), et l'en-tête du jour porte **AUJ.**. Un clic sur une ligne déplie son détail (impact, heure locale et fréquence) ; le survol donne l'événement complet et ses valeurs.
