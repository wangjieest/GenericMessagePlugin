//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "GMPCore.h"
#include "HAL/IConsoleManager.h"

#ifndef GMP_EXTEND_CONSOLE
#define GMP_EXTEND_CONSOLE 1
#endif

#if GMP_EXTEND_CONSOLE
#include "GMP/GMPJsonSerializer.h"

namespace GMP
{
namespace Serializer
{
	template<typename T>
	struct TParameterSerializer<T, std::enable_if_t<!TIsTOptional_V<T> && GMP::Class2Prop::TClassToPropTag<std::decay_t<T>>::value>>
	{
		template<bool bTryFile = true>
		static void ParameterSerialize(const FString& Str, T& Data)
		{
			if (Str.IsEmpty())
				return;
			GMP_IF_CONSTEXPR(bTryFile)
			{
				if (Str.StartsWith(TEXT("@")))
				{
					// test file
					FString Buffer;
					if (ensure(FFileHelper::LoadFileToString(Buffer, &Str[1])))
					{
						ParameterSerializeImpl(Buffer, Data);
						return;
					}
				}
			}
			FString TrimStr = Str.TrimStartAndEnd().TrimChar('"');
			ParameterSerializeImpl(TrimStr, Data);
		}

	protected:
		static void ParameterSerializeImpl(const FString& Str, T& Data)
		{
			for (auto i = 0; i < Str.Len(); ++i)
			{
				if (IsSpaces(Str[i]))
					continue;

				if ((Str[i] != TEXT('{') && Str[i] != TEXT('[')) || !ensure(Json::FromJson(Str, Data)))
				{
#if UE_5_02_OR_LATER
					TClass2Prop<T>::GetProperty()->ImportText_Direct(&Str[i], std::addressof(Data), nullptr, 0);
#else
					TClass2Prop<T>::GetProperty()->ImportText(&Str[i], std::addressof(Data), 0, nullptr);
#endif
				}
				break;
			}
		}
	};
}  // namespace Serializer
}  // namespace GMP

DECLARE_DELEGATE_ThreeParams(FXConsoleCommandWithWorldArgsAndOutputDeviceDelegate, const TArray<FString>&, UWorld*, FOutputDevice&);
using FXConsoleFullCmdDelegate = FXConsoleCommandWithWorldArgsAndOutputDeviceDelegate;

class IXConsoleManager 
#if !NO_CVARS
: public IConsoleManager
#endif
{
public:
	static GMP_API IXConsoleManager& Get();

	template<typename T>
	std::enable_if_t<std::is_same<T, bool>::value || std::is_same<T, int32>::value || std::is_same<T, float>::value || std::is_same<T, FString>::value, IConsoleVariable*>  //
		RegisterXConsoleVariableRef(const TCHAR* Name, T& RefValue, const TCHAR* Help, uint32 Flags)
	{
		return RegisterXConsoleVariable(Name, Help, Flags, TGMPClass2Prop<T>::GetProperty(), &RefValue, true);
	}

	template<typename T>
	std::enable_if_t<std::is_same<T, bool>::value || std::is_same<T, int32>::value || std::is_same<T, float>::value || std::is_same<T, FString>::value, IConsoleVariable*>  //
		RegisterXConsoleVariable(const TCHAR* Name, const T& DefalutValue, const TCHAR* Help, uint32 Flags)
	{
		return RegisterXConsoleVariable(Name, Help, Flags, TGMPClass2Prop<T>::GetProperty(), const_cast<T*>(&DefalutValue), false);
	}

public:
	virtual IConsoleCommand* RegisterXConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FXConsoleFullCmdDelegate& Command, uint32 Flags) = 0;

	template<int32 Ellipsis, typename F>
	IConsoleCommand* RegisterXConsoleCommandEx(const TCHAR* Name, const TCHAR* Help, F&& Lambda, uint32 Flags)
	{
		using TupleType = GMP::TypeTraits::TSigTupleType<F>;
		return RegisterXConsoleCommandEx(MakeStaticNames<TupleType, Ellipsis>(),
										 Name,
										 Help,
										 FXConsoleFullCmdDelegate::CreateLambda([Lambda{MoveTemp(Lambda)}, Name](const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& Ar) {
#if !UE_BUILD_SHIPPING
											 Ar.Logf(TEXT("FXConsoleCommandLambdaExecute %s(%s)"), Name, *FString::Join(Args, TEXT(",")));
#endif
											 if constexpr (Ellipsis == 0)
											 {
												 GMP::Serializer::SerializedInvoke(Args, Lambda);
											 }
											 else if constexpr (Ellipsis == 1)

											 {
												 GMP::Serializer::SerializedInvoke(Args, Lambda, InWorld);
											 }
											 else
											 {
												 GMP::Serializer::SerializedInvoke(Args, Lambda, InWorld, Ar);
											 }
										 }),
										 Flags);
	}

	static GMP_API void PauseXConsoleCommandPipeline(UWorld* InWorld, const TCHAR* Reason = nullptr);
	static GMP_API void ContinueXConsoleCommandPipeline(UWorld* InWorld, const TCHAR* Reason = nullptr);

	static GMP_API int32 CommandPipelineInteger();
	static GMP_API int32 CommandPipelineInteger(int32 InVal);

	static GMP_API const FString& CommandPipelineString();
	static GMP_API void CommandPipelineString(const FString& InStr);

	virtual const GMP::FArrayTypeNames* GetXConsoleCommandProps(const TCHAR* Name) const = 0;
	virtual TArray<FString> GetXConsoleCommandList() const = 0;

	template<typename Tup, int32 Ellipsis>
	static decltype(auto) MakeStaticNames()
	{
		return GMP::FMessageBody::MakeStaticNames((Tup*)nullptr, std::make_index_sequence<std::tuple_size<Tup>::value - Ellipsis>());
	}

private:
	virtual IConsoleVariable* RegisterXConsoleVariable(const TCHAR* Name, const TCHAR* Help, uint32 Flags, const FProperty* InProp, void* Addr, bool bValueRef) = 0;
	virtual IConsoleCommand* RegisterXConsoleCommandEx(const GMP::FArrayTypeNames& Names, const TCHAR* Name, const TCHAR* Help, const FXConsoleFullCmdDelegate& Command, uint32 Flags) = 0;
	friend class FXConsoleCommandLambdaControl;
};

class FXAutoConsoleObject
{
protected:
	FXAutoConsoleObject(IConsoleObject* InTarget)
		: Target(InTarget)
	{
		check(Target);
	}

public:
	virtual ~FXAutoConsoleObject() { IXConsoleManager::Get().UnregisterConsoleObject(Target); }

private:
	IConsoleObject* Target;
};

//  args of Lambda must end wtih [, UWorld* InWorld, FOutputDevice& Ar]
class FXConsoleCommandLambdaFull : private FAutoConsoleObject
{
public:
	template<typename F>
	FXConsoleCommandLambdaFull(const TCHAR* Name, const TCHAR* Help, F&& Lambda, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IXConsoleManager::Get().RegisterXConsoleCommandEx<2>(Name, Help, Forward<F>(Lambda), Flags))
	{
	}
};

//  args of Lambda must end wtih [, UWorld* InWorld]
class FXConsoleCommandLambda : private FAutoConsoleObject
{
public:
	template<typename F>
	FXConsoleCommandLambda(const TCHAR* Name, F&& Lambda, const TCHAR* Help = TEXT(""), uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IXConsoleManager::Get().RegisterXConsoleCommandEx<1>(Name, Help, Forward<F>(Lambda), Flags))
	{
	}
};

class FXConsoleCommandLambdaLite : private FAutoConsoleObject
{
public:
	template<typename F>
	FXConsoleCommandLambdaLite(const TCHAR* Name, const TCHAR* Help, F&& Lambda, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IXConsoleManager::Get().RegisterXConsoleCommandEx<0>(Name, Help, Forward<F>(Lambda), Flags))
	{
	}
};

class FXConsoleCommandLambdaControl : private FAutoConsoleObject
{
public:
	struct FXConsoleController
	{
		FXConsoleController(UWorld* InWorld, FOutputDevice& InAr)
			: World(InWorld)
			, Ar(InAr)
		{
		}
		~FXConsoleController() {}

		GMP_API void PauseXConsolePipeline(const TCHAR* Reason = nullptr);
		GMP_API void ContinueXConsolePipeline(const TCHAR* Reason = nullptr);

		template<typename FmtType, typename... Types>
		FORCEINLINE void CategorizedLogf(const FName& Category, ELogVerbosity::Type Verbosity, const FmtType& Fmt, Types... Args)
		{
			Ar.CategorizedLogf(Category, Verbosity, Fmt, Args...);
		}
		template<typename FmtType, typename... Types>
		FORCEINLINE void Logf(const FmtType& Fmt, Types... Args)
		{
			Ar.Logf(Fmt, Args...);
		}

		UWorld* GetWorld() const { return World.Get(); }

	protected:
		TWeakObjectPtr<UWorld> World;
		FOutputDevice& Ar;
	};

	template<typename F>
	FXConsoleCommandLambdaControl(const TCHAR* Name, const TCHAR* Help, F&& Lambda, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IXConsoleManager::Get().RegisterXConsoleCommandEx(IXConsoleManager::MakeStaticNames<GMP::TypeTraits::TSigTupleType<F>, 1>(),
																			   Name,
																			   Help,
																			   FXConsoleFullCmdDelegate::CreateLambda([Lambda, Name](const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& Ar) {
#if !UE_BUILD_SHIPPING
																				   Ar.Logf(TEXT("FXConsoleCommandLambdaControl %s(%s)"), Name, *FString::Join(Args, TEXT(",")));
#endif
																				   GMP::Serializer::SerializedInvoke(Args, Lambda, FXConsoleController(InWorld, Ar));
																			   }),
																			   Flags))
	{
	}
};
#else
class FXConsoleCommandLambdaDummy
{
public:
	template<typename... Ts>
	FXConsoleCommandLambdaDummy(Ts...)
	{
	}
};
using FXConsoleCommandLambdaLite = FXConsoleCommandLambdaDummy;
using FXConsoleCommandLambda = FXConsoleCommandLambdaDummy;
using FXConsoleCommandLambdaFull = FXConsoleCommandLambdaDummy;
using FXConsoleCommandLambdaControl = FXConsoleCommandLambdaDummy;
#endif
