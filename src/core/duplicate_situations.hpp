#pragma once

#include "core/duplicates.hpp"

#include <cstddef>
#include <vector>

namespace transfertphoto {

// Les trois situations de doublons, chacune avec ses propres choix (P24).
enum class DuplicateSituation {
    between,            // une copie au moins de chaque côté
    within_source,      // plusieurs copies dans la source, aucune en destination
    within_destination, // plusieurs copies dans la destination
};

struct GroupSituations {
    std::size_t source_copies = 0;
    std::size_t destination_copies = 0;

    // Les questions à poser pour ce groupe, dans cet ordre : entre source et
    // destination dès qu'il y a une copie de chaque côté, ce qui rend sans
    // objet la question dans la source ; dans la source sinon, s'il y en a
    // plusieurs ; dans la destination en plus, s'il y en a plusieurs.
    [[nodiscard]] std::vector<DuplicateSituation> questions() const;
    [[nodiscard]] bool has(DuplicateSituation situation) const;
};

[[nodiscard]] GroupSituations situations_of(const DuplicateGroup& group) noexcept;

} // namespace transfertphoto
