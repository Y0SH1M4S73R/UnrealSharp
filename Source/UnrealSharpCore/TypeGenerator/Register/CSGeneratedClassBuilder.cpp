﻿#include "CSGeneratedClassBuilder.h"
#include "CSGeneratedInterfaceBuilder.h"
#include "CSSimpleConstructionScriptBuilder.h"
#include "CSTypeRegistry.h"
#include "UnrealSharpCore/UnrealSharpCore.h"
#include "UnrealSharpCore/TypeGenerator/CSBlueprint.h"
#include "UObject/UnrealType.h"
#include "Engine/Blueprint.h"
#include "UnrealSharpCore/TypeGenerator/CSClass.h"
#include "UnrealSharpCore/TypeGenerator/Factories/CSFunctionFactory.h"
#include "UnrealSharpCore/TypeGenerator/Factories/CSPropertyFactory.h"

void FCSGeneratedClassBuilder::StartBuildingType()
{
	UClass* SuperClass = FCSTypeRegistry::GetClassFromName(TypeMetaData->ParentClass.Name);
	TSharedPtr<FCSharpClassInfo> ClassInfo = FCSTypeRegistry::GetClassInfoFromName(TypeMetaData->Name);
	
	Field->SetClassMetaData(ClassInfo);
	Field->SetSuperStruct(SuperClass);
	
#if WITH_EDITOR
	CreateClassEditor(SuperClass);
#else
	CreateClass(SuperClass);
#endif
}

#if WITH_EDITOR
void FCSGeneratedClassBuilder::CreateClassEditor(UClass* SuperClass)
{
	UBlueprint* Blueprint = static_cast<UBlueprint*>(Field->ClassGeneratedBy);
	if (!Blueprint)
	{
		UPackage* Package = UCSManager::Get().GetUnrealSharpPackage();
		FString BlueprintName = TypeMetaData->Name.ToString();
		Blueprint = NewObject<UCSBlueprint>(Package, *BlueprintName, RF_Public | RF_Standalone);
		Blueprint->GeneratedClass = Field;
		Blueprint->ParentClass = SuperClass;
		Field->ClassGeneratedBy = Blueprint;
	}
	else
	{
		Blueprint->CachedDependents.Empty();
		Blueprint->CachedDependencies.Empty();
		Blueprint->CachedUDSDependencies.Empty();
	}

	FCSTypeRegistry::Get().GetOnNewClassEvent().Broadcast(Field);
}
#endif

void FCSGeneratedClassBuilder::CreateClass(UClass* SuperClass)
{
	Field->ClassFlags = TypeMetaData->ClassFlags | SuperClass->ClassFlags & CLASS_ScriptInherit;

	Field->PropertyLink = SuperClass->PropertyLink;
	Field->ClassWithin = SuperClass->ClassWithin;
	Field->ClassCastFlags = SuperClass->ClassCastFlags;

	SetConfigName(Field, TypeMetaData);

	// Implement all Blueprint interfaces declared
	ImplementInterfaces(Field, TypeMetaData->Interfaces);

	// Generate properties for this class
	FCSPropertyFactory::CreateAndAssignProperties(Field, TypeMetaData->Properties);

	// Build the construction script that will spawn the components
	FCSSimpleConstructionScriptBuilder::BuildSimpleConstructionScript(Field, &Field->SimpleConstructionScript, TypeMetaData->Properties);

	// Generate functions for this class
	FCSFunctionFactory::GenerateVirtualFunctions(Field, TypeMetaData);
	FCSFunctionFactory::GenerateFunctions(Field, TypeMetaData->Functions);

	//Finalize class
	Field->ClassConstructor = &FCSGeneratedClassBuilder::ManagedObjectConstructor;

	Field->Bind();
	Field->StaticLink(true);
	Field->AssembleReferenceTokenStream();

	//Create the default object for this class
	UObject* DefaultObject = Field->GetDefaultObject();
	SetupDefaultTickSettings(DefaultObject);

	Field->SetUpRuntimeReplicationData();
	Field->UpdateCustomPropertyListForPostConstruction();

	RegisterFieldToLoader(ENotifyRegistrationType::NRT_Class);
	TryRegisterSubsystem(Field);
}

FName FCSGeneratedClassBuilder::GetFieldName() const
{
	FString FieldName = FString::Printf(TEXT("%s_C"), *TypeMetaData->Name.ToString());
	return *FieldName;
}

void FCSGeneratedClassBuilder::ManagedObjectConstructor(const FObjectInitializer& ObjectInitializer)
{
	UCSClass* ManagedClass = GetFirstManagedClass(ObjectInitializer.GetClass());
	TSharedPtr<const FCSharpClassInfo> ClassInfo = ManagedClass->GetClassInfo();

	//Execute the native class' constructor first.
	UClass* NativeClass = GetFirstNativeClass(ObjectInitializer.GetClass());
	NativeClass->ClassConstructor(ObjectInitializer);

	// Initialize properties that are not zero initialized such as FText.
	for (TFieldIterator<FProperty> PropertyIt(ManagedClass); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!IsManagedType(Property->GetOwnerClass()))
		{
			break;
		}

		if (Property->HasAllPropertyFlags(CPF_ZeroConstructor))
		{
			continue;
		}

		Property->InitializeValue_InContainer(ObjectInitializer.GetObj());
	}

	UCSManager::Get().CreateNewManagedObject(ObjectInitializer.GetObj(), ClassInfo->TypeHandle);
}

void FCSGeneratedClassBuilder::SetupDefaultTickSettings(UObject* DefaultObject) const
{
	if (AActor* Actor = Cast<AActor>(DefaultObject))
	{
		Actor->PrimaryActorTick.bCanEverTick = Field->CanTick();
		Actor->PrimaryActorTick.bStartWithTickEnabled = Field->CanTick();
	}
	else if (UActorComponent* ActorComponent = Cast<UActorComponent>(DefaultObject))
	{
		ActorComponent->PrimaryComponentTick.bCanEverTick = Field->CanTick();
		ActorComponent->PrimaryComponentTick.bStartWithTickEnabled = Field->CanTick();
	}
}

void FCSGeneratedClassBuilder::ImplementInterfaces(UClass* ManagedClass, const TArray<FName>& Interfaces)
{
	for (const FName& InterfaceName : Interfaces)
	{
		UClass* InterfaceClass = FCSTypeRegistry::GetInterfaceFromName(InterfaceName);

		if (!IsValid(InterfaceClass))
		{
			UE_LOG(LogUnrealSharp, Warning, TEXT("Can't find interface: %s"), *InterfaceName.ToString());
			continue;
		}

		FImplementedInterface ImplementedInterface;
		ImplementedInterface.Class = InterfaceClass;
		ImplementedInterface.bImplementedByK2 = true;
		ImplementedInterface.PointerOffset = 0;
		
		ManagedClass->Interfaces.Add(ImplementedInterface);
	}
}

void FCSGeneratedClassBuilder::TryRegisterSubsystem(UClass* ManagedClass)
{
	if (ManagedClass->IsChildOf<UEngineSubsystem>()
	#if WITH_EDITOR
	|| ManagedClass->IsChildOf<UEditorSubsystem>()
	#endif
	)
	{
		FSubsystemCollectionBase::ActivateExternalSubsystem(ManagedClass);
	}
}

void FCSGeneratedClassBuilder::SetConfigName(UClass* ManagedClass, const TSharedPtr<const FCSClassMetaData>& TypeMetaData)
{
	if (TypeMetaData->ClassConfigName.IsNone())
	{
		ManagedClass->ClassConfigName = ManagedClass->GetSuperClass()->ClassConfigName;
	}
	else
	{
		ManagedClass->ClassConfigName = TypeMetaData->ClassConfigName;
	}
}

UCSClass* FCSGeneratedClassBuilder::GetFirstManagedClass(UClass* Class)
{
	while (Class && !IsManagedType(Class))
	{
		Class = Class->GetSuperClass();
	}
	return Cast<UCSClass>(Class);
}

UClass* FCSGeneratedClassBuilder::GetFirstNativeClass(UClass* Class)
{
	while (!Class->HasAnyClassFlags(CLASS_Native) || IsManagedType(Class))
	{
		Class = Class->GetSuperClass();
	}
	return Class;
}

UClass* FCSGeneratedClassBuilder::GetFirstNonBlueprintClass(UClass* Class)
{
	while (Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		Class = Class->GetSuperClass();
	}
	return Class;
}

bool FCSGeneratedClassBuilder::IsManagedType(const UClass* Class)
{
	return Class->GetClass() == UCSClass::StaticClass();
}
