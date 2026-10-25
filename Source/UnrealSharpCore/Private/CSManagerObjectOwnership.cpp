#include "CSManager.h"

void UCSManager::ProtectReferencedObject(UObject* Object)
{
	if (Object && GUObjectArray.IsDisregardForGC(this) && !GUObjectArray.IsDisregardForGC(Object) && !Object->IsRooted())
	{
		Object->AddToRoot();
		OwnedRootObjects.Add(Object);
	}
}

void UCSManager::ReleaseObjectProtection(UObject* Object)
{
	if (OwnedRootObjects.Remove(Object) && Object->IsRooted())
	{
		Object->RemoveFromRoot();
	}
}

void UCSManager::RemoveAssembly(UCSManagedAssembly* Assembly)
{
	check(!Assembly->IsAssemblyLoaded() && !Assembly->IsAssemblyLoading());
	if (Assemblies.FindRef(Assembly->GetFName()) != Assembly)
	{
		return;
	}
	Assemblies.Remove(Assembly->GetFName());
	for (auto It = PackageToManagedAssembly.CreateIterator(); It; ++It)
	{
		if (It.Value() == Assembly)
		{
			It.RemoveCurrent();
		}
	}
	ReleaseObjectProtection(Assembly);
}

void UCSManager::ReleaseObjectReferences()
{
	PackageToManagedAssembly.Empty();
	Assemblies.Empty();
	GlobalManagedPackage = nullptr;
	ManagedPackages.Empty();
	auto Roots = MoveTemp(OwnedRootObjects);
	for (const TWeakObjectPtr<UObject>& Root : Roots)
	{
		if (UObject* Object = Root.Get(); Object && Object->IsRooted())
		{
			Object->RemoveFromRoot();
		}
	}
}

void UCSManager::OnEnginePreExit()
{
	bIsShuttingDown = true;
	if (bRegisteredDeleteListener)
	{
		GUObjectArray.RemoveUObjectDeleteListener(this);
		bRegisteredDeleteListener = false;
	}
	ReleaseObjectReferences();
}

void UCSManager::BeginDestroy()
{
	OnEnginePreExit();
	Super::BeginDestroy();
}
