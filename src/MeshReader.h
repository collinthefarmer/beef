#pragma once

// Reads a geometry's mesh out of the engine into MeshData: the skin
// partitions' vertex and index buffers (or the shape's own when it is not
// skinned), from the CPU copy the engine keeps when it has one, else read
// back from the GPU through the lab. Every pointer and count is checked;
// a mesh that cannot be read is a problem string, never a fault.

#include "Mesh.h"
#include "PCH.h"

#include <expected>
#include <memory>
#include <string>

namespace WornEnchantmentPBR
{
	[[nodiscard]] std::expected<std::shared_ptr<const MeshData>, std::string> ReadMesh(RE::BSGeometry* a_geometry);

	// A node's position in the geometry's bind-pose (skin) space: from the
	// skin data when the node is one of the geometry's bones, else the
	// node's current position relative to the actor's root. Empty when the
	// node is not found.
	[[nodiscard]] std::optional<Vec3> NodeBindPosition(RE::BSGeometry* a_geometry, RE::NiAVObject* a_root, std::string_view a_node);

	// A world position in the actor root's frame, which is bind-pose skin
	// space for a standing actor.
	[[nodiscard]] Vec3 ToRootSpace(RE::NiAVObject* a_root, const Vec3& a_world);
}
