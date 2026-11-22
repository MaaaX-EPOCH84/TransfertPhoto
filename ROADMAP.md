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

## Étape 2 — Inventaire  [E2]
2.1 ✅ [P6] Cadre de test — Catch2 v3.16.1 (dernière stable, relevée avec `gh`) par `FetchContent` sur l'étiquette, copie superficielle ; le test existant y est migré, un test CTest par cas Catch2 (`catch_discover_tests`). Option `TRANSFERTPHOTO_BUILD_APP` pour tester le cœur sans GTK 3 ni wxWidgets ; git ajouté au conteneur Ubuntu 22.04 de la CI. Écartés : l'archive GitHub générée (empreinte non garantie stable), les fichiers amalgamés des releases (non vérifié qu'ils fournissent un `main`). Attestation : sur le poste de MaaaX, 1 test sur 1 passe sans avertissement, et avec une valeur attendue fausse il échoue puis repasse une fois rétablie ; CI verte sur les trois systèmes à `3a8ab38` (exécution 44995518303, 1 test sur 1 sur chacun). Clos avant la CI à la demande de MaaaX, qui a montré deux défauts propres à Windows et macOS, corrigés après coup : Catch2 compilé en C++14 par défaut (échec de liens sur `std::string_view`, corrigé en le compilant en C++20) et noms de tests accentués déformés par la page de codes de Windows (tests introuvables, corrigé par des noms sans accents).
2.2 ⬜ [P7] Interface de source et source « dossier » — interface commune : lister les fichiers avec taille et date, lire leurs premiers octets ; elle prévoit qu'une source soit absente puis apparaisse (téléphone débranché), sans l'implémenter. Implémentation dossier par `std::filesystem` : liens symboliques de dossiers non suivis, dossiers illisibles notés sans arrêt, interruption possible.
2.3 ⬜ [P8] Reconnaissance par extension — table des formats décidés, 17 extensions photo (`jpg`, `jpeg`, `png`, `heic`, `heif`, `tif`, `tiff`, `webp`, `gif`, `dng`, `cr2`, `cr3`, `nef`, `arw`, `raf`, `orf`, `rw2`) et 8 vidéo (`mp4`, `mov`, `m4v`, `avi`, `mkv`, `mts`, `m2ts`, `3gp`), insensible à la casse.
2.4 ⬜ [P9] Confirmation par signature — premiers octets ; verdict concorde, désaccord ou illisible. Attrape les `._*` de macOS sur exFAT.
2.5 ⬜ [P10] Fichiers accompagnants — rattachement par le nom : `.xmp` (deux conventions), `.aae` d'iPhone y compris retouche, `.mov` de photo animée, `.thm` et `.lrv` GoPro, `.srt` DJI. Un accompagnant orphelin n'est pas transféré, il est compté et signalé (validé par MaaaX).
2.6 ⬜ [P11] Fichiers exclus et ignorés — fichiers et dossiers cachés exclus et comptés à part (noms en point, attribut caché sous Windows : corbeille Android `.trashed-…`, `.thumbnails`, dossiers système des cartes, `._*`) ; tout autre fichier ignoré et compté (validé par MaaaX).
2.7 ⬜ [P12] Résultat de l'inventaire — photos et vidéos avec leurs accompagnants, ignorés, exclus, anomalies ; progression remontée pour la fenêtre ; testé sur une arborescence fabriquée (carte d'appareil, carte de téléphone, dossier en désordre).
