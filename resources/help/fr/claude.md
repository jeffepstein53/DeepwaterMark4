# Claude

Deepwater peut laisser Claude lire ce que montrent ses pages : vous pouvez donc interroger Claude sur les marchés, le calendrier ou les banques centrales pendant que Deepwater est ouvert. C'est en lecture seule : Claude peut regarder, jamais agir. Ce chapitre n'est pas un onglet de l'en-tête : on y arrive par le **?**.

## L'activer

### Dans les options

Ouvrez la roue dentée, puis la section **CLAUDE**. **Laisser Claude lire les données de Deepwater** est éteint tant que vous ne l'allumez pas. Ensuite, **AJOUTER À CLAUDE CODE** ou **AJOUTER À CLAUDE DESKTOP** déclare Deepwater à ce programme, et une note sous les boutons dit quoi faire : redémarrer le programme, et garder Deepwater ouvert. Les boutons deviennent **RETIRER DE CLAUDE CODE** et **RETIRER DE CLAUDE DESKTOP** une fois l'ajout fait.

Pour Claude Desktop, Deepwater ajoute son entrée dans chaque exemplaire du fichier de configuration de Claude qu'il trouve sur l'ordinateur, et en garde d'abord une copie à côté, sous le même nom terminé par `.deepwater-backup`. Un fichier qu'il ne sait pas lire est laissé tel quel, avec un message. Vos autres serveurs et réglages restent comme ils sont. Quittez ensuite complètement Claude Desktop et rouvrez-le ; Deepwater ne peut pas savoir de l'extérieur lequel de ces fichiers une version donnée de Claude Desktop lit, donc si Deepwater n'y figure pas, utilisez le plug-in ci-dessous.

**Inclure mon journal de trading** ajoute un outil de plus, pour vos résultats. Il est éteint par défaut.

### Deepwater doit être ouvert

Claude atteint Deepwater par le programme qui tourne. Quand Deepwater est fermé, ou que l'interrupteur est éteint, Claude reçoit un message qui le dit. Deepwater ne s'ouvre jamais tout seul parce que Claude le demande.

### Le plug-in

Le dépôt du projet contient aussi un plug-in Claude prêt à l'emploi, dans le dossier `plugin`. Il retrouve seul le Deepwater installé et donne à Claude un court mode d'emploi des outils. C'est une autre porte d'entrée, pour Claude Code et pour la page **Plug-ins** de Claude Desktop, qui se passe des boutons.

## Ce que Claude peut lire

### Les outils

Claude dispose de 25 outils de lecture : contrôle des données (chaque chiffre clé comparé à une deuxième source, sources qui ne répondent plus), cours, historique des prix, calendrier économique, actualités, fil direct (Squawk), données macro, chaînes et faits macro, banques centrales, COT, sentiment des particuliers, indice de risque, prévisions et notes des banques, heures de marché, lectures techniques, poches de liquidité, saisonnalité, scores fondamentaux, et les données enregistrées de l'onglet Data : couverture technique, données techniques, empreinte (footprint), carnet d'ordres et intérêt ouvert. Avec l'interrupteur du journal, il y en a un 26ᵉ, pour le journal de trading.

### À quel point les réponses sont fraîches

Un outil à qui l'on demande des données plus vieilles que ce que sa source permet redemande la source avant de répondre, et chaque réponse dit l'âge de ses données : quand les cours ont été récupérés, il y a combien de temps date la dernière actualité, quand les chiffres du calendrier ont été téléchargés pour la dernière fois, quand les probabilités des banques centrales ont été calculées. Si une source ne répond pas, l'outil le dit et donne l'âge de ce qu'il a, au lieu de présenter de vieux chiffres comme frais.

## Ce qu'il ne fait jamais

### Les garde-fous

- **Aucun ordre, jamais.** Chaque outil lit seulement : aucun ne peut passer, modifier ni annuler une opération.
- **Aucune clé, aucun mot de passe.** Vos clés d'API et la session Myfxbook ne quittent jamais Deepwater.
- **Cet ordinateur seulement.** La connexion reste sur `127.0.0.1`, avec une clé secrète neuve à chaque lancement. Une page web, ou un programme qui n'a pas la clé, est refusé.
- **30 appels par minute au plus**, et un journal des appels : l'outil, ses arguments, s'il a marché et la taille de la réponse, jamais les données. Il est dans le dossier `mcp` de vos données.
- **Le texte des tiers est marqué comme donnée.** Les titres d'actualité et autres arrivent chez Claude dans un bloc marqué, avec un rappel que ce ne sont pas des instructions.

### Ce qui sort de votre ordinateur

Ce que Claude lit est envoyé à Anthropic, dans le cadre de votre conversation avec Claude. Éteignez l'option quand vous ne le souhaitez pas.
