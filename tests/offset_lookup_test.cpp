#include "../version/Core/Private/OffsetLookup.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <unordered_map>

int main()
{
	std::unordered_map<std::string, std::intptr_t> offsets{
		{"AShooterPlayerController.GiveItem", 0x1234}
	};

	std::intptr_t offset = -1;
	assert(API::Detail::TryGetOffset(offsets, std::string("AShooterPlayerController.GiveItem"), offset));
	assert(offset == 0x1234);
	assert(offsets.size() == 1);

	const auto original_offset = offset;
	assert(!API::Detail::TryGetOffset(offsets, std::string("AShooterPlayerController.Missing"), offset));
	assert(offset == original_offset);
	assert(offsets.size() == 1);
	assert(offsets.find("AShooterPlayerController.Missing") == offsets.end());

	return 0;
}
