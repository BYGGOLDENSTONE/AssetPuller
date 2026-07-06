#pragma once

#include "CoreMinimal.h"
#include "AssetPullerTypes.h"

/** Executes a confirmed pull plan: copies files, refreshes the asset registry, returns a report. */
class FAssetPullExecutor
{
public:
	/**
	 * Copies every CopyNew item of the plan, preserving relative paths. Never overwrites.
	 * bShowProgressDialog: show a cancellable progress dialog (off in commandlet runs).
	 */
	static FPullReport Execute(const FPullPlan& Plan, bool bShowProgressDialog);

private:
	/** Makes freshly copied packages visible in the running editor (registry + Content Browser). */
	static void RefreshAssetRegistry(const FPullReport& Report);
};
