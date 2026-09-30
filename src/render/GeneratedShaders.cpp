// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/GeneratedShaders.h"

#include "Identity.h"
#include "planners/ProgramShader.h"
#include "render/D3DResult.h"

#include <REX/W32/D3DCOMPILER.h>

#include <system_error>
#include <thread>

namespace BetterEnchantmentEffects {
using namespace REX::W32;

extern const char *const kShaderSource;

namespace {
CompiledShader CompileGeneratedShader(ID3D11Device *a_device,
                                      const std::string &a_source,
                                      const std::string &a_entry) {
  const std::string sourceName{Identity::kName};
  ComPtr<ID3DBlob> out;
  ComPtr<ID3DBlob> errors;
  CompiledShader shader;
  if (!a_device ||
      Failed(D3DCompile(a_source.data(), a_source.size(), sourceName.c_str(),
                        nullptr, nullptr, a_entry.c_str(), "ps_5_0",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out.GetAddressOf(),
                        errors.GetAddressOf()))) {
    logger::error("GeneratedShaders: {} compile failed: {}", a_entry,
                  errors.Get()
                      ? static_cast<const char *>(errors->GetBufferPointer())
                      : "no message");
    return shader;
  }
  if (Failed(a_device->CreatePixelShader(out->GetBufferPointer(),
                                         out->GetBufferSize(), nullptr,
                                         shader.GetAddressOf())))
    logger::error("GeneratedShaders: {} shader creation failed", a_entry);
  return shader;
}

std::optional<GeneratedShader> StartCompile(ID3D11Device *a_device,
                                            const std::string &a_text,
                                            std::string_view a_entry,
                                            std::uint64_t a_use) {
  std::promise<CompiledShader> promise;
  auto compile = promise.get_future().share();
  try {
    std::thread([device = ComPtr<ID3D11Device>{a_device},
                 source = std::string{kShaderSource} + "\n" + a_text,
                 entry = std::string{a_entry},
                 promise = std::move(promise)]() mutable {
      CompiledShader shader;
      try {
        shader = CompileGeneratedShader(device.Get(), source, entry);
      } catch (const std::exception &error) {
        logger::error("GeneratedShaders: {} compile threw: {}", entry,
                      error.what());
      }
      promise.set_value(std::move(shader));
    }).detach();
  } catch (const std::system_error &error) {
    logger::error("GeneratedShaders: {} compile not started: {}", a_entry,
                  error.what());
    return std::nullopt;
  }
  return GeneratedShader{std::move(compile), a_use};
}

bool Compiled(const GeneratedShader &a_shader) {
  return a_shader.compile.wait_for(std::chrono::seconds{0}) ==
         std::future_status::ready;
}

ID3D11PixelShader *Ready(GeneratedShader &a_shader, std::uint64_t a_use) {
  a_shader.lastUse = a_use;
  if (!Compiled(a_shader))
    return nullptr;
  try {
    return a_shader.compile.get().Get();
  } catch (const std::future_error &) {
    return nullptr;
  }
}

template <class Cache> bool MakeRoom(Cache &a_cache, bool &a_logged) {
  if (a_cache.size() < GeneratedShaders::kMaxPerCache)
    return true;
  auto oldest = a_cache.end();
  for (auto entry = a_cache.begin(); entry != a_cache.end(); ++entry)
    if (Compiled(entry->second) &&
        (oldest == a_cache.end() ||
         entry->second.lastUse < oldest->second.lastUse))
      oldest = entry;
  if (oldest == a_cache.end())
    return false;
  if (!a_logged) {
    logger::info("GeneratedShaders: shader cache full; evicting the "
                 "least recently used");
    a_logged = true;
  }
  a_cache.erase(oldest);
  return true;
}
}

ID3D11PixelShader *GeneratedShaders::ProgramFor(ID3D11Device *a_device,
                                                const FieldProgram &a_program) {
  if (!a_device)
    return nullptr;
  const auto use = ++uses_;
  auto text = GenerateProgramShader(a_program);
  if (const auto found = programs_.find(text); found != programs_.end())
    return Ready(found->second, use);
  if (!MakeRoom(programs_, fullLogged_))
    return nullptr;
  if (auto compile = StartCompile(a_device, text, kGeneratedProgramEntry, use))
    programs_.emplace(std::move(text), std::move(*compile));
  return nullptr;
}

ID3D11PixelShader *GeneratedShaders::StackFor(ID3D11Device *a_device,
                                              const StackShape &a_shape) {
  if (!a_device)
    return nullptr;
  const auto use = ++uses_;
  if (const auto found = stacks_.find(a_shape); found != stacks_.end())
    return Ready(found->second, use);
  const auto text = GenerateStackShader(a_shape);
  if (!text || !MakeRoom(stacks_, fullLogged_))
    return nullptr;
  if (auto compile = StartCompile(a_device, *text, kGeneratedStackEntry, use))
    stacks_.emplace(a_shape, std::move(*compile));
  return nullptr;
}
}
