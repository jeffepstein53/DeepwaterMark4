# Global

Le marché d'un coup d'œil : un bandeau de cours en direct, le fil en temps réel, l'appétit pour le risque, la force des devises, les banques centrales, le récap de chaque session et les quatre sessions FX à votre heure. Aucune page de cet onglet ne demande de clé ni de compte.

## Vue d'ensemble

La vue d'ensemble réunit tout le marché sur un seul écran. Le bandeau de cours en direct occupe le haut de la page, avec les horloges des sessions à droite. Dans une fenêtre large (1340 px et plus), les panneaux se répartissent en trois colonnes. Le Squawk est à gauche. Le sentiment de risque, la force des devises et les banques centrales sont au centre. La carte mondiale et le registre des actifs sont à droite. Dans une fenêtre plus étroite, la page passe à deux colonnes, et la carte et le registre se placent côte à côte sur une rangée en dessous, que vous atteignez en faisant défiler. Dans une très petite fenêtre, chaque panneau prend toute la largeur et la page défile.

Tant que sa source n'a pas répondu, un panneau affiche sobrement **CONNEXION…** ou **AUCUNE DONNÉE**. Deepwater n'invente aucun chiffre : une valeur absente s'affiche **—**.

### Bandeau de cours

Huit marchés défilent de droite à gauche : EURUSD, GBPUSD, USDJPY, XAUUSD, WTI, S&P 500, NASDAQ et DOW. Chacun affiche son dernier cours et sa variation depuis la clôture précédente. La variation est en vert à la hausse, en rouge à la baisse et en gris si le cours n'a pas bougé. Les cours viennent de Yahoo Finance et s'actualisent toutes les 20 secondes tant que la page est affichée.

À droite, les horloges **New York**, **Londres**, **Tokyo** et **Sydney** indiquent si chaque session est **Ouvert** ou **Fermé**. Elles suivent les mêmes horaires que [Heures de marché](help:global) et disparaissent quand la barre fait moins de 600 px.

### Squawk

Ce panneau est une version compacte du fil d'[Actualités › Squawk](help:news). Son en-tête porte l'étiquette **LIVE**, une description, le bouton **EN / FR** et le nombre d'**INFOS**. Juste en dessous, une ligne fait défiler tous les éléments à la suite (heure • texte, avec les variations de prix en vert ou en rouge).

Quatre onglets de filtre, chacun avec son nombre d'éléments, choisissent ce que montre la liste : **TOUT**, **DATA** (les titres, par défaut), **◇ MOVERS** (les variations de prix) et **PUBLICATIONS** (les statistiques économiques avec leur résultat). Deepwater mémorise l'onglet choisi. Chaque ligne donne l'heure locale, l'ancienneté (**il y a 12m**…), une icône du type d'élément et le texte. Un point désigne un titre, un losange une variation de prix et des barres une publication. Le survol met une ligne en évidence. La molette fait défiler la liste, puis la page une fois la liste en butée. Contrairement à la page Squawk, les lignes n'ouvrent pas leur source ici.

**EN / FR** affiche les titres traduits automatiquement en français sur ce Mac (Apple Translation), avec la mention **AUTO-TRADUIT**. Les variations de prix ne sont jamais traduites. Au départ, le bouton suit la langue de l'interface, puis il retient votre choix ; survolez-le pour connaître l'état du traducteur. Le pied, **Données du terminal · HH:MM:SS**, donne l'heure du titre le plus récent. Survolez-le pour la liste des flux, l'heure de la dernière récupération et les éventuelles erreurs.

### Sentiment de risque

La jauge indique si le marché recherche le risque ou le fuit. Elle va de 0 à 100. Sous 40, elle est en **RISK-OFF**, de 40 à 60 en **NEUTRE** et au-dessus de 60 en **RISK-ON**. La zone est écrite sous la valeur. Les pastilles **1S** et **1M** donnent la variation depuis une semaine et depuis un mois, avec l'ancienne valeur entre parenthèses, **(AVANT n)**. En dessous, **HISTORIQUE · 1 AN** trace les valeurs quotidiennes des 12 derniers mois, colorées par zone.

Le survol du titre explique la méthode. Le score est la moyenne des percentiles de sept composantes, chacune mesurée par rapport aux 252 séances précédentes. Les composantes sont calculées sur les clôtures quotidiennes de Yahoo Finance : Volatilité (VIX), Tendance actions (le S&P 500 face à sa moyenne 125 jours), Appétit crédit (HYG/LQD), AUD/JPY, Cuivre / Or, Actions vs valeur refuge et Momentum crypto. L'infobulle donne la valeur brute et le percentile de chaque composante, ainsi que l'heure de récupération des données.

La jauge s'actualise à l'ouverture de la page, puis toutes les 5 minutes tant que la page reste ouverte. La jauge complète et ses moteurs se trouvent dans [Positionnement › Sentiment de risque](help:positioning).

### Force des devises

Ce graphique montre quelles devises majeures gagnent du terrain sur les autres, parmi USD, EUR, GBP, JPY, CHF, CAD, AUD et NZD. Le titre est **FORCE DES DEVISES · NON PONDÉRÉE**. Chaque courbe est la moyenne non pondérée des variations en % d'une devise contre les sept autres, mesurées depuis la première barre à l'écran. Toutes les courbes partent donc de 0 %, et les huit valeurs s'additionnent à environ zéro.

Deepwater calcule lui-même le graphique à partir des bougies Yahoo Finance des 28 paires formées par les huit majeures. Le menu d'univers ne propose que ce choix : **FOREX · 8 majeures, 28 paires**. Pendant le chargement des paires, le graphique affiche **CHARGEMENT n/28 PAIRES…**. Si plus de 4 paires manquent, il affiche **n paires sur 28 indisponibles** plutôt qu'une moyenne faussée.

Les boutons d'unité de temps fixent la taille des barres et la profondeur d'historique chargée, et Deepwater mémorise votre choix. Entre deux rechargements, les cours en direct font évoluer la dernière barre. Les jours et les semaines suivent le calendrier de Londres.

| Bouton | Barres | Historique | Rechargé toutes les |
|---|---|---|---|
| **5m** | 5 min | 5 jours | 2 min |
| **15m** | 15 min | 1 mois | 5 min |
| **1H** (défaut) | 1 heure | 3 mois | 10 min |
| **4H** | barres 1 h groupées par 4 | 6 mois | 30 min |
| **J** | 1 jour | 2 ans | 30 min |
| **S** | 1 semaine | 10 ans | 60 min |

Au survol du graphique, un réticule apparaît, avec un point sur chaque courbe. L'heure de la barre s'affiche dans une pastille sous le graphique et dans le sous-titre, et la valeur sous le curseur s'affiche sur l'axe de droite. La molette zoome autour de la barre survolée et fait repartir chaque courbe de 0 % à la nouvelle première barre. Un double-clic réinitialise le zoom. Le ⓘ à côté du titre explique la méthode.

### Banques centrales

Le panneau compte huit lignes : Fed (USD), ECB (EUR), BoE (GBP), BoJ (JPY), RBA (AUD), RBNZ (NZD), BoC (CAD) et SNB (CHF). Chaque ligne donne le taux directeur sous **TAUX** et la date sous **PROCHAINE RÉUNION**. Viennent ensuite les probabilités de chaque décision possible à cette réunion, implicites dans les prix de marché, sous **BAISSE**, **MAINTIEN** et **HAUSSE**. L'issue la plus probable est en gras. Survolez le taux pour son émetteur et sa date, et les probabilités pour leur mode de calcul et l'heure de cotation.

Pour chaque banque, Deepwater utilise les prix gratuits qui existent :

| Banque | Probabilités tirées de |
|---|---|
| Fed | futures sur fed funds du CME (via Yahoo Finance) |
| BCE | futures €STR (via Yahoo Finance) |
| BoE | sa courbe OIS |
| BoJ | futures TONA de JPX |
| BNS | futures SARON d'Eurex |
| BoC | futures CORRA de la Bourse de Montréal |
| RBA, RBNZ | une image de probabilités FinancialJuice lue par OCR |

Sans prix gratuit, les probabilités s'affichent **—** et l'infobulle explique pourquoi. [Économie › Taux](help:economy) donne le détail réunion par réunion.

### Carte mondiale et registre des actifs

La carte est un globe en points qui fait un tour toutes les deux minutes. Les quatre centres des sessions FX y sont marqués dans la couleur de leur session. Vous ne pouvez pas interagir avec la carte, et elle s'arrête de tourner quand la page est masquée.

Le registre des actifs classe les pays par région, sous les onglets **★ G20**, **AMÉRIQUE**, **EUROPE**, **AFRIQUE**, **ASIE** et **OCÉANIE**. Deepwater mémorise l'onglet choisi, et son nombre de pays s'affiche en haut à droite. Chaque ligne montre le drapeau, le pays et un badge. Le badge désigne l'institution dont Deepwater utilise le taux directeur (**FED**, **ECB**, **BIS**…). Les lignes en évidence ont une série de taux directeur d'une banque centrale ou de la BRI. Les lignes atténuées n'ont que des données Banque mondiale. Survolez une ligne pour le nom complet de l'institution ou la note sur les données. La molette fait défiler la liste.

## Récap des sessions

Le bilan de chaque session de Tokyo, Londres et New York, ou de chaque semaine : ce qui a bougé, les statistiques publiées et ce qu'a dit le fil. À gauche, le **REGISTRE DES RÉCAPS** liste les fenêtres. Le centre montre la fiche de celle que vous sélectionnez. À droite, **PUBLICATIONS DE LA FENÊTRE** surmonte **FIL DE LA FENÊTRE**. Sous 1366 px de large, ces deux encadrés passent sous le registre. Sur petit écran, tout s'empile dans une colonne qui défile.

### Sessions et semaines

Le récap travaille sur des fenêtres fixes, du lundi au vendredi. Les horaires sont ceux de chaque place, à l'heure locale, heure d'été comprise. Une semaine va de l'ouverture de Tokyo le lundi à la clôture de New York le vendredi.

| Session | Horaires (heure locale) |
|---|---|
| Tokyo | 09:00–15:00 |
| Londres | 08:00–16:30 |
| New York | 08:30–16:00 |

Les onglets **SESSIONS** / **SEMAINES** du registre passent d'une liste à l'autre. La première contient les sessions des 62 derniers jours, groupées par jour. La seconde contient les 9 dernières semaines, groupées par mois. Dans les deux cas, la fenêtre la plus récente vient en premier. Chaque ligne donne le nom de la session (ou **SEMAINE n**), ses horaires à votre heure (ou les dates de la semaine) et le marché qui a le plus bougé, avec sa variation. La fenêtre la plus récente s'ouvre d'abord. Cliquez sur n'importe quelle ligne pour ouvrir sa fiche.

### Fiche de session

La fiche s'ouvre sur son titre, **FICHE DE SESSION · TKY / LDN / NYC / SEMAINE**, avec un drapeau et un nom. En dessous, une ligne donne la date et les horaires à votre heure, puis l'**Ampleur** (marchés en hausse ↑ / en baisse ↓). Elle indique aussi le nombre de publications et de titres, et une pastille **DIRECT** ou **CLÔTURÉE**.

Vient ensuite la **LECTURE AUTOMATIQUE** : un titre, un chapeau et un paragraphe par groupe de marchés. Des règles fixes la rédigent à partir des chiffres de la fiche, sans modèle de langage. Elle porte la mention **MIS À JOUR EN DIRECT** tant que la fenêtre est ouverte, et **CALCULÉ PAR DEEPWATER** une fois celle-ci close. Elle s'affiche d'abord dans la langue de l'interface. Ses boutons **EN** / **FR** changent sa langue jusqu'à la fermeture de l'application.

La **CHRONOLOGIE** trace un marché sur toute la fenêtre. Par défaut, c'est le plus gros mouvement. Cliquez sur l'une des six pastilles de mouvements au-dessus pour en tracer un autre. L'ouverture est en pointillé avec l'étiquette **OUV.**, et le plus haut et le plus bas de la fenêtre sont marqués à droite. Au survol, un réticule apparaît avec un encadré qui donne l'heure, le cours et la variation depuis l'ouverture.

Deux couloirs courent sous la courbe. Le couloir **PUBLICATIONS** place un losange à chaque heure de publication : doré pour un impact fort, gris pour un impact moyen. Un nombre apparaît quand plusieurs publications tombent à la même heure. Le couloir **FIL** dessine une barre par quart d'heure (par tranche de 2 heures pour une semaine). La barre est d'autant plus haute que les titres sont nombreux, et elle affiche leur nombre à partir de 2.

Les **MOUVEMENTS DE LA FENÊTRE** terminent la fiche avec 12 cartes en grille 4 × 3 : S&P 500, Or, EUR/USD, Bitcoin, Nasdaq 100, Argent, USD/JPY, Ethereum, Dow Jones, Pétrole WTI, EUR/GBP et Gaz naturel. Chaque carte donne la variation de l'ouverture à la clôture, une mini-courbe et les cours **O** / **C**. Cliquez sur une carte pour la tracer dans la chronologie.

### Publications et fil

**PUBLICATIONS DE LA FENÊTRE** liste les publications d'impact moyen et fort du calendrier économique. Chaque ligne donne l'heure, le drapeau, le losange d'impact et l'événement, puis **R** (réel), **P** (prévision) et **Pr** (précédent). Survolez une ligne pour le nom complet et les valeurs.

**FIL DE LA FENÊTRE** liste les titres archivés pendant la fenêtre. Quand un titre cite un marché suivi, il porte l'étiquette de ce marché (**WTI**, **GOLD**, **BTC**…). Survolez un titre pour sa source et son heure, ou cliquez dessus pour ouvrir la source dans le navigateur. Chaque colonne défile à la molette.

### D'où viennent les données

Les cours sont des barres 15 minutes de Yahoo Finance. Deepwater prend les futures sur indices, pour que Tokyo et Londres aient aussi des barres d'indices. Les fenêtres pas encore terminées sont rechargées toutes les 2 minutes, jusqu'à 20 minutes après leur clôture. Les publications viennent du calendrier économique d'[Actualités › Calendrier](help:news). Les titres viennent de l'archive du fil de Deepwater (FinancialJuice, investingLive, la Fed, la BCE et la Banque d'Angleterre), qui les conserve 70 jours.

Chaque source a ses limites. Yahoo ne conserve que 60 jours de barres 15 minutes. Une fenêtre plus ancienne, ou un jour férié, affiche donc **Aucune barre 15 minutes pour cette fenêtre**. L'archive du fil ne contient que ce que Deepwater a reçu pendant qu'il tournait avec une page du fil ouverte. Les fenêtres antérieures n'ont pas de titres, et l'encadré le signale. Les semaines du calendrier antérieures à l'installation ne sont pas disponibles non plus (voir [Actualités › Calendrier](help:news)).

## Heures de marché

Les quatre sessions FX en direct à votre heure, avec leurs chevauchements et un compte à rebours jusqu'à chaque ouverture et fermeture. Tout est calculé sur votre ordinateur à partir de la base des fuseaux horaires, et rien n'est téléchargé. La page ne comporte aucune commande : elle se lit.

### Sessions

| Session | Horaires (heure locale de la place) |
|---|---|
| Sydney | 07:00–16:00 |
| Tokyo | 09:00–18:00 |
| Londres | 08:00–17:00 |
| New York | 08:00–17:00 |

Les sessions vont du lundi au vendredi, heure d'été comprise. Ce sont les heures de bureau usuelles de chaque place, pas les heures d'ouverture des bourses. Les jours fériés ne sont pas pris en compte, et votre fuseau horaire est celui de l'ordinateur. Le badge en haut à droite résume la situation : **n/4 SESSIONS OUVERTES · VOTRE HEURE UTC±h**.

### Votre horloge

Sous le titre **VOTRE HORLOGE**, suivi de votre fuseau, une horloge à aiguilles tourne sur un cadran de 24 heures balayé par un radar. Les arcs colorés du cadran montrent la session en cours ou la prochaine session de chaque place, à votre heure. Les sessions ouvertes sont vives ; quand toutes sont fermées, c'est la prochaine à ouvrir qui l'est.

Sous le cadran figurent l'heure numérique, la date avec votre décalage UTC et une ligne d'état. La ligne d'état nomme la session ouverte (**SESSION … OUVERTE**) ou les deux places qui se chevauchent (**CHEVAUCHEMENT … · …**). Entre deux sessions, elle affiche **FX ENTRE DEUX SESSIONS**. Du vendredi 17:00 heure de New York à l'ouverture du lundi, elle affiche **WEEK-END · MARCHÉS FX FERMÉS**.

### Chronologie 24 h

La **CHRONOLOGIE 24 H · VOTRE HEURE** déroule votre journée de 00:00 à 24:00. Chaque session a sa ligne, avec les barres du jour et leurs horaires. Une session ouverte brille, et une ligne verticale marque l'instant présent. La ligne **CHEVAUCHEMENTS** en dessous montre les plages où deux places ou plus traitent ensemble, avec les places et les horaires (par exemple **LDN · NY**). Un jour sans chevauchement, elle affiche **AUCUN CHEVAUCHEMENT AUJOURD'HUI**.

### Cartes de session

Chaque session a sa carte : le nom, une pastille **OUVERT** / **FERMÉ** et l'heure locale de la place à la seconde. Un compte à rebours **FERME DANS** ou **OUVRE DANS** suit, avec une barre de progression pendant la session. La ligne du bas donne les horaires à l'heure locale de la place et la même session à votre heure.
