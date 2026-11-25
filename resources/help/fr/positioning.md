# Positionnement

L'onglet Positionnement montre qui détient quoi : comment les traders particuliers se répartissent entre long et short, ce que dit des opérateurs sur futures le rapport hebdomadaire Commitments of Traders de la CFTC, dans quel sens pointent les prévisions publiées par les banques, et quel risque les marchés sont prêts à prendre.

## Sentiment retail

Le Sentiment retail montre comment les traders particuliers sont placés sur chaque instrument, d'après le community outlook de Myfxbook. Il s'agit de repérer une foule déséquilibrée : les cartes de synthèse désignent les instruments les plus penchés d'un côté, le tableau les liste tous, et chaque instrument ouvre une fenêtre qui confronte la foule au prix.

### Connecter votre compte

L'outlook n'est accessible qu'avec votre propre compte Myfxbook. Vous pouvez vous connecter dans la page, sur la carte **CONNEXION MYFXBOOK** (e-mail, mot de passe, puis **CONNEXION**, ou Entrée dans le champ du mot de passe), ou dans l'onglet [Sources](help:sources). Une fois des données chargées, **FERMER** masque la carte. Les erreurs de connexion s'affichent dans la carte.

Votre mot de passe est envoyé une seule fois à Myfxbook, en HTTPS, pour ouvrir une session, et n'est jamais conservé. Seul le jeton de session est gardé ; il est effacé quand vous vous déconnectez ou quand il expire.

Le badge en haut à droite vous dit où vous en êtes. Connecté, il affiche **MYFXBOOK · PROCHAINE MAJ hh:mm · DÉCONNEXION**, et un clic vous déconnecte. Sinon, il affiche **MYFXBOOK NON CONNECTÉ · CONNEXION** ou **SESSION MYFXBOOK EXPIRÉE · CONNEXION**, et un clic ouvre la carte de connexion.

### Cartes de synthèse

Les cartes donnent le tableau d'ensemble avant de regarder chaque instrument ; survolez-en une pour sa définition. **TRÈS LONG** et **TRÈS SHORT** comptent les instruments où 65 % ou plus des positions retail sont longues (ou courtes), sur l'ensemble des instruments listés. **LE PLUS LONG** et **LE PLUS SHORT** désignent l'instrument à la plus forte part long (ou short), avec cette part.

**POSITIONS SUIVIES** est le nombre de positions ouvertes de l'outlook, tous instruments confondus. **BIAIS DE LA FOULE** est une barre calculée par Deepwater : la part des positions longues dans l'ensemble des positions, sur tous les instruments.

### Filtres et tri

Ces commandes restreignent et ordonnent le tableau :

- **Filtrer…** garde les symboles qui contiennent le texte saisi (la barre oblique est ignorée).
- **Tous**, **FX**, **Matières premières** et **Crypto** choisissent un groupe d'actifs.
- **Perso** ouvre un menu où cocher les instruments de votre liste, avec **Afficher seulement ma liste perso**.
- **Majeures d'abord**, **Plus extrêmes** (les plus éloignés de 50 %), **Plus actifs** (le plus de positions) et **A-Z** fixent l'ordre dans chaque groupe.

Le groupe, le tri et la liste perso sont mémorisés.

### Le tableau des instruments

Les instruments sont regroupés en FX, Matières premières, Crypto, Indices et Autres, chaque groupe avec son effectif. Chaque ligne donne le **SYMBOLE**, une barre **POSITIONNEMENT** partagée entre long et short, les parts **LONG** et **SHORT**, le **Δ LONG 24H**, le nombre de **POSITIONS** et un **STATUT**, qui lit la foule par seuils :

| Statut | Règle |
|---|---|
| TRÈS LONG | Long ≥ 65 % |
| PLUTÔT LONG | Long > 50 % |
| ÉQUILIBRÉ | 50 % |
| PLUTÔT SHORT | Short > 50 % |
| TRÈS SHORT | Short ≥ 65 % |

La colonne des 24 heures donne la variation, en points, de la part long depuis le relevé de l'app d'il y a environ 24 heures (à 3 heures près). Elle affiche **stable** si rien n'a bougé et **—** tant qu'aucun relevé de ce genre n'existe ; survolez-la pour les deux valeurs et l'heure du relevé. Un clic sur une ligne ouvre la fenêtre de l'instrument.

Le pied de page donne l'heure de la dernière réponse et un badge **DIFFÉRÉ**. Tant que l'historique de l'app couvre moins de 20 heures, il indique aussi combien de relevés ont déjà été enregistrés.

### Fenêtre de l'instrument

La fenêtre s'ouvre sur la paire, son nom complet et son statut ; ×, Échap ou un clic à l'extérieur la ferme. Trois pastilles confrontent la foule au marché : l'entrée moyenne des longs retail, l'entrée moyenne des shorts retail, et le dernier cours (la cotation en direct, sinon la dernière clôture 1 h).

Le graphique de prix utilise des bougies Yahoo Finance en différé : bougies 1 h pour **5J**, **2S** et **1M**, bougies quotidiennes pour **3M**, **6M** et **1A**. Survolez-le pour un réticule et les valeurs OHBC ; l'horloge en bas à droite donne votre heure locale et son décalage UTC.

**RÉPARTITION LONG / SHORT** est un camembert des deux parts. **STATISTIQUES DU POSITIONNEMENT RETAIL** liste les positions longues, courtes et totales, les prix moyens long et short, les parts des longs et des shorts, et les volumes long et short en lots. Ses quatre dernières lignes sont calculées par Deepwater et ne viennent pas des courtiers (survolez-les) : le **prix vs entrée** est le cours en direct moins l'entrée moyenne des longs, et l'entrée moyenne des shorts moins le cours, si bien qu'un chiffre positif signifie que ce camp gagne en moyenne ; le **P&L moyen estimé** donne la même chose en %.

**HISTORIQUE LONG / SHORT** trace la part long à chaque relevé enregistré, jusqu'aux 150 derniers. **OPEN INTEREST** montre l'open interest hebdomadaire CFTC du contrat future correspondant sur les 26 dernières semaines ; seules les paires contre l'USD, l'or, l'argent et le bitcoin en ont un, et pour les croisées la raison s'affiche. **VOLUME** et **POSITIONS** dessinent les longs vers le haut et les shorts vers le bas à chaque relevé, chaque côté rapporté à sa propre fourchette.

Sous les graphiques, un résumé écrit décrit l'état de la foule, la situation de chaque camp face à son entrée moyenne et ce qui a changé depuis le premier relevé de l'app, suivi des sources.

### D'où viennent les données

Myfxbook est interrogé au plus une fois toutes les 30 minutes, et seulement tant qu'une page qui affiche ces données est ouverte. Chaque réponse est enregistrée sur cet ordinateur, et les variations sur 24 heures comme les historiques sont construits à partir de ces relevés : ils partent vides et se remplissent tant que l'app tourne. Les 60 derniers jours sont chargés au lancement.

## COT

La page COT lit le rapport hebdomadaire Commitments of Traders de la CFTC, qui dit comment se placent les différentes catégories d'opérateurs sur futures, des couvreurs commerciaux aux fonds et aux petits porteurs. L'en-tête **COT avancé** porte la classe d'actifs (devises, matières premières ou indices), avec les boutons des marchés dessous et, à droite, la période utilisée par la répartition et les graphiques nets : **6M**, **1A**, **2A** ou **3A**.

La page se lit en deux rangées. **SYNTHÈSE** réunit le prix, la répartition hebdomadaire et l'anneau long:short ; **AVANCÉ** réunit les graphiques nets, les tableaux des rapports, les anneaux par secteur, les stats et la lecture automatique. La classe d'actifs, le marché, la période et la catégorie de la répartition sont mémorisés.

### Les marchés

| Classe | Marchés | Second rapport |
|---|---|---|
| Devises | USD (indice dollar ICE), EUR, GBP, JPY, CHF, CAD, AUD, NZD | Traders in Financial Futures (TFF) |
| Matières premières | Or, argent, cuivre, WTI, gaz naturel, maïs, blé, soja | Désagrégé |
| Indices | S&P 500, Nasdaq 100, Dow, Russell 2000, Nikkei, VIX, bitcoin | TFF |

Chaque marché a aussi le rapport Legacy, qui répartit les opérateurs entre Commerciaux, Grands spéculateurs et Petits porteurs.

### Prix

Le panneau de prix montre un an de bougies quotidiennes avec la dernière clôture et sa variation du jour ; survolez-le pour un réticule, ou survolez son titre pour la source du prix. Le bandeau du bas trace l'open interest sur cinq ans, avec sa dernière valeur et sa variation hebdomadaire.

Pour les devises, vous choisissez ce que vous regardez. **Non pondéré** affiche un indice calculé par Deepwater, moyenne géométrique de la devise face aux sept autres majeures, en base 100 au premier jour de l'année ; **Futures** affiche le future continu. Les autres marchés affichent toujours le future.

### Répartition long:short et positions nettes

La répartition hebdomadaire vous dit de quel côté penche une catégorie d'opérateurs. Ses boutons choisissent l'une des quatre catégories du second rapport (Dealers, Gérants d'actifs, Fonds à levier et Autres déclarants pour le TFF ; Producteurs/Négociants, Swap dealers, Gestion alternative et Autres déclarants pour le Désagrégé) ou **Non déclarables**. Chaque barre est une semaine et montre la part long de cette catégorie ; survolez-la pour la date et les pourcentages long et short. L'anneau **LONG:SHORT** donne la même catégorie dans le dernier rapport.

Les graphiques nets suivent long moins short sur la période choisie. **NET LEGACY** trace les Commerciaux, les Grands spéculateurs et les Petits porteurs ; **NET TRADERS IN FINANCIAL FUTURES** ou **NET DÉSAGRÉGÉ** trace les quatre catégories du second rapport ; **NET NON DÉCLARABLES (PETITS PORTEURS)** trace les petits porteurs.

### Tableaux, secteurs et stats

Les deux tableaux, **RAPPORT COT LEGACY** et **RAPPORT COT TFF / DÉSAGRÉGÉ**, donnent les chiffres derrière les graphiques. Leur ligne de titre indique l'open interest, sa variation hebdomadaire et le total des traders ; chaque catégorie a ensuite sa ligne :

| Colonne | Signification |
|---|---|
| Long, short | Positions détenues |
| VAR | Variation hebdomadaire |
| %OI | Part de l'open interest |
| TRADERS | Nombre de traders |
| Spreads | Positions en spread |
| L-S | Position nette |

**LONG:SHORT PAR SECTEUR** dessine un anneau par catégorie pour la dernière semaine. **STATS** donne pour chaque catégorie (long − short) / (long + short), en barre et en % : +100 % voudrait dire tout long, −100 % tout short.

### Lecture automatique

La lecture automatique met le rapport en mots. Elle est écrite par des règles fixes à partir des chiffres de la CFTC, sans IA, sous forme d'un titre, d'un paragraphe et de trois puces. Sa notion clé est l'extrême : une catégorie est « à l'extrême » quand sa position nette se situe dans les 10 % du haut ou du bas de sa fourchette sur 52 semaines.

Le titre nomme les catégories à l'extrême, ou à défaut le plus gros mouvement de la semaine. Le paragraphe traite des deux catégories les plus extrêmes (leur net, leur place dans la fourchette et leur mouvement de la semaine) et de la variation de l'open interest. Les puces donnent la catégorie la plus extrême, le plus gros mouvement de la semaine et la part long des fonds à levier ou de la gestion alternative.

La colonne de droite place la position nette de chaque catégorie sur un repère dans sa fourchette 52 semaines, et met en évidence les repères à l'extrême. **EN** / **FR** change la langue de la lecture seulement ; elle démarre dans la langue de l'interface et n'est pas mémorisée.

### D'où viennent les données

Les données viennent de l'API publique de la CFTC, futures seuls : environ cinq ans de rapports hebdomadaires par marché, gardés 6 heures. Dès qu'un rapport est attendu (vendredi 15 h 30, heure de New York) et manque, il est redemandé aussitôt, puis toutes les 10 minutes pendant trois heures et toutes les heures ensuite. Les positions sont celles du mardi, publiées le vendredi à 15 h 30, heure de New York. Le pied de page affiche l'heure de publication ; survolez **OFFICIEL** pour les dates du rapport et de publication. Les prix sont des bougies quotidiennes Yahoo Finance, et aucune clé n'est nécessaire.

Un nouveau rapport s'affiche tout seul quand la page est ouverte. Revenir sur la page recharge le marché quand ses données sont trop vieilles ou que le dernier chargement a échoué. Les messages au-dessus des panneaux signalent une erreur de la CFTC, l'absence de l'un des deux rapports (certains panneaux restent alors vides), ou **CFTC injoignable — rapport en cache affiché**.

## Positionnement bancaire

Le Positionnement bancaire transforme les prévisions publiées par les banques en vues de marché, pour voir de quel côté penchent les institutions sur chaque marché. L'en-tête porte **PRÉVISIONS FX DES BANQUES**, qui ouvre la fenêtre des prévisions, les deux vues **DESK** et **TOUTES LES VUES**, et un badge avec le nombre de notes bancaires archivées (dans [Actualités](help:news) › Recherche bancaire) et la date de dernière révision des tableaux de prévisions. Le desk affiche les cartes de marchés, puis les notes **MODE DE LECTURE** et **BANQUES** ; sur les très larges fenêtres, la liste complète des vues suit sous les cartes.

### Ce qu'est une vue

Une vue est calculée par Deepwater, elle ne vient pas d'une banque. Pour chaque banque, Deepwater prend sa prévision publiée pour la fin de trimestre la plus proche qui n'est pas encore passée, et la compare au cours du jour. Une prévision au-dessus du spot de plus que le seuil de la classe donne **LONG**, en dessous de plus que ce seuil **SHORT**, et entre les deux **RANGE**.

| Classe | Seuil |
|---|---|
| FX | 1 % |
| FX Asie & émergents | 1.5 % |
| Matières premières, indices | 3 % |
| Crypto | 5 % |
| Taux | 0.15 point de rendement |

Les taux se lisent comme des vues sur les obligations : un rendement prévu inférieur de plus de 0.15 point au rendement actuel donne LONG, puisque le prix des obligations monte quand les rendements baissent, et un rendement supérieur de plus de 0.15 point donne SHORT.

### Cartes de marchés

Il y a six cartes, une par classe : FX, FX Asie & émergents, Matières premières, Indices, Taux et Crypto. Chacune compte les vues LONG, SHORT et RANGE, les montre sur une barre de proportions long / range / short, et se termine par **positions · banques · marchés couverts**, les marchés couverts étant ceux qui ont au moins une vue, sur les marchés de la classe.

Au survol, la carte se soulève et son infobulle rappelle la règle et le seuil. Un clic ouvre **TOUTES LES VUES** sur cette classe.

### Toutes les vues

Toutes les vues est la liste complète, groupée par classe :

| Colonne | Signification |
|---|---|
| MARCHÉ | Le marché visé |
| BANQUE | La banque dont vient la prévision |
| VUE | LONG, SHORT ou RANGE |
| HORIZON | Trimestre de la prévision |
| PRÉVISION | La prévision de la banque |
| SPOT | Le cours du jour |
| JUSQU'À L'OBJECTIF | Variation en % jusqu'à la prévision, ou en points de rendement pour les taux |
| RÉVISION | Date de révision du tableau de prévisions |

Une classe sans vue affiche **— Aucune prévision des banques que Deepwater peut lire pour ces marchés**. La molette fait défiler la liste en douceur.

### Fenêtre des prévisions FX

Cette fenêtre confronte les prévisions FX des banques au prix. Des pastilles de paires s'alignent en haut, les mieux couvertes d'abord ; quand elles ne tiennent pas toutes, une pastille **+N** ouvre un menu avec les autres.

À gauche, un tableau donne chaque banque (**ÉTABLISSEMENT**, avec sa date de **RÉVISION**) sur six trimestres : celui en cours, les quatre suivants et le T4 dans deux ans. À droite, un an de bougies quotidiennes montre la paire avec son cours en direct, et une ligne pointillée par trimestre marque la moyenne des prévisions des banques ; les trimestres de même valeur partagent une ligne.

Cliquez sur une banque pour que les lignes montrent ses propres prévisions, et recliquez pour revenir aux moyennes. Les pastilles de trimestre affichent ou masquent la ligne de chaque trimestre, et **× FERMER** ou Échap ferme la fenêtre.

### D'où viennent les données

Les tableaux de prévisions sont lus chez ING THINK, qui couvre le FX, les matières premières et les bons du Trésor américain et permet la réutilisation avec attribution. TD, MUFG, KBC, Scotiabank et Commerzbank ne sont pas lus, car leurs conditions interdisent la réutilisation ou l'accès automatisé.

Les prévisions sont redemandées à l'ouverture de la page, au plus toutes les 6 heures ; si ING THINK est injoignable, la raison s'ajoute à la note **BANQUES**. Les cours spot viennent des cotations en direct de l'app et mettent les vues à jour tant que la page est affichée.

## Sentiment de risque

Le Sentiment de risque mesure le risque que les marchés sont prêts à prendre, sous la forme d'un score unique que Deepwater calcule sur sept marchés, présenté avec son historique sur un an à côté du S&P 500 et du VIX.

### Jauge composite

L'**APPÉTIT POUR LE RISQUE COMPOSITE** va de 0 à 100 : une valeur basse signifie que les marchés cherchent la sécurité, une valeur haute qu'ils vont chercher du risque.

| Zone | Score |
|---|---|
| EXTRÊME OFF | Moins de 20 |
| RISK-OFF | 20–39 |
| NEUTRE | 40–59 |
| RISK-ON | 60–79 |
| EXTRÊME ON | 80 et plus |

L'aiguille indique la valeur du jour, et deux repères sur l'échelle, **1S** et **1M**, montrent où elle se trouvait il y a 5 et 21 séances. Les pastilles donnent la variation sur une semaine et sur un mois, en points, avec la valeur d'alors (**ÉTAIT n**).

### Composantes

La lecture du jour de chaque composante est classée face à ses propres 252 séances précédentes, en percentile de 0 à 100, orienté pour que plus haut signifie toujours plus d'appétit pour le risque. Un percentile de 90 veut dire que la lecture est plus favorable au risque que lors de 90 % de ces séances. Le composite est la moyenne simple des composantes disponibles ce jour-là.

| Composante | Lecture |
|---|---|
| Volatilité (VIX) | Niveau du VIX, inversé (volatilité plus basse = plus d'appétit) |
| Tendance actions | S&P 500 face à sa moyenne 125 jours, en % |
| Appétit crédit | ETF obligataires high yield / investment grade (HYG/LQD), variation 20 jours du ratio |
| AUD/JPY | Variation 20 jours |
| Cuivre / Or | Variation 20 jours du ratio |
| Actions vs valeur refuge | Performance 20 jours du S&P 500 moins celle de l'or |
| Momentum crypto | Variation 20 jours du bitcoin |

Chaque ligne affiche la lecture brute, le percentile et une barre, et le survol d'une ligne la met en évidence. Une composante dont la dernière lecture a plus de 10 jours est écartée et affiche **—**.

### Graphiques

**HISTORIQUE COMPOSITE · 1 AN** trace le composite sur les séances du S&P 500, sur fond de zones : risk-off sous 40, neutre de 40 à 60, risk-on au-dessus de 60. Survolez-le pour la date et la valeur. **S&P 500 · 1 AN** et **VIX · INDICE DE VOLATILITÉ · 1 AN** montrent les clôtures quotidiennes avec la dernière valeur et sa variation sur un an ; le graphique du VIX marque 20 (élevé) et 30 (stress).

### D'où viennent les données

Toutes les lectures sont des clôtures quotidiennes Yahoo Finance : VIX, S&P 500, HYG, LQD, AUD/JPY, futures cuivre et or, et bitcoin. Elles sont redemandées toutes les 5 minutes tant que la page est affichée ; le pied de page donne l'heure de mise à jour. Si Yahoo Finance est injoignable, les panneaux affichent **Clôtures quotidiennes Yahoo Finance indisponibles**. Aucune clé n'est nécessaire.
