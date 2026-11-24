# Deepwater Mark 3

Deepwater Mark 3 est un terminal de marché pour Windows 64 bits. Il réunit dans une seule fenêtre :

- les nouvelles et le calendrier économique ;
- l'économie et les banques centrales ;
- le positionnement des acteurs (COT, sentiment) ;
- les marchés en direct et la liquidité ;
- le journal de trading et le calcul du risque ;
- les données macro et techniques que Deepwater enregistre lui-même.

Mark 3 est une version indépendante de Deepwater. Elle n'a ni l'onglet Graphiques ni l'onglet IA, et ne télécharge aucun modèle d'IA. Ses réglages et ses données sont rangés à part de ceux de Deepwater d'origine, si bien que les deux peuvent tourner sur le même PC sans se gêner.

## Installer

1. Ouvrez la page [Releases](https://github.com/jeffepstein53/DeepwaterMark3/releases) et téléchargez le dernier fichier `Deepwater-Mark3-X.Y.Z-win64.zip`.
2. Décompressez-le dans le dossier de votre choix.
3. Lancez `Deepwater.exe`, qui se trouve dans le dossier décompressé.

Rien d'autre n'est à installer : tout ce dont le programme a besoin est dans le dossier.

Le programme n'a pas de signature numérique, car celle-ci demande un certificat payant. Au premier lancement d'un fichier téléchargé, Windows peut donc afficher « Windows a protégé votre ordinateur ». Pour lancer quand même, cliquez sur « Informations complémentaires », puis sur « Exécuter quand même ».

## Mettre à jour

Deepwater Mark 3 cherche lui-même, à chaque lancement, les nouvelles versions publiées sur cette page. Quand il en trouve une, l'icône de téléchargement en haut à droite le signale et ouvre la page de la nouvelle version.

Pour l'installer :

1. Fermez Deepwater.
2. Décompressez le nouveau `.zip`.
3. Copiez le contenu de son dossier `Deepwater-Mark3` dans le dossier où se trouve votre `Deepwater.exe`, en remplaçant les fichiers.

Vos réglages et vos données ne sont pas dans ce dossier. Ils restent tels quels.

## Où sont vos données

- **Données** (calendriers enregistrés, notes des banques, données macro et techniques…) : `%APPDATA%\deepwater-terminal-corrige\Deepwater`
- **Réglages** : dans le registre de Windows, sous `HKEY_CURRENT_USER\Software\deepwater-terminal-corrige\Deepwater`

## Sources

Deepwater n'utilise que des sources publiques ou gratuites. La liste complète, avec ce que chacune fournit, se trouve dans l'onglet **Sources** du programme. Chaque source peut y être coupée.

Le calendrier économique vient du fichier gratuit de ForexFactory. Ce fichier ne couvre que la semaine en cours : la semaine suivante s'affiche dès qu'elle commence. En attendant, Deepwater conserve chaque semaine qu'il a déjà vue.

## Fabriquer le programme soi-même

Il faut :

- Qt 6.12, avec les modules `qthttpserver`, `qtwebsockets`, `qtpdf` et `qtwebview` ;
- Visual Studio 2022 (outils C++ x64) ;
- CMake et Ninja ;
- NSIS, seulement pour fabriquer l'installateur.

Lancez ces commandes dans une « x64 Native Tools Command Prompt » :

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\chemin\vers\Qt\6.12.0\msvc2022_64
cmake --build build
cmake --build build --target dist
```

Le programme prêt à copier, avec son `.zip` et son installateur, se trouve ensuite dans `build\dist`.

## Licence

GNU Affero General Public License 3.0 : voir [LICENSE](LICENSE). Le code source de chaque version publiée est sur ce dépôt.

Les calculs d'indicateurs (`src/charts`) viennent de Quant Terminal, sous licence MIT : voir [docs/charts/NOTICE.md](docs/charts/NOTICE.md). Les autres composants repris sont dans `third_party/`, chacun avec sa licence.
