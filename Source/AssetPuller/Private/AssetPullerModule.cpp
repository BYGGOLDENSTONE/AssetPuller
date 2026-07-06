#include "AssetPullerCommands.h"
#include "AssetPullerStyle.h"
#include "AssetPullerTypes.h"
#include "Modules/ModuleManager.h"
#include "SAssetPullerWindow.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

DEFINE_LOG_CATEGORY(LogAssetPuller);

static const FName AssetPullerTabName("AssetPuller");

#define LOCTEXT_NAMESPACE "FAssetPullerModule"

class FAssetPullerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FAssetPullerStyle::Initialize();
		FAssetPullerStyle::ReloadTextures();
		FAssetPullerCommands::Register();

		PluginCommands = MakeShareable(new FUICommandList);
		PluginCommands->MapAction(
			FAssetPullerCommands::Get().OpenPluginWindow,
			FExecuteAction::CreateRaw(this, &FAssetPullerModule::PluginButtonClicked),
			FCanExecuteAction());

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAssetPullerModule::RegisterMenus));

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AssetPullerTabName,
			FOnSpawnTab::CreateRaw(this, &FAssetPullerModule::OnSpawnPluginTab))
			.SetDisplayName(LOCTEXT("TabTitle", "Asset Puller"))
			.SetTooltipText(LOCTEXT("TabTooltip", "Pull assets and their dependencies from the source asset library"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		FAssetPullerStyle::Shutdown();
		FAssetPullerCommands::Unregister();
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AssetPullerTabName);
	}

private:
	TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SAssetPullerWindow)
			];
	}

	void PluginButtonClicked()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(AssetPullerTabName);
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		{
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FAssetPullerCommands::Get().OpenPluginWindow, PluginCommands);
		}
		{
			UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FAssetPullerCommands::Get().OpenPluginWindow));
			Entry.SetCommandList(PluginCommands);
		}
	}

	TSharedPtr<FUICommandList> PluginCommands;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetPullerModule, AssetPuller)
