#include "ApiUtils.h"

#include "../IBaseApi.h"

namespace AtlasApi
{
	// UWorld

	void ApiUtils::SetWorld(UWorld* uworld)
	{
		u_world_ = uworld;
	}

	UWorld* ApiUtils::GetWorld() const
	{
		return u_world_;
	}

	// AShooterGameMode

	void ApiUtils::SetShooterGameMode(AShooterGameMode* shooter_game_mode)
	{
		shooter_game_mode_ = shooter_game_mode;
	}

	AShooterGameMode* ApiUtils::GetShooterGameMode() const
	{
		return shooter_game_mode_;
	}

	// Status

	void ApiUtils::SetStatus(ArkApi::ServerStatus status)
	{
		status_ = status;
	}

	ArkApi::ServerStatus ApiUtils::GetStatus() const
	{
		return status_;
	}

	void ApiUtils::SetCheatManager(UShooterCheatManager* cheatmanager)
	{
		cheatmanager_ = cheatmanager;
	}

	UShooterCheatManager* ApiUtils::GetCheatManager() const
	{
		return cheatmanager_;
	}

	void ApiUtils::SetPlayerController(AShooterPlayerController* player_controller)
	{
		if (!player_controller)
			return;

		const uint64 steam_id = ArkApi::IApiUtils::GetSteamIdFromController(player_controller);

		if (steam_id != 0)
		{
			steam_id_map_[steam_id] = TWeakObjectPtr<AShooterPlayerController>(player_controller);
		}
	}

	void ApiUtils::RemovePlayerController(AShooterPlayerController* player_controller)
	{
		if (!player_controller)
			return;

		// Do not depend on Steam ID lookup during logout: the network ID may already
		// be unavailable. Remove every entry that points to this controller.
		for (auto iter = steam_id_map_.begin(); iter != steam_id_map_.end();)
		{
			if (iter->second.Get() == player_controller)
			{
				iter = steam_id_map_.erase(iter);
			}
			else
			{
				++iter;
			}
		}
	}

	AShooterPlayerController* ApiUtils::FindPlayerFromSteamId_Internal(uint64 steam_id) const
	{
		AShooterPlayerController* found_player = nullptr;

		if (steam_id == 0)
			return found_player;

		auto iter = steam_id_map_.find(steam_id);

		if (iter != steam_id_map_.end())
		{
			found_player = iter->second.Get();
		}

		return found_player;
	}

	// Free function
	ArkApi::IApiUtils& GetApiUtils()
	{
		return *API::game_api->GetApiUtils();
	}
} // namespace AtlasApi