// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/InterpreterReference.h"
#include "recipe/RecipeGraph.h"
#include "test_support.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
using Random = std::mt19937;

std::size_t Pick(Random &random, std::size_t count) {
  return std::uniform_int_distribution<std::size_t>{0, count - 1}(random);
}

std::string Scalar(Random &random, std::span<const std::string> leaves,
                   int depth) {
  if (depth <= 0 || Pick(random, 4) == 0) {
    if (Pick(random, 4) == 0)
      return std::to_string(static_cast<int>(Pick(random, 7)) - 3) + ".5";
    return leaves[Pick(random, leaves.size())];
  }
  const auto a = Scalar(random, leaves, depth - 1);
  const auto b = Scalar(random, leaves, depth - 1);
  switch (Pick(random, 16)) {
  case 0:
    return "(" + a + " + " + b + ")";
  case 1:
    return "(" + a + " - " + b + ")";
  case 2:
    return "(" + a + " * " + b + ")";
  case 3:
    return "(" + a + " / " + b + ")";
  case 4:
    return "min(" + a + ", " + b + ")";
  case 5:
    return "max(" + a + ", " + b + ")";
  case 6:
    return "clamp(" + a + ", 0, 1)";
  case 7:
    return "saturate(" + a + ")";
  case 8:
    return "abs(" + a + ")";
  case 9:
    return "frac(" + a + ")";
  case 10:
    return "sqrt(abs(" + a + "))";
  case 11:
    return "sin(" + a + ")";
  case 12:
    return "lerp(" + a + ", " + b + ", 0.25)";
  case 13:
    return "smoothstep(-1, 1, " + a + ")";
  case 14:
    return "step(" + a + ", " + b + ")";
  default:
    return "if(" + a + " > " + b + ", " + a + ", " + b + ")";
  }
}

std::string Producer(Random &random, int width) {
  const std::array<std::string, 3> leaves{"@s", "@a", "@b"};
  if (width == 1)
    return Scalar(random, leaves, 3);
  std::string vector = "[" + Scalar(random, leaves, 2);
  for (int i = 1; i < width; ++i)
    vector += ", " + Scalar(random, leaves, 2);
  return vector + "]";
}

std::string Consumer(Random &random, int width) {
  const std::array<std::string, 3> leaves{"@a", "@b", "@s"};
  const auto other = Scalar(random, leaves, 2);
  if (width == 1) {
    const std::array<std::string, 4> withProducer{"@p", "@a", "@s", "@p"};
    return "(" + Scalar(random, withProducer, 3) + ") + " + other;
  }
  switch (Pick(random, 3)) {
  case 0:
    return "dot(@p, @p) + " + other;
  case 1:
    return "length(@p) * " + other;
  default:
    return "@p * " + other;
  }
}

bool SameBits(Vec3 a, Vec3 b) {
  const auto same = [](float x, float y) {
    return (std::isnan(x) && std::isnan(y)) ||
           std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y);
  };
  return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}

struct Case {
  std::string producer;
  std::string consumer;
};
}

int main() {
  Random random{11};
  std::uniform_real_distribution<float> value{-2.0f, 2.0f};
  std::size_t evaluated = 0, differing = 0, compiled = 0, rejected = 0;
  for (int trial = 0; trial < 400; ++trial) {
    const int width = 1 + static_cast<int>(Pick(random, 3));
    const Case c{Producer(random, width), Consumer(random, width)};
    Recipe recipe;
    recipe.signals = {{"a", ConstantSignal{0.0f}}, {"b", ConstantSignal{0.0f}}};
    recipe.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
    recipe.masks = {{"p", c.producer}, {"c", c.consumer}};
    const auto graph = RecipeGraph::Compile(recipe);
    const auto producerNode = graph.FindNodeIndex("p");
    const auto consumerNode = graph.FindNodeIndex("c");
    if (!producerNode || !consumerNode)
      continue;
    const auto producer =
        InterpreterProgram::Compile(graph, {*producerNode, 0});
    const auto consumer =
        InterpreterProgram::Compile(graph, {*consumerNode, 0});
    if (!producer || !consumer) {
      ++rejected;
      continue;
    }
    std::optional<std::size_t> producerInput;
    for (std::size_t i = 0; i < consumer->Inputs().size(); ++i)
      if (const auto *texture =
              Get<InterpreterTextureInput>(consumer->Inputs()[i]);
          texture && texture->output.node == *producerNode)
        producerInput = i;
    if (!producerInput)
      continue;
    const auto fused =
        InterpreterProgram::Inline(*consumer, *producerInput, *producer);
    if (!fused) {
      ++rejected;
      continue;
    }
    ++compiled;
    for (int sample = 0; sample < 25; ++sample) {
      const auto random3 = [&] {
        const float v = value(random);
        return Vec3{v, v, v};
      };
      std::vector<Vec3> producerInputs;
      for (std::size_t i = 0; i < producer->Inputs().size(); ++i)
        producerInputs.push_back(random3());
      std::vector<Vec3> consumerInputs;
      for (std::size_t i = 0; i < consumer->Inputs().size(); ++i)
        consumerInputs.push_back(random3());
      auto stored = EvaluateInterpreter(*producer, {producerInputs, {}});
      if (producer->ResultType() == ValueType::kScalar)
        stored = {stored.x, stored.x, stored.x};
      stored = QuantizeUnorm8(stored);
      const auto &read = consumer->Instructions();
      const bool two = std::ranges::any_of(read, [&](const auto &i) {
        return i.opcode == InterpreterOpcode::kInput &&
               i.index == *producerInput && i.components == 2;
      });
      if (two)
        stored.z = 0.0f;
      consumerInputs[*producerInput] = stored;
      const auto unfused = EvaluateInterpreter(*consumer, {consumerInputs, {}});
      std::vector<Vec3> fusedInputs;
      for (std::size_t i = 0; i < consumerInputs.size(); ++i)
        if (i != *producerInput)
          fusedInputs.push_back(consumerInputs[i]);
      fusedInputs.insert(fusedInputs.end(), producerInputs.begin(),
                         producerInputs.end());
      const auto together = EvaluateInterpreter(*fused, {fusedInputs, {}});
      ++evaluated;
      if (!SameBits(unfused, together) && ++differing <= 5)
        std::printf("differs: p=%s c=%s\n", c.producer.c_str(),
                    c.consumer.c_str());
    }
  }
  std::printf("interpreter inline: %zu programs fused, %zu rejected, %zu "
              "evaluations\n",
              compiled, rejected, evaluated);
  Check(compiled > 200 && evaluated > 5000,
        "the generator fuses many random programs");
  test::Equal(differing, std::size_t{0},
              "an inlined program equals the consumer reading the "
              "producer's stored RGBA8 value, bit for bit");

  Recipe limited;
  limited.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
  limited.masks = {{"p", "@s * 2"}, {"c", "@p + @s"}};
  const auto graph = RecipeGraph::Compile(limited);
  const auto p =
      InterpreterProgram::Compile(graph, {*graph.FindNodeIndex("p"), 0});
  const auto c =
      InterpreterProgram::Compile(graph, {*graph.FindNodeIndex("c"), 0});
  Check(p && c, "the limit case compiles");
  if (p && c) {
    InterpreterLimits tight;
    tight.instructions = c->Instructions().size();
    Check(!InterpreterProgram::Inline(*c, 0, *p, tight).has_value(),
          "an inlined program over the instruction limit is refused");
    Check(!InterpreterProgram::Inline(*c, 1, *p).has_value() ||
              Is<InterpreterTextureInput>(c->Inputs()[1]),
          "only a texture input can be inlined");
    Check(!InterpreterProgram::Inline(*c, 9, *p).has_value(),
          "an out-of-range input is refused");
  }
  return test::Finish("interpreter inline");
}
