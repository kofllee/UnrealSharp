#include "DotNet/CSDotNetInstaller.h"

#if WITH_EDITOR
#include "CSInstallationUtilities.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindowDefinition.h"
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
#include "Widgets/Layout/SBox.h"
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
    Stage 'DOWNLOAD|Downloading the Microsoft installer...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri 'https://dot.net/v1/dotnet-install.ps1' -OutFile $installer
    $root = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet'
    Stage 'INSTALL|Installing .NET 10 SDK and Runtime...'
    & $installer -Channel 10.0 -InstallDir $root -NoPath
    Stage 'VERIFY|Verifying .NET 10 SDK and Runtime...'
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
    Stage 'SUCCESS|Installation successful. Continuing Unreal Editor startup...'
} catch {
    Stage ('FAILURE|Installation failed: ' + $_.Exception.Message)
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
trap 'printf "%s\n" "FAILURE|Installation failed. Check your internet connection and retry." > "$status_file"' ERR
printf "%s\n" "DOWNLOAD|Downloading the Microsoft installer..." > "$status_file"
curl --fail --location --silent --show-error https://dot.net/v1/dotnet-install.sh -o "$installer"
printf "%s\n" "INSTALL|Installing .NET 10 SDK and Runtime..." > "$status_file"
bash "$installer" --channel 10.0 --install-dir "$root" --no-path
printf "%s\n" "VERIFY|Verifying .NET 10 SDK and Runtime..." > "$status_file"
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
    printf "%s\n" "FAILURE|Installation failed: SDK or Runtime is incomplete." > "$status_file"
    exit 1
fi
printf "%s\n" "SUCCESS|Installation successful. Continuing Unreal Editor startup..." > "$status_file"
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

enum class EInstallPhase : uint8
{
	Ready,
	Downloading,
	Installing,
	Verifying,
	Failed,
	Completed
};

class SInstallSdk final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInstallSdk) {}
	SLATE_END_ARGS()
	void Construct(const FArguments&)
	{
		Status = FText::FromString(TEXT("The .NET 10 SDK and Runtime are required by UnrealSharp."));
		// The window may be created while the Unreal splash screen is still up.
		// Bring it forward once it has an actual native window and the modal pumps Slate.
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SInstallSdk::FocusWindow));
		ChildSlot
		[
			SNew(SBox).WidthOverride(490.0f)
			[
				SNew(SBorder).Padding(18)
				[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("UnrealSharp - .NET 10 SDK Setup")))
				]
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
						.Visibility(this, &SInstallSdk::GetCancelVisibility)
						.OnClicked(this, &SInstallSdk::Cancel)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton).Text(this, &SInstallSdk::GetInstallButtonText)
						.Visibility(this, &SInstallSdk::GetInstallVisibility)
						.IsEnabled(this, &SInstallSdk::CanInstall)
						.OnClicked(this, &SInstallSdk::Install)
					]
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
	bool WasCancelled() const { return bCancelled; }

private:
	EActiveTimerReturnType FocusWindow(double, float)
	{
		if (const TSharedPtr<SWindow> Dialog = Window.Pin())
		{
			Dialog->BringToFront(true);
			Dialog->FlashWindow();
		}
		return EActiveTimerReturnType::Stop;
	}

	FText GetStatus() const { return Status; }
	EVisibility GetProgressVisibility() const { return bInstalling ? EVisibility::Visible : EVisibility::Collapsed; }
	EVisibility GetCancelVisibility() const
	{
		return (!bSucceeded && (!bInstalling || Phase == EInstallPhase::Downloading))
			? EVisibility::Visible : EVisibility::Collapsed;
	}
	EVisibility GetInstallVisibility() const { return bInstalling || bSucceeded ? EVisibility::Collapsed : EVisibility::Visible; }
	FText GetInstallButtonText() const { return FText::FromString(Phase == EInstallPhase::Failed ? TEXT("Retry") : TEXT("Install")); }
	bool CanInstall() const { return !bInstalling && !bSucceeded; }

	// Stage is produced by the child script. Read it when cancelling too,
	// so a click queued just as installation starts cannot interrupt extraction.
	void RefreshStage()
	{
		// Cancel before Install has no status file; never try to read an empty path.
		if (StatusPath.IsEmpty()) { return; }
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *StatusPath)) { return; }
		Text = Text.TrimStartAndEnd();
		FString Code;
		FString Message;
		if (!Text.Split(TEXT("|"), &Code, &Message)) { return; }
		if (Code == TEXT("DOWNLOAD")) { Phase = EInstallPhase::Downloading; }
		else if (Code == TEXT("INSTALL")) { Phase = EInstallPhase::Installing; }
		else if (Code == TEXT("VERIFY")) { Phase = EInstallPhase::Verifying; }
		else if (Code == TEXT("FAILURE")) { Phase = EInstallPhase::Failed; }
		else if (Code == TEXT("SUCCESS")) { Phase = EInstallPhase::Completed; }
		if (!Message.IsEmpty()) { Status = FText::FromString(Message); }
	}
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
		Phase = EInstallPhase::Downloading;
		const FString Source = Script();
		if (Source.IsEmpty())
		{
			Phase = EInstallPhase::Failed;
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
			Phase = EInstallPhase::Failed;
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
			Phase = EInstallPhase::Failed;
			Status = FText::FromString(TEXT("Unable to start the installer. Click Retry to try again."));
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
		RefreshStage();
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
				Phase = EInstallPhase::Completed;
				UE_LOGFMT(LogUnrealSharp, Display, ".NET 10 SDK installation successful. Resuming current Editor startup.");
				if (const TSharedPtr<SWindow> Dialog = Window.Pin()) { Dialog->RequestDestroyWindow(); }
				return EActiveTimerReturnType::Stop;
			}
			Phase = EInstallPhase::Failed;
			Status = FText::FromString(TEXT("SDK installation finished but verification failed. Click Retry."));
		}
		else
		{
			Phase = EInstallPhase::Failed;
			Status = FText::FromString(FString::Printf(
				TEXT("Installation failed (exit code %d). Click Retry or Cancel."), Code));
		}
		return EActiveTimerReturnType::Stop;
	}
	FReply Cancel()
	{
		RefreshStage();
		if (bSucceeded || (bInstalling && Phase != EInstallPhase::Downloading))
		{
			// Files may already be installing; do not terminate the installer.
			return FReply::Handled();
		}
		CloseProcess();
		bInstalling = false;
		bCancelled = true;
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
	bool bCancelled = false;
	EInstallPhase Phase = EInstallPhase::Ready;
};

EInstallSdkResult InstallSdkDuringStartup()
{
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOGFMT(LogUnrealSharp, Error, "Cannot show SDK installer: Slate is not initialized during plugin loading.");
		return EInstallSdkResult::Failed;
	}
	const TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(FText::FromString(TEXT("UnrealSharp - Install .NET 10 SDK")))
		.SizingRule(ESizingRule::Autosized)
		.CreateTitleBar(false) // No system Close button to bypass safe-stage cancellation.
		.IsTopmostWindow(true)
		.ActivationPolicy(EWindowActivationPolicy::Always)
		.SupportsMinimize(false)
		.SupportsMaximize(false);
	const TSharedRef<SInstallSdk> Installer = SNew(SInstallSdk);
	Installer->SetWindow(Dialog);
	Dialog->SetContent(Installer);
	UE_LOGFMT(LogUnrealSharp, Display, "Waiting for .NET 10 SDK installation during Loading Plugins.");
	FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow(), false);
	if (Installer->Succeeded()) { return EInstallSdkResult::Success; }
	return Installer->WasCancelled() ? EInstallSdkResult::Cancelled : EInstallSdkResult::Failed;
}
} // namespace UnrealSharp::DotNetInstaller
#endif // WITH_EDITOR
