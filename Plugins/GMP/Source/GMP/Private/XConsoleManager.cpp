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
		return GMP::GameLocalObject<TArray<FXCmdGroup>>(InWorld);
	}
	struct FXConsoleCmdData
	{
		int32 PauseCnt = 0;
		int32 XCmdIndex = 0;
	};
	FXConsoleCmdData& GetXCmdData(UWorld* InWorld)
	{
		return GMP::GameLocalObject<FXConsoleCmdData>(InWorld);
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

		TSharedPtr<IHttpRouter> HttpRouter;
		bool TryInitHttp(TOptional<int32> InPortNum = {}, const EHttpServerRequestVerbs& HttpVerbs = EHttpServerRequestVerbs::VERB_POST)
		{
			if (HttpRouter)
				return true;
			int32 PortNum = InPortNum.Get(22222);
			if (!InPortNum.IsSet() && ensure(FCommandLine::IsInitialized()))
				FParse::Value(FCommandLine::Get(), TEXT("xcmdport="), PortNum);

			auto Module = static_cast<FHttpServerModule*>(InPortNum.IsSet() ? FModuleManager::Get().GetModule("HTTPServer") : FModuleManager::Get().LoadModule("HTTPServer"));
			if (!Module)
				return false;

			for (auto i = 0; i < 10; ++i)
			{
				HttpRouter = Module->GetHttpRouter(PortNum);
				if (HttpRouter)
					break;
				++PortNum;
			}
			if (!HttpRouter)
				return false;

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
						void Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const class FName& Category) override
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
					FHttpServerConverter Processor(reinterpret_cast<const char*>(Request.Body.GetData()), Request.Body.Num());
					TGuardValue<FOutputDevice*> XCmdGurad(XCmdAr, &Processor);
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
			return true;
		}
	};
#else
	struct FHttpRouteBinder
	{
		bool TryInitHttp() { return false; }
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

#if NO_CVARS
	class FConsoleManager
		: public IXConsoleManager
		, public FHttpRouteBinder
	{
	public:
		/** constructor */
		FConsoleManager()
			: bHistoryWasLoaded(false)
			, ThreadPropagationCallback(0)
			, bCallAllConsoleVariableSinks(true)
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

		// internally needed or ECVF_RenderThreadSafe
		IConsoleThreadPropagation* GetThreadPropagationCallback() { return ThreadPropagationCallback; }
		// internally needed or ECVF_RenderThreadSafe
		bool IsThreadPropagationThread() { IsInActualRenderingThread(); }

		void OnCVarChanged() { bCallAllConsoleVariableSinks = true; }

#if UE_5_00_OR_LATER
		virtual FConsoleVariableMulticastDelegate& OnCVarUnregistered() override { return ConsoleVariableUnregisteredDelegate; }
#endif
		// interface IConsoleManager -----------------------------------

		virtual IConsoleVariable* RegisterConsoleVariable(const TCHAR* Name, bool DefaultValue, const TCHAR* Help, uint32 Flags) override;
		virtual IConsoleVariable* RegisterConsoleVariable(const TCHAR* Name, int32 DefaultValue, const TCHAR* Help, uint32 Flags) override;
		virtual IConsoleVariable* RegisterConsoleVariable(const TCHAR* Name, float DefaultValue, const TCHAR* Help, uint32 Flags) override;
		virtual IConsoleVariable* RegisterConsoleVariable(const TCHAR* Name, const TCHAR* DefaultValue, const TCHAR* Help, uint32 Flags) override;
		virtual IConsoleVariable* RegisterConsoleVariable(const TCHAR* Name, const FString& DefaultValue, const TCHAR* Help, uint32 Flags) override;

		virtual IConsoleVariable* RegisterConsoleVariableRef(const TCHAR* Name, bool& RefValue, const TCHAR* Help, uint32 Flags) override;
		virtual IConsoleVariable* RegisterConsoleVariableRef(const TCHAR* Name, int32& RefValue, const TCHAR* Help, uint32 Flags) override;
		virtual IConsoleVariable* RegisterConsoleVariableRef(const TCHAR* Name, float& RefValue, const TCHAR* Help, uint32 Flags) override;
		virtual IConsoleVariable* RegisterConsoleVariableRef(const TCHAR* Name, FString& RefValue, const TCHAR* Help, uint32 Flags) override;
		virtual IConsoleVariable* RegisterConsoleVariableBitRef(const TCHAR* CVarName, const TCHAR* FlagName, uint32 BitNumber, uint8* Force0MaskPtr, uint8* Force1MaskPtr, const TCHAR* Help, uint32 Flags) override;

		virtual void CallAllConsoleVariableSinks() override;

		virtual FConsoleVariableSinkHandle RegisterConsoleVariableSink_Handle(const FConsoleCommandDelegate& Command) override;
		virtual void UnregisterConsoleVariableSink_Handle(FConsoleVariableSinkHandle Handle) override;

		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandDelegate& Command, uint32 Flags) override;
		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithArgsDelegate& Command, uint32 Flags) override;
		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldDelegate& Command, uint32 Flags) override;
		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldAndArgsDelegate& Command, uint32 Flags) override;
		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithArgsAndOutputDeviceDelegate& Command, uint32 Flags) override;
		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldArgsAndOutputDeviceDelegate& Command, uint32 Flags) override;
		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithOutputDeviceDelegate& Command, uint32 Flags) override;
#if UE_5_00_OR_LATER
		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithArgsAndOutputDeviceDelegate& Command, uint32 Flags) override;
		virtual FString FindConsoleObjectName(const IConsoleObject* Obj) const override
#else
		FString FindConsoleObjectName(const IConsoleObject* InVar) const
#endif
		{
			check(InVar);

			FScopeLock ScopeLock(&ConsoleObjectsSynchronizationObject);
			for (TMap<FString, IConsoleObject*>::TConstIterator PairIt(ConsoleObjects); PairIt; ++PairIt)
			{
				IConsoleObject* Var = PairIt.Value();

				if (Var == InVar)
				{
					const FString& Name = PairIt.Key();

					return Name;
				}
			}

			return FString();
		}

		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, uint32 Flags) override;
		virtual IConsoleObject* FindConsoleObject(const TCHAR* Name, bool bTrackFrequentCalls = true) const override;
		virtual IConsoleVariable* FindConsoleVariable(const TCHAR* Name, bool bTrackFrequentCalls = true) const override;
		virtual void ForEachConsoleObjectThatStartsWith(const FConsoleObjectVisitor& Visitor, const TCHAR* ThatStartsWith) const override;
		virtual void ForEachConsoleObjectThatContains(const FConsoleObjectVisitor& Visitor, const TCHAR* ThatContains) const override;
		virtual bool ProcessUserConsoleInput(const TCHAR* InInput, FOutputDevice& Ar, UWorld* InWorld) override;
		virtual void AddConsoleHistoryEntry(const TCHAR* Key, const TCHAR* Input) override {}
		virtual void GetConsoleHistory(const TCHAR* Key, TArray<FString>& Out) override {}
		virtual bool IsNameRegistered(const TCHAR* Name) const override
		{
			FScopeLock ScopeLock(&ConsoleObjectsSynchronizationObject);
			return ConsoleObjects.Contains(Name);
		}
		virtual void RegisterThreadPropagation(uint32 ThreadId, IConsoleThreadPropagation* InCallback) override;
		virtual void UnregisterConsoleObject(IConsoleObject* Object, bool bKeepState) override;

		template<typename T>
		IConsoleCommand* RegisterXConsoleCommandDelegate(const TCHAR* Name, const TCHAR* Help, const T& Command, uint32 Flags);
		virtual IConsoleCommand* RegisterXConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FXConsoleFullCmdDelegate& Command, uint32 Flags) override;
		virtual IConsoleCommand* RegisterXConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FXConsoleCommandWithArgsDelegate& Command, uint32 Flags) override;
		virtual IConsoleVariable* RegisterXConsoleVariable(const TCHAR* Name, const TCHAR* Help, uint32 Flags, const FProperty* InProp, void* Addr, bool bValueRef) override;
		bool ProcessUserXCommandInput(FString& Cmd, TArray<FString>& Args, FOutputDevice& Ar, UWorld* InWorld);
		bool IsProcessingCommand() const { return bIsProcessingCommamd; }

	private:  // ----------------------------------------------------
		/** Map of console variables and commands, indexed by the name of that command or variable */
		// [name] = pointer (pointer must not be 0)
		TMap<FString, IConsoleObject*> ConsoleObjects;

		bool bIsProcessingCommamd = false;
		TArray<FConsoleCommandDelegate> ConsoleVariableChangeSinks;

		IConsoleThreadPropagation* ThreadPropagationCallback;

		// if true the next call to CallAllConsoleVariableSinks() we will call all registered sinks
		bool bCallAllConsoleVariableSinks;
#if UE_5_00_OR_LATER
		FConsoleVariableMulticastDelegate ConsoleVariableUnregisteredDelegate;
#endif
		/** 
		* Used to prevent concurrent access to ConsoleObjects.
		* We don't aim to solve all concurrency problems (for example registering and unregistering a cvar on different threads, or reading a cvar from one thread while writing it from a different thread).
		* Rather we just ensure that operations on a cvar from one thread will not conflict with operations on another cvar from another thread.
	**/
		mutable FCriticalSection ConsoleObjectsSynchronizationObject;

		/** 
	 * @param Name must not be 0, must not be empty
	 * @param Obj must not be 0
	 * @return 0 if the name was already in use
	 */
		IConsoleObject* AddConsoleObject(const TCHAR* Name, IConsoleObject* Obj);

		/**
	 * @param Stream must not be 0
	 * @param Pattern must not be 0
	 */
		static bool MatchPartialName(const TCHAR* Stream, const TCHAR* Pattern);

		/** Returns true if Pattern is found in Stream, case insensitive. */
		static bool MatchSubstring(const TCHAR* Stream, const TCHAR* Pattern);

		/**
	 * Get string till whitespace, jump over whitespace
	 * inefficient but this code is not performance critical
	 */
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

		/** same as FindConsoleObject() but ECVF_CreatedFromIni are not filtered out (for internal use) */
		IConsoleObject* FindConsoleObjectUnfiltered(const TCHAR* Name) const
		{
			FScopeLock ScopeLock(&ConsoleObjectsSynchronizationObject);
			IConsoleObject* Var = ConsoleObjects.FindRef(Name);
			return Var;
		}

		/**
	 * Unregisters a console variable or command, if that object was registered.  For console variables, this will
	 * actually only "deactivate" the variable so if it becomes registered again the state may persist
	 * (unless bKeepState is false).
	 *
	 * @param	Name	Name of the console object to remove (not case sensitive)
	 * @param	bKeepState	if the current state is kept in memory until a cvar with the same name is registered
	 */
		void UnregisterConsoleObject(const TCHAR* Name, bool bKeepState);
	};

	DEFINE_LOG_CATEGORY_STATIC(LogXConsoleManager, Log, All);

	static inline bool IsWhiteSpace(TCHAR Value)
	{
		return Value == TCHAR(' ');
	}

	class FConsoleVariableBase : public IConsoleVariable
	{
	public:
		/**
	 * Constructor
	 * @param InHelp must not be 0, must not be empty
	 */
		FConsoleVariableBase(const TCHAR* InHelp, EConsoleVariableFlags InFlags)
			: Flags(InFlags)
			, bWarnedAboutThreadSafety(false)
		{
			SetHelp(InHelp);
		}

		// interface IConsoleVariable -----------------------------------
		virtual void Release() override { delete this; }

		virtual const TCHAR* GetHelp() const override { return *Help; }
		virtual void SetHelp(const TCHAR* Value) override
		{
			check(Value);

			Help = Value;
		}
		virtual EConsoleVariableFlags GetFlags() const override { return Flags; }
		virtual void SetFlags(const EConsoleVariableFlags Value) override { Flags = Value; }

		virtual class IConsoleVariable* AsVariable() override { return this; }

		/** Legacy funciton to add old single delegates to the new multicast delegate. */
		virtual void SetOnChangedCallback(const FConsoleVariableDelegate& Callback) override
		{
			OnChangedCallback.Remove(LegacyDelegateHandle);
			OnChangedCallback.Add(Callback);
		}

		/** Returns a multicast delegate with which to register. Called when this CVar changes. */
		virtual FConsoleVariableMulticastDelegate& OnChangedDelegate() { return OnChangedCallback; }

		// ------

		void OnChanged(EConsoleVariableFlags SetBy)
		{
			// SetBy can include set flags. Discard them here
			SetBy = EConsoleVariableFlags(SetBy & ~ECVF_SetFlagMask);

			// only change on main thread
			Flags = (EConsoleVariableFlags)(((uint32)Flags & ECVF_FlagMask) | SetBy);
			OnChangedCallback.Broadcast(this);
		}

	protected:  // -----------------------------------------
		// not using TCHAR* to allow chars support reloading of modules (otherwise we would keep a pointer into the module)
		FString Help;
		//
		EConsoleVariableFlags Flags;
		/** User function to call when the console variable is changed */
		FConsoleVariableMulticastDelegate OnChangedCallback;
		/** Store the handle to the delegate assigned via the legacy SetOnChangedCallback() so that the previous can be removed if called again. */
		FDelegateHandle LegacyDelegateHandle;

		/** True if this console variable has been used on the wrong thread and we have warned about it. */
		mutable bool bWarnedAboutThreadSafety;

		// @return 0:main thread, 1: render thread, later more
		uint32 GetShadowIndex() const
		{
			if ((uint32)Flags & ECVF_RenderThreadSafe)
			{
				return IsInGameThread() ? 0 : 1;
			}
			else
			{
				FConsoleManager& ConsoleManager = (FConsoleManager&)IXConsoleManager::Get();
				if (ConsoleManager.IsThreadPropagationThread() && FPlatformProcess::SupportsMultithreading())
				{
					if (!bWarnedAboutThreadSafety)
					{
						FString CVarName = ConsoleManager.FindConsoleObjectName(this);
						UE_LOG(LogXConsoleManager,
							   Warning,
							   TEXT("Console variable '%s' used in the render thread. Rendering artifacts could happen. Use ECVF_RenderThreadSafe or don't use in render thread."),
							   CVarName.IsEmpty() ? TEXT("unknown?") : *CVarName);
						bWarnedAboutThreadSafety = true;
					}
				}
				// other threads are not handled at the moment (e.g. sound)
			}

			return 0;
		}
	};

	class FConsoleCommandBase : public IConsoleCommand
	{
	public:
		/**
	 * Constructor
	 * @param InHelp must not be 0, must not be empty
	 */
		FConsoleCommandBase(const TCHAR* InHelp, EConsoleVariableFlags InFlags)
			: Help(InHelp)
			, Flags(InFlags)
		{
			check(InHelp);
			//check(*Help != 0); for now disabled as there callstack when we crash early during engine init
		}

		// interface IConsoleVariable -----------------------------------
		virtual void Release() override { delete this; }

		virtual const TCHAR* GetHelp() const override { return *Help; }
		virtual void SetHelp(const TCHAR* InValue) override
		{
			check(InValue);
			check(*InValue != 0);

			Help = InValue;
		}
		virtual EConsoleVariableFlags GetFlags() const override { return Flags; }
		virtual void SetFlags(const EConsoleVariableFlags Value) override { Flags = Value; }

		virtual struct IConsoleCommand* AsCommand() override { return this; }

	private:  // -----------------------------------------
		// not using TCHAR* to allow chars support reloading of modules (otherwise we would keep a pointer into the module)
		FString Help;

		EConsoleVariableFlags Flags;
	};

	template<class T>
	void OnCVarChange(T& Dst, const T& Src, EConsoleVariableFlags Flags, EConsoleVariableFlags SetBy)
	{
		FConsoleManager& ConsoleManager = (FConsoleManager&)IXConsoleManager::Get();

		if (IsInGameThread())
		{
			if ((Flags & ECVF_RenderThreadSafe) && ConsoleManager.GetThreadPropagationCallback())
			{
				// defer the change to be in order with other rendering commands
				ConsoleManager.GetThreadPropagationCallback()->OnCVarChange(Dst, Src);
			}
			else
			{
				// propagate the change right away
				Dst = Src;
			}
		}
		else
		{
			// CVar Changes can only be initiated from the main thread
			check(0);
		}

		if ((SetBy & ECVF_Set_NoSinkCall_Unsafe) == 0)
		{
			ConsoleManager.OnCVarChanged();
		}
	}

	// T: bool, int32, float, FString
	template<class T>
	class FConsoleVariable : public FConsoleVariableBase
	{
	public:
		FConsoleVariable(T DefaultValue, const TCHAR* Help, EConsoleVariableFlags Flags)
			: FConsoleVariableBase(Help, Flags)
			, Data(DefaultValue)
		{
		}

		// interface IConsoleVariable -----------------------------------
#if UE_5_04_OR_LATER
		virtual void Set(const TCHAR* InValue, EConsoleVariableFlags SetBy = ECVF_SetByCode, FName Tag = NAME_None) override
		{
			if (CanChange(SetBy))
			{
				TTypeFromString<T>::FromString(Data.ShadowedValue[0], InValue);
				OnChanged(SetBy);
			}
		}
#else
		virtual void Set(const TCHAR* InValue, EConsoleVariableFlags SetBy)
		{
			if (CanChange(SetBy))
			{
				TTypeFromString<T>::FromString(Data.ShadowedValue[0], InValue);
				OnChanged(SetBy);
			}
		}
#endif

		virtual bool GetBool() const override;
		virtual int32 GetInt() const override;
		virtual float GetFloat() const override;
		virtual FString GetString() const override;

		virtual bool IsVariableBool() const override { return false; }
		virtual bool IsVariableInt() const override { return false; }
		virtual bool IsVariableFloat() const override { return false; }
		virtual bool IsVariableString() const override { return false; }

		virtual class TConsoleVariableData<bool>* AsVariableBool() override { return nullptr; }
		virtual class TConsoleVariableData<int32>* AsVariableInt() override { return nullptr; }
		virtual class TConsoleVariableData<float>* AsVariableFloat() override { return nullptr; }
		virtual class TConsoleVariableData<FString>* AsVariableString() override { return nullptr; }

		// ----------------------------------------------------
	private:
		const T& Value() const
		{
			// remove const
			FConsoleVariable<T>* This = (FConsoleVariable<T>*)this;
			return This->Data.GetReferenceOnAnyThread();
		}

		void OnChanged(EConsoleVariableFlags SetBy)
		{
			// propagate from main thread to render thread
			OnCVarChange(Data.ShadowedValue[1], Data.ShadowedValue[0], Flags, SetBy);
			FConsoleVariableBase::OnChanged(SetBy);
		}
		TConsoleVariableData<T> Data;
	};

	// specialization for all

	template<>
	bool FConsoleVariable<bool>::IsVariableBool() const
	{
		return true;
	}
	template<>
	bool FConsoleVariable<int32>::IsVariableInt() const
	{
		return true;
	}
	template<>
	bool FConsoleVariable<float>::IsVariableFloat() const
	{
		return true;
	}
	template<>
	bool FConsoleVariable<FString>::IsVariableString() const
	{
		return true;
	}

	// specialization for bool

	template<>
	bool FConsoleVariable<bool>::GetBool() const
	{
		return Value();
	}
	template<>
	int32 FConsoleVariable<bool>::GetInt() const
	{
		return Value() ? 1 : 0;
	}
	template<>
	float FConsoleVariable<bool>::GetFloat() const
	{
		return Value() ? 1.0f : 0.0f;
	}
	template<>
	FString FConsoleVariable<bool>::GetString() const
	{
		return Value() ? TEXT("true") : TEXT("false");
	}
	template<>
	TConsoleVariableData<bool>* FConsoleVariable<bool>::AsVariableBool()
	{
		return &Data;
	}

	// specialization for int32

	template<>
	bool FConsoleVariable<int32>::GetBool() const
	{
		return Value() != 0;
	}
	template<>
	int32 FConsoleVariable<int32>::GetInt() const
	{
		return Value();
	}
	template<>
	float FConsoleVariable<int32>::GetFloat() const
	{
		return (float)Value();
	}
	template<>
	FString FConsoleVariable<int32>::GetString() const
	{
		return FString::Printf(TEXT("%d"), Value());
	}

	template<>
	TConsoleVariableData<int32>* FConsoleVariable<int32>::AsVariableInt()
	{
		return &Data;
	}

	// specialization for float

	template<>
	bool FConsoleVariable<float>::GetBool() const
	{
		return Value() != 0;
	}
	template<>
	int32 FConsoleVariable<float>::GetInt() const
	{
		return (int32)Value();
	}
	template<>
	float FConsoleVariable<float>::GetFloat() const
	{
		return Value();
	}
	template<>
	FString FConsoleVariable<float>::GetString() const
	{
		return FString::Printf(TEXT("%g"), Value());
	}
	template<>
	TConsoleVariableData<float>* FConsoleVariable<float>::AsVariableFloat()
	{
		return &Data;
	}

	// specialization for FString

	template<>
	void FConsoleVariable<FString>::Set(const TCHAR* InValue, EConsoleVariableFlags SetBy)
	{
		if (CanChange(SetBy))
		{
			Data.ShadowedValue[0] = InValue;
			OnChanged(SetBy);
		}
	}
	template<>
	bool FConsoleVariable<FString>::GetBool() const
	{
		bool OutValue = false;
		TTypeFromString<bool>::FromString(OutValue, *Value());
		return OutValue;
	}
	template<>
	int32 FConsoleVariable<FString>::GetInt() const
	{
		int32 OutValue = 0;
		TTypeFromString<int32>::FromString(OutValue, *Value());
		return OutValue;
	}
	template<>
	float FConsoleVariable<FString>::GetFloat() const
	{
		float OutValue = 0.0f;
		TTypeFromString<float>::FromString(OutValue, *Value());
		return OutValue;
	}
	template<>
	FString FConsoleVariable<FString>::GetString() const
	{
		return Value();
	}
	template<>
	TConsoleVariableData<FString>* FConsoleVariable<FString>::AsVariableString()
	{
		return &Data;
	}

	// ----

	// T: int32, float, bool
	template<class T>
	class FConsoleVariableRef : public FConsoleVariableBase
	{
	public:
		FConsoleVariableRef(T& InRefValue, const TCHAR* Help, EConsoleVariableFlags Flags)
			: FConsoleVariableBase(Help, Flags)
			, RefValue(InRefValue)
			, MainValue(InRefValue)
		{
		}

		// interface IConsoleVariable -----------------------------------
		virtual void Set(const TCHAR* InValue, EConsoleVariableFlags SetBy)
		{
			if (CanChange(SetBy))
			{
				TTypeFromString<T>::FromString(MainValue, InValue);
				OnChanged(SetBy);
			}
		}

		virtual bool IsVariableBool() const override { return false; }
		virtual bool IsVariableInt() const override { return false; }
		virtual bool IsVariableFloat() const override { return false; }
		virtual bool IsVariableString() const override { return false; }

		virtual bool GetBool() const { return (bool)MainValue; }
		virtual int32 GetInt() const { return (int32)MainValue; }
		virtual float GetFloat() const { return (float)MainValue; }
		virtual FString GetString() const { return TTypeToString<T>::ToString(MainValue); }

	private:  // ----------------------------------------------------
		// reference the the value (should not be changed from outside), if ECVF_RenderThreadSafe this is the render thread version, otherwise same as MainValue
		T& RefValue;
		//  main thread version
		T MainValue;

		const T& Value() const
		{
			uint32 Index = GetShadowIndex();
			checkSlow(Index < 2);
			return (Index == 0) ? MainValue : RefValue;
		}

		void OnChanged(EConsoleVariableFlags SetBy)
		{
			if (CanChange(SetBy))
			{
				// propagate from main thread to render thread or to reference
				OnCVarChange(RefValue, MainValue, Flags, SetBy);
				FConsoleVariableBase::OnChanged(SetBy);
			}
		}
	};

	template<>
	bool FConsoleVariableRef<bool>::IsVariableBool() const
	{
		return true;
	}

	template<>
	bool FConsoleVariableRef<int32>::IsVariableInt() const
	{
		return true;
	}

	template<>
	bool FConsoleVariableRef<float>::IsVariableFloat() const
	{
		return true;
	}

	// specialization for float

	template<>
	FString FConsoleVariableRef<float>::GetString() const
	{
		// otherwise we get 2.1f would become "2.100000"
		return FString::SanitizeFloat(RefValue);
	}

	// string version

	class FConsoleVariableStringRef : public FConsoleVariableBase
	{
	public:
		FConsoleVariableStringRef(FString& InRefValue, const TCHAR* Help, EConsoleVariableFlags Flags)
			: FConsoleVariableBase(Help, Flags)
			, RefValue(InRefValue)
			, MainValue(InRefValue)
		{
		}

		// interface IConsoleVariable -----------------------------------
		virtual void Set(const TCHAR* InValue, EConsoleVariableFlags SetBy)
		{
			if (CanChange(SetBy))
			{
				MainValue = InValue;
				OnChanged(SetBy);
			}
		}
		virtual bool GetBool() const
		{
			bool Result = false;
			TTypeFromString<bool>::FromString(Result, *MainValue);
			return Result;
		}
		virtual int32 GetInt() const
		{
			int32 Result = 0;
			TTypeFromString<int32>::FromString(Result, *MainValue);
			return Result;
		}
		virtual float GetFloat() const
		{
			float Result = 0.0f;
			TTypeFromString<float>::FromString(Result, *MainValue);
			return Result;
		}
		virtual FString GetString() const { return MainValue; }
		virtual bool IsVariableString() const override { return true; }

	private:  // ----------------------------------------------------
		// reference the the value (should not be changed from outside), if ECVF_RenderThreadSafe this is the render thread version, otherwise same as MainValue
		FString& RefValue;
		// main thread version
		FString MainValue;

		const FString& Value() const
		{
			uint32 Index = GetShadowIndex();
			checkSlow(Index < 2);
			return (Index == 0) ? MainValue : RefValue;
		}

		void OnChanged(EConsoleVariableFlags SetBy)
		{
			if (CanChange(SetBy))
			{
				// propagate from main thread to render thread or to reference
				OnCVarChange(RefValue, MainValue, Flags, SetBy);
				FConsoleVariableBase::OnChanged(SetBy);
			}
		}
	};

	class FConsoleVariableBitRef : public FConsoleVariableBase
	{
	public:
		FConsoleVariableBitRef(const TCHAR* FlagName, uint32 InBitNumber, uint8* InForce0MaskPtr, uint8* InForce1MaskPtr, const TCHAR* Help, EConsoleVariableFlags Flags)
			: FConsoleVariableBase(Help, Flags)
			, Force0MaskPtr(InForce0MaskPtr)
			, Force1MaskPtr(InForce1MaskPtr)
			, BitNumber(InBitNumber)
		{
		}

		// interface IConsoleVariable -----------------------------------
		virtual void Set(const TCHAR* InValue, EConsoleVariableFlags SetBy)
		{
			if (CanChange(SetBy))
			{
				int32 Value = FCString::Atoi(InValue);

				check(IsInGameThread());

				FMath::SetBoolInBitField(Force0MaskPtr, BitNumber, Value == 0);
				FMath::SetBoolInBitField(Force1MaskPtr, BitNumber, Value == 1);

				OnChanged(SetBy);
			}
		}
		virtual bool GetBool() const { return GetInt() != 0; }
		virtual int32 GetInt() const
		{
			// we apply the bitmask on game thread (showflags) so we don't have to do any special thread handling
			check(IsInGameThread());

			bool Force0 = FMath::ExtractBoolFromBitfield(Force0MaskPtr, BitNumber);
			bool Force1 = FMath::ExtractBoolFromBitfield(Force1MaskPtr, BitNumber);

			if (!Force0 && !Force1)
			{
				// not enforced to be 0 or 1
				return 2;
			}

			return Force1 ? 1 : 0;
		}
		virtual float GetFloat() const { return (float)GetInt(); }
		virtual FString GetString() const { return FString::Printf(TEXT("%d"), GetInt()); }

	private:  // ----------------------------------------------------
		uint8* Force0MaskPtr;
		uint8* Force1MaskPtr;
		uint32 BitNumber;
	};

	IConsoleVariable* FConsoleManager::RegisterConsoleVariableBitRef(const TCHAR* CVarName, const TCHAR* FlagName, uint32 BitNumber, uint8* Force0MaskPtr, uint8* Force1MaskPtr, const TCHAR* Help, uint32 Flags)
	{
		return AddConsoleObject(CVarName, new FConsoleVariableBitRef(FlagName, BitNumber, Force0MaskPtr, Force1MaskPtr, Help, (EConsoleVariableFlags)Flags))->AsVariable();
	}

	void FConsoleManager::CallAllConsoleVariableSinks()
	{
		check(IsInGameThread());

		if (bCallAllConsoleVariableSinks)
		{
			for (uint32 i = 0; i < (uint32)ConsoleVariableChangeSinks.Num(); ++i)
			{
				ConsoleVariableChangeSinks[i].ExecuteIfBound();
			}

			bCallAllConsoleVariableSinks = false;
		}
	}

	FConsoleVariableSinkHandle FConsoleManager::RegisterConsoleVariableSink_Handle(const FConsoleCommandDelegate& Command)
	{
		ConsoleVariableChangeSinks.Add(Command);
		return FConsoleVariableSinkHandle(Command.GetHandle());
	}

	void FConsoleManager::UnregisterConsoleVariableSink_Handle(FConsoleVariableSinkHandle Handle)
	{
		ConsoleVariableChangeSinks.RemoveAll([=](const FConsoleCommandDelegate& Delegate) { return Handle.HasSameHandle(Delegate); });
	}

	class FConsoleCommand : public FConsoleCommandBase
	{
	public:
		FConsoleCommand(const FConsoleCommandDelegate& InitDelegate, const TCHAR* InitHelp, const EConsoleVariableFlags InitFlags)
			: FConsoleCommandBase(InitHelp, InitFlags)
			, Delegate(InitDelegate)
		{
		}

		// interface IConsoleCommand -----------------------------------
		virtual bool Execute(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& OutputDevice) override
		{
			// NOTE: Args are ignored for FConsoleCommand.  Use FConsoleCommandWithArgs if you need parameters.
			return Delegate.ExecuteIfBound();
		}

	private:
		/** User function to call when the console command is executed */
		FConsoleCommandDelegate Delegate;
	};

	class FConsoleCommandWithArgs : public FConsoleCommandBase
	{
	public:
		FConsoleCommandWithArgs(const FConsoleCommandWithArgsDelegate& InitDelegate, const TCHAR* InitHelp, const EConsoleVariableFlags InitFlags)
			: FConsoleCommandBase(InitHelp, InitFlags)
			, Delegate(InitDelegate)
		{
		}

		// interface IConsoleCommand -----------------------------------
		virtual bool Execute(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& OutputDevice) override { return Delegate.ExecuteIfBound(Args); }

	private:
		/** User function to call when the console command is executed */
		FConsoleCommandWithArgsDelegate Delegate;
	};

	/* Console command that can be given a world parameter. */
	class FConsoleCommandWithWorld : public FConsoleCommandBase
	{
	public:
		FConsoleCommandWithWorld(const FConsoleCommandWithWorldDelegate& InitDelegate, const TCHAR* InitHelp, const EConsoleVariableFlags InitFlags)
			: FConsoleCommandBase(InitHelp, InitFlags)
			, Delegate(InitDelegate)
		{
		}

		// interface IConsoleCommand -----------------------------------
		virtual bool Execute(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& OutputDevice) override { return Delegate.ExecuteIfBound(InWorld); }

	private:
		/** User function to call when the console command is executed */
		FConsoleCommandWithWorldDelegate Delegate;
	};

	/* Console command that can be given a world parameter and args. */
	class FConsoleCommandWithWorldAndArgs : public FConsoleCommandBase
	{
	public:
		FConsoleCommandWithWorldAndArgs(const FConsoleCommandWithWorldAndArgsDelegate& InitDelegate, const TCHAR* InitHelp, const EConsoleVariableFlags InitFlags)
			: FConsoleCommandBase(InitHelp, InitFlags)
			, Delegate(InitDelegate)
		{
		}

		// interface IConsoleCommand -----------------------------------
		virtual bool Execute(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& OutputDevice) override { return Delegate.ExecuteIfBound(Args, InWorld); }

	private:
		/** User function to call when the console command is executed */
		FConsoleCommandWithWorldAndArgsDelegate Delegate;
	};

	/* Console command that can be given a world parameter, args and an output device. */
	class FConsoleCommandWithArgsAndOutputDevice : public FConsoleCommandBase
	{
	public:
		FConsoleCommandWithArgsAndOutputDevice(const FConsoleCommandWithArgsAndOutputDeviceDelegate& InitDelegate, const TCHAR* InitHelp, const EConsoleVariableFlags InitFlags)
			: FConsoleCommandBase(InitHelp, InitFlags)
			, Delegate(InitDelegate)
		{
		}

		// interface IConsoleCommand -----------------------------------
		virtual bool Execute(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& OutputDevice) override { return Delegate.ExecuteIfBound(Args, InWorld, OutputDevice); }

	private:
		/** User function to call when the console command is executed */
		FConsoleCommandWithArgsAndOutputDeviceDelegate Delegate;
	};

	/* Console command that can be given a world parameter, args and an output device. */
	class FConsoleCommandWithWorldArgsAndOutputDevice : public FConsoleCommandBase
	{
	public:
		FConsoleCommandWithWorldArgsAndOutputDevice(const FConsoleCommandWithWorldArgsAndOutputDeviceDelegate& InitDelegate, const TCHAR* InitHelp, const EConsoleVariableFlags InitFlags)
			: FConsoleCommandBase(InitHelp, InitFlags)
			, Delegate(InitDelegate)
		{
		}

		// interface IConsoleCommand -----------------------------------
		virtual bool Execute(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& OutputDevice) override { return Delegate.ExecuteIfBound(Args, InWorld, OutputDevice); }

	private:
		/** User function to call when the console command is executed */
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate Delegate;
	};

	/* Console command that can be given an output device. */
	class FConsoleCommandWithOutputDevice : public FConsoleCommandBase
	{
	public:
		FConsoleCommandWithOutputDevice(const FConsoleCommandWithOutputDeviceDelegate& InitDelegate, const TCHAR* InitHelp, const EConsoleVariableFlags InitFlags)
			: FConsoleCommandBase(InitHelp, InitFlags)
			, Delegate(InitDelegate)
		{
		}

		// interface IConsoleCommand -----------------------------------
		virtual bool Execute(const TArray<FString>& Args, UWorld* InWorld, FOutputDevice& OutputDevice) override { return Delegate.ExecuteIfBound(OutputDevice); }

	private:
		/** User function to call when the console command is executed */
		FConsoleCommandWithOutputDeviceDelegate Delegate;
	};

	// only needed for auto completion of Exec commands
	class FConsoleCommandExec : public FConsoleCommandBase
	{
	public:
		FConsoleCommandExec(const TCHAR* InitHelp, const EConsoleVariableFlags InitFlags)
			: FConsoleCommandBase(InitHelp, InitFlags)
		{
		}

		// interface IConsoleCommand -----------------------------------
		virtual bool Execute(const TArray<FString>& Args, UWorld* InCmdWorld, FOutputDevice& OutputDevice) override { return false; }
	};

	IConsoleVariable* FConsoleManager::RegisterConsoleVariable(const TCHAR* Name, bool DefaultValue, const TCHAR* Help, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleVariable<bool>(DefaultValue, Help, (EConsoleVariableFlags)Flags))->AsVariable();
	}

	IConsoleVariable* FConsoleManager::RegisterConsoleVariable(const TCHAR* Name, int32 DefaultValue, const TCHAR* Help, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleVariable<int32>(DefaultValue, Help, (EConsoleVariableFlags)Flags))->AsVariable();
	}

	IConsoleVariable* FConsoleManager::RegisterConsoleVariable(const TCHAR* Name, float DefaultValue, const TCHAR* Help, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleVariable<float>(DefaultValue, Help, (EConsoleVariableFlags)Flags))->AsVariable();
	}

	IConsoleVariable* FConsoleManager::RegisterConsoleVariable(const TCHAR* Name, const TCHAR* DefaultValue, const TCHAR* Help, uint32 Flags)
	{
		return RegisterConsoleVariable(Name, FString(DefaultValue), Help, Flags);
	}

	IConsoleVariable* FConsoleManager::RegisterConsoleVariable(const TCHAR* Name, const FString& DefaultValue, const TCHAR* Help, uint32 Flags)
	{
		// not supported
		check((Flags & (uint32)ECVF_RenderThreadSafe) == 0);
		return AddConsoleObject(Name, new FConsoleVariable<FString>(DefaultValue, Help, (EConsoleVariableFlags)Flags))->AsVariable();
	}

	IConsoleVariable* FConsoleManager::RegisterConsoleVariableRef(const TCHAR* Name, bool& RefValue, const TCHAR* Help, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleVariableRef<bool>(RefValue, Help, (EConsoleVariableFlags)Flags))->AsVariable();
	}

	IConsoleVariable* FConsoleManager::RegisterConsoleVariableRef(const TCHAR* Name, int32& RefValue, const TCHAR* Help, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleVariableRef<int32>(RefValue, Help, (EConsoleVariableFlags)Flags))->AsVariable();
	}

	IConsoleVariable* FConsoleManager::RegisterConsoleVariableRef(const TCHAR* Name, float& RefValue, const TCHAR* Help, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleVariableRef<float>(RefValue, Help, (EConsoleVariableFlags)Flags))->AsVariable();
	}

	IConsoleVariable* FConsoleManager::RegisterConsoleVariableRef(const TCHAR* Name, FString& RefValue, const TCHAR* Help, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleVariableStringRef(RefValue, Help, (EConsoleVariableFlags)Flags))->AsVariable();
	}

	IConsoleCommand* FConsoleManager::RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandDelegate& Command, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleCommand(Command, Help, (EConsoleVariableFlags)Flags))->AsCommand();
	}

	IConsoleCommand* FConsoleManager::RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleCommandExec(Help, (EConsoleVariableFlags)Flags))->AsCommand();
	}

	IConsoleCommand* FConsoleManager::RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithArgsDelegate& Command, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleCommandWithArgs(Command, Help, (EConsoleVariableFlags)Flags))->AsCommand();
	}

	IConsoleCommand* FConsoleManager::RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldDelegate& Command, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleCommandWithWorld(Command, Help, (EConsoleVariableFlags)Flags))->AsCommand();
	}

	IConsoleCommand* FConsoleManager::RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldAndArgsDelegate& Command, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleCommandWithWorldAndArgs(Command, Help, (EConsoleVariableFlags)Flags))->AsCommand();
	}

	IConsoleCommand* FConsoleManager::RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithArgsAndOutputDeviceDelegate& Command, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleCommandWithArgsAndOutputDevice(Command, Help, (EConsoleVariableFlags)Flags))->AsCommand();
	}

	IConsoleCommand* FConsoleManager::RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldArgsAndOutputDeviceDelegate& Command, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleCommandWithWorldArgsAndOutputDevice(Command, Help, (EConsoleVariableFlags)Flags))->AsCommand();
	}

	IConsoleCommand* FConsoleManager::RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithOutputDeviceDelegate& Command, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleCommandWithOutputDevice(Command, Help, (EConsoleVariableFlags)Flags))->AsCommand();
	}

#if UE_5_00_OR_LATER
	IConsoleCommand* FConsoleManager::RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithArgsAndOutputDeviceDelegate& Command, uint32 Flags)
	{
		return AddConsoleObject(Name, new FConsoleCommandWithArgsAndOutputDevice(Command, Help, (EConsoleVariableFlags)Flags))->AsCommand();
	}
#endif

	IConsoleVariable* FConsoleManager::FindConsoleVariable(const TCHAR* Name, bool bTrackFrequentCalls) const
	{
		IConsoleObject* Obj = FindConsoleObject(Name, bTrackFrequentCalls);

		if (Obj)
		{
			if (Obj->TestFlags(ECVF_Unregistered))
			{
				return 0;
			}

			return Obj->AsVariable();
		}

		return 0;
	}

	IConsoleObject* FConsoleManager::FindConsoleObject(const TCHAR* Name, bool bTrackFrequentCalls) const
	{
		IConsoleObject* CVar = FindConsoleObjectUnfiltered(Name);

		if (CVar && CVar->TestFlags(ECVF_CreatedFromIni))
		{
			return 0;
		}

		return CVar;
	}

	void FConsoleManager::UnregisterConsoleObject(IConsoleObject* CVar, bool bKeepState)
	{
		if (!CVar)
		{
			return;
		}
		FScopeLock ScopeLock(&ConsoleObjectsSynchronizationObject);

		// Slow search for console object
		const FString ObjName = FindConsoleObjectName(CVar);
		if (!ObjName.IsEmpty())
		{
			UnregisterConsoleObject(*ObjName, bKeepState);
		}
	}

	void FConsoleManager::UnregisterConsoleObject(const TCHAR* Name, bool bKeepState)
	{
		FScopeLock ScopeLock(&ConsoleObjectsSynchronizationObject);

		IConsoleObject* Object = FindConsoleObject(Name);

		if (Object)
		{
			IConsoleVariable* CVar = Object->AsVariable();

			if (CVar)
			{
				ConsoleVariableUnregisteredDelegate.Broadcast(CVar);
			}

			if (CVar && bKeepState)
			{
				// to be able to restore the value if we just recompile a module
				CVar->SetFlags(ECVF_Unregistered);
			}
			else
			{
				ConsoleObjects.Remove(Name);
				Object->Release();
			}
		}
	}

	void FConsoleManager::ForEachConsoleObjectThatStartsWith(const FConsoleObjectVisitor& Visitor, const TCHAR* ThatStartsWith) const
	{
		check(Visitor.IsBound());
		check(ThatStartsWith);

		//@caution, potential deadlock if the visitor tries to call back into the cvar system. Best not to do this, but we could capture and array of them, then release the lock, then dispatch the visitor.
		FScopeLock ScopeLock(&ConsoleObjectsSynchronizationObject);
		for (TMap<FString, IConsoleObject*>::TConstIterator PairIt(ConsoleObjects); PairIt; ++PairIt)
		{
			const FString& Name = PairIt.Key();
			IConsoleObject* CVar = PairIt.Value();

			if (MatchPartialName(*Name, ThatStartsWith))
			{
				Visitor.Execute(*Name, CVar);
			}
		}
	}

	void FConsoleManager::ForEachConsoleObjectThatContains(const FConsoleObjectVisitor& Visitor, const TCHAR* ThatContains) const
	{
		check(Visitor.IsBound());
		check(ThatContains);

		TArray<FString> ThatContainsArray;
		FString(ThatContains).ParseIntoArray(ThatContainsArray, TEXT(" "), true);
		int32 ContainsStringLength = FCString::Strlen(ThatContains);

		//@caution, potential deadlock if the visitor tries to call back into the cvar system. Best not to do this, but we could capture and array of them, then release the lock, then dispatch the visitor.
		FScopeLock ScopeLock(&ConsoleObjectsSynchronizationObject);
		for (TMap<FString, IConsoleObject*>::TConstIterator PairIt(ConsoleObjects); PairIt; ++PairIt)
		{
			const FString& Name = PairIt.Key();
			IConsoleObject* CVar = PairIt.Value();

			if (ContainsStringLength == 1)
			{
				if (MatchPartialName(*Name, ThatContains))
				{
					Visitor.Execute(*Name, CVar);
				}
			}
			else
			{
				bool bMatchesAll = true;

				for (int32 MatchIndex = 0; MatchIndex < ThatContainsArray.Num(); MatchIndex++)
				{
					if (!MatchSubstring(*Name, *ThatContainsArray[MatchIndex]))
					{
						bMatchesAll = false;
					}
				}

				if (bMatchesAll && ThatContainsArray.Num() > 0)
				{
					Visitor.Execute(*Name, CVar);
				}
			}
		}
	}

	bool FConsoleManager::ProcessUserConsoleInput(const TCHAR* InInput, FOutputDevice& Ar, UWorld* InWorld)
	{
		check(InInput);

		const TCHAR* It = InInput;

		FString Param1 = GetTextSection(It);
		if (Param1.IsEmpty())
		{
			return false;
		}

		// Remove a trailing ? if present, to kick it into help mode
		const bool bCommandEndedInQuestion = Param1.EndsWith(TEXT("?"), ESearchCase::CaseSensitive);
		if (bCommandEndedInQuestion)
		{
			Param1.MidInline(0, Param1.Len() - 1, false);
		}

		// look for the <cvar>@<platform> syntax
		FName PlatformName;
		if (Param1.Contains(TEXT("@")))
		{
			FString Left, Right;
			Param1.Split(TEXT("@"), &Left, &Right);

			if (Left.Len() && Right.Len())
			{
				Param1 = Left;
				PlatformName = *Right;
			}
		}

		IConsoleObject* CObj = FindConsoleObject(*Param1);
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

		// fix case for nicer printout
		Param1 = FindConsoleObjectName(CObj);

		IConsoleCommand* CCmd = CObj->AsCommand();
		IConsoleVariable* CVar = CObj->AsVariable();
		TSharedPtr<IConsoleVariable> PlatformCVar;

		if (PlatformName != NAME_None)
		{
			if (CVar == nullptr)
			{
				Ar.Logf(TEXT("Ignoring platform portion (@%s), which is only valid for looking up CVars"), *PlatformName.ToString());
			}
			else
			{
#if defined(ALLOW_OTHER_PLATFORM_CONFIG) && ALLOW_OTHER_PLATFORM_CONFIG
				PlatformCVar = CVar->GetPlatformValueVariable(PlatformName);
				CVar = PlatformCVar.Get();
				if (!CVar)
				{
					Ar.Logf(TEXT("Unable find CVar %s for platform %s (possibly invalid platform name?)"), *Param1, *PlatformName.ToString());
					return false;
				}
#else
				Ar.Logf(TEXT("Unable to lookup a CVar value on another platform in this build"));
				return false;
#endif
			}
		}

		if (CCmd)
		{
			// Process command
			// Build up argument list
			TArray<FString> Args;
			FString(It).ParseIntoArrayWS(Args);

			const bool bShowHelp = bCommandEndedInQuestion || ((Args.Num() == 1) && (Args[0] == TEXT("?")));
			if (bShowHelp)
			{
				// get help
				Ar.Logf(TEXT("HELP for '%s':\n%s"), *Param1, CCmd->GetHelp());
			}
			else
			{
				// if a delegate was bound, we execute it and it should return true,
				// otherwise it was a Exec console command and this returns FASLE
				return CCmd->Execute(Args, InWorld, Ar);
			}
		}
		else if (CVar)
		{
			// Process variable
			bool bShowHelp = bCommandEndedInQuestion;
			bool bShowCurrentState = false;

			if (*It == 0)
			{
				bShowCurrentState = true;
			}
			else
			{
				FString Param2 = FString(It).TrimStartAndEnd();

				const bool bReadOnly = CVar->TestFlags(ECVF_ReadOnly);

				if (Param2.Len() >= 2)
				{
					if (Param2[0] == (TCHAR)'\"' && Param2[Param2.Len() - 1] == (TCHAR)'\"')
					{
						Param2.MidInline(1, Param2.Len() - 2, false);
					}
					// this is assumed to be unintended e.g. copy and paste accident from ini file
					if (Param2.Len() > 0 && Param2[0] == (TCHAR)'=')
					{
						Ar.Logf(TEXT("Warning: Processing the console input parameters the leading '=' is ignored (only needed for ini files)."));
						Param2.MidInline(1, Param2.Len() - 1, false);
					}
				}

				if (Param2 == TEXT("?"))
				{
					bShowHelp = true;
				}
				else
				{
					if (PlatformName != NAME_None)
					{
						Ar.Logf(TEXT("Error: Unable to set a value for %s another platform!"), *Param1);
					}
					else if (bReadOnly)
					{
						Ar.Logf(TEXT("Error: %s is read only!"), *Param1);
					}
					else
					{
						// set value
						CVar->Set(*Param2, ECVF_SetByConsole);

						Ar.Logf(TEXT("%s = \"%s\""), *Param1, *CVar->GetString());
					}
				}
			}

			if (bShowHelp)
			{
				// get help
				const bool bReadOnly = CVar->TestFlags(ECVF_ReadOnly);
				Ar.Logf(TEXT("HELP for '%s'%s:\n%s"), *Param1, bReadOnly ? TEXT("(ReadOnly)") : TEXT(""), CVar->GetHelp());
				bShowCurrentState = true;
			}

			if (bShowCurrentState)
			{
				Ar.Logf(TEXT("%s = \"%s\"      LastSetBy: %s"), *Param1, *CVar->GetString(), GetSetByTCHAR(CVar->GetFlags()));
			}
		}

		return true;
	}

	IConsoleObject* FConsoleManager::AddConsoleObject(const TCHAR* Name, IConsoleObject* Obj)
	{
		check(Name);
		check(*Name != 0);
		check(Obj);

		FScopeLock ScopeLock(&ConsoleObjectsSynchronizationObject);  // we will lock on the entire add process
		IConsoleObject* ExistingObj = ConsoleObjects.FindRef(Name);

		if (Obj->GetFlags() & ECVF_Scalability)
		{
			// Scalability options cannot be cheats - otherwise using the options menu would mean cheating
			check(!(Obj->GetFlags() & ECVF_Cheat));
			// Scalability options cannot be read only - otherwise the options menu cannot work
			check(!(Obj->GetFlags() & ECVF_ReadOnly));
		}

		if (Obj->GetFlags() & ECVF_RenderThreadSafe)
		{
			if (Obj->AsCommand())
			{
				// This feature is not supported for console commands
				check(0);
			}
		}

		if (ExistingObj)
		{
			// An existing console object was found that has the same name as the object being registered.
			// In most cases this is not allowed, but if there is a variable with the same name and is
			// in an 'unregistered' state or we're hot reloading dlls, we may be able to replace or update that variable.
#if WITH_HOT_RELOAD
			const bool bCanUpdateOrReplaceObj = (ExistingObj->AsVariable() || ExistingObj->AsCommand()) && (GIsHotReload || ExistingObj->TestFlags(ECVF_Unregistered));
#else
			const bool bCanUpdateOrReplaceObj = ExistingObj->AsVariable() && ExistingObj->TestFlags(ECVF_Unregistered);
#endif
			if (!bCanUpdateOrReplaceObj)
			{
				// NOTE: The reason we don't assert here is because when using HotReload, locally-initialized static console variables will be
				//       re-registered, and it's desirable for the new variables to clobber the old ones.  Because this happen outside of the
				//       hot reload stack frame (GIsHotReload=true), we can't detect and handle only those cases, so we opt to warn instead.
				UE_LOG(LogXConsoleManager, Warning, TEXT("Console object named '%s' already exists but is being registered again, but we weren't expected it to be! (FConsoleManager::AddConsoleObject)"), Name);
			}

			IConsoleVariable* ExistingVar = ExistingObj->AsVariable();
			IConsoleCommand* ExistingCmd = ExistingObj->AsCommand();
			const int ExistingType = ExistingVar ? ExistingCmd ? 3 : 2 : 1;

			IConsoleVariable* Var = Obj->AsVariable();
			IConsoleCommand* Cmd = Obj->AsCommand();
			const int NewType = Var ? Cmd ? 3 : 2 : 1;

			// Validate that we have the same type for the existing console object and for the new one, because
			// never allowed to replace a command with a variable or vice-versa
			if (ExistingType != NewType)
			{
				UE_LOG(LogXConsoleManager, Fatal, TEXT("Console object named '%s' can't be replaced with the new one of different type!"), Name);
			}

			if (ExistingVar && Var)
			{
				if (ExistingVar->TestFlags(ECVF_CreatedFromIni))
				{
					// This is to prevent cheaters to set a value from an ini of a cvar that is created later
					// TODO: This is not ideal as it also prevents consolevariables.ini to set the value where we allow that. We could fix that.
					if (!Var->TestFlags(ECVF_Cheat))
					{
						// The existing one came from the ini, get the value
						Var->Set(*ExistingVar->GetString(), (EConsoleVariableFlags)((uint32)ExistingVar->GetFlags() & ECVF_SetByMask));
					}

					// destroy the existing one (no need to call sink because that will happen after all ini setting have been loaded)
					ExistingVar->Release();

					ConsoleObjects.Add(Name, Var);
					return Var;
				}
#if WITH_HOT_RELOAD
				else if (GIsHotReload)
				{
					// Variable is being replaced due to a hot reload - copy state across to new variable, but only if the type hasn't changed
					{
						if (ExistingVar->IsVariableFloat())
						{
							Var->Set(ExistingVar->GetFloat());
						}
					}
					{
						if (ExistingVar->IsVariableInt())
						{
							Var->Set(ExistingVar->GetInt());
						}
					}
					{
						if (ExistingVar->IsVariableString())
						{
							Var->Set(*ExistingVar->GetString());
						}
					}
					ExistingVar->Release();
					ConsoleObjects.Add(Name, Var);
					return Var;
				}
#endif
				else
				{
					// Copy data over from the new variable,
					// but keep the value from the existing one.
					// This way references to the old variable are preserved (no crash).
					// Changing the type of a variable however is not possible with this.
					ExistingVar->SetFlags(Var->GetFlags());
					ExistingVar->SetHelp(Var->GetHelp());

					// Name was already registered but got unregistered
					Var->Release();

					return ExistingVar;
				}
			}
			else if (ExistingCmd)
			{
				// Replace console command with the new one and release the existing one.
				// This should be safe, because we don't have FindConsoleVariable equivalent for commands.
				ConsoleObjects.Add(Name, Cmd);
				ExistingCmd->Release();

				return Cmd;
			}

			// Should never happen
			return nullptr;
		}
		else
		{
			ConsoleObjects.Add(Name, Obj);
			return Obj;
		}
	}

	bool FConsoleManager::MatchPartialName(const TCHAR* Stream, const TCHAR* Pattern)
	{
		while (*Pattern)
		{
			if (FChar::ToLower(*Stream) != FChar::ToLower(*Pattern))
			{
				return false;
			}

			++Stream;
			++Pattern;
		}

		return true;
	}

	bool FConsoleManager::MatchSubstring(const TCHAR* Stream, const TCHAR* Pattern)
	{
		while (*Stream)
		{
			int32 StreamIndex = 0;
			int32 PatternIndex = 0;

			do
			{
				if (Pattern[PatternIndex] == 0)
				{
					return true;
				}
				else if (FChar::ToLower(Stream[StreamIndex]) != FChar::ToLower(Pattern[PatternIndex]))
				{
					break;
				}

				PatternIndex++;
				StreamIndex++;
			} while (Stream[StreamIndex] != 0 || Pattern[PatternIndex] == 0);

			++Stream;
		}

		return false;
	}

	void FConsoleManager::RegisterThreadPropagation(uint32 ThreadId, IConsoleThreadPropagation* InCallback)
	{
		if (InCallback)
		{
			// at the moment we only support one thread besides the main thread
			check(!ThreadPropagationCallback);
		}
		else
		{
			// bad input parameters
			check(!ThreadId);
		}

		ThreadPropagationCallback = InCallback;
		// `ThreadId` is ignored as only RenderingThread is supported
	}

#else  // !NO_CVARS

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

		virtual void CallAllConsoleVariableSinks() override { return ConsoleManager.CallAllConsoleVariableSinks(); }

		virtual FConsoleVariableSinkHandle RegisterConsoleVariableSink_Handle(const FConsoleCommandDelegate& Command) override { return ConsoleManager.RegisterConsoleVariableSink_Handle(Command); }
		virtual void UnregisterConsoleVariableSink_Handle(FConsoleVariableSinkHandle Handle) override { return ConsoleManager.UnregisterConsoleVariableSink_Handle(Handle); }

		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandDelegate& Command, uint32 Flags) override { return ConsoleManager.RegisterConsoleCommand(Name, Help, Command, Flags); }
		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithArgsDelegate& Command, uint32 Flags) override
		{
			return ConsoleManager.RegisterConsoleCommand(Name, Help, Command, Flags);
		}
		virtual IConsoleCommand* RegisterConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FConsoleCommandWithWorldDelegate& Command, uint32 Flags) override
		{
			return ConsoleManager.RegisterConsoleCommand(Name, Help, Command, Flags);
		}
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
		template<typename T>
		IConsoleCommand* RegisterXConsoleCommandDelegate(const TCHAR* Name, const TCHAR* Help, const T& Command, uint32 Flags);
		virtual IConsoleCommand* RegisterXConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FXConsoleFullCmdDelegate& Command, uint32 Flags) override;
		virtual IConsoleCommand* RegisterXConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FXConsoleCommandWithArgsDelegate& Command, uint32 Flags) override;
		virtual IConsoleVariable* RegisterXConsoleVariable(const TCHAR* Name, const TCHAR* Help, uint32 Flags, const FProperty* InProp, void* Addr, bool bValueRef) override;
		bool ProcessUserXCommandInput(FString& Cmd, TArray<FString>& Args, FOutputDevice& Ar, UWorld* InWorld);
		bool IsProcessingCommand() const { return bIsProcessingCommamd; }

#if defined(ALLOW_OTHER_PLATFORM_CONFIG) && ALLOW_OTHER_PLATFORM_CONFIG
#if UE_5_04_OR_LATER
		virtual void LoadAllPlatformCVars(FName PlatformName, const FString& DeviceProfileName = FString()) override;
		virtual void PreviewPlatformCVars(FName PlatformName, const FString& DeviceProfileName, FName PreviewModeTag) override;
		virtual void ClearAllPlatformCVars(FName PlatformName = NAME_None, const FString& DeviceProfileName = FString()) override;
#else
		virtual void LoadAllPlatformCVars(FName PlatformName, const FString& DeviceProfileName = FString());
		virtual void PreviewPlatformCVars(FName PlatformName, const FString& DeviceProfileName, FName PreviewModeTag);
		virtual void ClearAllPlatformCVars(FName PlatformName = NAME_None, const FString& DeviceProfileName = FString());
#endif
#endif

#if UE_5_00_OR_LATER
		virtual FConsoleVariableMulticastDelegate& OnCVarUnregistered() override { return ConsoleManager.OnCVarUnregistered(); }
#endif

#if UE_5_04_OR_LATER
		virtual void UnsetAllConsoleVariablesWithTag(FName Tag, EConsoleVariableFlags Priority = ECVF_SetByMask) override;
		virtual FConsoleObjectWithNameMulticastDelegate& OnConsoleObjectUnregistered() override;
#endif

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
		ensure(false);
		return nullptr;
	}

#if UE_5_04_OR_LATER
	FConsoleObjectWithNameMulticastDelegate& FConsoleManager::OnConsoleObjectUnregistered()
	{
		return ConsoleObjectUnregisteredDelegate;
	}

	void FConsoleManager::UnsetAllConsoleVariablesWithTag(FName Tag, EConsoleVariableFlags Priority)
	{
#if 0
	TSet<IConsoleVariable*>* TaggedSet = UE::ConsoleManager::Private::TaggedCVars.FindRef(Tag);
	if (TaggedSet == nullptr)
	{
		return;
	}

	for (IConsoleVariable* Var : *TaggedSet)
	{
		Var->Unset(Priority, Tag);
	}

	UE::ConsoleManager::Private::TaggedCVars.Remove(Tag);
#endif
	}
#endif

#if defined(ALLOW_OTHER_PLATFORM_CONFIG) && ALLOW_OTHER_PLATFORM_CONFIG
	void FConsoleManager::LoadAllPlatformCVars(FName PlatformName, const FString& DeviceProfileName)
	{
#if 0
	FName PlatformKey = MakePlatformKey(PlatformName, DeviceProfileName);

	// protect the cached CVar info from two threads trying to get a platform CVar at once, and both attempting to load all of the cvars at the same time
	FScopeLock Lock(&CachedPlatformsAndDeviceProfilesLock);
	if (CachedPlatformsAndDeviceProfiles.Contains(PlatformKey))
	{
		return;
	}
	CachedPlatformsAndDeviceProfiles.Add(PlatformKey);

	// use the platform's base DeviceProfile for emulation
	VisitPlatformCVarsForEmulation(PlatformName,
								   DeviceProfileName.IsEmpty() ? PlatformName.ToString() : DeviceProfileName,
								   [PlatformName, PlatformKey](const FString& CVarName, const FString& CVarValue, EConsoleVariableFlags SetByAndPreview) {
									   // make sure the named cvar exists
									   IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
									   if (CVar == nullptr)
									   {
										   return;
									   }

									   // find or make the cvar for this platformkey
									   FConsoleVariableBase* PlatformCVar = FindOrCreatePlatformCVar(CVar, PlatformKey);

									   // now cache the passed in value
									   int32 SetBy = SetByAndPreview & ECVF_SetByMask;
									   PlatformCVar->SetOtherPlatformValue(*CVarValue, (EConsoleVariableFlags)SetBy, NAME_None);

									   UE_LOG(LogConsoleManager, Verbose, TEXT("Loading %s@%s = %s [get = %s]"), *CVarName, *PlatformKey.ToString(), *CVarValue, *CVar->GetPlatformValueVariable(*PlatformName.ToString())->GetString());
								   });
#endif
	}

	void FConsoleManager::PreviewPlatformCVars(FName PlatformName, const FString& DeviceProfileName, FName PreviewModeTag)
	{
#if 0
	UE_LOG(LogConsoleManager, Display, TEXT("Previewing Platform '%s', DeviceProfile '%s', ModeTag '%s'"), *PlatformName.ToString(), *DeviceProfileName, *PreviewModeTag.ToString());

	LoadAllPlatformCVars(PlatformName, DeviceProfileName.Len() ? DeviceProfileName : PlatformName.ToString());

	FName PlatformKey = MakePlatformKey(PlatformName, DeviceProfileName);

	for (auto Pair : ConsoleObjects)
	{
		if (IConsoleVariable* CVar = Pair.Value->AsVariable())
		{
			// we want Preview but not Cheat
			if ((CVar->GetFlags() & (ECVF_Preview | ECVF_Cheat)) == ECVF_Preview)
			{
				EConsoleVariableFlags Flags = ECVF_SetByPreview;
				if (CVar->GetFlags() & ECVF_ScalabilityGroup)
				{
					// we want to set SG cvars so they can be queried, but not send updates so that we don't use host platform's cvars
					Flags = (EConsoleVariableFlags)(Flags | ECVF_Set_SetOnly_Unsafe);
				}

				// if we have a value for the platform, then set it in the real CVar
				if (CVar->HasPlatformValueVariable(PlatformKey, GSpecialDPNameForPremadePlatformKey))
				{
					TSharedPtr<IConsoleVariable> PlatformCVar = CVar->GetPlatformValueVariable(PlatformKey, GSpecialDPNameForPremadePlatformKey);
					CVar->Set(*PlatformCVar->GetString(), Flags, PreviewModeTag);

					UE_LOG(LogConsoleManager, Display, TEXT("  |-> Setting %s = %s"), *Pair.Key, *PlatformCVar->GetString());
				}
			}
		}
	}
#endif
	}

	void FConsoleManager::ClearAllPlatformCVars(FName PlatformName, const FString& DeviceProfileName)
	{
#if 0
	FName PlatformKey = MakePlatformKey(PlatformName, DeviceProfileName);

	// protect the cached CVar info from two threads trying to get a platform CVar at once, and both attempting to load all of the cvars at the same time
	FScopeLock Lock(&CachedPlatformsAndDeviceProfilesLock);

	if (!CachedPlatformsAndDeviceProfiles.Contains(PlatformKey))
	{
		return;
	}
	CachedPlatformsAndDeviceProfiles.Remove(PlatformKey);

	for (auto Pair : ConsoleObjects)
	{
		if (IConsoleVariable* CVar = Pair.Value->AsVariable())
		{
			// clear any cached values for this key
			CVar->ClearPlatformVariables(PlatformKey);
		}
	}
#endif
	}

#endif
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
			Buffer++;
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
					auto Old = bIn--;
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

	FXConsoleCommandWithWorldArgsAndOutputDevice CVar_XConsoleCmdList(TEXT("z.XCmdList"), TEXT("z.XCmdList FilePathList..."), FXConsoleFullCmdDelegate::CreateLambda([](const TArray<FString>& Paths, UWorld* InWorld, FOutputDevice& Ar) {
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

	template<typename T>
	IConsoleCommand* FConsoleManager::RegisterXConsoleCommandDelegate(const TCHAR* Name, const TCHAR* Help, const T& Command, uint32 Flags)
	{
		UE_LOG(LogXConsoleManager, Log, TEXT("RegisterXConsoleCommand : %s"), Name);

		IConsoleCommand* Ret = nullptr;
		Ret = RegisterConsoleCommand(Name, Help, Command, Flags);

		return Ret;
	}

	IConsoleCommand* FConsoleManager::RegisterXConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FXConsoleFullCmdDelegate& Command, uint32 Flags)
	{
		return RegisterXConsoleCommandDelegate(Name, Help, Command, Flags);
	}

	IConsoleCommand* FConsoleManager::RegisterXConsoleCommand(const TCHAR* Name, const TCHAR* Help, const FXConsoleCommandWithArgsDelegate& Command, uint32 Flags)
	{
		return RegisterXConsoleCommandDelegate(Name, Help, Command, Flags);
	}
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

void FXConsoleController::PauseXConsolePipeline(const TCHAR* Reason)
{
	IXConsoleManager::PauseXConsoleCommandPipeline(GetWorld(), Reason);
}

void FXConsoleController::ContinueXConsolePipeline(const TCHAR* Reason)
{
	IXConsoleManager::ContinueXConsoleCommandPipeline(GetWorld(), Reason);
}

FXConsoleCommandLambdaFull XVar_RequestExitWithStatus(TEXT("z.RequestExitWithStatus"), TEXT("z.RequestExitWithStatus"), [](const bool Force, const uint8 ReturnCode, UWorld* InWorld, FOutputDevice& Ar) {
	UE_LOG(LogTemp, Display, TEXT(" RequestExitWithStatus Command args : Force = %s, ReturnCode = %d"), Force ? TEXT("true") : TEXT("false"), ReturnCode);
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

