#include "CSManagedAssembly.h"

#include "CSManagedPluginCallbacks.h"
#include "UnrealSharpCore.h"
#include "Misc/Paths.h"
#include "CSManager.h"
#include "Logging/StructuredLog.h"
#include "ReflectionData/CSEnumReflectionData.h"
#include "Types/CSEnum.h"
#include "Compilers/CSManagedDelegateCompiler.h"
#include "Utilities/CSClassUtilities.h"
#include "Utilities/CSUtilities.h"

FCSAssemblyEvents::FCSAssemblyEvent FCSAssemblyEvents::OnAssemblyLoaded;
FCSAssemblyEvents::FCSAssemblyEvent FCSAssemblyEvents::OnAssemblyUnloaded;

void UCSManagedAssembly::Initialize(const FStringView InAssemblyPath, bool bInIsCollectible)
{
	if (!AssemblyFilePath.IsEmpty())
	{
		return;
	}

	AssemblyFilePath = FPaths::ConvertRelativePathToFull(InAssemblyPath.GetData());

#if defined(_WIN32)
	AssemblyFilePath.ReplaceInline(TEXT("/"), TEXT("\\"));
#endif

	bIsCollectible = bInIsCollectible;

	FOnManagedTypeStructureChanged::FDelegate Delegate = FOnManagedTypeStructureChanged::FDelegate::CreateUObject(
		this, &UCSManagedAssembly::OnTypeReflectionDataChanged);
	FCSManagedTypeDefinitionEvents::AddOnReflectionDataChangedDelegate(Delegate);
}

bool UCSManagedAssembly::LoadAssembly()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString(TEXT("UCSManagedAssembly::LoadManagedAssembly: ") + GetName()));

	if (IsAssemblyLoaded())
	{
		UE_LOGFMT(LogUnrealSharp, Display, "{0} is already loaded", GetName());
		return true;
	}

	if (!FPaths::FileExists(AssemblyFilePath))
	{
		UE_LOGFMT(LogUnrealSharp, Error, "Assembly path does not exist: {0}", AssemblyFilePath);
		return false;
	}

	bIsLoading = true;
	AssemblyHandle = MakeShared<FGCHandle>();

	FGCHandle NewAssemblyGCHandle = GetManagedPluginCallbacks().LoadPlugin(*AssemblyFilePath, bIsCollectible);

	if (NewAssemblyGCHandle.IsNull())
	{
		UE_LOGFMT(LogUnrealSharp, Fatal, "Failed to load assembly: {0}", AssemblyFilePath);
	}

	*AssemblyHandle = NewAssemblyGCHandle;

	for (const TSharedPtr<FCSManagedTypeDefinition>& QueuedType : PendingCompilationTypes)
	{
		QueuedType->Compile();
	}

#if WITH_EDITOR
	PendingCompilationTypes.Reset();
#else
	PendingCompilationTypes.Empty();
#endif

	bIsLoading = false;

	FCSAssemblyEvents::OnAssemblyLoaded.Broadcast(this);
	return true;
}

void UCSManagedAssembly::UnloadAssembly()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString(TEXT("UCSManagedAssembly::UnloadAssembly: ") + GetName()));

	if (!bIsCollectible)
	{
		UE_LOGFMT(LogUnrealSharp, Warning,
		          "Assembly {0} is not collectible and will not be unloaded. It will be unloaded when the editor shuts down.",
		          GetName());
		return;
	}

	if (!IsAssemblyLoaded())
	{
		UE_LOGFMT(LogUnrealSharp, Display, "{0} is already unloaded", GetName());
		return;
	}

	bIsUnloading = true;
	FGCHandleIntPtr AssemblyHandlePtr = AssemblyHandle->GetHandle();
	UCSManager::Get().ReleaseAssemblyManagedObjects(AssemblyHandle);
	for (TSharedPtr<FGCHandle> Handle : ManagedHandles)
	{
		Handle->Dispose(AssemblyHandlePtr);
	}

	ManagedMethodHandles.Empty();
	ManagedTypeHandles.Empty();
	ManagedHandles.Empty();

	AssemblyHandle->Dispose(AssemblyHandlePtr);
	AssemblyHandle.Reset();

	GetManagedPluginCallbacks().UnloadPlugin(*AssemblyFilePath);
	bIsUnloading = false;

	FCSAssemblyEvents::OnAssemblyUnloaded.Broadcast(this);
}

TSharedPtr<FCSManagedTypeDefinition> UCSManagedAssembly::FindOrAddManagedTypeDefinition(UClass* Field)
{
	if (const ICSManagedTypeInterface* ManagedClass = FCSClassUtilities::GetManagedType(Field))
	{
		return ManagedClass->GetManagedTypeDefinition();
	}

	const FCSFieldName FieldName = FCSFieldName::FromNativeBase(Field);
	return FindOrAddManagedTypeDefinition(FieldName);
}

TSharedPtr<FCSManagedTypeDefinition> UCSManagedAssembly::FindOrAddManagedTypeDefinition(const FCSFieldName& FieldName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCSAssembly::FindOrAddManagedTypeDefinition);

	TSharedPtr<FCSManagedTypeDefinition>& ManagedTypeDefinition = ManagedTypeRegistry.FindOrAdd(FieldName);

	if (ManagedTypeDefinition.IsValid())
	{
		return ManagedTypeDefinition;
	}

	UField* Field = FCSUtilities::FindField<UField>(FieldName);
	if (!IsValid(Field))
	{
		UE_LOGFMT(LogUnrealSharp, Error, "Failed to find native class: {0}", FieldName.GetSourceName());
		return nullptr;
	}

	ManagedTypeDefinition = FCSManagedTypeDefinition::CreateFromNativeField(Field, this);
	return ManagedTypeDefinition;
}

void UCSManagedAssembly::RegisterManagedType(TCHAR* InFieldName, const TCHAR* InNamespace, ECSFieldType FieldType, uint8* TypeGCHandle, TCHAR* ReflectionJsonString)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCSManagedAssembly::RegisterManagedType);
	UE_LOGFMT(LogUnrealSharp, Verbose, "Registering type {0}.{1}", InNamespace, InFieldName);

	const FCSFieldName FieldName(InFieldName, FCSNamespace(InNamespace), GetFName(), FieldType);
	TSharedPtr<FCSManagedTypeDefinition>& ManagedTypeDefinition = ManagedTypeRegistry.FindOrAdd(FieldName);
	
#if WITH_EDITOR
	if (ManagedTypeDefinition.IsValid() && !FCSUtilities::ShouldReloadDefinition(
		ManagedTypeDefinition.ToSharedRef(), ReflectionJsonString))
	{
		ManagedTypeDefinition->SetTypeGCHandle(TypeGCHandle);
		ManagedTypeDefinition->SetDirtyFlags(None);
		return;
	}
#endif

	UCSManagedTypeCompiler* Compiler = FCSUtilities::ResolveCompilerFromFieldType(FieldType);
	TSharedPtr<FCSTypeReferenceReflectionData> NewReflectionData = Compiler->CreateReflectionData();
	NewReflectionData->SerializeFromJsonString(ReflectionJsonString);

	if (ManagedTypeDefinition.IsValid())
	{
		ManagedTypeDefinition->SetReflectionData(NewReflectionData);
	}
	else
	{
		ManagedTypeDefinition = FCSManagedTypeDefinition::CreateFromReflectionData(NewReflectionData, this, Compiler);
	}

	ManagedTypeDefinition->SetTypeGCHandle(TypeGCHandle);
}

TSharedPtr<FGCHandle> UCSManagedAssembly::CreateManagedObjectFromNative(const UObject* Object)
{
	UClass* Class = FCSClassUtilities::GetFirstNonBlueprintClass(Object->GetClass());
	TSharedPtr<FCSManagedTypeDefinition> ManagedTypeDefinition = FindOrAddManagedTypeDefinition(Class);
	return ManagedTypeDefinition.IsValid() ? CreateManagedObjectFromNative(Object, ManagedTypeDefinition->GetTypeGCHandle()) : nullptr;
}

TSharedPtr<FGCHandle> UCSManagedAssembly::CreateManagedObjectFromNative(
	const UObject* Object, const TSharedPtr<FGCHandle>& TypeGCHandle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCSManagedAssembly::CreateManagedObjectFromNative);

	if (bIsUnloading || !TypeGCHandle.IsValid() || TypeGCHandle->IsNull())
	{
		return nullptr;
	}

	const FCSObjectID ObjectID = Object->GetUniqueID();
	if (TSharedPtr<FGCHandle>* Existing = UCSManager::Get().GetManagedObjectHandles().FindByHash(ObjectID.Get(), ObjectID))
	{
		TSharedPtr<FGCHandle> Previous = *Existing;
		if (Previous.IsValid() && !Previous->IsNull())
		{
			return Previous;
		}
		UCSManager::Get().GetManagedObjectHandles().RemoveByHash(ObjectID.Get(), ObjectID);
		UCSManager::Get().DisposeManagedObjectHandle(Previous);
	}

	TCHAR* Error = nullptr;
	FGCHandle NewObjectHandle = GetManagedCallbacks().
		CreateNewManagedObject(Object, TypeGCHandle->GetPointer(), &Error);

	if (NewObjectHandle.IsNull())
	{
		UE_LOGFMT(LogUnrealSharp, Fatal, "Failed to create managed counterpart for {0}:\n{1}", Object->GetName(),
		          Error);
	}

	TSharedPtr<FGCHandle> Handle = MakeShared<FGCHandle>(NewObjectHandle);

	UCSManager::Get().GetManagedObjectHandles().AddByHash(ObjectID.Get(), ObjectID, Handle);
	UCSManager::Get().RegisterManagedObjectHandle(Handle, AssemblyHandle);
	return Handle;
}

TSharedPtr<FGCHandle> UCSManagedAssembly::GetOrCreateManagedInterface(UObject* Object, UClass* InterfaceClass)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCSManagedAssembly::GetOrCreateManagedInterface);

	if (bIsUnloading)
	{
		return nullptr;
	}

	UClass* NonBlueprintClass = FCSClassUtilities::GetFirstNonBlueprintClass(InterfaceClass);
	TSharedPtr<FCSManagedTypeDefinition> ClassInfo = FindOrAddManagedTypeDefinition(NonBlueprintClass);
	TSharedPtr<FGCHandle> TypeHandle = ClassInfo.IsValid() ? ClassInfo->GetTypeGCHandle() : nullptr;
	if (!TypeHandle.IsValid() || TypeHandle->IsNull())
	{
		return nullptr;
	}

	const FCSObjectID ObjectID = Object->GetUniqueID();
	auto* InterfaceWrappersMap = UCSManager::Get().GetManagedInterfaceWrappers().FindByHash(ObjectID.Get(), ObjectID);

	const uint32 TypeId = InterfaceClass->GetUniqueID();
	if (InterfaceWrappersMap)
	{
		if (TSharedPtr<FGCHandle>* Existing = InterfaceWrappersMap->FindByHash(TypeId, TypeId))
		{
			TSharedPtr<FGCHandle> Previous = *Existing;
			if (Previous.IsValid() && !Previous->IsNull())
			{
				return Previous;
			}
			InterfaceWrappersMap->RemoveByHash(TypeId, TypeId);
			UCSManager::Get().DisposeManagedObjectHandle(Previous);
		}
	}

	TSharedPtr<FGCHandle>* ObjectHandle = UCSManager::Get().GetManagedObjectHandles().FindByHash(
		ObjectID.Get(), ObjectID);
	if (!ObjectHandle || !ObjectHandle->IsValid() || (*ObjectHandle)->IsNull())
	{
		return nullptr;
	}

	FGCHandle NewManagedObjectWrapper = GetManagedCallbacks().CreateNewManagedObjectWrapper(
		(*ObjectHandle)->GetPointer(), TypeHandle->GetPointer());

	if (NewManagedObjectWrapper.IsNull())
	{
		UE_LOGFMT(LogUnrealSharp, Fatal, "Failed to create managed counterpart for {0}", Object->GetName());
	}

	TSharedPtr<FGCHandle> Handle = MakeShared<FGCHandle>(NewManagedObjectWrapper);
	UCSManager::Get().GetManagedInterfaceWrappers().FindOrAddByHash(ObjectID.Get(), ObjectID).AddByHash(TypeId, TypeId, Handle);
	UCSManager::Get().RegisterManagedObjectHandle(Handle, AssemblyHandle);

	return Handle;
}

void UCSManagedAssembly::OnTypeReflectionDataChanged(TSharedPtr<FCSManagedTypeDefinition> ManagedTypeDefinition)
{
	if (ManagedTypeDefinition->GetOwningAssembly() != this)
	{
		return;
	}

	PendingCompilationTypes.Add(ManagedTypeDefinition);
}
