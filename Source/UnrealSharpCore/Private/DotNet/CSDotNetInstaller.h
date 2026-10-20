#pragma once
#include "CoreMinimal.h"
#if WITH_EDITOR
namespace UnrealSharp::DotNetInstaller
{
	// Called synchronously during Loading Plugins. Never restarts the Editor.
	enum class EInstallSdkResult : uint8
	{
		Success,
		Cancelled,
		Failed
	};
	EInstallSdkResult InstallSdkDuringStartup();
}
#endif
