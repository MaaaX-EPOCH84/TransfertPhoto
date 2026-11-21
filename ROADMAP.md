# Feuille de route — TransfertPhoto

Statut : ⬜ à faire · 🔶 en cours · ✅ fait · ❌ annulé

| # | id | Étape | Contenu | État |
|---|---|---|---|---|
| 1 | E1 | Socle | wxWidgets statique, CI sur les trois systèmes, glibc 2.35 | ✅ |
| 2 | E2 | Inventaire | Parcours de la source, formats, fichiers accompagnants, fichiers ignorés | ⬜ |
| 3 | E3 | Dates | Exiv2, date des vidéos, repli nom puis modification, dates invraisemblables | ⬜ |
| 4 | E4 | Doublons exacts | Taille puis xxHash puis octet par octet, dans les trois situations | ⬜ |
| 5 | E5 | Transfert sûr | `AAAA/MM/JJ` ou à plat, copie vérifiée, déplacement, corbeille, journal | ⬜ |
| 6 | E6 | Fenêtre | Source, destination, options, grille de vignettes JPEG, dialogues, récapitulatif | ⬜ |
| 7 | E7 | Vignettes étendues | Vignette intégrée, RAW, HEIC, WebP, image vidéo par FFmpeg | ⬜ |
| 8 | E8 | Séries | Comparaison des voisines, seuil réglable, choix dans la série | ⬜ |
| 9 | E9 | Appareils | Source commune ; WPD, ImageCaptureCore, GVFS | ⬜ |
| 10 | E10 | Distribution | Releases GitHub, icône, numéro de version | ⬜ |

## Étape 1 — Socle  [E1]
1.1 ✅ [P1] Chaîne de construction — CMake 3.28 au minimum ; wxWidgets 3.2.12 par `FetchContent` depuis l'archive de release (empreinte SHA-256 figée, sources tierces incluses), lié en statique avec ses bibliothèques d'image embarquées ; MSVC avec runtime statique, Apple Clang en binaire universel, GCC. Écartés : Qt 6 (DLL, ou LGPL en statique), Dear ImGui (ni dialogues natifs ni sobriété au repos), vcpkg (outil de plus), paquets du système (dynamiques).
1.2 ✅ [P2] Squelette du code — bibliothèque `transfertphoto::core` sans interface graphique, fenêtre wxWidgets vide dans `src/app`, un test lancé par `ctest`. Identifiants en anglais, commentaires en français.
1.3 ✅ [P3] CI GitHub Actions sur Linux, Windows et macOS — dépôt public `MaaaX-EPOCH84/TransfertPhoto` ; chaque job construit, teste, affiche les dépendances dynamiques du binaire et publie l'exécutable en artefact. Parallélisme borné à 4 tâches (sans nombre, make épuisait la mémoire). Attestation : exécution 44957331806 verte sur les trois systèmes ; Windows sans DLL hors système, macOS `x86_64 arm64` sans dépendance hors système.
1.4 ✅ [P4] Exécutable Linux portable — construit dans un conteneur Ubuntu 22.04, la CI échoue au-delà de la glibc 2.35. Attestation : glibc 2.34 requise, aucune bibliothèque manquante sur le poste de MaaaX (glibc 2.39), lancé sous écran virtuel, fenêtre « TransfertPhoto 0.1.0 » ouverte. Écartés : `ubuntu-latest` (glibc 2.43), Debian 11 et plus ancien (C++20 incomplet).
1.5 ✅ [P5] Licence et langue du dépôt — GPL version 3 ou ultérieure, fichier `LICENSE` ; messages de commit en français, les deux premiers réécrits avec l'accord de MaaaX.
