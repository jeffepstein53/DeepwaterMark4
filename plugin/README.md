# Plug-in Deepwater pour Claude

Ce dossier contient le plug-in « deepwater » : il laisse Claude (Claude Code, Claude Desktop) lire les données de
Deepwater Mark 4, en lecture seule. Il apporte :

- le serveur MCP `deepwater` (`.mcp.json`), lancé par `scripts/deepwater-mcp.cmd`, qui retrouve seul où Deepwater Mark 4 est installé ;
- un mode d'emploi pour Claude (`skills/deepwater/SKILL.md`).

Il faut que Deepwater Mark 4 soit installé et **ouvert**, avec **Options › CLAUDE** allumé.

## Installer dans Claude Code

```
claude plugin marketplace add <ce dossier>
claude plugin install deepwater@deepwater-local
```

## Installer dans Claude Desktop

Ajoutez le fichier `deepwater.plugin` depuis la page **Plug-ins** de Claude Desktop. Ce fichier est un zip de
`plugin/deepwater` (la fiche `.claude-plugin/plugin.json` à sa racine).

## Vérifier

```
claude plugin validate plugin/deepwater
claude plugin validate plugin
```
