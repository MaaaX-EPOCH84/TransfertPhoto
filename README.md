# TransfertPhoto

Utilitaire de transfert de photos et de vidéos d'un répertoire, d'un appareil
photo ou d'un téléphone vers un répertoire, avec détection des doublons et
classement par année, mois et jour.

Projet en cours de démarrage : seule la chaîne de construction est en place.

## Construire

Prérequis : CMake 3.28 ou plus récent et un compilateur C++20. Sous Linux, les
fichiers de développement de GTK 3 (`libgtk-3-dev` sous Debian et Ubuntu).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

L'exécutable Linux publié par la CI est construit sous Ubuntu 22.04 et
demande la glibc 2.35 ou plus récente.

wxWidgets est téléchargé et compilé en statique à la première configuration,
ce qui prend plusieurs minutes.

## Licence

TransfertPhoto est un logiciel libre, distribué sous la licence publique
générale GNU, version 3 ou toute version ultérieure (GPL-3.0-or-later). Le
texte complet est dans le fichier `LICENSE`.
