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
 int32 Lookups=0,Creations=0;
 UPTRINT Next=1000;
 void __stdcall Dispose(FGCHandleIntPtr Handle,FGCHandleIntPtr Assembly)
 {
  ++Frees.FindOrAdd(Handle.ManagedHandlePtr);Owners.Add(Handle.ManagedHandlePtr,Assembly.ManagedHandlePtr);
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
  ~FCallbacksScope(){Reentrant.Reset();GetManagedCallbacks()=Saved;GetManagedPluginCallbacks()=Plugins;FCSManagedHandleTestAccess::Instance()=Manager;}
 };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSManagedHandleLifecycleTest,"UnrealSharp.Handles.Ownership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCSManagedHandleLifecycleTest::RunTest(const FString&)
{
 TStrongObjectPtr<UCSManager> Manager(NewObject<UCSManager>());FCallbacksScope Scope(Manager.Get());
 TStrongObjectPtr<UCSManagedAssembly> A(NewObject<UCSManagedAssembly>()),B(NewObject<UCSManagedAssembly>());
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
