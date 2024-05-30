//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "GMPCore.h"
#include "HAL/IConsoleManager.h"

#ifndef GMP_EXTEND_CONSOLE
#define GMP_EXTEND_CONSOLE 0
#endif

#if GMP_EXTEND_CONSOLE
#include "GMP/GMPJsonSerializer.h"

namespace GMP {namespace Serializer
{
template<typename T>
struct TParameterSerializer<T, std::enable_if_t<GMP::Class2Prop::TClassToPropTag<std::decay_t<T>>::value>>
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
		ParameterSerializeImpl(Str, Data);
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
}}  // namespace GMP::Serializer

/** Console variable delegate type  This is a void callback function. */
DECLARE_DELEGATE_OneParam(FXConsoleVariableDelegate, IConsoleVariable*);

/** Console variable multicast delegate type. */
DECLARE_MULTICAST_DELEGATE_OneParam(FXConsoleVariableMulticastDelegate, IConsoleVariable*);

/** Console command delegate type (takes no arguments.)  This is a void callback function. */
DECLARE_DELEGATE(FXConsoleCommandDelegate);

/** Console command delegate type (with arguments.)  This is a void callback function that always takes a list of arguments. */
DECLARE_DELEGATE_OneParam(FXConsoleCommandWithArgsDelegate, const TArray<FString>&);

/** Console command delegate type with a world argument. This is a void callback function that always takes a world. */
DECLARE_DELEGATE_OneParam(FXConsoleCommandWithWorldDelegate, UWorld*);

/** Console command delegate type (with a world and arguments.)  This is a void callback function that always takes a list of arguments and a world. */
DECLARE_DELEGATE_TwoParams(FXConsoleCommandWithWorldAndArgsDelegate, const TArray<FString>&, UWorld*);

/** Console command delegate type (with arguments and output device.)  This is a void callback function that always takes a list of arguments and output device. */
DECLARE_DELEGATE_TwoParams(FXConsoleCommandWithArgsAndOutputDeviceDelegate, const TArray<FString>&, FOutputDevice&);

/** Console command delegate type (with a world arguments and output device.)  This is a void callback function that always takes a list of arguments, a world and output device. */
DECLARE_DELEGATE_ThreeParams(FXConsoleCommandWithWorldArgsAndOutputDeviceDelegate, const TArray<FString>&, UWorld*, FOutputDevice&);
using FXConsoleFullCmdDelegate = FXConsoleCommandWithWorldArgsAndOutputDeviceDelegate;

/** Console command delegate type with the output device passed through. */
DECLARE_DELEGATE_OneParam(FXConsoleCommandWithOutputDeviceDelegate, FOutputDevice&);

class IXConsoleManager : public IConsoleManager
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

	static GMP_API void PauseXConsoleCommandPipeline(UWorld* InWorld, const TCHAR* Reason = nullptr);
	static GMP_API void ContineXConsoleCommandPipeline(UWorld* InWorld, const TCHAR* Reason = nullptr);

	static GMP_API int32 CommandPipelineInteger();
	static GMP_API int32 CommandPipelineInteger(int32 InVal);

	static GMP_API const FString& CommandPipelineString();
	static GMP_API void CommandPipelineString(const FString& InStr);

private:
	virtual IConsoleVariable* RegisterXConsoleVariable(const TCHAR* Name, const TCHAR* Help, uint32 Flags, const FProperty* InProp, void* Addr, bool bValueRef) = 0;
	virtual IConsoleCommand* RegisterXConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FXConsoleCommandWithArgsDelegate& Command, uint32 Flags) = 0;
};

//  args of Lambda must end wtih , UWorld* InWorld, FOutputDevice& Ar)
class FXConsoleCommandLambdaFull : private FAutoConsoleObject
{
public:
	template<typename F>
	FXConsoleCommandLambdaFull(const TCHAR* Name, const TCHAR* Help, F&& Lambda, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(
			IXConsoleManager::Get().RegisterXConsoleCommand(Name,
															Help,
															FXConsoleFullCmdDelegate::CreateLambda([Lambda](const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& Ar) { GMP::Serializer::SerializedInvoke(Args, Lambda, InWorld, Ar); }),
															Flags))
	{
	}
};

class FXConsoleCommandLambdaLite : private FAutoConsoleObject
{
public:
	template<typename F>
	FXConsoleCommandLambdaLite(const TCHAR* Name, const TCHAR* Help, F&& Lambda, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(
			IXConsoleManager::Get().RegisterXConsoleCommand(Name,
															Help,
															FXConsoleFullCmdDelegate::CreateLambda([Lambda](const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& Ar) { GMP::Serializer::SerializedInvoke(Args, Lambda); }),
															Flags))
	{
	}
};

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

class FXConsoleCommandLambdaControl : private FAutoConsoleObject
{
public:
	template<typename F>
	FXConsoleCommandLambdaControl(const TCHAR* Name, const TCHAR* Help, F&& Lambda, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IXConsoleManager::Get().RegisterXConsoleCommand(
			Name,
			Help,
			FXConsoleFullCmdDelegate::CreateLambda([Lambda](const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& Ar) { GMP::Serializer::SerializedInvoke(Args, Lambda, FXConsoleController(InWorld, Ar)); }),
			Flags))
	{
	}
};

#if !NO_CVARS
using FXConsoleVariable = FAutoConsoleVariable;
using FXConsoleVariableRef = FAutoConsoleVariableRef;

using FXConsoleCommandWithWorldAndArgs = FAutoConsoleCommandWithWorldAndArgs;
using FXConsoleCommandWithWorld = FAutoConsoleCommandWithWorld;
using FXConsoleCommandWithOutputDevice = FAutoConsoleCommandWithOutputDevice;
#if UE_5_00_OR_LATER
using FXConsoleCommandWithArgsAndOutputDevice = FAutoConsoleCommandWithArgsAndOutputDevice;
#endif
using FXConsoleCommandWithWorldArgsAndOutputDevice = FAutoConsoleCommandWithWorldArgsAndOutputDevice;

#else

/**
 * Autoregistering float, int or string console variable
 */
class FXConsoleVariable : private FAutoConsoleObject
{
public:
	template<typename T>
	FXConsoleVariable(const TCHAR* Name, T DefaultValue, const TCHAR* Help, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IXConsoleManager::Get().RegisterXConsoleVariable(Name, DefaultValue, Help, Flags))
	{
	}

	template<typename T>
	FXConsoleVariable(const TCHAR* Name, T DefaultValue, const TCHAR* Help, const FXConsoleVariableDelegate& Callback, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IXConsoleManager::Get().RegisterXConsoleVariable(Name, DefaultValue, Help, Flags))
	{
		AsVariable()->SetOnChangedCallback(Callback);
	}

	/** Dereference back to a console variable**/
	FORCEINLINE IConsoleVariable& operator*() { return *AsVariable(); }
	FORCEINLINE const IConsoleVariable& operator*() const { return *AsVariable(); }
	/** Dereference back to a console variable**/
	FORCEINLINE IConsoleVariable* operator->() { return AsVariable(); }
	FORCEINLINE const IConsoleVariable* operator->() const { return AsVariable(); }
};
/**
 * Autoregistering float, int, bool, FString REF variable class...this changes that value when the console variable is changed. 
 */
class FXConsoleVariableRef : private FAutoConsoleObject
{
public:
	template<typename T>
	FXConsoleVariableRef(const TCHAR* Name, T& RefValue, const TCHAR* Help, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IConsoleManager::Get().RegisterXConsoleVariableRef(Name, RefValue, Help, Flags))
	{
	}

	template<typename T>
	FXConsoleVariableRef(const TCHAR* Name, T& RefValue, const TCHAR* Help, const FConsoleVariableDelegate& Callback, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IConsoleManager::Get().RegisterXConsoleVariableRef(Name, RefValue, Help, Flags))
	{
		AsVariable()->SetOnChangedCallback(Callback);
	}

	/** Dereference back to a variable**/
	FORCEINLINE IConsoleVariable& operator*() { return *AsVariable(); }
	FORCEINLINE const IConsoleVariable& operator*() const { return *AsVariable(); }
	/** Dereference back to a variable**/
	FORCEINLINE IConsoleVariable* operator->() { return AsVariable(); }
	FORCEINLINE const IConsoleVariable* operator->() const { return AsVariable(); }
};

class FXConsoleCommandWithWorld : private FAutoConsoleObject
{
public:
	FXConsoleCommandWithWorld(const TCHAR* Name, const TCHAR* Help, const FXConsoleCommandWithWorldDelegate& Command, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(
			IXConsoleManager::Get().RegisterXConsoleCommand(Name, Help, FXConsoleFullCmdDelegate::CreateLambda([Command](const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& OutDevice) { Command.ExecuteIfBound(InWorld); }), Flags))
	{
	}
};

class FXConsoleCommandWithWorldAndArgs : private FAutoConsoleObject
{
public:
	FXConsoleCommandWithWorldAndArgs(const TCHAR* Name, const TCHAR* Help, const FXConsoleCommandWithWorldAndArgsDelegate& Command, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(
			IXConsoleManager::Get().RegisterXConsoleCommand(Name,
															Help,
															FXConsoleFullCmdDelegate::CreateLambda([Command](const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& OutDevice) { Command.ExecuteIfBound(Args, InWorld); }),
															Flags))
	{
	}
};

#if UE_5_00_OR_LATER
class FAutoConsoleCommandWithArgsAndOutputDevice : private FAutoConsoleObject
{
public:
	FAutoConsoleCommandWithArgsAndOutputDevice(const TCHAR* Name, const TCHAR* Help, const FXConsoleCommandWithArgsAndOutputDeviceDelegate& Command, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IXConsoleManager::Get().RegisterConsoleCommand(Name, Help, Command, Flags))
	{
	}
};
#endif

class FXConsoleCommandWithOutputDevice : private FAutoConsoleObject
{
public:
	FXConsoleCommandWithOutputDevice(const TCHAR* Name, const TCHAR* Help, const FXConsoleCommandWithOutputDeviceDelegate& Command, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IXConsoleManager::Get().RegisterXConsoleCommand(Name,
																			 Help,
																			 FXConsoleFullCmdDelegate::CreateLambda([Command](const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& OutDevice) { Command.ExecuteIfBound(OutDevice); }),
																			 Flags))
	{
	}
};

class FXConsoleCommandWithWorldArgsAndOutputDevice : private FAutoConsoleObject
{
public:
	FXConsoleCommandWithWorldArgsAndOutputDevice(const TCHAR* Name, const TCHAR* Help, const FXConsoleFullCmdDelegate& Command, uint32 Flags = ECVF_Default)
		: FAutoConsoleObject(IXConsoleManager::Get().RegisterXConsoleCommand(Name, Help, Command, Flags))
	{
	}
};

#endif
#else
class FXConsoleCommandLambdaDummy
{
public:
	template<typename F>
	FXConsoleCommandLambdaDummy(const TCHAR* Name, const TCHAR* Help, F&& Lambda, uint32 Flags = ECVF_Default)
	{
	}
};
using FXConsoleCommandLambdaLite = FXConsoleCommandLambdaDummy;
using FXConsoleCommandLambdaFull = FXConsoleCommandLambdaDummy;
using FXConsoleCommandLambdaControl = FXConsoleCommandLambdaDummy;

#endif
