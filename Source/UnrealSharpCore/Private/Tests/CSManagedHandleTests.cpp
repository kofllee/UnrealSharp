#if WITH_DEV_AUTOMATION_TESTS
#include "CSManager.h"
#include "Curves/CurveFloat.h"
#include "CSManagedPluginCallbacks.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

struct FCSManagedHandleTestAccess
{
 static UCSManager*& Instance(){return UCSManager::Instance;}
 static void Register(UCSManager& Manager,const TSharedPtr<FGCHandle>& Handle,const TSharedPtr<FGCHandle>& Owner){Manager.RegisterManagedObjectHandle(Handle,Owner);}
 static void Dispose(UCSManager& Manager,const TSharedPtr<FGCHandle>& Handle){Manager.DisposeManagedObjectHandle(Handle);}
 static void Loading(UCSManagedAssembly& Assembly,bool Value){Assembly.bIsLoading=Value;}
 static void OwnRoot(UCSManager& Manager,UObject* Object){Object->AddToRoot();Manager.OwnedRootObjects.Add(Object);}
 static int32 Roots(UCSManager& Manager){return Manager.OwnedRootObjects.Num();}
 static void Protect(UCSManager& Manager,UObject* Object){Manager.ProtectReferencedObject(Object);}
 static void RegisterAssembly(UCSManager& Manager,UCSManagedAssembly* Assembly){Manager.Assemblies.Add(Assembly->GetFName(),Assembly);Manager.PackageToManagedAssembly.Add(-501,Assembly);}
 static void Remove(UCSManager& Manager,UCSManagedAssembly* Assembly){Manager.RemoveAssembly(Assembly);}
 static void Shutdown(UCSManager& Manager){Manager.OnEnginePreExit();}
 static int32 CachedAssemblies(UCSManager& Manager){return Manager.PackageToManagedAssembly.Num();}
 static int32 Owners(UCSManager& Manager){return Manager.ManagedHandleOwners.Num();}
 static int32 Metadata(UCSManagedAssembly& Assembly){return Assembly.ManagedHandles.Num();}
 static void Initialize(UCSManagedAssembly& Assembly,const TSharedPtr<FGCHandle>& Owner){Assembly.AssemblyHandle=Owner;Assembly.bIsCollectible=true;}
};
namespace
{
 struct FDisposeTag{using Type=FCSManagedCallbacks::ManagedCallbacks_Dispose FCSManagedCallbacks::*;friend Type Access(FDisposeTag);};
 template<class Tag,typename Tag::Type Member>struct TTestMember{friend typename Tag::Type Access(Tag){return Member;}};
 template struct TTestMember<FDisposeTag,&FCSManagedCallbacks::Dispose>;
 TMap<uint8*,int32> Frees;
 TMap<uint8*,uint8*> Owners;
 TSharedPtr<FGCHandle> Reentrant;
 TFunction<void()> DuringDispose;
 int32 Lookups=0,Creations=0;
 UPTRINT Next=1000;
 void __stdcall Dispose(FGCHandleIntPtr Handle,FGCHandleIntPtr Assembly)
 {
  ++Frees.FindOrAdd(Handle.ManagedHandlePtr);Owners.Add(Handle.ManagedHandlePtr,Assembly.ManagedHandlePtr);
  if(DuringDispose){auto Callback=MoveTemp(DuringDispose);Callback();}
  if(Reentrant.IsValid())Reentrant->Dispose(Assembly);
 }
 uint8* __stdcall Method(void*,const TCHAR*){++Lookups;return reinterpret_cast<uint8*>(++Next);}
 FGCHandleIntPtr __stdcall Create(const void*,void*,TCHAR**){++Creations;return {reinterpret_cast<uint8*>(++Next)};}
 void __stdcall Unload(const TCHAR*){}
 TSharedPtr<FGCHandle> Handle(){return MakeShared<FGCHandle>(reinterpret_cast<uint8*>(++Next));}
 struct FCallbacksScope
 {
  FCSManagedCallbacks Saved=GetManagedCallbacks();FCSManagedPluginCallbacks Plugins=GetManagedPluginCallbacks();UCSManager* Manager=FCSManagedHandleTestAccess::Instance();
  FCallbacksScope(UCSManager* TestManager){GetManagedCallbacks().*Access(FDisposeTag{})=&Dispose;GetManagedCallbacks().GetManagedMethod=&Method;GetManagedCallbacks().CreateNewManagedObject=&Create;GetManagedPluginCallbacks().UnloadPlugin=&Unload;FCSManagedHandleTestAccess::Instance()=TestManager;Frees.Empty();Owners.Empty();Lookups=0;Creations=0;}
  ~FCallbacksScope(){DuringDispose=nullptr;Reentrant.Reset();GetManagedCallbacks()=Saved;GetManagedPluginCallbacks()=Plugins;FCSManagedHandleTestAccess::Instance()=Manager;}
 };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSManagedHandleLifecycleTest,"UnrealSharp.Handles.Ownership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCSManagedHandleLifecycleTest::RunTest(const FString&)
{
 TStrongObjectPtr<UCSManager> Manager(NewObject<UCSManager>());FCallbacksScope Scope(Manager.Get());
 TStrongObjectPtr<UCSManagedAssembly> A(NewObject<UCSManagedAssembly>()),B(NewObject<UCSManagedAssembly>());
 TStrongObjectPtr<UCSManagedAssembly> LoadingAssembly(NewObject<UCSManagedAssembly>());
 FCSManagedHandleTestAccess::Loading(*LoadingAssembly,true);TestFalse(TEXT("Reentrant load before the assembly cell is assigned is rejected"),LoadingAssembly->LoadAssembly());FCSManagedHandleTestAccess::Loading(*LoadingAssembly,false);
 auto OwnerA=Handle(),OwnerB=Handle();FCSManagedHandleTestAccess::Initialize(*A,OwnerA);FCSManagedHandleTestAccess::Initialize(*B,OwnerB);
 auto Object=Handle(),Interface=Handle(),Other=Handle();uint8* ObjectPtr=Object->GetPointer();uint8* InterfacePtr=Interface->GetPointer();
 Manager->GetManagedObjectHandles().Add(-101,Object);Manager->GetManagedInterfaceWrappers().FindOrAdd(-101).Add(-201,Interface);Manager->GetManagedObjectHandles().Add(-102,Other);
 FCSManagedHandleTestAccess::Register(*Manager,Object,OwnerA);FCSManagedHandleTestAccess::Register(*Manager,Interface,OwnerB);FCSManagedHandleTestAccess::Register(*Manager,Other,OwnerB);
 auto InterfaceA=Handle();Manager->GetManagedInterfaceWrappers().FindOrAdd(-102).Add(-202,InterfaceA);FCSManagedHandleTestAccess::Register(*Manager,InterfaceA,OwnerA);
 A->UnloadAssembly();
 TestTrue(TEXT("Object handle invalidated on unload"),Object->IsNull());TestTrue(TEXT("Cross-assembly interface invalidated with its object"),Interface->IsNull());
 TestEqual(TEXT("Object disposed once"),Frees.FindRef(ObjectPtr),1);TestEqual(TEXT("Interface disposed once"),Frees.FindRef(InterfacePtr),1);
 TestEqual(TEXT("Interface uses its own assembly"),Owners.FindRef(InterfacePtr),OwnerB->GetPointer());
 TestEqual(TEXT("Unrelated live wrapper survives"),Manager->GetManagedObjectHandles().Num(),1);TestFalse(TEXT("Unrelated handle alive"),Other->IsNull());
 TestTrue(TEXT("Unloading an interface assembly releases its wrapper on a surviving object"),InterfaceA->IsNull());
 Manager->NotifyUObjectDeleted(nullptr,-101);Object->Dispose();A->UnloadAssembly();TestEqual(TEXT("Delete after unload does not double-free"),Frees.FindRef(ObjectPtr),1);
 auto InterfaceOnly=Handle();uint8* InterfaceOnlyPtr=InterfaceOnly->GetPointer();Manager->GetManagedInterfaceWrappers().FindOrAdd(-103).Add(-201,InterfaceOnly);FCSManagedHandleTestAccess::Register(*Manager,InterfaceOnly,OwnerB);
 Manager->NotifyUObjectDeleted(nullptr,-103);TestEqual(TEXT("Orphan interface map is cleaned"),Frees.FindRef(InterfaceOnlyPtr),1);
 auto Unknown=Handle();uint8* UnknownPtr=Unknown->GetPointer();Manager->GetManagedObjectHandles().Add(-104,Unknown);Manager->NotifyUObjectDeleted(nullptr,-104);TestEqual(TEXT("Missing owner uses managed fallback"),Frees.FindRef(UnknownPtr),1);
 auto ReentrantHandle=Handle();uint8* ReentrantPtr=ReentrantHandle->GetPointer();Reentrant=ReentrantHandle;ReentrantHandle->Dispose();Reentrant.Reset();TestEqual(TEXT("Reentrant dispose once"),Frees.FindRef(ReentrantPtr),1);
 FCSFieldName Field(FName(TEXT("FakeType")),FCSNamespace(TEXT("Tests")),FName(TEXT("Tests")),ECSFieldType::Class);
 auto Type=B->AddTypeHandle(Field,reinterpret_cast<uint8*>(++Next));auto First=B->GetManagedMethod(Type,TEXT("Invoke_Test"));
 bool Same=true;for(int32 I=0;I<50000;++I)Same&=B->GetManagedMethod(Type,TEXT("Invoke_Test"))==First;TestTrue(TEXT("Method cache returns same live cell"),Same);
 auto Foreign=Handle();AddExpectedError(TEXT("Type handle does not belong to this assembly"),EAutomationExpectedErrorFlags::Contains,1);TestFalse(TEXT("Foreign type lookup rejected without allocating"),B->GetManagedMethod(Foreign,TEXT("Invoke_Test")).IsValid());Foreign->Dispose();
 TestEqual(TEXT("Interop handle remains pointer-sized"),sizeof(FGCHandle),sizeof(void*));
 TestEqual(TEXT("One callback for repeated method lookup"),Lookups,1);TestEqual(TEXT("Only type and method metadata retained"),FCSManagedHandleTestAccess::Metadata(*B),2);
 auto Duplicate=reinterpret_cast<uint8*>(++Next);TestTrue(TEXT("Duplicate type registration preserves canonical cell"),B->AddTypeHandle(Field,Duplicate)==Type);TestEqual(TEXT("Duplicate incoming type handle released"),Frees.FindRef(Duplicate),1);
 TStrongObjectPtr<UObject> DirectObject(NewObject<UCurveFloat>());auto Direct=B->CreateManagedObjectFromNative(DirectObject.Get(),Type);TestTrue(TEXT("Direct wrapper creation preserves live canonical handle"),B->CreateManagedObjectFromNative(DirectObject.Get(),Type)==Direct);TestEqual(TEXT("Direct wrapper created only once"),Creations,1);
 auto Rejected=reinterpret_cast<uint8*>(++Next);auto RejectedRegistration=reinterpret_cast<uint8*>(++Next);
 DuringDispose=[&,Rejected,RejectedRegistration]()
 {
  TestFalse(TEXT("Reentrant load while unloading rejected before replacing the owner cell"),B->LoadAssembly());
  TestFalse(TEXT("New incoming type rejected during unload"),B->AddTypeHandle(Field,Rejected).IsValid());
  TestEqual(TEXT("Rejected incoming type freed once"),Frees.FindRef(Rejected),1);
  TestFalse(TEXT("Owned alias rejected during unload"),B->AddTypeHandle(Field,Type->GetPointer()).IsValid());
  TestFalse(TEXT("Owned type alias is not freed early"),Type->IsNull());
  B->RegisterManagedType(const_cast<TCHAR*>(TEXT("LateType")),TEXT("Tests"),ECSFieldType::Class,RejectedRegistration,const_cast<TCHAR*>(TEXT("invalid JSON must never be parsed")));
  TestEqual(TEXT("Rejected registration handle freed without compiling metadata"),Frees.FindRef(RejectedRegistration),1);
 };
 B->UnloadAssembly();TestTrue(TEXT("Type invalidated"),Type->IsNull());TestTrue(TEXT("Method invalidated"),First->IsNull());TestTrue(TEXT("Other object invalidated"),Other->IsNull());TestEqual(TEXT("No owner records after unload"),FCSManagedHandleTestAccess::Owners(*Manager),0);TestEqual(TEXT("No metadata after unload"),FCSManagedHandleTestAccess::Metadata(*B),0);
 for(int32 Cycle=0;Cycle<5;++Cycle)
 {
  auto Reloaded=Handle();FCSManagedHandleTestAccess::Initialize(*B,Reloaded);auto NewType=B->AddTypeHandle(Field,reinterpret_cast<uint8*>(++Next));auto NewMethod=B->GetManagedMethod(NewType,TEXT("Invoke_Test"));
  B->UnloadAssembly();TestTrue(TEXT("Reload generation method invalidated"),NewMethod->IsNull());
 }
 TestEqual(TEXT("One lookup per assembly generation"),Lookups,6);
 return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSManagerRootOwnershipTest,"UnrealSharp.Handles.NativeRootOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCSManagerRootOwnershipTest::RunTest(const FString&)
{
 TStrongObjectPtr<UCSManager> Manager(NewObject<UCSManager>());
 TStrongObjectPtr<UCSManagedAssembly> Owned(NewObject<UCSManagedAssembly>()),External(NewObject<UCSManagedAssembly>());
 FCSManagedHandleTestAccess::Protect(*Manager,Owned.Get());
 TestFalse(TEXT("Ordinary Editor manager does not root its child"),Owned->IsRooted());
 External->AddToRoot();FCSManagedHandleTestAccess::Protect(*Manager,External.Get());
 TestEqual(TEXT("External root is not claimed"),FCSManagedHandleTestAccess::Roots(*Manager),0);
 FCSManagedHandleTestAccess::OwnRoot(*Manager,Owned.Get());FCSManagedHandleTestAccess::RegisterAssembly(*Manager,Owned.Get());
 FCSManagedHandleTestAccess::Remove(*Manager,Owned.Get());
 TestFalse(TEXT("Real native removal releases owned root"),Owned->IsRooted());
 TestEqual(TEXT("Native removal clears secondary cache"),FCSManagedHandleTestAccess::CachedAssemblies(*Manager),0);
 FCSManagedHandleTestAccess::Remove(*Manager,Owned.Get());
 FCSManagedHandleTestAccess::OwnRoot(*Manager,Owned.Get());FCSManagedHandleTestAccess::RegisterAssembly(*Manager,Owned.Get());
 FCSManagedHandleTestAccess::Shutdown(*Manager);FCSManagedHandleTestAccess::Shutdown(*Manager);
 TestFalse(TEXT("Ownership shutdown unroots once"),Owned->IsRooted());
 TestTrue(TEXT("Ownership shutdown preserves external root"),External->IsRooted());
 TestEqual(TEXT("No owned registrations survive shutdown"),FCSManagedHandleTestAccess::Roots(*Manager),0);
 TestFalse(TEXT("Shutdown prevents registry resurrection"),Manager->LoadAssemblyByPath(TEXT("not-a-path")) != nullptr);
 External->RemoveFromRoot();return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSReentrantLoadStateTest,"UnrealSharp.Handles.ReentrantLoadState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCSReentrantLoadStateTest::RunTest(const FString&)
{
 TStrongObjectPtr<UCSManager> Manager(NewObject<UCSManager>());
 TStrongObjectPtr<UCSManagedAssembly> Assembly(NewObject<UCSManagedAssembly>(Manager.Get(),TEXT("ReentrantLoadState")));
 TSharedPtr<FGCHandle> Owner=MakeShared<FGCHandle>(reinterpret_cast<uint8*>(1));
 FCSManagedHandleTestAccess::Initialize(*Assembly,Owner);FCSManagedHandleTestAccess::RegisterAssembly(*Manager,Assembly.Get());
 const FString Path=Assembly->GetName()+TEXT(".dll");
 FCSManagedHandleTestAccess::Loading(*Assembly,true);
 TestTrue(TEXT("Populated assembly cell reports loaded while compilation is still loading"),Assembly->IsAssemblyLoaded());
 TestFalse(TEXT("Direct load rejects loading even after the cell is assigned"),Assembly->LoadAssembly());
 TestNull(TEXT("Manager load rejects loading before its loaded fast path"),Manager->LoadAssemblyByPath(Path));
 TestTrue(TEXT("Rejected loads preserve the loading state"),Assembly->IsAssemblyLoading());
 TestTrue(TEXT("Rejected loads preserve the owner cell"),Assembly->GetAssemblyHandle().Get()==Owner.Get());
 TestTrue(TEXT("Rejected loads preserve the owner handle"),Owner->GetPointer()==reinterpret_cast<uint8*>(1));
 TestTrue(TEXT("Rejected loads preserve the registry entry"),Manager->FindAssembly(Assembly->GetFName())==Assembly.Get());
 FCSManagedHandleTestAccess::Loading(*Assembly,false);
 TestTrue(TEXT("Completed direct load still uses its loaded fast path"),Assembly->LoadAssembly());
 TestTrue(TEXT("Completed manager load returns the existing assembly"),Manager->LoadAssemblyByPath(Path)==Assembly.Get());
 Owner->Invalidate();FCSManagedHandleTestAccess::Remove(*Manager,Assembly.Get());
 return true;
}
#endif
