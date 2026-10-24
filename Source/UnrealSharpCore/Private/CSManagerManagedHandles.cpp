#include "CSManager.h"

void UCSManager::RegisterManagedObjectHandle(const TSharedPtr<FGCHandle>& Handle, const TSharedPtr<const FGCHandle>& Owner)
{
	check(Handle.IsValid() && Owner.IsValid());
	ManagedHandleOwners.Add(Handle.Get(), Owner);
}

void UCSManager::DisposeManagedObjectHandle(const TSharedPtr<FGCHandle>& Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}
	TSharedPtr<const FGCHandle> Owner;
	ManagedHandleOwners.RemoveAndCopyValue(Handle.Get(), Owner);
	Handle->Dispose(Owner.IsValid() ? Owner->GetHandle() : FGCHandleIntPtr{});
}

void UCSManager::NotifyUObjectDeleted(const UObjectBase* Object, int32 Index)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCSManager::NotifyUObjectDeleted);
	const FCSObjectID ObjectID(Index);
	TSharedPtr<FGCHandle> ObjectHandle;
	ManagedObjectHandles.RemoveAndCopyValueByHash(ObjectID.Get(), ObjectID, ObjectHandle);
	TMap<FCSObjectID, TSharedPtr<FGCHandle>> Interfaces;
	ManagedInterfaceWrapperHandles.RemoveAndCopyValueByHash(ObjectID.Get(), ObjectID, Interfaces);
	for (const auto& Entry : Interfaces)
	{
		DisposeManagedObjectHandle(Entry.Value);
	}
	DisposeManagedObjectHandle(ObjectHandle);
}

void UCSManager::ReleaseAssemblyManagedObjects(const TSharedPtr<const FGCHandle>& Owner)
{
	TArray<TSharedPtr<FGCHandle>> ObjectsToDispose;
	TArray<TSharedPtr<FGCHandle>> InterfacesToDispose;
	TSet<FCSObjectID> RemovedObjects;
	auto IsOwned = [this, &Owner](const TSharedPtr<FGCHandle>& Handle)
	{
		const auto* Found = ManagedHandleOwners.Find(Handle.Get());
		return Found && Found->Get() == Owner.Get();
	};
	for (auto It = ManagedObjectHandles.CreateIterator(); It; ++It)
	{
		if (IsOwned(It.Value()))
		{
			RemovedObjects.Add(It.Key());
			ObjectsToDispose.Add(It.Value());
			It.RemoveCurrent();
		}
	}
	for (auto It = ManagedInterfaceWrapperHandles.CreateIterator(); It; ++It)
	{
		for (auto Interface = It.Value().CreateIterator(); Interface; ++Interface)
		{
			if (RemovedObjects.Contains(It.Key()) || IsOwned(Interface.Value()))
			{
				InterfacesToDispose.Add(Interface.Value());
				Interface.RemoveCurrent();
			}
		}
		if (It.Value().IsEmpty())
		{
			It.RemoveCurrent();
		}
	}
	for (const auto& Handle : InterfacesToDispose)
	{
		DisposeManagedObjectHandle(Handle);
	}
	for (const auto& Handle : ObjectsToDispose)
	{
		DisposeManagedObjectHandle(Handle);
	}
}
