#pragma once

#include <unordered_map>

namespace API
{
namespace Detail
{
	template <typename Map, typename Key, typename Value>
	bool TryGetOffset(const Map& offsets, const Key& name, Value& offset)
	{
		const auto it = offsets.find(name);
		if (it == offsets.end())
			return false;

		offset = it->second;
		return true;
	}
} // namespace Detail
} // namespace API
