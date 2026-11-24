# Deepwater Mark 4

Deepwater Mark 4 est un terminal de marché pour Windows 64 bits. Il réunit dans une seule fenêtre :

- les nouvelles et le calendrier économique ;
- l'économie et les banques centrales ;
- le positionnement des acteurs (COT, sentiment) ;
- les marchés en direct et la liquidité ;
- le journal de trading et le calcul du risque ;
- les données macro et techniques que Deepwater enregistre lui-même.

Mark 4 est une version indépendante de Deepwater. Elle n'a ni l'onglet Graphiques ni l'onglet IA, et ne télécharge aucun modèle d'IA. Ses réglages et ses données sont rangés à part de ceux de Deepwater d'origine, si bien que les deux peuvent tourner sur le même PC sans se gêner.

## Installer

1. Ouvrez la page [Releases](https://github.com/jeffepstein53/DeepwaterMark4/releases) et téléchargez le dernier fichier `Deepwater-Mark4-X.Y.Z-win64.exe`.
2. Double-cliquez dessus. Une petite fenêtre montre l'avancement, puis Deepwater s'ouvre. Aucun mot de passe administrateur n'est demandé.

L'installateur place le programme dans `%LOCALAPPDATA%\Programs\Deepwater Mark 4`, ajoute l'icône « Deepwater Mark 4 » au menu Démarrer et une ligne dans « Applications installées », d'où on peut le désinstaller. Désinstaller ne touche ni à vos réglages ni à vos données.

Le programme n'a pas de signature numérique, car celle-ci demande un certificat payant. Au premier lancement d'un fichier téléchargé, Windows peut donc afficher « Windows a protégé votre ordinateur ». Pour lancer quand même, cliquez sur « Informations complémentaires », puis sur « Exécuter quand même ».

Sur la page d'une version, GitHub ajoute lui-même « Source code (zip) » et « Source code (tar.gz) » : ils contiennent le code du programme, pas le programme. À ignorer pour l'installer.

## Mettre à jour

Deepwater Mark 4 cherche lui-même, à chaque lancement, les nouvelles versions publiées sur cette page. Quand il en trouve une, l'icône de téléchargement en haut à droite le signale. Un clic sur « Installer et redémarrer » télécharge le nouvel installateur, l'exécute sans rien demander, puis rouvre Deepwater.

Vos réglages et vos données ne sont pas dans le dossier du programme. Ils restent tels quels.

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
cmake --build build --target installer
```

L'installateur `Deepwater-Mark4-X.Y.Z-win64.exe` se trouve ensuite dans `build\dist` (`--target dist` fabrique en plus un `.zip` du programme).

## Licence

GNU Affero General Public License 3.0 : voir [LICENSE](LICENSE). Le code source de chaque version publiée est sur ce dépôt.

Les calculs d'indicateurs (`src/charts`) viennent de Quant Terminal, sous licence MIT : voir [docs/charts/NOTICE.md](docs/charts/NOTICE.md). Les autres composants repris sont dans `third_party/`, chacun avec sa licence.
