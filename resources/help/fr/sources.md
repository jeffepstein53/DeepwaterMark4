# Sources

Cet onglet recense toutes les sources extérieures que lit Deepwater : si elles répondent, ce qu'elles fournissent, les pages qui s'en servent, leurs conditions et la clé ou le compte qu'elles demandent, avec un interrupteur pour les couper. Une seconde vue dessine d'où viennent les données et quels onglets les utilisent.

## Sources de données

La page s'ouvre sur **SOURCES DE DONNÉES**, suivi des totaux : sources, actives, en échec et désactivées. Le champ de recherche trouve une source par son éditeur, son identifiant, son serveur, ses données ou les pages qui l'utilisent, sans tenir compte de la casse. Les filtres voisins, chacun avec son nombre, vont de **TOUTES** à **CLÉ OU COMPTE**, pour les sources qui demandent une clé ou un compte, en passant par les sources actives, en échec, désactivées ou pas encore contactées.

### État

L'état d'une source vient des propres requêtes de Deepwater et se conserve d'un lancement à l'autre. Une source est **ACTIVE** quand elle a répondu, en **ÉCHEC** après deux échecs de suite, ou si elle n'a jamais répondu et a échoué au moins une fois. Une source que vous avez coupée ici est **DÉSACTIVÉE** : Deepwater ne la contacte plus. **NON CONTACTÉE** veut simplement dire qu'elle n'a pas encore été lue, puisqu'une source n'est lue qu'à l'ouverture d'une page qui l'utilise.

Sous chaque fiche figurent sa dernière réponse, son dernier échec avec l'erreur, et le nombre de réponses et d'échecs enregistrés. Les réponses tirées du cache ne sont pas des requêtes et ne changent rien.

### Fiche d'une source

Une fiche commence par l'éditeur, puis l'identifiant de la source, ses serveurs et son rythme, par exemple `1 requête / 2 s` : Deepwater n'interroge jamais un serveur plus vite. L'accès est **LIBRE**, **CLÉ API** ou **COMPTE**. Viennent ensuite ce que Deepwater en tire, **Utilisée par** pour les pages qui la lisent, et **Conditions** pour dire pourquoi et comment elle peut être utilisée.

L'interrupteur de droite coupe la source : une fois coupée, Deepwater refuse toute requête vers ses serveurs. Les sources qui partagent un serveur sont coupées aussi, et la fiche dit lesquelles. Une source à clé qui n'en a pas affiche **Aucune clé enregistrée : les données de cette source restent vides.**

### Clés et mots de passe

Une source prend une clé ici : e-Stat. Collez la clé et appuyez sur **ENREGISTRER LA CLÉ** (ou Entrée) ; **SUPPRIMER** l'efface, et la fiche affiche **● CLÉ ENREGISTRÉE** ou **○ AUCUNE CLÉ**. Une clé n'est gardée que sur cet ordinateur, n'est jamais réaffichée et n'est envoyée qu'à sa propre source. Elle sert dès le prochain rafraîchissement des pages qui la lisent.

Myfxbook, qui fournit le sentiment retail, demande plutôt votre e-mail et votre mot de passe. **CONNEXION** ouvre la session et **DÉCONNEXION** la ferme ; l'état affiche **● CONNECTÉ**, **… CONNEXION EN COURS**, **○ NON CONNECTÉ** ou **○ SESSION EXPIRÉE — RECONNECTEZ-VOUS**, ou bien l'erreur. Le mot de passe est envoyé une seule fois à Myfxbook en HTTPS et n'est jamais stocké ; seul le jeton de session est gardé, jusqu'à la déconnexion ou son expiration.

### Les sources

| Source | Ce qu'elle fournit | Clé |
|---|---|---|
| Yahoo Finance | Cours et bougies (forex, indices, futures, actions, crypto), recherche de symboles, titres RSS | Non |
| Binance spot (et son miroir public) | Bougies crypto, tickers, meilleur bid/ask, bougies en direct ; bougies de Technical data | Non |
| Binance futures USD-M | Bougies des perpétuels, bid/ask, open interest, ratio long/short, financement, liquidations en direct | Non |
| Archives publiques de Binance | Fichiers quotidiens de transactions (historique footprint), profondeur du carnet et open interest du perpétuel | Non |
| Dukascopy | Bougies forex (bid) depuis 2003 (Technical data) | Non |
| Forex Factory | Calendrier économique de la semaine | Non |
| Calendrier RSS de Myfxbook | Valeurs réelles des publications récentes | Non |
| Myfxbook community outlook | % long/short retail, positions, prix moyens | Compte |
| FinancialJuice, investingLive | Fils de titres | Non |
| Investing.com, CNBC, MarketWatch, Nasdaq | Actualités RSS (et images des articles) | Non |
| YouTube | Directs intégrés dans Squawk | Non |
| Federal Reserve Board | Calendrier du FOMC, communiqués, votes, communiqués de presse | Non |
| Fed de New York | Taux effectif des fed funds, fourchette cible | Non |
| Banque centrale européenne | Taux directeurs, €STR, courbe des taux, réunions, presse | Non |
| Banque d'Angleterre | Bank Rate, SONIA, rendements des gilts, dates et votes du MPC, actualités | Non |
| Banque du Canada | Taux du financement à un jour, CORRA, rendements, calendrier, inflation de base | Non |
| Banque nationale suisse | Taux directeur, SARON, calendrier | Non |
| Banque du Japon | Calendrier des réunions, taux au jour le jour (TONA) | Non |
| JPX, Eurex, Bourse de Montréal | Règlements des futures de taux (TONA, SARON, CORRA) | Non |
| Polymarket | Marchés sur les décisions des banques centrales (sonde seulement) | Non |
| BRI (BIS) | Taux directeurs, prix à la consommation | Non |
| OCDE | Croissance du PIB, chômage, confiance, taux ; historiques de publication | Non |
| Banque mondiale | PIB annuel, population, dette, réserves… | Non |
| Eurostat | Inflation de base de la zone euro | Non |
| e-Stat (Japon) | IPC de base japonais | Oui |
| Trésor américain | Courbe des rendements au pair, quotidienne | Non |
| Ministère des Finances du Japon | Rendements des JGB | Non |
| Bureau of Labor Statistics (US) | Emplois non agricoles | Non |
| CFTC | Commitments of Traders | Non |
| SEC EDGAR | Total de bilan des banques systémiques | Non |
| ING THINK | Notes de recherche et tableaux de prévisions | Non |
| Pages de recherche des banques | Liens seulement, rien de téléchargé | Non |
| GitHub (publications de Deepwater Mark 4) | Nouvelles versions de Deepwater : vérification et téléchargement des mises à jour | Non |
| ntfy.sh | Un message à chaque nouvelle version publiée, pour que l'icône de mise à jour s'allume aussitôt | Non |

## Sources graph

Cette vue répond d'un coup d'œil à la question « d'où cette page tire-t-elle ses données ? ». L'en-tête donne le nombre de sources, de sous-onglets et de liens tracés entre eux.

### Lire le diagramme

La colonne de gauche réunit toutes les sources, groupées sous **SURTOUT POUR** l'onglet qu'elles alimentent en premier, chacune avec une pastille d'état. En dessous, **LOCAL, HORS RÉSEAU** rassemble ce qui vient de cet ordinateur : les exports de votre courtier (MT4, MT5, cTrader) et l'horloge de la machine. Les différents flux d'une même institution partagent une seule boîte ; c'est le cas de la Réserve fédérale, de la BCE, de la Banque d'Angleterre, de la Banque du Canada et de Yahoo.

La colonne de droite liste les onglets principaux et leurs sous-onglets dans l'ordre de la navigation, une couleur par onglet. Une courbe relie une source à chaque sous-onglet qu'elle alimente. Le nombre porté par une source compte les sous-onglets qu'elle alimente, celui d'un sous-onglet les sources qu'il lit. La légende donne les couleurs des pastilles : **ACTIVE**, **ÉCHEC**, **EN ATTENTE**, **DÉSACTIVÉE** et **LOCAL**.

Certaines sources déclarent des pages absentes de la navigation ; ces pages sont listées sous le diagramme.

### L'explorer

Survolez une source, un sous-onglet ou un onglet, et ses liens s'allument. L'infobulle d'une source donne son éditeur, ses données, son accès, son état (flux par flux pour une boîte fusionnée), ses conditions et les pages qui l'utilisent ; sur un sous-onglet, elle liste les sources qu'il lit. Sur écran tactile, touchez une boîte pour l'allumer et voir son infobulle, puis touchez-la à nouveau, ou touchez le fond, pour effacer.

Pour trouver une source, tapez dans **Rechercher une source…**. Chaque mot doit figurer dans le nom, l'éditeur, l'identifiant ou les données d'une source ; la casse et les accents sont ignorés, et les mots français trouvent aussi leur forme anglaise : « banque » trouve « bank ». Les résultats s'allument, la vue défile jusqu'au premier, et Entrée passe au suivant ; leur nombre s'affiche à côté du champ.
