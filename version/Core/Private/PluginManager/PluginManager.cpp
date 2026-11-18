#include "PluginManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <Logger/Logger.h>
#include <Tools.h>

#include "../Helpers.h"
#include "../IBaseApi.h"

namespace API
{
	namespace
	{
		namespace fs = std::filesystem;

		bool IsValidPluginName(const std::string& plugin_name)
		{
			if (plugin_name.empty() || plugin_name.size() > 128 || plugin_name == "." || plugin_name == "..")
			{
				return false;
			}

			return std::all_of(plugin_name.begin(), plugin_name.end(),
			                   [](unsigned char character)
			                   {
				                   return std::isalnum(character) || character == '_' || character == '-' || character == '.';
			                   });
		}

		fs::path GetPluginRootPath()
		{
			return fs::path(Tools::GetCurrentDir()) / game_api->GetApiName() / "Plugins";
		}

		fs::path ResolvePluginDirectory(const std::string& plugin_name)
		{
			if (!IsValidPluginName(plugin_name))
			{
				throw std::invalid_argument("Invalid plugin name");
			}

			const fs::path root = fs::weakly_canonical(GetPluginRootPath());
			if (!fs::exists(root) || !fs::is_directory(root))
			{
				throw std::runtime_error("Plugin directory does not exist");
			}

			const fs::path unresolved = root / plugin_name;
			const auto status = fs::symlink_status(unresolved);
			if (fs::is_symlink(status))
			{
				throw std::runtime_error("Plugin directories cannot be symbolic links");
			}
			if (!fs::is_directory(status))
			{
				throw std::runtime_error("Plugin directory does not exist");
			}

			const fs::path resolved = fs::weakly_canonical(unresolved);
			if (resolved.parent_path() != root)
			{
				throw std::runtime_error("Plugin directory escapes the configured plugin root");
			}

			return resolved;
		}

		fs::path ResolvePluginFilePath(const std::string& plugin_name, const std::string& filename, bool required)
		{
			const fs::path plugin_directory = ResolvePluginDirectory(plugin_name);
			const fs::path unresolved = plugin_directory / filename;
			const auto status = fs::symlink_status(unresolved);

			if (fs::is_symlink(status))
			{
				throw std::runtime_error("Plugin files cannot be symbolic links");
			}
			if (!fs::exists(status))
			{
				if (required)
				{
					throw std::runtime_error("Required plugin file does not exist: " + filename);
				}
				return unresolved;
			}
			if (!fs::is_regular_file(status))
			{
				throw std::runtime_error("Plugin path is not a regular file: " + filename);
			}

			const fs::path resolved = fs::weakly_canonical(unresolved);
			if (resolved.parent_path() != plugin_directory)
			{
				throw std::runtime_error("Plugin file escapes its plugin directory");
			}

			return resolved;
		}

		std::string JoinPluginNames(const std::vector<std::string>& names)
		{
			std::ostringstream result;
			for (size_t index = 0; index < names.size(); ++index)
			{
				if (index != 0)
				{
					result << ", ";
				}
				result << names[index];
			}
			return result.str();
		}
	} // namespace
	PluginManager& PluginManager::Get()
	{
		static PluginManager instance;
		return instance;
	}

	nlohmann::json PluginManager::GetAllPDBConfigs()
	{
		auto result = nlohmann::json::object();
		const auto plugin_root = GetPluginRootPath();

		if (!fs::exists(plugin_root) || !fs::is_directory(plugin_root))
		{
			return result;
		}

		for (const auto& entry : fs::directory_iterator(plugin_root))
		{
			try
			{
				const auto status = entry.symlink_status();
				if (fs::is_symlink(status) || !fs::is_directory(status))
				{
					continue;
				}

				const auto plugin_name = entry.path().filename().string();
				if (!IsValidPluginName(plugin_name))
				{
					Log::GetLog()->warn("Skipping plugin directory with an invalid name");
					continue;
				}

				const auto plugin_pdb_config = ReadPluginPDBConfig(plugin_name);
				MergePdbConfig(result, plugin_pdb_config);
			}
			catch (const std::exception& error)
			{
				Log::GetLog()->warn("({}) {}", __FUNCTION__, error.what());
			}
		}

		return result;
	}

	nlohmann::json PluginManager::ReadPluginPDBConfig(const std::string& plugin_name)
	{
		auto plugin_pdb_config = nlohmann::json::object();
		const auto config_path = ResolvePluginFilePath(plugin_name, "PdbConfig.json", false);

		if (!fs::exists(config_path))
		{
			return plugin_pdb_config;
		}

		std::ifstream file{config_path};
		if (!file.is_open())
		{
			throw std::runtime_error("Failed to open PdbConfig.json for plugin " + plugin_name);
		}

		file >> plugin_pdb_config;
		return plugin_pdb_config;
	}

	nlohmann::json PluginManager::ReadSettingsConfig()
	{
		nlohmann::json config = nlohmann::json::object({});

		const std::string config_path = Tools::GetCurrentDir() + "/config.json";
		std::ifstream file{config_path};
		if (!file.is_open())
		{
			return config;
		}

		file >> config;
		file.close();

		return config;
	}

	void PluginManager::LoadAllPlugins()
	{
		const auto plugin_root = GetPluginRootPath();
		if (!fs::exists(plugin_root) || !fs::is_directory(plugin_root))
		{
			Log::GetLog()->warn("Plugin directory does not exist: {}", plugin_root.string());
			return;
		}

		std::vector<std::string> pending_plugins;
		for (const auto& entry : fs::directory_iterator(plugin_root))
		{
			try
			{
				const auto status = entry.symlink_status();
				if (fs::is_symlink(status) || !fs::is_directory(status))
				{
					continue;
				}

				const auto plugin_name = entry.path().filename().string();
				if (!IsValidPluginName(plugin_name))
				{
					Log::GetLog()->warn("Skipping plugin directory with an invalid name");
					continue;
				}

				ResolvePluginDirectory(plugin_name);
				const auto update_path = ResolvePluginFilePath(plugin_name, plugin_name + ".dll.ArkApi", false);
				if (fs::exists(update_path))
				{
					const auto plugin_path = ResolvePluginFilePath(plugin_name, plugin_name + ".dll", false);
					fs::copy_file(update_path, plugin_path, fs::copy_options::overwrite_existing);
					fs::remove(update_path);
				}

				pending_plugins.push_back(plugin_name);
			}
			catch (const std::exception& error)
			{
				Log::GetLog()->warn("({}) {}", __FUNCTION__, error.what());
			}
		}

		std::sort(pending_plugins.begin(), pending_plugins.end());
		size_t loaded_count = 0;

		while (!pending_plugins.empty())
		{
			bool made_progress = false;

			for (auto plugin = pending_plugins.begin(); plugin != pending_plugins.end();)
			{
				try
				{
					const auto plugin_info = ReadPluginInfo(*plugin);
					const auto dependencies = plugin_info["Dependencies"].get<std::vector<std::string>>();
					const bool dependencies_loaded = std::all_of(
						dependencies.begin(), dependencies.end(),
						[this](const std::string& dependency)
						{
							return IsPluginLoaded(dependency);
						});

					if (!dependencies_loaded)
					{
						++plugin;
						continue;
					}

					const std::string plugin_name = *plugin;
					const auto& loaded_plugin = LoadPlugin(plugin_name);
					Log::GetLog()->info("Loaded plugin {} V{} ({})",
					                    loaded_plugin->full_name.empty() ? loaded_plugin->name : loaded_plugin->full_name,
				                    loaded_plugin->version, loaded_plugin->description);
					plugin = pending_plugins.erase(plugin);
					++loaded_count;
					made_progress = true;
				}
				catch (const std::exception& error)
				{
					Log::GetLog()->warn("Failed to load plugin {}: {}", *plugin, error.what());
					plugin = pending_plugins.erase(plugin);
					made_progress = true;
				}
			}

			if (!made_progress)
			{
				for (const auto& plugin_name : pending_plugins)
				{
					try
					{
						const auto plugin_info = ReadPluginInfo(plugin_name);
						const auto dependencies = plugin_info["Dependencies"].get<std::vector<std::string>>();
						std::vector<std::string> unresolved;
						for (const auto& dependency : dependencies)
						{
							if (!IsPluginLoaded(dependency))
							{
								unresolved.push_back(dependency);
							}
						}
						Log::GetLog()->error("Skipping plugin {} because dependencies are unresolved: {}",
						                    plugin_name, JoinPluginNames(unresolved));
					}
					catch (const std::exception& error)
					{
						Log::GetLog()->error("Skipping plugin {}: {}", plugin_name, error.what());
					}
				}
				break;
			}
		}

		// Set auto plugins reloading.
		auto settings = ReadSettingsConfig();
		enable_plugin_reload_ = settings["settings"].value("AutomaticPluginReloading", false);
		if (enable_plugin_reload_)
		{
			reload_sleep_seconds_ = settings["settings"].value("AutomaticPluginReloadSeconds", 5);
			save_world_before_reload_ = settings["settings"].value("SaveWorldBeforePluginReload", true);
		}

		Log::GetLog()->info("Finished loading plugins; {} loaded", loaded_count);
	}

	std::shared_ptr<Plugin>& PluginManager::LoadPlugin(const std::string& plugin_name) noexcept(false)
	{
		if (IsPluginLoaded(plugin_name))
		{
			throw std::runtime_error("Plugin " + plugin_name + " was already loaded");
		}

		const auto full_dll_path = ResolvePluginFilePath(plugin_name, plugin_name + ".dll", true);
		const auto plugin_info = ReadPluginInfo(plugin_name);
		const auto dependencies = plugin_info["Dependencies"].get<std::vector<std::string>>();

		std::vector<std::string> missing_dependencies;
		for (const auto& dependency : dependencies)
		{
			if (!IsPluginLoaded(dependency))
			{
				missing_dependencies.push_back(dependency);
			}
		}
		if (!missing_dependencies.empty())
		{
			throw std::runtime_error("Plugin " + plugin_name + " requires unloaded dependencies: " +
			                         JoinPluginNames(missing_dependencies));
		}

		const auto required_version = static_cast<float>(plugin_info["MinApiVersion"]);
		if (required_version != .0f && game_api->GetVersion() < required_version)
		{
			throw std::runtime_error("Plugin " + plugin_name + " requires newer API version!");
		}

		HINSTANCE h_module = LoadLibraryA(full_dll_path.string().c_str());
		if (h_module == nullptr)
		{
			throw std::runtime_error(
				"Failed to load plugin - " + plugin_name + "\nError code: " + std::to_string(GetLastError()));
		}

		try
		{
			// Plugin_Init is called after LoadLibrary returns, outside the loader lock.
			using pfnPluginInit = void(__fastcall*)();
			const auto pfn_init = reinterpret_cast<pfnPluginInit>(GetProcAddress(h_module, "Plugin_Init"));
			if (pfn_init != nullptr)
			{
				pfn_init();
			}

			auto plugin = std::make_shared<Plugin>(h_module, plugin_name, plugin_info["FullName"],
			                                      plugin_info["Description"], plugin_info["Version"],
			                                      plugin_info["MinApiVersion"], dependencies);
			loaded_plugins_.push_back(std::move(plugin));
			return loaded_plugins_.back();
		}
		catch (...)
		{
			FreeLibrary(h_module);
			throw;
		}
	}

	void PluginManager::UnloadPlugin(const std::string& plugin_name) noexcept(false)
	{
		const auto iter = FindPlugin(plugin_name);
		if (iter == loaded_plugins_.end())
		{
			throw std::runtime_error("Plugin " + plugin_name + " is not loaded");
		}

		ResolvePluginFilePath(plugin_name, plugin_name + ".dll", true);

		using pfnPluginUnload = void(__fastcall*)();
		const auto pfn_unload = reinterpret_cast<pfnPluginUnload>(GetProcAddress((*iter)->h_module, "Plugin_Unload"));
		if (pfn_unload != nullptr)
		{
			pfn_unload();
		}

		const BOOL result = FreeLibrary((*iter)->h_module);
		if (result == 0)
		{
			throw std::runtime_error(
				"Failed to unload plugin - " + plugin_name + "\nError code: " + std::to_string(GetLastError()));
		}

		loaded_plugins_.erase(std::remove(loaded_plugins_.begin(), loaded_plugins_.end(), *iter), loaded_plugins_.end());
	}

	nlohmann::json PluginManager::ReadPluginInfo(const std::string& plugin_name)
	{
		nlohmann::json plugin_info = nlohmann::json::object();
		const auto config_path = ResolvePluginFilePath(plugin_name, "PluginInfo.json", false);

		if (fs::exists(config_path))
		{
			std::ifstream file{config_path};
			if (!file.is_open())
			{
				throw std::runtime_error("Failed to open PluginInfo.json for plugin " + plugin_name);
			}
			file >> plugin_info;
		}

		if (!plugin_info.is_object())
		{
			throw std::runtime_error("PluginInfo.json must contain a JSON object for plugin " + plugin_name);
		}

		const auto dependencies = plugin_info.value("Dependencies", std::vector<std::string>{});
		for (const auto& dependency : dependencies)
		{
			if (!IsValidPluginName(dependency) || dependency == plugin_name)
			{
				throw std::runtime_error("Plugin " + plugin_name + " declares an invalid dependency name");
			}
		}

		nlohmann::json result = nlohmann::json::object();
		result["FullName"] = plugin_info.value("FullName", std::string{});
		result["Description"] = plugin_info.value("Description", std::string{"No description"});
		result["Version"] = plugin_info.value("Version", 1.00f);
		result["MinApiVersion"] = plugin_info.value("MinApiVersion", .0f);
		result["Dependencies"] = dependencies;
		return result;
	}

	

	std::vector<std::shared_ptr<Plugin>>::const_iterator PluginManager::FindPlugin(const std::string& plugin_name)
	{
		const auto iter = std::find_if(loaded_plugins_.begin(), loaded_plugins_.end(),
		                               [plugin_name](const std::shared_ptr<Plugin>& plugin) -> bool
		                               {
			                               return plugin->name == plugin_name;
		                               });

		return iter;
	}

	bool PluginManager::IsPluginLoaded(const std::string& plugin_name)
	{
		return FindPlugin(plugin_name) != loaded_plugins_.end();
	}

	void PluginManager::DetectPluginChangesTimerCallback()
	{
		auto& pluginManager = Get();

		const time_t now = time(nullptr);
		if (now < pluginManager.next_reload_check_
			|| !pluginManager.enable_plugin_reload_)
		{
			return;
		}

		pluginManager.next_reload_check_ = now + pluginManager.reload_sleep_seconds_;

		pluginManager.DetectPluginChanges();
	}

	void PluginManager::DetectPluginChanges()
	{
		const auto plugin_root = GetPluginRootPath();
		if (!fs::exists(plugin_root) || !fs::is_directory(plugin_root))
		{
			return;
		}

		bool save_world = save_world_before_reload_;
		for (const auto& entry : fs::directory_iterator(plugin_root))
		{
			try
			{
				const auto status = entry.symlink_status();
				if (fs::is_symlink(status) || !fs::is_directory(status))
				{
					continue;
				}

				const auto plugin_name = entry.path().filename().string();
				if (!IsValidPluginName(plugin_name))
				{
					continue;
				}

				ResolvePluginDirectory(plugin_name);
				const auto plugin_file_path = ResolvePluginFilePath(plugin_name, plugin_name + ".dll", false);
				const auto new_plugin_file_path = ResolvePluginFilePath(plugin_name, plugin_name + ".dll.ArkApi", false);
				if (!fs::exists(new_plugin_file_path) || FindPlugin(plugin_name) == loaded_plugins_.end())
				{
					continue;
				}

				if (save_world)
				{
					Log::GetLog()->info("Saving world before reloading plugins ...");
					auto* game_mode = ArkApi::GetApiUtils().GetShooterGameMode();
					if (game_mode == nullptr)
					{
						Log::GetLog()->error("Cannot reload plugins because the game mode is unavailable for the configured world save.");
						return;
					}

#ifdef ATLAS_GAME
					game_mode->SaveWorld(ESaveWorldType::Normal, false);
#else
					game_mode->SaveWorld();
#endif
					Log::GetLog()->info("World save requested.");
					save_world = false;
				}

				try
				{
					UnloadPlugin(plugin_name);
					fs::copy_file(new_plugin_file_path, plugin_file_path, fs::copy_options::overwrite_existing);
					fs::remove(new_plugin_file_path);
					LoadPlugin(plugin_name);
					Log::GetLog()->info("Reloaded plugin - {}", plugin_name);
				}
				catch (const std::exception& error)
				{
					Log::GetLog()->warn("({}) {}", __FUNCTION__, error.what());
				}
			}
			catch (const std::exception& error)
			{
				Log::GetLog()->warn("({}) {}", __FUNCTION__, error.what());
			}
		}
	}

	} // namespace API