#include "Hooks.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include <Logger/Logger.h>

#include "../../MinHook.h"
#include "Offsets.h"
#include "IBaseApi.h"

namespace API
{
	Hooks::Hooks()
	{
		const MH_STATUS status = MH_Initialize();
		if (status != MH_OK)
		{
			Log::GetLog()->critical("Can't initialize MinHook ({})", MH_StatusToString(status));
			throw std::runtime_error("Failed to initialize MinHook");
		}
	}

	bool Hooks::SetHookInternal(const std::string& func_name, LPVOID detour, LPVOID* original)
	{
		LPVOID target = Offsets::Get().GetAddress(func_name);
		if (target == nullptr)
		{
			Log::GetLog()->error("{} does not exist", func_name);
			return false;
		}

		auto& hook_vector = all_hooks_[func_name];
		if (!hook_vector.empty() && !hook_vector.back()->enabled)
		{
			Log::GetLog()->critical(
				"Cannot add hook for {} because the previous hook is in an indeterminate state",
				func_name);
			return false;
		}

		const LPVOID new_target = hook_vector.empty()
			? target
			: hook_vector.back()->detour;

		// Allocate bookkeeping before changing executable code so allocation
		// failure cannot leave an untracked detour installed.
		hook_vector.reserve(hook_vector.size() + 1);
		auto hook = std::make_shared<Hook>(new_target, detour, original);

		LPVOID trampoline = nullptr;
		const MH_STATUS create_status = MH_CreateHook(new_target, detour, &trampoline);
		if (create_status != MH_OK)
		{
			Log::GetLog()->error(
				"Failed to create hook for {} ({})",
				func_name, MH_StatusToString(create_status));
			return false;
		}

		// Publish the trampoline before enabling can allow a detour to run.
		if (original != nullptr)
		{
			*original = trampoline;
		}

		const MH_STATUS enable_status = MH_EnableHook(new_target);
		if (enable_status != MH_OK)
		{
			const MH_STATUS remove_status = MH_RemoveHook(new_target);
			if (remove_status == MH_OK)
			{
				if (original != nullptr)
				{
					*original = nullptr;
				}
				Log::GetLog()->error(
					"Failed to enable hook for {} ({}); the created hook was rolled back",
					func_name, MH_StatusToString(enable_status));
			}
			else
			{
				hook->enabled = false;
				hook_vector.push_back(std::move(hook));
				Log::GetLog()->critical(
					"Failed to enable hook for {} ({}) and rollback failed ({}); hook bookkeeping was retained",
					func_name, MH_StatusToString(enable_status), MH_StatusToString(remove_status));
			}
			return false;
		}

		hook->enabled = true;
		hook_vector.push_back(std::move(hook));
		return true;
	}

	bool Hooks::DisableHook(const std::string& func_name, LPVOID detour)
	{
		const LPVOID target = Offsets::Get().GetAddress(func_name);
		if (target == nullptr)
		{
			Log::GetLog()->error("{} does not exist", func_name);
			return false;
		}

		auto& hook_vector = all_hooks_[func_name];
		const auto iter = std::find_if(
			hook_vector.begin(), hook_vector.end(),
			[detour](const std::shared_ptr<Hook>& hook) -> bool
			{
				return hook->detour == detour;
			});

		if (iter == hook_vector.end())
		{
			Log::GetLog()->warn("Failed to find hook ({})", func_name);
			return false;
		}

		const auto requested_hook = *iter;
		std::vector<std::shared_ptr<Hook>> removed_hooks;
		removed_hooks.reserve(hook_vector.size());

		// Remove from the end of the chain first so no remaining detour can
		// call through a trampoline that has already been removed.
		while (!hook_vector.empty())
		{
			const auto hook = hook_vector.back();
			const MH_STATUS remove_status = MH_RemoveHook(hook->target);
			if (remove_status != MH_OK)
			{
				Log::GetLog()->error(
					"Failed to remove hook for {} ({}); attempting to restore the removed suffix",
					func_name, MH_StatusToString(remove_status));

				bool restored = true;
				try
				{
					for (auto restore = removed_hooks.rbegin(); restore != removed_hooks.rend(); ++restore)
					{
						if (!SetHookInternal(func_name, (*restore)->detour, (*restore)->original))
						{
							restored = false;
							break;
						}
					}
				}
				catch (...)
				{
					restored = false;
				}

				if (!restored)
				{
					Log::GetLog()->critical(
						"Hook chain for {} could not be fully restored after a removal failure; restart the server before changing this hook chain again",
						func_name);
				}
				return false;
			}

			if (hook->original != nullptr)
			{
				*hook->original = nullptr;
			}
			removed_hooks.push_back(hook);
			hook_vector.pop_back();
			if (hook == requested_hook)
			{
				break;
			}
		}

		// The preceding hooks remain installed. Rebuild only the removed
		// suffix that must survive, in its original order.
		bool requested_hook_removed = false;
		try
		{
			for (auto restore = removed_hooks.rbegin(); restore != removed_hooks.rend(); ++restore)
			{
				if (*restore == requested_hook)
				{
					requested_hook_removed = true;
					continue;
				}

				if (!SetHookInternal(func_name, (*restore)->detour, (*restore)->original))
				{
					Log::GetLog()->critical(
						"Hook chain for {} was only partially rebuilt after removing a hook; restart the server before changing this hook chain again",
						func_name);
					return false;
				}
			}
		}
		catch (...)
		{
			Log::GetLog()->critical(
				"Hook chain for {} could not be fully rebuilt after removing a hook; restart the server before changing this hook chain again",
				func_name);
			return false;
		}

		return requested_hook_removed;
	}
} // namespace API

// Free function
ArkApi::IHooks& ArkApi::GetHooks()
{
	return reinterpret_cast<IHooks&>(*API::game_api->GetHooks());
}
