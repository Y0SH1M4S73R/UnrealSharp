﻿#pragma once

#include "CSDeveloperSettings.h"
#include "CSMetaDataUtils.h"
#include "UObject/Class.h"
#include "UObject/Field.h"
#include "UnrealSharpCore/CSManager.h"

template<typename TMetaData, class TField>
class TCSGeneratedTypeBuilder
{
public:
	
	virtual ~TCSGeneratedTypeBuilder() = default;
	
	TCSGeneratedTypeBuilder(TSharedPtr<TMetaData> InTypeMetaData) : TypeMetaData(InTypeMetaData), Field(nullptr)
	{
	
	}

	TField* CreateType()
	{
		UPackage* Package = UCSManager::Get().GetUnrealSharpPackage();
		FName FieldName = GetFieldName();

#if WITH_EDITOR
		Field = FindObject<TField>(Package, *FieldName.ToString());
		if (!Field)
#endif
		{
			Field = NewObject<TField>(Package, TField::StaticClass(), FieldName, RF_Public | RF_Standalone);
		}

#if WITH_EDITOR
		FCSMetaDataUtils::ApplyMetaData(TypeMetaData->MetaData, Field);
		ApplyDisplayName();
#endif
		
		return Field;
	}

	// Start TCSGeneratedTypeBuilder interface
	virtual void StartBuildingType() = 0;
	virtual FName GetFieldName() const { return TypeMetaData->Name; }
	// End of interface

	void RegisterFieldToLoader(ENotifyRegistrationType RegistrationType)
	{
		UPackage* Package = UCSManager::Get().GetUnrealSharpPackage();
		
		NotifyRegistrationEvent(*Package->GetName(),
		*Field->GetName(),
		RegistrationType,
		ENotifyRegistrationPhase::NRP_Finished,
		nullptr,
		false,
		Field);
	}

protected:
	
	TSharedPtr<const TMetaData> TypeMetaData;
	TField* Field;

private:

	void ApplyDisplayName()
	{
#if WITH_EDITOR
		static FString DisplayNameKey = TEXT("DisplayName");
		if (!Field->HasMetaData(*DisplayNameKey))
		{
			Field->SetMetaData(*DisplayNameKey, *TypeMetaData->Name.ToString());
		}
		
		if (GetDefault<UCSDeveloperSettings>()->bSuffixGeneratedTypes)
		{
			FString DisplayName = Field->GetMetaData(*DisplayNameKey);
			DisplayName += TEXT(" (C#)");
			Field->SetMetaData(*DisplayNameKey, *DisplayName);
		}
#endif
	}
};

