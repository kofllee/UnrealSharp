#pragma once
#include "CoreMinimal.h"
#if WITH_EDITOR
namespace UnrealSharp::DotNetInstaller
{
	// Called synchronously during Loading Plugins. Never restarts the Editor.
	bool InstallSdkDuringStartup();
}
#endif
