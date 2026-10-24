#include "CSManagedAssembly.h"
#include "UnrealSharpCore.h"

TSharedPtr<FGCHandle> UCSManagedAssembly::FindTypeHandle(const FCSFieldName& FieldName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCSManagedAssembly::FindTypeHandle);

	if (bIsUnloading)
	{
		return nullptr;
	}

	if (TSharedPtr<FGCHandle>* ExistingHandle = ManagedTypeHandles.Find(FieldName); ExistingHandle && ExistingHandle->IsValid() && !(*ExistingHandle)->IsNull())
	{
		return *ExistingHandle;
	}

	if (!AssemblyHandle.IsValid() || AssemblyHandle->IsNull())
	{
		return nullptr;
	}

	TStringBuilder<256> FullName;
	FieldName.AppendFullName(FullName);

	uint8* TypeHandle = GetManagedCallbacks().GetManagedTypeHandle(AssemblyHandle->GetPointer(), *FullName);

	if (!TypeHandle)
	{
		return nullptr;
	}

	UE_LOGFMT(LogUnrealSharp, Verbose, "Resolved managed type handle for '{TypeName}'", FullName.ToString());
	return AddTypeHandle(FieldName, TypeHandle);
}

TSharedPtr<FGCHandle> UCSManagedAssembly::AddTypeHandle(const FCSFieldName& FieldName, uint8* TypeHandle)
{
	if (!TypeHandle || bIsUnloading)
	{
		return nullptr;
	}

	if (TSharedPtr<FGCHandle>* Existing = ManagedTypeHandles.Find(FieldName); Existing && Existing->IsValid())
	{
		if ((*Existing)->IsNull())
		{
			**Existing = FGCHandle(TypeHandle);
		}
		else if ((*Existing)->GetPointer() != TypeHandle)
		{
			FGCHandle Duplicate(TypeHandle);
			Duplicate.Dispose(AssemblyHandle.IsValid() ? AssemblyHandle->GetHandle() : FGCHandleIntPtr{});
		}
		return *Existing;
	}

	TSharedPtr<FGCHandle> NewTypeHandle = MakeShared<FGCHandle>(TypeHandle);
	ManagedTypeHandles.Add(FieldName, NewTypeHandle);
	ManagedHandles.Add(NewTypeHandle);
	ManagedMethodHandles.FindOrAdd(NewTypeHandle.Get());
	return NewTypeHandle;
}

TSharedPtr<FGCHandle> UCSManagedAssembly::GetManagedMethod(const TSharedPtr<FGCHandle>& TypeHandle,
                                                           const FString& MethodName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCSManagedAssembly::GetManagedMethod);
	UE_LOGFMT(LogUnrealSharp, Verbose, "Looking up managed method {0}", MethodName);

	if (bIsUnloading || !TypeHandle.IsValid() || TypeHandle->IsNull())
	{
		UE_LOGFMT(LogUnrealSharp, Error, "Type handle is invalid for method {0}", MethodName);
		return nullptr;
	}

	const auto* CachedMethods = ManagedMethodHandles.Find(TypeHandle.Get());
	if (!CachedMethods)
	{
		UE_LOGFMT(LogUnrealSharp, Error, "Type handle does not belong to this assembly: {0}", MethodName);
		return nullptr;
	}
	if (const TSharedPtr<FGCHandle>* Existing = CachedMethods->Find(MethodName); Existing && Existing->IsValid() && !(*Existing)->IsNull())
	{
		return *Existing;
	}

	uint8* MethodHandle = GetManagedCallbacks().GetManagedMethod(TypeHandle->GetPointer(), *MethodName);

	if (!MethodHandle)
	{
		UE_LOGFMT(LogUnrealSharp, Error, "Failed to find managed method for {0}", MethodName);
		return nullptr;
	}

	TSharedPtr<FGCHandle>& Result = ManagedMethodHandles.FindChecked(TypeHandle.Get()).FindOrAdd(MethodName);
	if (Result.IsValid())
	{
		*Result = FGCHandle(MethodHandle);
	}
	else
	{
		Result = MakeShared<FGCHandle>(MethodHandle);
		ManagedHandles.Add(Result);
	}
	return Result;
}
