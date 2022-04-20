#pragma once
#if defined(JSENV_API)
#include "GMPCore.h"
#include "Misc/ScopeExit.h"
#include "V8Utils.h"
#include "v8.h"

// export FPropertyTranslator::Create JSENV_API
#include "../Private/PropertyTranslator.h"
// export RegisterAddon JSENV_API
#include "JSClassRegister.h"

GMP_EXTERNAL_SIGSOURCE(v8::Isolate)
namespace PuertsSupport
{
// Runs user-specified code when the given javascript object is garbage collected
template<class Lambda>
void BindWeakCallback(v8::Isolate* InIsolate, const v8::Local<v8::Object>& JsObj, Lambda&& Callback)
{
	struct SetWeakCallbackData
	{
		SetWeakCallbackData(Lambda&& InCallback, v8::Isolate* Isolate, const v8::Local<v8::Object>& InJsObj)
			: Callback(std::move(InCallback))
		{
			this->GlobalRef.Reset(Isolate, InJsObj);
		}
		// function to call for cleanup
		Lambda Callback;
		v8::Global<v8::Object> GlobalRef;
	};

	auto CbData = new SetWeakCallbackData(std::move(Callback), InIsolate, JsObj);

	// set the callback on the javascript object to be called when it's garbage collected
	CbData->GlobalRef.template SetWeak<SetWeakCallbackData>(
		CbData,
		[](const v8::WeakCallbackInfo<SetWeakCallbackData>& data) {
			auto* Data = data.GetParameter();
			Data->Callback();         // run user-specified code
			Data->GlobalRef.Reset();  // free the V8 reference
			delete Data;              // delete the heap variable so it isn't leaked
		},
		v8::WeakCallbackType::kParameter);
}
using namespace puerts;

// function ListenObjectMessage(watchedobj, msgkey, weakobj, function [,times])
// function ListenObjectMessage(watchedobj, msgkey, weakobj, globalfuncstr [,times])
inline void Puerts_ListenObjectMessage(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	uint64 RetKey = 0;
	do
	{
		enum GMP_Listen_Index : int32
		{
			WatchedObj = 0,
			MessageKey,
			WeakObject,
			Function,
			Times,
		};

		auto Isolate = Info.GetIsolate();
		v8::Isolate::Scope IsolateScope(Isolate);
		v8::HandleScope HandleScope(Isolate);
		v8::Local<v8::Context> Context = Isolate->GetCurrentContext();
		v8::Context::Scope ContextScope(Context);

		auto NumArgs = Info.Length();
		if (NumArgs < 4)
			break;

		auto FuncArg = Info[GMP_Listen_Index::Function];
		bool bIsGlobalFunc = false;
		if (FuncArg->IsString())
		{
			FuncArg = Context->Global()->Get(Context, FuncArg).ToLocalChecked();
			bIsGlobalFunc = true;
		}
		if (!ensure(FuncArg->IsFunction()))
			break;

		int32 LeftTimes = -1;
		if (NumArgs == GMP_Listen_Index::Times)
		{
			LeftTimes = Info[GMP_Listen_Index::Times]->Int32Value(Context).ToChecked();
		}

		const FName MsgKey = *v8::String::Utf8Value(Isolate, Info[GMP_Listen_Index::MessageKey]);
		if (!ensure(!MsgKey.IsNone()))
			break;

		UObject* WatchedObject = FV8Utils::GetUObject(Context, Info[GMP_Listen_Index::WatchedObj]);
		UObject* WeakObj = FV8Utils::GetUObject(Context, Info[GMP_Listen_Index::WeakObject]);

		struct CallbackHolder
		{
			v8::Global<v8::Context> ContextHandle;
			v8::Global<v8::Function> FuncHandle;
			CallbackHolder(v8::Isolate* InIsolate, v8::Local<v8::Function>& InFunc)
				: ContextHandle(InIsolate, InIsolate->GetCurrentContext())
				, FuncHandle(InIsolate, InFunc)
			{
			}
			~CallbackHolder()
			{
				ContextHandle.Reset();
				FuncHandle.Reset();
			}
		};

		auto LocalFunc = FuncArg.As<v8::Function>();
		auto Holder = MakeUnique<CallbackHolder>(Isolate, LocalFunc);
		RetKey = FGMPHelper::ScriptListenMessage(
			WatchedObject ? FGMPSigSource(WatchedObject) : FGMPSigSource(Isolate),
			MsgKey,
			WeakObj,
			[WeakObj, Isolate, Holder{std::move(Holder)}](GMP::FMessageBody& Body) {
				v8::Isolate::Scope Isolatescope(Isolate);
				v8::HandleScope HandleScope(Isolate);
				auto CbContext = Holder->ContextHandle.Get(Isolate);
				v8::Context::Scope ContextScope(CbContext);
				auto CbFunc = Holder->FuncHandle.Get(Isolate);

#if WITH_EDITOR
				if (!ensure(!CbFunc.IsEmpty()))
					return;
#endif

				auto Types = Body.GetMessageTypes(WeakObj);
#if !GMP_WITH_TYPENAME
				if (!ensure(Types))
				{
					GMP_WARNING(TEXT("GetMessageTypes is null"));
					return;
				}
#endif

				auto& Addrs = Body.GetParams();

#if GMP_WITH_TYPENAME
				auto GetTypeName = [&](int32 In) { return Addrs[In].TypeName; };
#else
				auto GetTypeName = [&](int32 In) { return (*Types)[In]; };
#endif

				const int32 NumArgs = Addrs.Num();
				bool bSucc = true;
				TArray<std::unique_ptr<FPropertyTranslator>, TInlineAllocator<8>> Incs;
				for (auto Idx = 0; Idx < NumArgs; ++Idx)
				{
					FProperty* Prop = nullptr;
					if (GMPReflection::PropertyFromString(GetTypeName(Idx).ToString(), Prop) && Prop)
					{
						auto Inc = FPropertyTranslator::Create(Prop);
						if (Inc)
						{
							Incs.Add(std::move(Inc));
							continue;
						}
					}

					GMP_ERROR(TEXT("cannot get property from [%s]"), *GetTypeName(Idx).ToString());
					bSucc = false;
					break;
				}

				if (bSucc)
				{
					v8::Local<v8::Value>* Args = static_cast<v8::Local<v8::Value>*>(FMemory_Alloca(sizeof(v8::Local<v8::Value>) * NumArgs));
					FMemory::Memset(Args, 0, sizeof(v8::Local<v8::Value>) * NumArgs);
					for (auto Idx = 0; Idx < NumArgs; ++Idx)
					{
						auto& Inc = Incs[Idx];
						Args[Idx] = Inc->UEToJs(Isolate, CbContext, Addrs[Idx].ToAddr(), true);
					}

					const GMP::FArrayTypeNames* OldParams = nullptr;
					if (ensure(Body.IsSignatureCompatible(false, OldParams)))
					{
						v8::TryCatch TryCatch(Isolate);
						auto ReturnVal = CbFunc->Call(CbContext, CbContext->Global(), NumArgs, Args);
						if (TryCatch.HasCaught())
						{
							GMP_WARNING(TEXT("Exception:%s"), *FV8Utils::TryCatchToString(Isolate, &TryCatch));
							TryCatch.ReThrow();
						}
					}
					else
					{
						GMP_WARNING(TEXT("SignatureMismatch On Puerts Listen %s"), *Body.MessageKey().ToString());
					}
				}
			},
			LeftTimes);

#define WITH_V8_WEAK_DETECTION 1
#if WITH_V8_WEAK_DETECTION
		if (!WeakObj)
		{
			auto JsObj = Info[GMP_Listen_Index::WeakObject];
			if (JsObj->IsObject())
				BindWeakCallback(Isolate, JsObj.As<v8::Object>(), [RetKey, MsgKey] { FGMPHelper::ScriptUnListenMessage(MsgKey, RetKey); });
			else
				BindWeakCallback(Isolate, LocalFunc, [RetKey, MsgKey] { FGMPHelper::ScriptUnListenMessage(MsgKey, RetKey); });
		}
#endif
	} while (false);
	Info.GetReturnValue().Set((double)RetKey);
}

// function UnListenObjectMessage(msgkey, ListenedObj)
// function UnListenObjectMessage(msgkey, Key)
inline void Puerts_UnListenObjectMessage(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	auto NumArgs = Info.Length();
	if (NumArgs >= 2)
	{
		auto Isolate = Info.GetIsolate();
		v8::Isolate::Scope IsolateScope(Isolate);
		v8::HandleScope HandleScope(Isolate);
		v8::Local<v8::Context> Context = Isolate->GetCurrentContext();
		v8::Context::Scope ContextScope(Context);

		const FName MsgKey = *FV8Utils::ToFString(Info.GetIsolate(), Info[0]);
		UObject* ListenedObj = FV8Utils::GetUObject(Context, Info[1]);
		uint64 Key = Info[NumArgs > 2 ? 2 : 1]->IntegerValue(Context).ToChecked();

		if (ListenedObj)
			FGMPHelper::ScriptUnListenMessage(MsgKey, ListenedObj);
		else
			FGMPHelper::ScriptUnListenMessage(MsgKey, Key);
	}
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4750)  // warning C4750: function with _alloca() inlined into a loop
#endif
// function NotifyObjectMessage(obj, msgkey, ...)
inline void Puerts_NotifyObjectMessage(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	auto Isolate = Info.GetIsolate();
	bool bSucc = false;
	[&] {
		v8::Isolate::Scope IsolateScope(Isolate);
		v8::HandleScope HandleScope(Isolate);
		v8::Local<v8::Context> Context = Isolate->GetCurrentContext();
		v8::Context::Scope ContextScope(Context);

		auto NumArgs = Info.Length();
		if (!ensure(NumArgs >= 2))
		{
			FV8Utils::ThrowException(Isolate, "ivalid call to NotifyObjectMessage");
			return;
		}

		UObject* Sender = FV8Utils::GetUObject(Context, Info[0]);
		FName MsgKey = *FV8Utils::ToFString(Isolate, Info[1]);

		GMP::FTypedAddresses Params;
		Params.Reserve(NumArgs);

		TArray<FGMPTypedAddr::FPropertyValuePair, TInlineAllocator<8>> PropPairs;
		PropPairs.Reserve(NumArgs);

		auto Types = GMP::FMessageBody::GetMessageTypes(Sender, MsgKey);
		if (!ensure(Types && NumArgs - 2 >= Types->Num()))
		{
			GMP_WARNING(TEXT("GetMessageTypes is null"));
			return;
		}

		for (auto i = 2; i < NumArgs; ++i)
		{
			FProperty* Prop = nullptr;
			if (!GMPReflection::PropertyFromString((*Types)[i - 2].ToString(), Prop))
			{
				return;
			}
			auto Inc = FPropertyTranslator::Create(Prop);
			if (!Inc)
				return;

			auto& Ref = PropPairs.Emplace_GetRef(Prop, FMemory_Alloca(Prop->ElementSize));
			Inc->JsToUE(Isolate, Context, Info[i], Ref.Addr, false);
			Params.AddDefaulted_GetRef().SetAddr(Ref);
		}

		bSucc = FGMPHelper::ScriptNotifyMessage(MsgKey, Params, Sender);
	}();

	Info.GetReturnValue().Set(bSucc);
	if (!bSucc)
		FV8Utils::ThrowException(Info.GetIsolate(), "unable notify message");
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

inline void GMP_ExportToPuerts(v8::Local<v8::Context> Context, v8::Local<v8::Object> Exports)
{
	v8::Isolate* Isolate = Context->GetIsolate();
#if 0
	v8::Persistent<Context> Context = v8::Context::New(Context->GetIsolate(), Context->Global());

#else
	Exports->Set(Context, FV8Utils::ToV8String(Isolate, "NotifyObjectMessage"), v8::FunctionTemplate::New(Isolate, Puerts_NotifyObjectMessage)->GetFunction(Context).ToLocalChecked().As<v8::Value>()).Check();
	Exports->Set(Context, FV8Utils::ToV8String(Isolate, "ListenObjectMessage"), v8::FunctionTemplate::New(Isolate, Puerts_ListenObjectMessage)->GetFunction(Context).ToLocalChecked().As<v8::Value>()).Check();
	Exports->Set(Context, FV8Utils::ToV8String(Isolate, "UnListenObjectMessage"), v8::FunctionTemplate::New(Isolate, Puerts_UnListenObjectMessage)->GetFunction(Context).ToLocalChecked().As<v8::Value>()).Check();
#endif
}

struct GMP_ExportToPuertsObj
{
	GMP_ExportToPuertsObj() { RegisterAddon("GMP", GMP_ExportToPuerts); }
} GMP_ExportToPuertsObjReg;
}  // namespace PuertsSupport
#endif
