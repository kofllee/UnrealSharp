#include "DotNet/CSDotNetInstaller.h"

#if WITH_EDITOR
#include "CSInstallationUtilities.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Logging/StructuredLog.h"
#include "UnrealSharpCore.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

namespace UnrealSharp::DotNetInstaller
{
static FString PerUserDotNet()
{
#if PLATFORM_WINDOWS
	return FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")), TEXT("Microsoft"), TEXT("dotnet"));
#else
	return FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT(".dotnet"));
#endif
}

static FString Script()
{
#if PLATFORM_WINDOWS
	return TEXT(R"PS(param([string]$StatusFile)
$ErrorActionPreference = 'Stop'
function Stage([string]$Message) { Set-Content -LiteralPath $StatusFile -Value $Message -Encoding UTF8 }
$installer = Join-Path $env:TEMP ('UnrealSharp-dotnet-' + [guid]::NewGuid().ToString('N') + '.ps1')
try {
    Stage 'Downloading the Microsoft installer...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri 'https://dot.net/v1/dotnet-install.ps1' -OutFile $installer
    $root = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet'
    Stage 'Installing .NET 10 SDK and Runtime...'
    & $installer -Channel 10.0 -InstallDir $root -NoPath
    Stage 'Verifying .NET 10 SDK and Runtime...'
    $sdk = @(Get-ChildItem (Join-Path $root 'sdk') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^10\.\d+\.\d+$' -and [version]$_.Name -ge [version]'10.0.201' -and
            (Test-Path (Join-Path $_.FullName 'MSBuild.dll')) }).Count -gt 0
    $runtime = @(Get-ChildItem (Join-Path $root 'shared\Microsoft.NETCore.App') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^10\.\d+\.\d+$' -and
            (Test-Path (Join-Path $_.FullName 'coreclr.dll')) }).Count -gt 0
    $fxr = @(Get-ChildItem (Join-Path $root 'host\fxr') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^10\.\d+\.\d+$' -and
            (Test-Path (Join-Path $_.FullName 'hostfxr.dll')) }).Count -gt 0
    if (-not ($sdk -and $runtime -and $fxr)) { throw 'A complete .NET 10 SDK installation was not found.' }
    Stage 'Installation successful. Continuing Unreal Editor startup...'
} catch {
    Stage ('Installation failed: ' + $_.Exception.Message)
    exit 1
} finally {
    Remove-Item -LiteralPath $installer -Force -ErrorAction SilentlyContinue
}
)PS");
#elif PLATFORM_MAC || PLATFORM_LINUX
	return TEXT(R"SH(#!/usr/bin/env bash
set -euo pipefail
status_file="$1"
root="$HOME/.dotnet"
installer="$(mktemp)"
trap 'rm -f "$installer"' EXIT
trap 'printf "%s\n" "Installation failed. Check your internet connection and retry." > "$status_file"' ERR
printf "%s\n" "Downloading the Microsoft installer..." > "$status_file"
curl --fail --location --silent --show-error https://dot.net/v1/dotnet-install.sh -o "$installer"
printf "%s\n" "Installing .NET 10 SDK and Runtime..." > "$status_file"
bash "$installer" --channel 10.0 --install-dir "$root" --no-path
printf "%s\n" "Verifying .NET 10 SDK and Runtime..." > "$status_file"
sdk_ok=false
runtime_ok=false
fxr_ok=false
for sdk in "$root"/sdk/10.*; do
    if [[ -f "$sdk/MSBuild.dll" ]]; then sdk_ok=true; break; fi
done
for framework in "$root"/shared/Microsoft.NETCore.App/10.*; do
    if [[ -f "$framework/libcoreclr.dylib" || -f "$framework/libcoreclr.so" ]]; then runtime_ok=true; break; fi
done
for fxr in "$root"/host/fxr/10.*; do
    if [[ -f "$fxr/libhostfxr.dylib" || -f "$fxr/libhostfxr.so" ]]; then fxr_ok=true; break; fi
done
if [[ "$sdk_ok" != true || "$runtime_ok" != true || "$fxr_ok" != true ]]; then
    printf "%s\n" "Installation failed: SDK or Runtime is incomplete." > "$status_file"
    exit 1
fi
printf "%s\n" "Installation successful. Continuing Unreal Editor startup..." > "$status_file"
)SH");
#else
	return FString();
#endif
}

static void ActivateSdk()
{
	const FString Root = PerUserDotNet();
	FPlatformMisc::SetEnvironmentVar(TEXT("DOTNET_ROOT"), *Root);
#if PLATFORM_WINDOWS
	FPlatformMisc::SetEnvironmentVar(TEXT("DOTNET_ROOT_X64"), *Root);
#endif
	const FString OldPath = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	const FString NewPath = Root + FPlatformMisc::GetPathVarDelimiter() + OldPath;
	FPlatformMisc::SetEnvironmentVar(TEXT("PATH"), *NewPath);
}

class SInstallSdk final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInstallSdk) {}
	SLATE_END_ARGS()
	void Construct(const FArguments&)
	{
		Status = FText::FromString(TEXT("The .NET 10 SDK and Runtime are required by UnrealSharp."));
		ChildSlot
		[
			SNew(SBorder).Padding(18)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Install the official Microsoft .NET 10 SDK for your user account. Administrator rights are not required.")))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
				[
					SNew(STextBlock).Text(this, &SInstallSdk::GetStatus).AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
				[
					SNew(SProgressBar).Percent(TOptional<float>())
					.Visibility(this, &SInstallSdk::GetProgressVisibility)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
					[
						SNew(SButton).Text(FText::FromString(TEXT("Cancel")))
						.OnClicked(this, &SInstallSdk::Cancel)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton).Text(FText::FromString(TEXT("Install")))
						.IsEnabled(this, &SInstallSdk::CanInstall)
						.OnClicked(this, &SInstallSdk::Install)
					]
				]
			]
		];
	}
	~SInstallSdk()
	{
		CloseProcess();
		Cleanup();
	}
	void SetWindow(const TSharedRef<SWindow>& InWindow) { Window = InWindow; }
	bool Succeeded() const { return bSucceeded; }

private:
	FText GetStatus() const { return Status; }
	EVisibility GetProgressVisibility() const { return bInstalling ? EVisibility::Visible : EVisibility::Collapsed; }
	bool CanInstall() const { return !bInstalling; }
	void Cleanup()
	{
		if (!ScriptPath.IsEmpty()) { IFileManager::Get().Delete(*ScriptPath); ScriptPath.Empty(); }
		if (!StatusPath.IsEmpty()) { IFileManager::Get().Delete(*StatusPath); StatusPath.Empty(); }
	}
	void CloseProcess()
	{
		if (Process.IsValid())
		{
			if (FPlatformProcess::IsProcRunning(Process)) { FPlatformProcess::TerminateProc(Process, true); }
			FPlatformProcess::CloseProc(Process);
			Process = FProcHandle();
		}
	}
	FReply Install()
	{
		if (bInstalling) { return FReply::Handled(); }
		CloseProcess();
		Cleanup();
		const FString Source = Script();
		if (Source.IsEmpty())
		{
			Status = FText::FromString(TEXT("Automatic installation is not supported on this platform."));
			return FReply::Handled();
		}
#if PLATFORM_WINDOWS
		const TCHAR* Extension = TEXT(".ps1");
		const TCHAR* Executable = TEXT("powershell.exe");
#else
		const TCHAR* Extension = TEXT(".sh");
		const TCHAR* Executable = TEXT("/bin/bash");
#endif
		ScriptPath = FPaths::Combine(FPlatformProcess::UserTempDir(),
			FString::Printf(TEXT("UnrealSharpSdk_%s%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits), Extension));
		StatusPath = ScriptPath + TEXT(".status");
		if (!FFileHelper::SaveStringToFile(Source, *ScriptPath))
		{
			Status = FText::FromString(TEXT("Unable to prepare the installer."));
			Cleanup();
			return FReply::Handled();
		}
#if PLATFORM_WINDOWS
		const FString Arguments = FString::Printf(TEXT("-NoProfile -ExecutionPolicy Bypass -File \"%s\" -StatusFile \"%s\""), *ScriptPath, *StatusPath);
#else
		const FString Arguments = FString::Printf(TEXT("\"%s\" \"%s\""), *ScriptPath, *StatusPath);
#endif
		Process = FPlatformProcess::CreateProc(Executable, *Arguments, false, true, true,
			nullptr, 0, nullptr, nullptr);
		if (!Process.IsValid())
		{
			Status = FText::FromString(TEXT("Unable to start the installer. Click Install to retry."));
			Cleanup();
			return FReply::Handled();
		}
		bInstalling = true;
		Status = FText::FromString(TEXT("Starting .NET 10 SDK installation..."));
		RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SInstallSdk::Poll));
		return FReply::Handled();
	}
	EActiveTimerReturnType Poll(double, float)
	{
		if (!bInstalling) { return EActiveTimerReturnType::Stop; }
		FString Stage;
		if (FFileHelper::LoadFileToString(Stage, *StatusPath) && !Stage.TrimStartAndEnd().IsEmpty())
		{
			Status = FText::FromString(Stage.TrimStartAndEnd());
		}
		if (FPlatformProcess::IsProcRunning(Process)) { return EActiveTimerReturnType::Continue; }
		int32 Code = -1;
		const bool bGotCode = FPlatformProcess::GetProcReturnCode(Process, &Code);
		FPlatformProcess::CloseProc(Process);
		Process = FProcHandle();
		bInstalling = false;
		Cleanup();
		if (bGotCode && Code == 0)
		{
			ActivateSdk();
			if (UnrealSharp::InstallationUtilities::IsDotNetSdkInstalled())
			{
				bSucceeded = true;
				UE_LOGFMT(LogUnrealSharp, Display, ".NET 10 SDK installation successful. Resuming current Editor startup.");
				if (const TSharedPtr<SWindow> Dialog = Window.Pin()) { Dialog->RequestDestroyWindow(); }
				return EActiveTimerReturnType::Stop;
			}
			Status = FText::FromString(TEXT("SDK installation finished but verification failed. Click Install to retry."));
		}
		else
		{
			Status = FText::FromString(FString::Printf(
				TEXT("Installation failed (exit code %d). Click Install to retry."), Code));
		}
		return EActiveTimerReturnType::Stop;
	}
	FReply Cancel()
	{
		CloseProcess();
		bInstalling = false;
		Cleanup();
		if (const TSharedPtr<SWindow> Dialog = Window.Pin()) { Dialog->RequestDestroyWindow(); }
		return FReply::Handled();
	}

	TWeakPtr<SWindow> Window;
	FText Status;
	FString ScriptPath;
	FString StatusPath;
	FProcHandle Process;
	bool bInstalling = false;
	bool bSucceeded = false;
};

bool InstallSdkDuringStartup()
{
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOGFMT(LogUnrealSharp, Error, "Cannot show SDK installer: Slate is not initialized during plugin loading.");
		return false;
	}
	const TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(FText::FromString(TEXT("UnrealSharp - Install .NET 10 SDK")))
		.ClientSize(FVector2D(510, 235))
		.SupportsMinimize(false)
		.SupportsMaximize(false);
	const TSharedRef<SInstallSdk> Installer = SNew(SInstallSdk);
	Installer->SetWindow(Dialog);
	Dialog->SetContent(Installer);
	UE_LOGFMT(LogUnrealSharp, Display, "Waiting for .NET 10 SDK installation during Loading Plugins.");
	FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow(), false);
	return Installer->Succeeded();
}
} // namespace UnrealSharp::DotNetInstaller
#endif // WITH_EDITOR
