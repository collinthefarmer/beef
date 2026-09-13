#include "studio/SourcePlan.h"
#include "test_support.h"

#include <utility>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  const SourceKind roughness = MaterialSource{MaterialChannel::kRoughness};
  const SourceKind metallic = MaterialSource{MaterialChannel::kMetallic};
  const SourceCatalog original{{{"metal", metallic}}, {"metal", "rough"}};
  SourcePlanBuilder sources(original);
  const std::string rough = sources.ReuseOrAdd("rough", roughness);
  Check(rough != "rough" && rough != "metal",
        "a new source avoids names reserved by existing sources and masks");
  Check(sources.ReuseOrAdd("rough", roughness) == rough &&
            sources.ReuseOrAdd("anotherAlias", roughness) == rough,
        "repeated requests and aliases reuse the source staged earlier");
  Check(sources.ReuseOrAdd("anotherMetal", metallic) == "metal",
        "existing equivalent sources are reused without staging an edit");
  Check(original.sources.size() == 1 && original.reservedNames.size() == 2,
        "staging sources leaves the caller's naming input unchanged");
  const auto edits = std::move(sources).TakeEdits();
  const AddSource *added =
      edits.size() == 1 ? Get<AddSource>(edits.front()) : nullptr;
  Check(added && added->name == rough && added->kind == roughness,
        "all requests for the staged definition produce only one AddSource");

  SourcePlanBuilder distinct(SourceCatalog{});
  const std::string first = distinct.ReuseOrAdd("shared", roughness);
  const std::string second = distinct.ReuseOrAdd("shared", metallic);
  Check(first != second && distinct.ReuseOrAdd("alias", metallic) == second,
        "different definitions keep distinct names while their aliases reuse "
        "them");
  Check(std::move(distinct).TakeEdits().size() == 2,
        "different definitions each stage one source");

  SourcePlanBuilder named(
      SourceCatalog{{{"first", roughness}, {"preferred", roughness}},
                    {"first", "preferred"}});
  Check(named.ReuseOrAdd("preferred", roughness) == "preferred",
        "the requested existing name wins over an equivalent source under "
        "another name");
  Check(std::move(named).TakeEdits().empty(),
        "existing sources stage no additions");
  return test::Finish("studio_sourceplan");
}
