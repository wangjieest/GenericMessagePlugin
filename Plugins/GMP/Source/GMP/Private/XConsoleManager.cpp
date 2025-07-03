//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "XConsoleManager.h"
#include "XConsolePythonSupport.h"

DEFINE_LOG_CATEGORY_STATIC(LogXConsoleManager, Log, All);

#if GMP_EXTEND_CONSOLE
#include "Engine/Engine.h"
#include "GMPWorldLocals.h"
#include "HAL/ConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AsciiSet.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/RemoteConfigIni.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Stats/Stats.h"
#include "GameFramework/PlayerController.h"

#if defined(HTTPSERVER_API) && defined(GMP_HTTPSERVER)
#include "Runtime/Online/HTTPServer/Public/HttpRequestHandler.h"
#include "Runtime/Online/HTTPServer/Public/HttpServerModule.h"
#include "Runtime/Online/HTTPServer/Public/HttpServerResponse.h"
#include "Runtime/Online/HTTPServer/Public/IHttpRouter.h"
#include "Misc/DelayedAutoRegister.h"
#endif

namespace GMPConsoleManger
{
template<typename Builder>
static Builder& AppendEscapeJsonString(Builder& AppendTo, const FString& StringVal)
{
	AppendTo += TEXT("\"");
	for (const TCHAR* Char = *StringVal; *Char != TCHAR('\0'); ++Char)
	{
		switch (*Char)
		{
			case TCHAR('\\'):
				AppendTo += TEXT("\\\\");
				break;
			case TCHAR('\n'):
				AppendTo += TEXT("\\n");
				break;
			case TCHAR('\t'):
				AppendTo += TEXT("\\t");
				break;
			case TCHAR('\b'):
				AppendTo += TEXT("\\b");
				break;
			case TCHAR('\f'):
				AppendTo += TEXT("\\f");
				break;
			case TCHAR('\r'):
				AppendTo += TEXT("\\r");
				break;
			case TCHAR('\"'):
				AppendTo += TEXT("\\\"");
				break;
			default:
				// Must escape control characters
				if (*Char >= TCHAR(32))
				{
					AppendTo += *Char;
				}
				else
				{
					AppendTo.Appendf(TEXT("\\u%04x"), *Char);
				}
		}
	}
	AppendTo += TEXT("\"");

	return AppendTo;
}

static FString EscapeJsonString(const FString& StringVal)
{
	FString Result;
	return GMPConsoleManger::AppendEscapeJsonString(Result, StringVal);
}

static FOutputDevice* XCmdAr = nullptr;
void ProcessXCommandFromCmdStr(UWorld* InWorld, const TCHAR* CmdStr, FOutputDevice* OutAr = XCmdAr);

struct FXCmdGroup
{
	FString Cmd;
	TArray<FString> Args;
};
TArray<FXCmdGroup>& GetXCmdGroups(UWorld* InWorld)
{
	return *GMP::GameLocalObject<TArray<FXCmdGroup>>(InWorld);
}
struct FXConsoleCmdData
{
	int32 PauseCnt = 0;
	int32 XCmdIndex = 0;
};
FXConsoleCmdData& GetXCmdData(UWorld* InWorld)
{
	return *GMP::GameLocalObject<FXConsoleCmdData>(InWorld);
}

#if defined(HTTPSERVER_API) && defined(GMP_HTTPSERVER)
struct FHttpRouteBinder
{
	static auto IsUtf8(const uint8* Bytes, int32 Len)
	{
		int32 num;
		auto Offset = 0;
		while (Bytes[Offset] != 0x00 && Offset < Len)
		{
			if ((*Bytes & 0x80) == 0x00)
			{
				num = 1;
			}
			else if ((*Bytes & 0xE0) == 0xC0)
			{
				num = 2;
			}
			else if ((*Bytes & 0xF0) == 0xE0)
			{
				num = 3;
			}
			else if ((*Bytes & 0xF8) == 0xF0)
			{
				num = 4;
			}
			else
				return false;

			Offset += 1;
			for (int i = 1; i < num; ++i)
			{
				if ((Bytes[Offset] & 0xC0) != 0x80)
					return false;
				Offset += 1;
			}
		}
		return true;
	};

	;
	TMap<int32, TWeakPtr<IHttpRouter>> HttpRouters;
	TSharedPtr<IHttpRouter> InitHttp(FHttpServerModule* ServerModule, TOptional<int32> InPortNum = {}, const EHttpServerRequestVerbs& HttpVerbs = EHttpServerRequestVerbs::VERB_POST)
	{
		int32 PortNum = InPortNum.Get(22222);
		if (!InPortNum.IsSet() && ensure(FCommandLine::IsInitialized()))
			FParse::Value(FCommandLine::Get(), TEXT("xcmdport="), PortNum);

		auto Find = HttpRouters.Find(PortNum);
		if (Find && Find->IsValid())
		{
			return Find->Pin();
		}

		TSharedPtr<IHttpRouter> HttpRouter;
		for (auto i = 0; i < 10; ++i)
		{
			HttpRouter = ServerModule->GetHttpRouter(PortNum);
			if (HttpRouter)
				break;
			++PortNum;
		}
		if (!HttpRouter)
			return HttpRouter;
		HttpRouters.Add(PortNum, HttpRouter);
		auto Handler = [HttpVerbs](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {
			do
			{
				if (!ensure(GWorld && Request.Verb == HttpVerbs))
					break;

				if (!ensure(IsUtf8(Request.Body.GetData(), Request.Body.Num())))
					break;

				class FHttpServerConverter
					: public FUTF8ToTCHAR
					, public FOutputDevice
				{
				public:
					using FUTF8ToTCHAR::FUTF8ToTCHAR;
					virtual void Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const class FName& Category) override
					{
						FTCHARToUTF8 ConvertToUtf8(Data);
						const uint8* ConvertToUtf8Bytes = (reinterpret_cast<const uint8*>(ConvertToUtf8.Get()));
						Response->Body.Append(ConvertToUtf8Bytes, ConvertToUtf8.Length());
					}
					TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
					TUniquePtr<FHttpServerResponse> ReleaseResponse()
					{
						Response->Code = EHttpServerResponseCodes::Ok;
						TArray<FString> ContentTypeValue = {FString::Printf(TEXT("%s;charset=utf-8"), TEXT("text/plain"))};
						Response->Headers.Add(TEXT("content-type"), MoveTemp(ContentTypeValue));
						return MoveTemp(Response);
					}
				};
				FHttpServerConverter Processor(reinterpret_cast<const char*>(Request.Body.GetData()), Request.Body.Num() + 1);
				TGuardValue<FOutputDevice*> XCmdGuard(XCmdAr, &Processor);
				ProcessXCommandFromCmdStr(GWorld, Processor.Get());
				OnComplete(Processor.ReleaseResponse());

				return true;
			} while (false);
			return false;
		};
#if UE_5_04_OR_LATER
		ensure(HttpRouter->BindRoute(FHttpPath(TEXT("/xcmd")), HttpVerbs, FHttpRequestHandler::CreateLambda(Handler)));
#else
		ensure(HttpRouter->BindRoute(FHttpPath(TEXT("/xcmd")), HttpVerbs, std::move(Handler)));
#endif
		//ServerModule->StartAllListeners();
		return HttpRouter;
	}

	bool TryInitHttp(TOptional<int32> InPortNum = {}, const EHttpServerRequestVerbs& HttpVerbs = EHttpServerRequestVerbs::VERB_POST)
	{
		auto Module = static_cast<FHttpServerModule*>(InPortNum.IsSet() ? FModuleManager::Get().GetModule("HTTPServer") : FModuleManager::Get().LoadModule("HTTPServer"));
		if (!Module)
			return false;
		return !!InitHttp(Module, InPortNum, HttpVerbs);
	}
	void BindHttpServer()
	{
		GMP::FGMPModuleUtils::template OnModuleLifetime<FHttpServerModule>(  //
			"HTTPServer",
			TDelegate<void(FHttpServerModule*)>::CreateLambda([this](FHttpServerModule* Module) {
				if (Module && Module->IsAvailable())
				{
					InitHttp(Module);
				}
			}));
	}
};
#else
struct FHttpRouteBinder
{
	bool TryInitHttp() { return false; }
	void BindHttpServer() {}
};
#endif

static inline bool IsWhiteSpace(TCHAR Value)
{
	return Value == TCHAR(' ');
}
static FString GetTextSection(const TCHAR*& It)
{
	FString ret;

	while (*It)
	{
		if (IsWhiteSpace(*It))
		{
			break;
		}

		ret += *It++;
	}

	while (IsWhiteSpace(*It))
	{
		++It;
	}

	return ret;
}
static const TCHAR* GetSetByTCHAR(EConsoleVariableFlags InSetBy)
{
	EConsoleVariableFlags SetBy = (EConsoleVariableFlags)((uint32)InSetBy & ECVF_SetByMask);

	switch (SetBy)
	{
#define CASE(A)         \
	case ECVF_SetBy##A: \
		return TEXT(#A);
		// Could also be done with enum reflection instead
		CASE(Constructor)
		CASE(Scalability)
		CASE(GameSetting)
		CASE(ProjectSetting)
		CASE(DeviceProfile)
		CASE(SystemSettingsIni)
		CASE(ConsoleVariablesIni)
		CASE(Commandline)
		CASE(Code)
		CASE(Console)
#undef CASE
	}
	return TEXT("<UNKNOWN>");
}
}  // namespace GMPConsoleManger

namespace GMPConsoleManger
{
class FConsoleManager final
	: public IXConsoleManager
	, public FHttpRouteBinder
{
public:
	/** constructor */
	FConsoleManager()
		: ConsoleManager(IConsoleManager::Get())
	{
	}

	/** destructor */
	~FConsoleManager()
	{
		for (TMap<FString, IConsoleObject*>::TConstIterator PairIt(ConsoleObjects); PairIt; ++PairIt)
		{
			IConsoleObject* Var = PairIt.Value();

			delete Var;
		}
	}

	// interface IConsoleManager -----------------------------------
	virtual IConsoleVariable* RegisterConsoleVariable(const TCHAR* Name, bool DefaultValue, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleVariable(Name, DefaultValue, Help, Flags); }
	virtual IConsoleVariable* RegisterConsoleVariable(const TCHAR* Name, int32 DefaultValue, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleVariable(Name, DefaultValue, Help, Flags); }
	virtual IConsoleVariable* RegisterConsoleVariable(const TCHAR* Name, float DefaultValue, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleVariable(Name, DefaultValue, Help, Flags); }
	virtual IConsoleVariable* RegisterConsoleVariable(const TCHAR* Name, const TCHAR* DefaultValue, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleVariable(Name, DefaultValue, Help, Flags); }
	virtual IConsoleVariable* RegisterConsoleVariable(const TCHAR* Name, const FString& DefaultValue, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleVariable(Name, DefaultValue, Help, Flags); }

	virtual IConsoleVariable* RegisterConsoleVariableRef(const TCHAR* Name, bool& RefValue, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleVariableRef(Name, RefValue, Help, Flags); }
	virtual IConsoleVariable* RegisterConsoleVariableRef(const TCHAR* Name, int32& RefValue, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleVariableRef(Name, RefValue, Help, Flags); }
	virtual IConsoleVariable* RegisterConsoleVariableRef(const TCHAR* Name, float& RefValue, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleVariableRef(Name, RefValue, Help, Flags); }
	virtual IConsoleVariable* RegisterConsoleVariableRef(const TCHAR* Name, FString& RefValue, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleVariableRef(Name, RefValue, Help, Flags); }
	virtual IConsoleVariable* RegisterConsoleVariableBitRef(const TCHAR* CVarName, const TCHAR* FlagName, uint32 BitNumber, uint8* Force0MaskPtr, uint8* Force1MaskPtr, const TCHAR* Help, uint32 Flags) override
	{
		return ConsoleManager.RegisterConsoleVariableBitRef(CVarName, FlagName, BitNumber, Force0MaskPtr, Force1MaskPtr, Help, Flags);
	}
#if UE_5_06_OR_LATER
	virtual IConsoleVariable* RegisterConsoleVariableRef(const TCHAR* Name, FName& RefValue, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleVariableRef(Name, RefValue, Help, Flags); }
#endif
	virtual void CallAllConsoleVariableSinks() override { return ConsoleManager.CallAllConsoleVariableSinks(); }

	virtual FConsoleVariableSinkHandle RegisterConsoleVariableSink_Handle(const FConsoleCommandDelegate& Command) override { return ConsoleManager.RegisterConsoleVariableSink_Handle(Command); }
	virtual void UnregisterConsoleVariableSink_Handle(FConsoleVariableSinkHandle Handle) override { return ConsoleManager.UnregisterConsoleVariableSink_Handle(Handle); }

	virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandDelegate& Command, uint32 Flags) override { return ConsoleManager.RegisterConsoleCommand(Name, Help, Command, Flags); }
	virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithArgsDelegate& Command, uint32 Flags) override { return ConsoleManager.RegisterConsoleCommand(Name, Help, Command, Flags); }
	virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldDelegate& Command, uint32 Flags) override { return ConsoleManager.RegisterConsoleCommand(Name, Help, Command, Flags); }
	virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldAndArgsDelegate& Command, uint32 Flags) override
	{
		return ConsoleManager.RegisterConsoleCommand(Name, Help, Command, Flags);
	}
	virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldArgsAndOutputDeviceDelegate& Command, uint32 Flags) override
	{
		return ConsoleManager.RegisterConsoleCommand(Name, Help, Command, Flags);
	}
	virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithOutputDeviceDelegate& Command, uint32 Flags) override
	{
		return ConsoleManager.RegisterConsoleCommand(Name, Help, Command, Flags);
	}
	virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, uint32 Flags) override { return ConsoleManager.RegisterConsoleCommand(Name, Help, Flags); }
#if UE_5_00_OR_LATER
	virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithArgsAndOutputDeviceDelegate& Command, uint32 Flags) override
	{
		return ConsoleManager.RegisterConsoleCommand(Name, Help, Command, Flags);
	}
	virtual FString FindConsoleObjectName(const IConsoleObject* Obj) const override { return ConsoleManager.FindConsoleObjectName(Obj); }
#endif

	virtual IConsoleObject* FindConsoleObject(const TCHAR* Name, bool bTrackFrequentCalls = true) const override { return ConsoleManager.FindConsoleObject(Name, bTrackFrequentCalls); }
	virtual IConsoleVariable* FindConsoleVariable(const TCHAR* Name, bool bTrackFrequentCalls = true) const override { return ConsoleManager.FindConsoleVariable(Name, bTrackFrequentCalls); }
	virtual void ForEachConsoleObjectThatStartsWith(const FConsoleObjectVisitor& Visitor, const TCHAR* ThatStartsWith) const override { return ConsoleManager.ForEachConsoleObjectThatStartsWith(Visitor, ThatStartsWith); }
	virtual void ForEachConsoleObjectThatContains(const FConsoleObjectVisitor& Visitor, const TCHAR* ThatContains) const override { return ConsoleManager.ForEachConsoleObjectThatContains(Visitor, ThatContains); }
	virtual bool ProcessUserConsoleInput(const TCHAR* InInput, FOutputDevice& Ar, UWorld* InWorld) override { return ConsoleManager.ProcessUserConsoleInput(InInput, Ar, InWorld); }
	virtual void AddConsoleHistoryEntry(const TCHAR* Key, const TCHAR* Input) override { return ConsoleManager.AddConsoleHistoryEntry(Key, Input); }
	virtual void GetConsoleHistory(const TCHAR* Key, TArray<FString>& Out) override { return ConsoleManager.GetConsoleHistory(Key, Out); }
	virtual bool IsNameRegistered(const TCHAR* Name) const override { return ConsoleManager.IsNameRegistered(Name); }
	virtual void RegisterThreadPropagation(uint32 ThreadId, IConsoleThreadPropagation* InCallback) override { return ConsoleManager.RegisterThreadPropagation(ThreadId, InCallback); }
	virtual void UnregisterConsoleObject(IConsoleObject* Object, bool bKeepState) override { return ConsoleManager.UnregisterConsoleObject(Object, bKeepState); }
#if UE_5_00_OR_LATER
	virtual void UnregisterConsoleObject(const TCHAR* Name, bool bKeepState = true) override { return ConsoleManager.UnregisterConsoleObject(Name, bKeepState); }
#else
	virtual void UnregisterConsoleObject(const TCHAR* Name, bool bKeepState = true) override { return ConsoleManager.UnregisterConsoleObject(Name, bKeepState); }
#endif
	virtual IConsoleCommand* RegisterXConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FXConsoleFullCmdDelegate& Command, uint32 Flags) override { return RegisterConsoleCommand(Name, Help, Command, Flags); }
	virtual IConsoleCommand* RegisterXConsoleCommandEx(const GMP::FArrayTypeNames& Names, const TCHAR* Name, const TCHAR* Help, const FXConsoleFullCmdDelegate& Command, uint32 Flags) override
	{
#if !UE_BUILD_SHIPPING
		FString Parameter = FString::JoinBy(Names, TEXT(","), [](FName Name) { return Name.ToString(); });
		UE_LOG(LogXConsoleManager, Log, TEXT("RegisterXConsoleCommand : %s(%s)"), Name, *Parameter);
#endif
		ConsoleParameters.Add(Name, Names);

		return RegisterConsoleCommand(Name, Help, Command, Flags);
	}
	virtual IConsoleVariable* RegisterXConsoleVariable(const TCHAR* Name, const TCHAR* Help, uint32 Flags, const FProperty* InProp, void* Addr, bool bValueRef) override;

#if defined(ALLOW_OTHER_PLATFORM_CONFIG) && ALLOW_OTHER_PLATFORM_CONFIG
#if UE_5_04_OR_LATER
	virtual void LoadAllPlatformCVars(FName PlatformName, const FString& DeviceProfileName = FString()) override { ConsoleManager.LoadAllPlatformCVars(PlatformName, DeviceProfileName); }
	virtual void PreviewPlatformCVars(FName PlatformName, const FString& DeviceProfileName, FName PreviewModeTag) override { ConsoleManager.PreviewPlatformCVars(PlatformName, DeviceProfileName, PreviewModeTag); }
	virtual void ClearAllPlatformCVars(FName PlatformName = NAME_None, const FString& DeviceProfileName = FString()) override { ConsoleManager.ClearAllPlatformCVars(PlatformName, DeviceProfileName); }
#endif
#if UE_5_06_OR_LATER
	virtual void StompPlatformCVars(FName PlatformName, const FString& DeviceProfileName, FName Tag, EConsoleVariableFlags SetBy, EConsoleVariableFlags RequiredFlags, EConsoleVariableFlags DisallowedFlags) override
	{
		ConsoleManager.StompPlatformCVars(PlatformName, DeviceProfileName, Tag, SetBy, RequiredFlags, DisallowedFlags);
	}
#endif
#endif

#if UE_5_00_OR_LATER
	virtual FConsoleVariableMulticastDelegate& OnCVarUnregistered() override { return ConsoleManager.OnCVarUnregistered(); }
#endif

#if UE_5_04_OR_LATER
	virtual void UnsetAllConsoleVariablesWithTag(FName Tag, EConsoleVariableFlags Priority = ECVF_SetByMask) override { ConsoleManager.UnsetAllConsoleVariablesWithTag(Tag, Priority); }
	virtual FConsoleObjectWithNameMulticastDelegate& OnConsoleObjectUnregistered() override { return ConsoleManager.OnConsoleObjectUnregistered(); }
#endif

#if UE_5_06_OR_LATER
	virtual void BatchUpdateTag(FName Tag, const TMap<FName, FString>& CVarsAndValues) override { ConsoleManager.BatchUpdateTag(Tag, CVarsAndValues); }
#endif

	bool ProcessUserXCommandInput(FString& Cmd, TArray<FString>& Args, FOutputDevice& Ar, UWorld* InWorld);
	bool IsProcessingCommand() const { return bIsProcessingCommamd; }

private:  // ----------------------------------------------------
	IConsoleManager& ConsoleManager;
	bool bIsProcessingCommamd = false;
	/** Map of console variables and commands, indexed by the name of that command or variable */
	// [name] = pointer (pointer must not be 0)
	TMap<FString, IConsoleObject*> ConsoleObjects;

	FConsoleVariableMulticastDelegate ConsoleVariableUnregisteredDelegate;
#if UE_5_04_OR_LATER
	FConsoleObjectWithNameMulticastDelegate ConsoleObjectUnregisteredDelegate;
#endif
	FCriticalSection CachedPlatformsAndDeviceProfilesLock;
	TSet<FName> CachedPlatformsAndDeviceProfiles;

	virtual const GMP::FArrayTypeNames* GetXConsoleCommandProps(const TCHAR* Name) const override { return ConsoleParameters.Find(FString(Name)); }
	TMap<FString, const GMP::FArrayTypeNames> ConsoleParameters;
};
#endif

IConsoleVariable* FConsoleManager::RegisterXConsoleVariable(const TCHAR* Name, const TCHAR* Help, uint32 Flags, const FProperty* InProp, void* Addr, bool bValueRef)
{
	if (InProp->IsA<FBoolProperty>())
	{
		return bValueRef ? RegisterConsoleVariableRef(Name, *reinterpret_cast<bool*>(Addr), Help, Flags) : RegisterConsoleVariable(Name, *reinterpret_cast<bool*>(Addr), Help, Flags);
	}
	else if (InProp->IsA<FIntProperty>())
	{
		return bValueRef ? RegisterConsoleVariableRef(Name, *reinterpret_cast<int32*>(Addr), Help, Flags) : RegisterConsoleVariable(Name, *reinterpret_cast<int32*>(Addr), Help, Flags);
	}
	else if (InProp->IsA<FFloatProperty>())
	{
		return bValueRef ? RegisterConsoleVariableRef(Name, *reinterpret_cast<float*>(Addr), Help, Flags) : RegisterConsoleVariable(Name, *reinterpret_cast<float*>(Addr), Help, Flags);
	}
	else if (InProp->IsA<FStrProperty>())
	{
		return bValueRef ? RegisterConsoleVariableRef(Name, *reinterpret_cast<FString*>(Addr), Help, Flags) : RegisterConsoleVariable(Name, *reinterpret_cast<FString*>(Addr), Help, Flags);
	}
#if UE_5_06_OR_LATER
	else if (InProp->IsA<FNameProperty>())
	{
		return bValueRef ? RegisterConsoleVariableRef(Name, *reinterpret_cast<FName*>(Addr), Help, Flags) : RegisterConsoleVariable(Name, reinterpret_cast<FName*>(Addr)->ToString(), Help, Flags);
	}
#endif
	ensure(false);
	return nullptr;
}

#if PLATFORM_TCHAR_IS_4_BYTES
template<typename CharType>
using TFromUTF32 = TStringPointer<UTF32CHAR, CharType>;
template<typename CharType>
using TFromUTF16 = TStringConversion<TUTF16ToUTF32_Convert<UTF16CHAR, CharType>>;
#else
template<typename CharType>
using TFromUTF16 = TStringPointer<UTF16CHAR, CharType>;
template<typename CharType>
using TFromUTF32 = TStringConversion<TUTF32ToUTF16_Convert<UTF32CHAR, CharType>>;
#endif

template<typename CharType, typename T>
bool TryParseString(const CharType*& Buffer, T& StrValue)
{
	auto IsSpaces = [](const CharType Ch) {
		constexpr FAsciiSet SpaceCharacters = FAsciiSet(" \t\n\r");
		return !!SpaceCharacters.Test(Ch);
	};

	//skip spaces
	while (IsSpaces(*Buffer))
	{
		++Buffer;
	}

	// if end
	if (*Buffer == 0)
		return false;

	// if no quote
	constexpr FAsciiSet QuoteCharacters = FAsciiSet("\"\'");

	auto QuoteCh = *Buffer;
	if (!QuoteCharacters.Test(QuoteCh))
	{
		while (*Buffer && !IsSpaces(*Buffer))
		{
			StrValue += *Buffer++;
		}
		return true;
	}

	Buffer++;  // eat opening quote

	auto ShouldParse = [](const CharType Ch) {
		constexpr FAsciiSet StopCharacters = FAsciiSet("\n\r") + '\0';
		return StopCharacters.Test(Ch) == 0;
	};

	while (ShouldParse(*Buffer))
	{
		if (*Buffer != CharType('\\'))  // unescaped character
		{
			if (QuoteCh == *Buffer)
			{
				auto Next = *++Buffer;                       // eat closing quote
				return IsSpaces(Next)                        // stopped
					   || TryParseString(Buffer, StrValue);  // continue
			}
			else
			{
				StrValue += *Buffer++;
			}
		}
		else if (*++Buffer == CharType('\\'))  // escaped backslash "\\"
		{
			StrValue += CharType('\\');
			++Buffer;
		}
		else if (*Buffer == CharType('"'))  // escaped double quote "\""
		{
			StrValue += CharType('"');
			++Buffer;
		}
		else if (*Buffer == CharType('\''))  // escaped single quote "\'"
		{
			StrValue += CharType('\'');
			++Buffer;
		}
		else if (*Buffer == CharType('n'))  // escaped newline
		{
			StrValue += CharType('\n');
			++Buffer;
		}
		else if (*Buffer == CharType('r'))  // escaped carriage return
		{
			StrValue += CharType('\r');
			++Buffer;
		}
		else if (*Buffer == CharType('t'))  // escaped tab
		{
			StrValue += CharType('\t');
			++Buffer;
		}
		else if (FChar::IsOctDigit(*Buffer))  // octal sequence (\012)
		{
			TStringBuilder<16> OctSequence;
			while (ShouldParse(*Buffer) && FChar::IsOctDigit(*Buffer) && OctSequence.Len() < 3)  // Octal sequences can only be up-to 3 digits long
			{
				OctSequence += *Buffer++;
			}

			StrValue += (CharType)FCString::Strtoi(*OctSequence, nullptr, 8);
		}
		else if (*Buffer == CharType('x') && FChar::IsHexDigit(*(Buffer + 1)))  // hex sequence (\xBEEF)
		{
			++Buffer;

			TStringBuilder<16> HexSequence;
			while (ShouldParse(*Buffer) && FChar::IsHexDigit(*Buffer))
			{
				HexSequence += *Buffer++;
			}

			StrValue += (CharType)FCString::Strtoi(*HexSequence, nullptr, 16);
		}
		else if (*Buffer == CharType('u') && FChar::IsHexDigit(*(Buffer + 1)))  // UTF-16 sequence (\u1234)
		{
			++Buffer;

			TStringBuilder<4> UnicodeSequence;
			while (ShouldParse(*Buffer) && FChar::IsHexDigit(*Buffer) && UnicodeSequence.Len() < 4)  // UTF-16 sequences can only be up-to 4 digits long
			{
				UnicodeSequence += *Buffer++;
			}

			const UTF16CHAR Utf16Char = static_cast<UTF16CHAR>(FCString::Strtoi(*UnicodeSequence, nullptr, 16));
			const FUTF16ToTCHAR Utf16Str(&Utf16Char, /* Len */ 1);
			StrValue += FStringView(Utf16Str.Get(), Utf16Str.Length());
		}
		else if (*Buffer == CharType('U') && FChar::IsHexDigit(*(Buffer + 1)))  // UTF-32 sequence (\U12345678)
		{
			++Buffer;

			TStringBuilder<8> UnicodeSequence;
			while (ShouldParse(*Buffer) && FChar::IsHexDigit(*Buffer) && UnicodeSequence.Len() < 8)  // UTF-32 sequences can only be up-to 8 digits long
			{
				UnicodeSequence += *Buffer++;
			}

			const UTF32CHAR Utf32Char = static_cast<UTF32CHAR>(FCString::Strtoi(*UnicodeSequence, nullptr, 16));
			const FUTF32ToTCHAR Utf32Str(&Utf32Char, /* Len */ 1);
			StrValue += FStringView(Utf32Str.Get(), Utf32Str.Length());
		}
		else  // unhandled escape sequence
		{
			StrValue += CharType('\\');
			StrValue += *Buffer++;
		}
	}

	// Require closing quote
	if (!ensureMsgf(QuoteCh == '\0' || *Buffer++ == QuoteCh, TEXT("there is no closing quote")))
	{
		return false;
	}

	return true;
}

TArray<FString> XSplitCommandLine(const TCHAR*& CmdLine, bool bEnableSingleQuote = false)
{
	TArray<FString> Argv;
	TStringBuilder<256> ParamterBuilder;
#if 1
	while (TryParseString(CmdLine, ParamterBuilder))
	{
		Argv.Add(ParamterBuilder.ToString());
		ParamterBuilder.Reset();
	}
#else
	bool bInDQ = false;
	bool bInSQ = false;
	bool bInTEXT = false;
	bool bInSPACE = false;
	while (auto Ch = *CmdLine++)
	{
		if (bInDQ)
		{
			if (Ch == '\"')
			{
				bInDQ = false;
			}
			else
			{
				ParamterBuilder.Append(Ch);
			}
		}
		else if (bInSQ)
		{
			if (Ch == '\'')
			{
				bInSQ = false;
			}
			else
			{
				ParamterBuilder.Append(Ch);
			}
		}
		else
		{
			switch (Ch)
			{
				case '\"':
					bInDQ = true;
					bInTEXT = true;
					bInSPACE = false;
					break;
				case ' ':
				case '\t':
				case '\r':
				case '\n':
					if (bInTEXT)
					{
						Argv.Add(ParamterBuilder.ToString());
						ParamterBuilder.Reset();
					}
					bInTEXT = false;
					bInSPACE = true;
					break;
				case '\'':
					if (bEnableSingleQuote)
					{
						bInSQ = true;
						bInTEXT = true;
						bInSPACE = false;
						break;
					}
				default:
					bInTEXT = true;
					ParamterBuilder.Append(Ch);
					bInSPACE = false;
					break;
			}
		}
	}
	if (ParamterBuilder.Len() > 0)
	{
		bInTEXT = false;
		Argv.Add(ParamterBuilder.ToString());
	}
	ensure(!bInDQ && !bInSQ && !bInTEXT);
#endif
	return Argv;
}

bool FConsoleManager::ProcessUserXCommandInput(FString& Cmd, TArray<FString>& Args, FOutputDevice& Ar, UWorld* InWorld)
{
#if defined(WITH_EDITOR)
#define AR_LOGF(Fmt, ...) Ar.Logf(Fmt, ##__VA_ARGS__)
#else
#define AR_LOGF(Fmt, ...)
#endif
	UE_LOG(LogXConsoleManager, Log, TEXT("ProcessUserXCommandInput Cmd : %s with %d Args"), *Cmd, Args.Num());

	auto Old = bIsProcessingCommamd;
	bIsProcessingCommamd = true;
	ON_SCOPE_EXIT
	{
		bIsProcessingCommamd = Old;
	};

	// Remove a trailing ? if present, to kick it into help mode
	const bool bCommandEndedInQuestion = Cmd.EndsWith(TEXT("?"), ESearchCase::CaseSensitive);
	if (bCommandEndedInQuestion)
	{
		Cmd.MidInline(0, Cmd.Len() - 1, EAllowShrinking::No);
	}

	IConsoleObject* CObj = FindConsoleObject(*Cmd);
	if (!CObj)
	{
		return false;
	}

#if DISABLE_CHEAT_CVARS
	if (CObj->TestFlags(ECVF_Cheat))
	{
		return false;
	}
#endif  // DISABLE_CHEAT_CVARS

	if (CObj->TestFlags(ECVF_Unregistered))
	{
		return false;
	}

	IConsoleCommand* CCmd = CObj->AsCommand();
	IConsoleVariable* CVar = CObj->AsVariable();
	if (CCmd)
	{
		const bool bShowHelp = bCommandEndedInQuestion || ((Args.Num() == 1) && (Args[0] == TEXT("?")));
		if (bShowHelp)
		{
			// get help
			AR_LOGF(TEXT("HELP for '%s':\n%s"), *Cmd, CCmd->GetHelp());
		}
		else
		{
			// if a delegate was bound, we execute it and it should return true,
			// otherwise it was a Exec console command and this returns FASLE
			AR_LOGF(TEXT("ExecCmd : %s"), *Cmd);
			return CCmd->Execute(Args, InWorld, Ar);
		}
	}
	else if (CVar)
	{
		// Process variable
		bool bShowHelp = bCommandEndedInQuestion;
		bool bShowCurrentState = false;

		if (Args.Num() == 0)
		{
			bShowCurrentState = true;
		}
		else
		{
			const bool bReadOnly = CVar->TestFlags(ECVF_ReadOnly);
			auto& Arg0 = Args[0];
			if (Arg0.Len() >= 2)
			{
				if (Arg0[0] == (TCHAR)'\"' && Arg0[Arg0.Len() - 1] == (TCHAR)'\"')
				{
					Arg0.MidInline(1, Arg0.Len() - 2, EAllowShrinking::No);
				}
				// this is assumed to be unintended e.g. copy and paste accident from ini file
				if (Arg0.Len() > 0 && Arg0[0] == (TCHAR)'=')
				{
					AR_LOGF(TEXT("Warning: Processing the console input parameters the leading '=' is ignored (only needed for ini files)."));
					Arg0.MidInline(1, Arg0.Len() - 1, EAllowShrinking::No);
				}
			}

			if (Arg0 == TEXT("?"))
			{
				bShowHelp = true;
			}
			else
			{
				if (bReadOnly)
				{
					AR_LOGF(TEXT("Error: %s is read only!"), *Cmd, *CVar->GetString());
				}
				else
				{
					// set value
					AR_LOGF(TEXT("SetCVar : %s"), *Cmd);
					CVar->Set(*Arg0, ECVF_SetByConsole);

					AR_LOGF(TEXT("%s = \"%s\""), *Cmd, *CVar->GetString());

					CallAllConsoleVariableSinks();
				}
			}
		}

		if (bShowHelp)
		{
			// get help
			const bool bReadOnly = CVar->TestFlags(ECVF_ReadOnly);
			AR_LOGF(TEXT("HELP for '%s'%s:\n%s"), *Cmd, bReadOnly ? TEXT("(ReadOnly)") : TEXT(""), CVar->GetHelp());
			bShowCurrentState = true;
		}

		if (bShowCurrentState)
		{
			AR_LOGF(TEXT("%s = \"%s\"      LastSetBy: %s"), *Cmd, *CVar->GetString(), GetSetByTCHAR(CVar->GetFlags()));
		}
	}
#undef AR_LOGF
	return true;
}

static FConsoleManager* XConsoleManager = nullptr;

static void ProcessingNextXCmdList(UWorld* InWorld, FOutputDevice* OutAr = XCmdAr)
{
	auto& LocalXCmdData = GetXCmdData(InWorld);

	if (!ensureWorldMsgf(InWorld, LocalXCmdData.PauseCnt >= 0, TEXT("PauseXConsoleCommandPipeline & ContinueXConsoleCommandPipeline mismatched")))
		return;

	OutAr = OutAr ? OutAr : (XCmdAr ? XCmdAr : GLog);
	auto& XCmdIndex = LocalXCmdData.XCmdIndex;
	auto& XCmdGroups = GetXCmdGroups(InWorld);
	UE_LOG(LogXConsoleManager, Log, TEXT("ProcessingNextXCmdList CmdGroupsSize %d, CmdIndex %d"), XCmdGroups.Num(), XCmdIndex);
	while (XConsoleManager && XCmdGroups.IsValidIndex(XCmdIndex))
	{
		XConsoleManager->ProcessUserXCommandInput(XCmdGroups[XCmdIndex].Cmd, XCmdGroups[XCmdIndex].Args, *OutAr, InWorld);
		++XCmdIndex;
		if ([](auto& bIn) {
				auto Old = bIn;
				if (bIn > 0)
					--bIn;
				return Old > 0;
			}(LocalXCmdData.PauseCnt))
			break;
	}
};

const TCHAR* GetCurCmdName(UWorld* InWorld)
{
	do
	{
		if (!XConsoleManager)
			break;
		auto& XCmdIndex = GetXCmdData(InWorld).XCmdIndex;
		auto& XCmdGroups = GetXCmdGroups(InWorld);
		if (!XCmdGroups.IsValidIndex(XCmdIndex))
			break;
		return *XCmdGroups[XCmdIndex].Cmd;
	} while (false);
	return TEXT("None");
}
static void InsertsXCommandImpl(UWorld* InWorld, const TCHAR* InStr, bool bSearchDelim = false)
{
	UE_LOG(LogXConsoleManager, Log, TEXT("InsertsXCommandImpl"));
	do
	{
		auto AllArgs = XSplitCommandLine(InStr);
		static const TCHAR XCmdDelim[] = TEXT("---");

		int32 FromIdx = 0;
		if (bSearchDelim && !AllArgs.Find(XCmdDelim, FromIdx))
			break;

		auto& XCmdIndex = GetXCmdData(InWorld).XCmdIndex;
		auto& XCmdGroups = GetXCmdGroups(InWorld);
		auto InsertIdx = FMath::Min(XCmdGroups.Num(), XCmdIndex + 1);
		for (auto i = FromIdx; i <= AllArgs.Num(); ++i)
		{
			if (i == AllArgs.Num() || AllArgs[i] == XCmdDelim)
			{
				if (FromIdx < i)
				{
					auto& Pair = XCmdGroups.InsertDefaulted_GetRef(InsertIdx++);
					Pair.Cmd = MoveTemp(AllArgs[FromIdx]);
					Pair.Args.Reserve(i - FromIdx);
					for (auto j = FromIdx + 1; j < i; ++j)
					{
						Pair.Args.Emplace(MoveTemp(AllArgs[j]));
					}
				}
				FromIdx = i + 1;
			}
		}
	} while (false);
};
template<bool bTryInitHttp = PLATFORM_DESKTOP>
IXConsoleManager* GetSingleton()
{
	if (!XConsoleManager)
	{
		XConsoleManager = new FConsoleManager;  // we will leak this
		UE_LOG(LogXConsoleManager, Log, TEXT("Create XConsoleManager"));

#if UE_5_01_OR_LATER
#define X_IF_CONSTEXPR if constexpr
#elif PLATFORM_COMPILER_HAS_IF_CONSTEXPR
#define X_IF_CONSTEXPR if constexpr
#else
#define X_IF_CONSTEXPR if
#endif
		X_IF_CONSTEXPR(bTryInitHttp)
		{
			static FDelayedAutoRegisterHelper TryInitHttpServer(EDelayedRegisterRunPhase::EndOfEngineInit, [] { XConsoleManager->TryInitHttp(); });
		}
#undef X_IF_CONSTEXPR
	}
	check(XConsoleManager);
	return XConsoleManager;
}

void ProcessXCommandFromCmdline(UWorld* InWorld)
{
	// first time
	if (TrueOnFirstCall([] {}))
	{
		GetSingleton<false>();
		UE_LOG(LogXConsoleManager, Log, TEXT("ProcessXCommandFromCmdline : %s\n"), FCommandLine::GetOriginalForLogging());
		auto& XCmdGroups = GMPConsoleManger::GetXCmdGroups(InWorld);
		XCmdGroups.Empty();
		InsertsXCommandImpl(InWorld, FCommandLine::GetOriginalForLogging(), true);

		auto& LocalXCmdData = GMPConsoleManger::GetXCmdData(InWorld);
		if (LocalXCmdData.PauseCnt > 0)
		{
			UE_LOG(LogXConsoleManager, Warning, TEXT("ProcessingNextXCmdList Paused"));
			return;
		}

		if (XCmdGroups.Num() > 0)
		{
			ProcessingNextXCmdList(InWorld);
		}
	}
}

void ProcessXCommandFromCmdStr(UWorld* InWorld, const TCHAR* CmdStr, FOutputDevice* OutAr)
{
	if (ensure(CmdStr && InWorld))
	{
		InsertsXCommandImpl(InWorld, CmdStr);
		if (!XConsoleManager->IsProcessingCommand())
		{
			ProcessingNextXCmdList(InWorld, OutAr);
		}
	}
}

FAutoConsoleCommandWithWorldArgsAndOutputDevice CVar_XConsoleCmdList(TEXT("z.XCmdList"), TEXT("z.XCmdList FilePathList..."), FXConsoleFullCmdDelegate::CreateLambda([](const TArray<FString>& Paths, UWorld* InWorld, FOutputDevice& Ar) {
																		 for (auto i = 0; i < Paths.Num(); ++i)
																		 {
																			 FString Buffer;
																			 if (ensure(FFileHelper::LoadFileToString(Buffer, *Paths[i])))
																			 {
																				 InsertsXCommandImpl(InWorld, *Buffer);
																			 }
																		 }

																		 if (!XConsoleManager->IsProcessingCommand())
																		 {
																			 ProcessingNextXCmdList(InWorld, &Ar);
																		 }
																	 }));

static int32 PipelineInt = 0;
static FString PipelineString;
}  // namespace GMPConsoleManger

IXConsoleManager& IXConsoleManager::Get()
{
	return *GMPConsoleManger::GetSingleton();
}

void IXConsoleManager::PauseXConsoleCommandPipeline(UWorld* InWorld, const TCHAR* Reason)
{
	UE_LOG(LogXConsoleManager, Log, TEXT("XConsoleCommandline - Paused : %s"), Reason ? Reason : GMPConsoleManger::GetCurCmdName(InWorld));
	GMPConsoleManger::GetXCmdData(InWorld).PauseCnt++;
}

void IXConsoleManager::ContinueXConsoleCommandPipeline(UWorld* InWorld, const TCHAR* Reason)
{
	UE_LOG(LogXConsoleManager, Log, TEXT("XConsoleCommandline-Continued: %s"), Reason ? Reason : GMPConsoleManger::GetCurCmdName(InWorld));
	GMPConsoleManger::ProcessingNextXCmdList(InWorld);
}

int32 IXConsoleManager::CommandPipelineInteger()
{
	return GMPConsoleManger::PipelineInt;
}

int32 IXConsoleManager::CommandPipelineInteger(int32 InVal)
{
	if (GMPConsoleManger::XCmdAr)
	{
		GMPConsoleManger::XCmdAr->Logf(TEXT("{\"code\":%d}\n"), InVal);
	}

	Swap(InVal, GMPConsoleManger::PipelineInt);
	return InVal;
}

const FString& IXConsoleManager::CommandPipelineString()
{
	return GMPConsoleManger::PipelineString;
}

void IXConsoleManager::CommandPipelineString(const FString& InStr)
{
	if (GMPConsoleManger::XCmdAr)
	{
		GMPConsoleManger::XCmdAr->Logf(TEXT("{\"msg\":%s}\n"), *GMPConsoleManger::EscapeJsonString(InStr));
	}

	GMPConsoleManger::PipelineString = InStr;
}

void FXConsoleCommandLambdaControl::FXConsoleController::PauseXConsolePipeline(const TCHAR* Reason)
{
	IXConsoleManager::PauseXConsoleCommandPipeline(GetWorld(), Reason);
}

void FXConsoleCommandLambdaControl::FXConsoleController::ContinueXConsolePipeline(const TCHAR* Reason)
{
	IXConsoleManager::ContinueXConsoleCommandPipeline(GetWorld(), Reason);
}

FXConsoleCommandLambdaFull XVar_RequestExitWithStatus(TEXT("z.RequestExitWithStatus"), TEXT("z.RequestExitWithStatus"), [](const bool Force, const uint8 ReturnCode, UWorld* InWorld, FOutputDevice& Ar) {
	UE_LOG(LogXConsoleManager, Display, TEXT(" RequestExitWithStatus Command args : Force = %s, ReturnCode = %d"), Force ? TEXT("true") : TEXT("false"), ReturnCode);
	FPlatformMisc::RequestExitWithStatus(Force, ReturnCode);
});

FXConsoleCommandLambdaFull XVar_PipelinetDelay(TEXT("z.PipelineDelay"), TEXT("z.PipelineDelay Seconds"), [](float DelaySeconds, UWorld* InWorld, FOutputDevice& Ar) {
	static auto GetTimerManager = [](UWorld* InWorld) {
#if WITH_EDITOR
		if (GIsEditor && (!InWorld || !InWorld->IsGameWorld()))
		{
			return &GEditor->GetTimerManager().Get();
		}
		else
#endif
		{
			check(InWorld || GWorld);
			return IsValid(InWorld) ? &InWorld->GetTimerManager() : &GWorld->GetTimerManager();
		}
	};
	auto TimerMgr = GetTimerManager(InWorld);
	if (ensure(TimerMgr))
	{
		FTimerHandle TimerHandle;
		IXConsoleManager::PauseXConsoleCommandPipeline(InWorld, TEXT("PipelineDelay"));
		TimerMgr->SetTimer(TimerHandle,
						   FTimerDelegate::CreateWeakLambda(InWorld,
															[InWorld] {
																IXConsoleManager::CommandPipelineInteger(0);
																IXConsoleManager::ContinueXConsoleCommandPipeline(InWorld, TEXT("PipelineDelay"));
															}),
						   DelaySeconds,
						   false);
		return;
	}
	IXConsoleManager::CommandPipelineInteger(1);
});

FXConsoleCommandLambdaFull XVar_PipelineExitSilent(TEXT("z.PipelineExitSilent"), TEXT("z.PipelineExitSilent(with PipelineResult)"), [](UWorld* InWorld, FOutputDevice& Ar) {
	//
	FPlatformMisc::RequestExitWithStatus(false, IXConsoleManager::CommandPipelineInteger());
});

FXConsoleCommandLambdaFull XVar_PipelinetExit(TEXT("z.PipelineExit"), TEXT("z.PipelineExit(with PipelineResult)"), [](UWorld* InWorld, FOutputDevice& Ar) {
	//
	FPlatformMisc::RequestExitWithStatus(true, IXConsoleManager::CommandPipelineInteger());
});

FXConsoleCommandLambdaFull XVar_PipelinetExitIf(TEXT("z.PipelineExitIf"), TEXT("z.PipelineExitIf IntValToCompare ReturnCode"), [](int32 IntVal, int32 ReturnCode, UWorld* InWorld, FOutputDevice& Ar) {
	if (IntVal == IXConsoleManager::CommandPipelineInteger())
		FPlatformMisc::RequestExitWithStatus(true, ReturnCode);
});

FXConsoleCommandLambdaFull XVar_PipelineExitIfNot(TEXT("z.PipelineExitIfNot"), TEXT("z.PipelineExitIfNot IntValToCompare ReturnCode"), [](int32 IntVal, int32 ReturnCode, UWorld* InWorld, FOutputDevice& Ar) {
	if (IntVal != IXConsoleManager::CommandPipelineInteger())
		FPlatformMisc::RequestExitWithStatus(true, ReturnCode);
});

static void PipelineWriteResultImpl(const FString& FilePath, UWorld* InWorld, FOutputDevice& Ar)
{
	if (FilePath.IsEmpty())
		Ar.Logf(TEXT("%d"), IXConsoleManager::CommandPipelineInteger());
	else
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("%d"), IXConsoleManager::CommandPipelineInteger()), *FilePath);
}

FXConsoleCommandLambdaFull XVar_PipelineWriteResultInt(TEXT("z.PipelineWriteResult"), TEXT("z.PipelineWriteResult FilePath"), [](const FString& FilePath, UWorld* InWorld, FOutputDevice& Ar) {
	//
	PipelineWriteResultImpl(FilePath, InWorld, Ar);
});

static void PipelineWriteResultStrImpl(const FString& FilePath, UWorld* InWorld, FOutputDevice& Ar)
{
	if (FilePath.IsEmpty())
		Ar.Logf(TEXT("%s"), *IXConsoleManager::CommandPipelineString());
	else
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("%s"), *IXConsoleManager::CommandPipelineString()), *FilePath);
}

FXConsoleCommandLambdaFull XVar_PipelineWriteResultStr(TEXT("z.PipelineWriteResultStr"), TEXT("z.PipelineWriteResultStr FilePath"), [](const FString& FilePath, UWorld* InWorld, FOutputDevice& Ar) {
	//
	PipelineWriteResultStrImpl(FilePath, InWorld, Ar);
});

FXConsoleCommandLambdaFull XVar_PipelineExec(TEXT("z.PipelineExec"), TEXT("PipelineExec \"cmds...\""), [](const FString& CmdBuffer, UWorld* InWorld, FOutputDevice& Ar) {
//
#if !UE_BUILD_SHIPPING
	if (CmdBuffer.EndsWith(TEXT(" ...")) && InWorld && InWorld->IsGameWorld() && ensure(InWorld->GetFirstPlayerController()))
	{
		InWorld->GetFirstPlayerController()->ServerExecRPC(CmdBuffer.LeftChop(3));
	}
	else
#endif
	{
		GEngine->Exec(InWorld, *CmdBuffer, Ar);
	}
});

FXConsoleCommandLambdaFull XVar_PipelineCrashIt(TEXT("z.PipelineCrashIt"), TEXT("PipelineCrashIt "), [](int32 IntVal, UWorld* InWorld, FOutputDevice& Ar) {
	//
	int32* Ptr = nullptr;
	*Ptr = IntVal;
});
FXConsoleCommandLambdaFull XVar_PipelineHangIt(TEXT("z.PipelineHangIt"), TEXT("PipelineHangIt "), [](TOptional<int32> Seconds, UWorld* InWorld, FOutputDevice& Ar) {
	//
	UE_LOG(LogXConsoleManager, Log, TEXT("PipelineHangIt : %d"), Seconds.Get(30));
	FPlatformProcess::Sleep(Seconds.Get(30));
});

#if PLATFORM_DESKTOP
FXConsoleCommandLambdaFull XVar_PipelineServer(TEXT("z.PipelineInitHttp"), TEXT("PipelineInitHttp [PortNum]"), [](TOptional<int32> PortNum, UWorld* InWorld, FOutputDevice& Ar) {
	if (GMPConsoleManger::XConsoleManager)
	{
		GMPConsoleManger::XConsoleManager->TryInitHttp(PortNum.Get(22222));
	}
});
#endif

#if WITH_EDITOR
#if defined(PYTHONSCRIPTPLUGIN_API)
#include "IPythonScriptPlugin.h"
using IXPythonScriptPlugin = IPythonScriptPlugin;
#else
#include "CoreTypes.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class IXPythonScriptPlugin : public IModuleInterface
{
public:
	static IXPythonScriptPlugin* Get()
	{
		static const FName ModuleName = "PythonScriptPlugin";
		return FModuleManager::GetModulePtr<IXPythonScriptPlugin>(ModuleName);
	}
	virtual bool IsPythonAvailable() const = 0;
	virtual bool ExecPythonCommand(const TCHAR* InPythonCommand) = 0;
	virtual bool ExecPythonCommandEx(struct FPythonCommandEx& InOutPythonCommand) = 0;
	virtual FSimpleMulticastDelegate& OnPythonInitialized() = 0;
	virtual FSimpleMulticastDelegate& OnPythonShutdown() = 0;
};
#endif

FXConsoleCommandLambdaFull XVar_PipelineRunPython(TEXT("z.PipelineRunPy"), TEXT("z.PipelineRunPy PythonScript"), [](const FString& PythonScript, UWorld* InWorld, FOutputDevice& Ar) {
	auto Ptr = IXPythonScriptPlugin::Get();
	if (ensureAlways(Ptr && Ptr->IsPythonAvailable()))
		Ptr->ExecPythonCommand(*PythonScript);
});
#endif

void ProcessXCommandFromCmdline(UWorld* InWorld, const TCHAR* Msg)
{
	UE_LOG(LogXConsoleManager, Log, TEXT("ProcessXCommandFromCmdline : %s for %s"), Msg, *GetNameSafe(InWorld));
#if GMP_EXTEND_CONSOLE
	if (InWorld)
	{
		GMPConsoleManger::ProcessXCommandFromCmdline(InWorld);
	}
#endif
}

#if WITH_EDITOR
void UXConsolePythonSupport::XConsolePauseCommandPipeline(UWorld* InWorld, const FString& Reason)
{
#if GMP_EXTEND_CONSOLE
	IXConsoleManager::PauseXConsoleCommandPipeline(InWorld, *Reason);
#endif
}

void UXConsolePythonSupport::XConsoleContinueCommandPipeline(UWorld* InWorld, const FString& Reason)
{
#if GMP_EXTEND_CONSOLE
	IXConsoleManager::ContinueXConsoleCommandPipeline(InWorld, *Reason);
#endif
}

int32 UXConsolePythonSupport::XConsoleGetPipelineInteger()
{
#if GMP_EXTEND_CONSOLE
	return IXConsoleManager::CommandPipelineInteger();
#else
	return 0;
#endif
}

void UXConsolePythonSupport::XConsoleSetPipelineInteger(const int32& InVal)
{
#if GMP_EXTEND_CONSOLE
	IXConsoleManager::CommandPipelineInteger(InVal);
#endif
}

FString UXConsolePythonSupport::XConsoleGetPipelineString()
{
#if GMP_EXTEND_CONSOLE
	return IXConsoleManager::CommandPipelineString();
#else
	return "";
#endif
}

void UXConsolePythonSupport::XConsoleSetPipelineString(const FString& InVal)
{
#if GMP_EXTEND_CONSOLE
	IXConsoleManager::CommandPipelineString(InVal);
#endif
}
#endif

UXConsoleExecCommandlet::UXConsoleExecCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = false;
}
int32 UXConsoleExecCommandlet::Main(const FString& Params)
{
	UE_LOG(LogXConsoleManager, Display, TEXT("UXConsoleExecCommandlet::Main : %s"), *Params);
	ProcessXCommandFromCmdline(GWorld, *Params);
	return 0;
}
