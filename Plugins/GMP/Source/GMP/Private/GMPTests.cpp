//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// GMP automation tests (UE test framework). Each Test_* is the single source of truth, driven
// by two runners that share it: (1) UE Automation entries (GMP_IMPLEMENT_AUTOMATION_TEST ->
// IMPLEMENT_SIMPLE_AUTOMATION_TEST, Session Frontend / -ExecCmds="Automation RunTests GMP");
// (2) the GMPUnitTest commandlet (-run=GMPUnitTest) which calls RunAllGMPTests() as a convenient
// headless entry the automation framework does not cover (single exit code for CI).
// Tests follow their feature switches (#if GMP_WITH_DIRECT_SIGNAL / GMP_WITH_SIGNAL_ORDER /
// GMP_WITH_MSG_HOLDER / WITH_EDITOR) so they enable/disable with the functionality they cover.
// The UCLASS test fixtures (probes/interface/commandlet) live in GMPUnitTestCommandlet.h so UHT scans them.
#include "GMPUnitTestCommandlet.h"

#include "GMPMacros.h"
#include "GMPSignalsInc.h"
#include "GMPUtils.h"
#include "GMPHub.h"
#include "GMPBPFastCall.h"  // C++->BP zero-copy FastCall under test (T20-T23)
#include "GMPRpcUtils.h"    // RPC path: compile-only smoke (needs real net to run; see GMPRpc_CompileSmoke)
#include "GMPRpcProxy.h"    // UGMPRpcProxy full definition (needed for UObject* conversion in RecvRPC)
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/AutomationTest.h"

#if GMP_WITH_DIRECT_SIGNAL
#include "GMPHubOpt.h"
// MSGKEY_SLOT is a test-only helper: it yields the raw TKeySlot handle for the A-class explicit fast path
// (SendObjectMessageDirect / ListenObjectMessageDirect, void-fire three-arg core). Production code uses plain
// MSGKEY (which auto-routes to the slot via SendObjectMessageByStore under static-store); only these tests need
// the raw slot to exercise the void-fire path, so the macro lives here instead of the public GMPMessageKeySlot.h.
#define MSGKEY_SLOT(str) GMP::GetKeySlot<C_STRING_TYPE(str)>()
#endif

#include "GMPFlexSignal.h"
#include "GMPFlexBackend.h"

DEFINE_LOG_CATEGORY_STATIC(LogGMPUnitTest, Log, All);
namespace GMPUnitTest
{
using namespace GMP;
// Slot-direct helpers (SendObjectMessageDirect / ListenObjectMessageDirect / ...) live in GMP::DirectTyped now --
// they are internal (bypass the MSGKEY check); only these fire-core tests reach them directly. Production C++ uses
// MSGKEY via FMessageUtils. Pull the namespace in here so the existing unqualified test calls keep resolving.
using namespace GMP::DirectTyped;

// ---- test scaffolding ------------------------------------------------------
// Each Test_* (returns bool = passed) is the single source of truth, driven by two runners:
//   (1) the commandlet UGMPUnitTestCommandlet::Main -> RunAllGMPTests() (CI: -run=GMPUnitTest, log "N run, M failed").
//   (2) UE Automation (CI/Session Frontend: -ExecCmds="Automation RunTests GMP"; per-case isolation/timing/report).
// On a GMP_TEST_CHECK failure it logs/sets bCasePass (commandlet) and, if inside an automation run, reports via GCurrentAutoTest->AddError.
static int32 GNumRun = 0;
static int32 GNumFail = 0;
// Non-null means we are inside an automation RunTest; a CHECK failure is also reported to that automation case (per-CHECK error).
static class FAutomationTestBase* GCurrentAutoTest = nullptr;

#define GMP_TEST_CHECK(expr)                                                                       \
	do                                                                                             \
	{                                                                                              \
		if (!(expr))                                                                               \
		{                                                                                          \
			UE_LOG(LogGMPUnitTest, Error, TEXT("    CHECK FAILED: %s  (%s:%d)"), TEXT(#expr), TEXT(__FILE__), __LINE__); \
			bCasePass = false;                                                                     \
			if (GCurrentAutoTest)                                                                   \
				GCurrentAutoTest->AddError(FString::Printf(TEXT("CHECK FAILED: %s (%s:%d)"), TEXT(#expr), TEXT(__FILE__), __LINE__)); \
		}                                                                                          \
	} while (0)

#define GMP_TEST_BEGIN(name)             \
	bool bCasePass = true;               \
	const TCHAR* CaseName = TEXT(name);  \
	++GNumRun;

#define GMP_TEST_END()                                                            \
	if (!bCasePass)                                                               \
		++GNumFail;                                                               \
	UE_LOG(LogGMPUnitTest, Display, TEXT("  [%s] %s"), bCasePass ? TEXT("PASS") : TEXT("FAIL"), CaseName); \
	return bCasePass;

// Generates a UE Automation entry for a Test_*. PrettyName uses dotted hierarchy (Session Frontend tree).
// Headless-friendly: ProductFilter + ApplicationContextMask (no RHI; runs under -nullrhi -unattended). RunTest sets
// GCurrentAutoTest so GMP_TEST_CHECK reports per-CHECK errors, calls the same Test_* (zero duplication), and falls back on its return value.
#define GMP_IMPLEMENT_AUTOMATION_TEST(TestFunc, PrettyName)                                            \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(F##TestFunc##AutoTest, PrettyName,                                \
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)             \
	bool F##TestFunc##AutoTest::RunTest(const FString& Parameters)                                     \
	{                                                                                                  \
		TGuardValue<FAutomationTestBase*> Guard(GCurrentAutoTest, this);                               \
		const bool bPass = TestFunc();                                                                 \
		TestTrue(TEXT(PrettyName), bPass);                                                             \
		return true;                                                                                   \
	}

// A throwaway concrete UObject used as a message source / listener holder (no world needed).
// Uses UGMPTestProbe (concrete) -- NOT the abstract UObject base (see UGMPTestProbe comment).
static UObject* MakeProbe()
{
	UObject* Obj = NewObject<UGMPTestProbe>(GetTransientPackage(), UGMPTestProbe::StaticClass(), NAME_None, RF_Transient);
	Obj->AddToRoot();  // keep alive across the case unless we explicitly drop it
	return Obj;
}

static UGMPWorldProbe* MakeWorldProbe(UWorld*& OutWorld)
{
	OutWorld = NewObject<UWorld>(GetTransientPackage(), UWorld::StaticClass(), NAME_None, RF_Transient);
	OutWorld->AddToRoot();

	UGMPWorldProbe* Obj = NewObject<UGMPWorldProbe>(GetTransientPackage(), UGMPWorldProbe::StaticClass(), NAME_None, RF_Transient);
	Obj->TestWorld = OutWorld;
	Obj->AddToRoot();
	return Obj;
}

static void ReleaseWorldProbe(UGMPWorldProbe* Obj, UWorld* World)
{
	if (Obj)
		Obj->RemoveFromRoot();
	if (World)
		World->RemoveFromRoot();
}

static FMessageHub* Hub() { return FMessageUtils::GetMessageHub(); }

// A non-UObject signal source: deriving from ISigSource lets a plain C++ object act as a FSigSource
// (address tagged with the ESignal bit). alignas(8) is required (GMP_SIG_BASE_ALIGN).
struct alignas(8) FProbeSigSource : public ISigSource
{
	int32 Tag = 0;
};

// ---- T1: FName send/recv consistency (backward compat) ----------------------
static bool Test_FNameBasic()
{
	GMP_TEST_BEGIN("T1.FName basic send/recv");
	UObject* Src = MakeProbe();
	FSigHandle H;
	int32 Got = 0;
	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.FName"), Src, &H, [&](int32 V) { Got += V; });
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.FName"), Src, int32(7));
	GMP_TEST_CHECK(Got == 7);
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.FName"), Src, int32(5));
	GMP_TEST_CHECK(Got == 12);
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_FNameBasic, "GMP.Core.FNameBasic")

static bool Test_FlexSignalGmpStoragePolicy()
{
	GMP_TEST_BEGIN("T-Lite.GMPFunction storage policy");
	using namespace GMP::FlexSig;

	// (a) FGmpFunctionStoragePolicy : Store(functor) -> Handle; GetSelf -> &functor
	int captured = 40;
	auto fn = [&captured](int add) { captured += add; };
	using FnT = decltype(fn);
	auto h = FGmpFunctionStoragePolicy::Store(fn);              // GMPFunction inline
	void* self = FGmpFunctionStoragePolicy::GetSelf(h);        // = &functor (GMP GetObjectAddress)
	GMP_TEST_CHECK(self != nullptr);
	(*static_cast<FnT*>(self))(2);
	GMP_TEST_CHECK(captured == 42);

	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_FlexSignalGmpStoragePolicy, "GMP.Flex.GMPFunctionStoragePolicy")

#if GMP_WITH_DIRECT_SIGNAL
// ---- T2: slot-direct send == FName send -------------------------------------
static bool Test_SlotDirect()
{
	GMP_TEST_BEGIN("T2.slot-direct == FName");
	UObject* Src = MakeProbe();
	FSigHandle H;
	int32 Got = 0;
	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.Slot"), Src, &H, [&](int32 V) { Got += V; });

	auto Slot = MSGKEY_SLOT("GMP.UT.Slot");
	// Both modes: the slot resolves to a store. (Modular: lazy ResolvePtr behind GetStore(); static: the
	// static store object's address.) GetStore() is the mode-agnostic accessor.
	GMP_TEST_CHECK(Slot.GetStore() != nullptr);  // slot resolves to the store
	SendObjectMessageDirect(Slot, FSigSource(Src), int32(10));
	GMP_TEST_CHECK(Got == 10);
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Slot"), Src, int32(10));
	GMP_TEST_CHECK(Got == 20);  // FName path hits the same store/listener
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_SlotDirect, "GMP.Typed.SlotDirect")

// ---- T2b: the everyday FGMPHelper entry routes a compile-time MSGKEY to the same store the direct path uses ----
// Rule under test (simplified): "MSGKEY + DIRECT + monolithic (GMP_WITH_STATIC_STORE) -> slot direct; otherwise
// by-name". This test only runs under GMP_WITH_DIRECT_SIGNAL (and not bNoDirect), so it covers two cases:
//   - GMP_WITH_STATIC_STORE on  : the everyday FGMPHelper entry selects the compile-time-key typed overload, which
//     goes through GetKeySlot -> the per-type static store. The MSGKEY_SLOT direct path uses the SAME static store.
//   - GMP_WITH_STATIC_STORE off (modular/editor, the default ==1): NO typed overload exists, so the everyday entry
//     goes by-name into MessageSignals[FName]; the MSGKEY_SLOT direct path resolves lazily by FName
//     (FStaticSignalSlot::ResolvePtr -> Hub->ResolveDirectSlotStore(FName)) to that SAME by-name store.
// Either way the invariant is identical and is what we assert: a listener registered via the everyday entry and a
// MSGKEY_SLOT direct send on the same key land in the SAME store and reach each other. (Path = slot vs by-name is a
// compile-time decision shown by GetKeySlot's #if, not directly observable from the test except via logs.)
//   (1) compile-time proof: MSGKEY("x") yields a TMSGKEYTyped<KeyT> carrying the compile-time key type, regardless
//       of mode (it is the precondition for the typed overload to win overload resolution when one exists).
//   (2) behavior proof: the everyday-entry listener is hit by a MSGKEY_SLOT direct send and by the everyday send.
static bool Test_TypedEntryAutoSlot()
{
	GMP_TEST_BEGIN("T2b.everyday FGMPHelper(MSGKEY) shares the slot/by-name store with the direct path");
	// (1) compile-time: the macro must produce a key-carrying TMSGKEYTyped (decltype does not evaluate, so the
	// trace-mode ctor side effect is not triggered here). Holds in every mode (TMSGKEYTyped is defined in
	// GMPMessageKey.h unconditionally; only the typed *overloads* are gated by GMP_WITH_STATIC_STORE).
	static_assert(std::is_same<decltype(MSGKEY("GMP.UT.TypedEntry")), GMP::TMSGKEYTyped<C_STRING_TYPE("GMP.UT.TypedEntry")>>::value,
				  "MSGKEY must yield a TMSGKEYTyped carrying the compile-time key (precondition for typed-overload selection)");

	UObject* Src = MakeProbe();
	FSigHandle H;
	int32 Got = 0;
	// everyday entry (FGMPHelper == GMP::FMessageUtils): listen with a compile-time MSGKEY. Selects the typed slot
	// overload under static-store, or the by-name overload otherwise -- both land in the key's canonical store.
	GMP::FMessageUtils::ListenObjectMessage(FSigSource(Src), MSGKEY("GMP.UT.TypedEntry"), &H, [&](int32 V) { Got += V; });

	// direct slot send on the same key must hit it -> the everyday listen landed in the same store.
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedEntry");
	GMP_TEST_CHECK(Slot.GetStore() != nullptr);
	SendObjectMessageDirect(Slot, FSigSource(Src), int32(7));
	GMP_TEST_CHECK(Got == 7);  // slot-direct send reached the everyday-entry listener -> same store

	// and the everyday SEND entry also reaches it (round-trips through the same store).
	GMP::FMessageUtils::SendObjectMessage(FSigSource(Src), MSGKEY("GMP.UT.TypedEntry"), int32(5));
	GMP_TEST_CHECK(Got == 12);
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedEntryAutoSlot, "GMP.Typed.TypedEntryAutoSlot")

// ---- T3: typed direct full-arg + reduced-arg prefix -------------------------
static bool Test_TypedDirect()
{
	GMP_TEST_BEGIN("T3.typed direct full + reduced-arg");
	UObject* Src = MakeProbe();
	FSigHandle H1, H2;
	int32 RecvA = 0; float RecvB = 0.f; int32 OnlyA = 0;
	auto Slot = MSGKEY_SLOT("GMP.UT.Typed");
	ListenObjectMessageDirect(Slot, FSigSource(Src), &H1, [&](int32 a, float b) { RecvA += a; RecvB += b; });
	ListenObjectMessageDirect(Slot, FSigSource(Src), &H2, [&](int32 a) { OnlyA = a; });  // reduced-arg = prefix
	SendObjectMessageDirect(Slot, FSigSource(Src), int32(10), 2.5f);
	GMP_TEST_CHECK(RecvA == 10);
	GMP_TEST_CHECK(RecvB == 2.5f);
	GMP_TEST_CHECK(OnlyA == 10);
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedDirect, "GMP.Typed.TypedDirect")

// ---- T4: signature-class gate does NOT misfire on legal typed coexistence (S6 + R005) ----
// GMP's contract is "one key, one signature family" (the Sends/Recvs table rejects mixing a typed and a body
// signature on the same key in non-Shipping). So the S6 SigHash gate is a CATEGORY gate (body vs typed) that:
//   (a) must NOT drop legal typed listeners, including reduced-arg (prefix) ones sharing a key -- the R005 fix;
//   (b) keeps body and typed callables from cross-invoking when they ever land in one store (Shipping fallback).
// Here we verify (a): several typed listeners of differing arity on ONE key all receive a full typed send.
// (Body isolation on a *separate* key is already covered by T1; mixing body+typed on one key is a contract
//  violation rejected upstream by the signature table, so we don't construct that illegal case here.)
static bool Test_TypedCoexistenceNoMisfire()
{
	GMP_TEST_BEGIN("T4.coexistence no-misfire: mixed-arity listeners all reached (reduced-arg, R005; S6 gate removed in routeB-2d)");
	UObject* Src = MakeProbe();
	FSigHandle H2, H1, H0;
	int32 Hits2 = 0; int32 A2 = 0; float B2 = 0.f;
	int32 Hits1 = 0; int32 A1 = 0;
	int32 Hits0 = 0;

	auto Slot = MSGKEY_SLOT("GMP.UT.Coexist");
	// full-arg (int,float)
	ListenObjectMessageDirect(Slot, FSigSource(Src), &H2, [&](int32 a, float b) { ++Hits2; A2 = a; B2 = b; });
	// reduced-arg (int) -- prefix of the broadcast signature
	ListenObjectMessageDirect(Slot, FSigSource(Src), &H1, [&](int32 a) { ++Hits1; A1 = a; });
	// reduced-arg (void) -- empty prefix
	ListenObjectMessageDirect(Slot, FSigSource(Src), &H0, [&]() { ++Hits0; });

	// one full typed send must reach ALL three (same TYPED class id -> none gated out)
	SendObjectMessageDirect(Slot, FSigSource(Src), int32(42), 3.5f);
	GMP_TEST_CHECK(Hits2 == 1); GMP_TEST_CHECK(A2 == 42); GMP_TEST_CHECK(B2 == 3.5f);
	GMP_TEST_CHECK(Hits1 == 1); GMP_TEST_CHECK(A1 == 42);  // reduced-arg NOT dropped by the gate (R005)
	GMP_TEST_CHECK(Hits0 == 1);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedCoexistenceNoMisfire, "GMP.Typed.TypedCoexistenceNoMisfire")
#endif  // GMP_WITH_DIRECT_SIGNAL

// ---- T5: leveled dispatch (object covers world + global) --------------------
// Object send fires three tiers: source==obj, source==obj's world, source==Any(global).
// Without a real world we verify the obj-level + global-level merge: an object send
// reaches both an object-scoped listener and a global (NullSigSrc) listener.
static bool Test_LeveledDispatch()
{
	GMP_TEST_BEGIN("T5.leveled dispatch (object+global)");
	UObject* Src = MakeProbe();
	FSigHandle HObj, HGlobal;
	int32 ObjHits = 0, GlobalHits = 0;

	// object-scoped listener
	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.Level"), Src, &HObj, [&](int32) { ++ObjHits; });
	// global listener (NullSigSrc) -- registered with Source = Any
	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.Level"), FSigSource::NullSigSrc, &HGlobal, [&](int32) { ++GlobalHits; });

	// send to the object: must reach BOTH the object listener and the global listener
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Level"), Src, int32(1));
	GMP_TEST_CHECK(ObjHits == 1);
	GMP_TEST_CHECK(GlobalHits == 1);  // global tier covered by an object send

	// a global send (NullSigSrc) reaches the global listener but NOT the object-scoped one
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Level"), FSigSource::NullSigSrc, int32(1));
	GMP_TEST_CHECK(GlobalHits == 2);
	GMP_TEST_CHECK(ObjHits == 1);  // object listener not hit by a pure-global send

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_LeveledDispatch, "GMP.Core.LeveledDispatch")

// ---- T5c: leveled dispatch covers object + real world + global ---------------
static bool Test_LeveledDispatchWorldTier()
{
	GMP_TEST_BEGIN("T5c.leveled dispatch (object+world+global)");
	UWorld* World = nullptr;
	UGMPWorldProbe* Src = MakeWorldProbe(World);
	const auto Key = MSGKEY("GMP.UT.LevelWorld");
	FSigHandle HObj, HWorld, HGlobal;
	int32 ObjHits = 0, WorldHits = 0, GlobalHits = 0;

	Hub()->ListenObjectMessage(Key, FSigSource(Src), &HObj, [&](int32) { ++ObjHits; });
	Hub()->ListenObjectMessage(Key, FSigSource(World), &HWorld, [&](int32) { ++WorldHits; });
	Hub()->ListenObjectMessage(Key, FSigSource::NullSigSrc, &HGlobal, [&](int32) { ++GlobalHits; });

	Hub()->SendObjectMessage(Key, FSigSource(Src), int32(1));
	GMP_TEST_CHECK(ObjHits == 1);
	GMP_TEST_CHECK(WorldHits == 1);
	GMP_TEST_CHECK(GlobalHits == 1);

	Hub()->SendObjectMessage(Key, FSigSource(World), int32(1));
	GMP_TEST_CHECK(ObjHits == 1);
	GMP_TEST_CHECK(WorldHits == 2);
	GMP_TEST_CHECK(GlobalHits == 2);

	ReleaseWorldProbe(Src, World);
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_LeveledDispatchWorldTier, "GMP.Core.LeveledDispatchWorldTier")

#if GMP_WITH_SIGNAL_ORDER
// ---- T5b: leveled dispatch keeps explicit order across object + global tiers --
static bool Test_LeveledDispatchOrder()
{
	GMP_TEST_BEGIN("T5b.leveled dispatch explicit order (object+global)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.LevelOrder");
	FSigHandle HGlobalLate, HObjEarly, HObjLate, HGlobalMid;
	TArray<int32> Seen;

	// Register in a deliberately shuffled order; dispatch must sort by FGMPListenOptions::Order.
	Hub()->ListenObjectMessage(Key, FSigSource::NullSigSrc, &HGlobalLate, [&](int32) { Seen.Add(4); }, FGMPListenOptions(-1, 20));
	Hub()->ListenObjectMessage(Key, Src, &HObjEarly, [&](int32) { Seen.Add(1); }, FGMPListenOptions(-1, -10));
	Hub()->ListenObjectMessage(Key, Src, &HObjLate, [&](int32) { Seen.Add(3); }, FGMPListenOptions(-1, 10));
	Hub()->ListenObjectMessage(Key, FSigSource::NullSigSrc, &HGlobalMid, [&](int32) { Seen.Add(2); }, FGMPListenOptions(-1, 0));

	Hub()->SendObjectMessage(Key, Src, int32(1));
	GMP_TEST_CHECK(Seen.Num() == 4);
	if (Seen.Num() == 4)
	{
		GMP_TEST_CHECK(Seen[0] == 1);
		GMP_TEST_CHECK(Seen[1] == 2);
		GMP_TEST_CHECK(Seen[2] == 3);
		GMP_TEST_CHECK(Seen[3] == 4);
	}

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_LeveledDispatchOrder, "GMP.Core.LeveledDispatchOrder")

// ---- T5d: leveled dispatch keeps explicit order across object + world + global tiers --
static bool Test_LeveledDispatchWorldTierOrder()
{
	GMP_TEST_BEGIN("T5d.leveled dispatch explicit order (object+world+global)");
	UWorld* World = nullptr;
	UGMPWorldProbe* Src = MakeWorldProbe(World);
	const auto Key = MSGKEY("GMP.UT.LevelWorldOrder");
	FSigHandle HGlobalLate, HObjMid, HWorldEarly;
	TArray<int32> Seen;

	Hub()->ListenObjectMessage(Key, FSigSource::NullSigSrc, &HGlobalLate, [&](int32) { Seen.Add(3); }, FGMPListenOptions(-1, 30));
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &HObjMid, [&](int32) { Seen.Add(2); }, FGMPListenOptions(-1, 10));
	Hub()->ListenObjectMessage(Key, FSigSource(World), &HWorldEarly, [&](int32) { Seen.Add(1); }, FGMPListenOptions(-1, -10));

	Hub()->SendObjectMessage(Key, FSigSource(Src), int32(1));
	GMP_TEST_CHECK(Seen.Num() == 3);
	if (Seen.Num() == 3)
	{
		GMP_TEST_CHECK(Seen[0] == 1);
		GMP_TEST_CHECK(Seen[1] == 2);
		GMP_TEST_CHECK(Seen[2] == 3);
	}

	ReleaseWorldProbe(Src, World);
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_LeveledDispatchWorldTierOrder, "GMP.Core.LeveledDispatchWorldTierOrder")
#endif

// ---- T6: disconnect + GC weak-ref fallback ----------------------------------
static bool Test_DisconnectAndGC()
{
	GMP_TEST_BEGIN("T6.disconnect + GC weak-ref fallback");
	UObject* Src = MakeProbe();

	// (a) explicit disconnect via handle scope
	int32 Got = 0;
	{
		FSigHandle Scoped;
		Hub()->ListenObjectMessage(MSGKEY("GMP.UT.Disc"), Src, &Scoped, [&](int32 V) { Got += V; });
		Hub()->SendObjectMessage(MSGKEY("GMP.UT.Disc"), Src, int32(4));
		GMP_TEST_CHECK(Got == 4);
	}  // Scoped destructs -> DisconnectAll
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Disc"), Src, int32(4));
	GMP_TEST_CHECK(Got == 4);  // no further delivery after disconnect

	// (b) GC weak-ref fallback: a listener UObject collected -> its callback is skipped & purged.
	int32 Got2 = 0;
	// concrete subclass (not abstract UObject) + intentionally NOT rooted so CollectGarbage can reclaim it below.
	UObject* Listener = NewObject<UGMPTestProbe>(GetTransientPackage(), UGMPTestProbe::StaticClass(), NAME_None, RF_Transient);
	// listener holds its own auto-disconnect collection via the UObject path; bind keyed to the listener object.
	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.GC"), Src, Listener, [&](int32 V) { Got2 += V; });
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.GC"), Src, int32(2));
	GMP_TEST_CHECK(Got2 == 2);
	// drop the only reference and collect; the stale listener must be skipped (no crash) on next send.
	Listener = nullptr;
	CollectGarbage(RF_NoFlags, true);
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.GC"), Src, int32(2));
	GMP_TEST_CHECK(Got2 == 2);  // stale listener skipped, count unchanged, no crash

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_DisconnectAndGC, "GMP.Core.DisconnectAndGC")

#if GMP_ENABLE_STATIC_DISCONNECT
// ---- T6c: StaticDisconnect by FGMPKey alone (global connection pool) ----
// With GMP_ENABLE_STATIC_DISCONNECT on, a listener can be torn down using only its FGMPKey handle, without holding the
// owning signal/store: FSignalImpl::StaticDisconnect(Key) resolves the store via the global key->store pool.
static bool Test_StaticDisconnectByKey()
{
	GMP_TEST_BEGIN("T6c.StaticDisconnect by FGMPKey alone (connection pool)");
	UObject* Src = MakeProbe();

	// Every listen registers into the pool (AddSigElmImpl), so StaticDisconnect works for ANY listener type --
	// including a nullptr (GMP_LISTENER_ANY) listener that has no collection/UObject auto-teardown of its own.
	int32 Got = 0;
	const FGMPKey Key = Hub()->ListenObjectMessage(MSGKEY("GMP.UT.StaticDisc"), Src, GMP_LISTENER_ANY(), [&](int32 V) { Got += V; });
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.StaticDisc"), Src, int32(7));
	GMP_TEST_CHECK(Got == 7);

	GMP::FSignalImpl::StaticDisconnect(Key);  // disconnect using only the key handle (resolved via the pool)
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.StaticDisc"), Src, int32(7));
	GMP_TEST_CHECK(Got == 7);  // no further delivery after StaticDisconnect

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_StaticDisconnectByKey, "GMP.Core.StaticDisconnectByKey")
#endif  // GMP_ENABLE_STATIC_DISCONNECT

// ---- T6b: stale UObject listener is skipped on the first post-GC broadcast ----
static bool Test_AutoInvalidationPurgesStaleListenerImmediately()
{
	GMP_TEST_BEGIN("T6b.auto invalidation purges stale listener immediately");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.GCImmediate");
	int32 Hits = 0;

	UObject* Listener = NewObject<UGMPTestProbe>(GetTransientPackage(), UGMPTestProbe::StaticClass(), NAME_None, RF_Transient);
	Hub()->ListenObjectMessage(Key, Src, Listener, [&](int32 V) { Hits += V; });
	Hub()->SendObjectMessage(Key, Src, int32(1));
	GMP_TEST_CHECK(Hits == 1);

	Listener = nullptr;
	CollectGarbage(RF_NoFlags, true);
	Hub()->SendObjectMessage(Key, Src, int32(1));
	GMP_TEST_CHECK(Hits == 1);  // first send after GC must not invoke the stale listener
	Hub()->SendObjectMessage(Key, Src, int32(1));
	GMP_TEST_CHECK(Hits == 1);  // stale entry was purged, so subsequent sends also stay quiet

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_AutoInvalidationPurgesStaleListenerImmediately, "GMP.Core.AutoInvalidationPurgesStaleListenerImmediately")

// ---- T7: per-object source isolation ----------------------------------------
// Two distinct UObject sources on the same key: a send to A must reach only A's listener, not B's.
static bool Test_SourceObjectIsolation()
{
	GMP_TEST_BEGIN("T7.source isolation (objA vs objB)");
	UObject* SrcA = MakeProbe();
	UObject* SrcB = MakeProbe();
	FSigHandle HA, HB;
	int32 GotA = 0, GotB = 0;
	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.Iso"), SrcA, &HA, [&](int32 V) { GotA += V; });
	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.Iso"), SrcB, &HB, [&](int32 V) { GotB += V; });

	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Iso"), SrcA, int32(1));
	GMP_TEST_CHECK(GotA == 1);
	GMP_TEST_CHECK(GotB == 0);  // B not touched by a send to A
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Iso"), SrcB, int32(1));
	GMP_TEST_CHECK(GotA == 1);
	GMP_TEST_CHECK(GotB == 1);

	SrcA->RemoveFromRoot();
	SrcB->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_SourceObjectIsolation, "GMP.Core.SourceObjectIsolation")

// ---- T8: TWeakObjectPtr / TObjectPtr as source ------------------------------
// FSigSource accepts weak/strong object ptr wrappers (they resolve to the underlying UObject address).
static bool Test_SourceWeakAndObjectPtr()
{
	GMP_TEST_BEGIN("T8.source TWeakObjectPtr/TObjectPtr");
	UObject* Src = MakeProbe();
	FSigHandle H;
	int32 Got = 0;
	// listen with a raw object source
	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.Wptr"), Src, &H, [&](int32 V) { Got += V; });

	// send via TWeakObjectPtr wrapper -> same source address
	TWeakObjectPtr<UObject> Weak(Src);
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Wptr"), FSigSource(Weak), int32(3));
	GMP_TEST_CHECK(Got == 3);

	// send via TObjectPtr wrapper -> same source address
	TObjectPtr<UObject> Strong(Src);
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Wptr"), FSigSource(Strong), int32(4));
	GMP_TEST_CHECK(Got == 7);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_SourceWeakAndObjectPtr, "GMP.Core.SourceWeakAndObjectPtr")

// ---- T9: ISigSource (non-UObject) as source ---------------------------------
// A plain C++ object deriving ISigSource is a valid source; its callbacks are cleaned up on destruction.
static bool Test_SourceISigSource()
{
	GMP_TEST_BEGIN("T9.source ISigSource (non-UObject)");
	UObject* Listener = MakeProbe();
	int32 Got = 0;
	{
		FProbeSigSource SigA, SigB;
		FSigHandle HA, HB;
		Hub()->ListenObjectMessage(MSGKEY("GMP.UT.ISig"), FSigSource(&SigA), &HA, [&](int32 V) { Got += V; });
		Hub()->ListenObjectMessage(MSGKEY("GMP.UT.ISig"), FSigSource(&SigB), &HB, [&](int32 V) { Got += 100 * V; });

		// send to SigA only
		Hub()->SendObjectMessage(MSGKEY("GMP.UT.ISig"), FSigSource(&SigA), int32(2));
		GMP_TEST_CHECK(Got == 2);  // only SigA listener (isolation across ISigSource instances)

		// send to SigB only
		Hub()->SendObjectMessage(MSGKEY("GMP.UT.ISig"), FSigSource(&SigB), int32(1));
		GMP_TEST_CHECK(Got == 102);
	}  // SigA/SigB destruct -> ISigSource cleanup; further sends must not crash
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.ISig"), FSigSource::NullSigSrc, int32(1));  // global send, no crash
	GMP_TEST_CHECK(Got == 102);  // unchanged (those sources are gone)

	Listener->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_SourceISigSource, "GMP.Core.SourceISigSource")

// ---- T10: keyed sub-source (MakeSigSourceKey / FindSigSourceKey) -------------
// A keyed sub-source (obj + FName) is a distinct source from the bare object; FindSigSourceKey resolves to the
// same source MakeSigSourceKey created; the keyed source is isolated from the bare-object source.
// NOTE: GMP only stores one ext-key set per object (SigSourceKey creates only when the object's set is absent;
// see GMPSignalsImpl.cpp SigSourceKey -- pre-existing behavior), so we use ONE keyed source per object here.
static bool Test_SourceKeyed()
{
	GMP_TEST_BEGIN("T10.keyed sub-source (obj+name)");
	UObject* Obj = MakeProbe();
	FSigHandle HKey, HBare;
	int32 GotKey = 0, GotBare = 0;

	const FSigSource SrcK = FSigSource::MakeSigSourceKey(Obj, TEXT("slotA"));
	GMP_TEST_CHECK(SrcK.IsValid());
	GMP_TEST_CHECK(!(SrcK == FSigSource(Obj)));  // keyed source != bare object source

	// FindSigSourceKey must resolve to the same source MakeSigSourceKey created
	const FSigSource Found = FSigSource::FindSigSourceKey(Obj, TEXT("slotA"));
	GMP_TEST_CHECK(Found == SrcK);

	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.Keyed"), SrcK, &HKey, [&](int32 V) { GotKey += V; });
	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.Keyed"), Obj, &HBare, [&](int32 V) { GotBare += V; });

	// send to the keyed source -> only the keyed listener
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Keyed"), SrcK, int32(5));
	GMP_TEST_CHECK(GotKey == 5);
	GMP_TEST_CHECK(GotBare == 0);  // bare-object listener isolated from the keyed source

	// the resolved Found source addresses the same listener
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Keyed"), Found, int32(5));
	GMP_TEST_CHECK(GotKey == 10);

	// teardown: disconnect listeners, then explicitly remove the ext-key sub-source before the host is GC'd.
	// (RemoveSourceKey now works -- the SigSourceKeys-stores-the-host bug was fixed in SigSourceKey.)
	HKey.DisconnectAll();
	HBare.DisconnectAll();
	FSigSource::RemoveSourceKey(Obj, TEXT("slotA"));
	GMP_TEST_CHECK(!FSigSource::FindSigSourceKey(Obj, TEXT("slotA")).IsValid());  // gone after teardown

	Obj->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_SourceKeyed, "GMP.Core.SourceKeyed")

// ---- T11: AnySigSrc wildcard + global tier ----------------------------------
// A NullSigSrc listener is registered at the global tier and is reached by sends to any object source
// (already partly covered by T5); here we also confirm a pure-global send reaches it.
static bool Test_SourceAnyWildcard()
{
	GMP_TEST_BEGIN("T11.global listener reached by object + global sends");
	UObject* SrcA = MakeProbe();
	UObject* SrcB = MakeProbe();
	FSigHandle HGlobal;
	int32 GlobalHits = 0;
	Hub()->ListenObjectMessage(MSGKEY("GMP.UT.Any"), FSigSource::NullSigSrc, &HGlobal, [&](int32) { ++GlobalHits; });

	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Any"), SrcA, int32(1));  // object A send -> global tier covered
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Any"), SrcB, int32(1));  // object B send -> global tier covered
	Hub()->SendObjectMessage(MSGKEY("GMP.UT.Any"), FSigSource::NullSigSrc, int32(1));  // pure-global send
	GMP_TEST_CHECK(GlobalHits == 3);  // global listener hit by all three

	SrcA->RemoveFromRoot();
	SrcB->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_SourceAnyWildcard, "GMP.Core.SourceAnyWildcard")

// ---- T-EQ: ==1/==0 equivalence cases (gate-agnostic FName path; run under both ==0 and ==1 to prove identical behavior) ----------
// These use only gate-agnostic API (StoreObjectMessage/OnceObjectMessage/ScriptRequestMessage + FName ListenObjectMessage),
// not *Direct/MSGKEY_SLOT (==1 only). ==1 store-replay rebuilds via the tri-arg path, ==0 via body; observable behavior must match.
// store/once/R-R equivalence was previously tested only under ==1 via typed-direct API; ==0 had no coverage -- added here (see doc/gmp-direct-signal-equivalence-audit.md).

// T-EQ1: StoreMessage late-delivery -- a listener registered after the store still receives the stored value (both gates).
static bool Test_EquivStoreLateDelivery()
{
	GMP_TEST_BEGIN("T-EQ1.store late-delivery (gate-agnostic FName path)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.EQ.Store");

	int32 Payload = 77;
	Hub()->StoreObjectMessage(Key, FSigSource(Src), Payload);  // store (no current listener)

	int32 Got = 0, Hits = 0;
	FSigHandle H;
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &H, [&](int32 v) { ++Hits; Got = v; });  // late listener
	GMP_TEST_CHECK(Hits == 1);   // the late listener receives the stored value immediately
	GMP_TEST_CHECK(Got == 77);

	Hub()->RemoveStoredObjectMessage(Key, FSigSource(Src));
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_EquivStoreLateDelivery, "GMP.Core.EquivStoreLateDelivery")

// T-EQ1b: StoreMessage follows source lifecycle -- after the source is invalidated, a store-only message no longer replays.
static bool Test_EquivStoreRemovedWithSourceLifecycle()
{
	GMP_TEST_BEGIN("T-EQ1b.store removed with source lifecycle (gate-agnostic)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.EQ.StoreLifecycle");
	const FSigSource Source(Src);

	Hub()->StoreObjectMessage(Key, Source, int32(101));  // store-only: no listener has registered yet
	FSigSource::RemoveSource(Source);                   // same public invalidation route used by UObject deletion

	int32 Hits = 0;
	FSigHandle H;
	Hub()->ListenObjectMessage(Key, Source, &H, [&](int32) { ++Hits; });
	GMP_TEST_CHECK(Hits == 0);  // lifecycle cleanup removed the stored value before late listen

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_EquivStoreRemovedWithSourceLifecycle, "GMP.Core.EquivStoreRemovedWithSourceLifecycle")

// T-EQ2: OnceMessage -- after the first late listener consumes it, the stored value is removed; a second late listener gets nothing (both gates, R010 once flag).
static bool Test_EquivOnceConsumed()
{
	GMP_TEST_BEGIN("T-EQ2.once consumed-then-gone (gate-agnostic)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.EQ.Once");

	int32 Payload = 9;
	Hub()->OnceObjectMessage(Key, FSigSource(Src), Payload);

	int32 Hits1 = 0, Hits2 = 0;
	FSigHandle H1, H2;
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &H1, [&](int32) { ++Hits1; });  // consumes the once-stored value
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &H2, [&](int32) { ++Hits2; });  // already consumed, receives nothing
	GMP_TEST_CHECK(Hits1 == 1);
	GMP_TEST_CHECK(Hits2 == 0);   // removed after the once consume (R010: the once flag is not lost on the non-single-struct path)

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_EquivOnceConsumed, "GMP.Core.EquivOnceConsumed")

// T-EQ2b: with a live listener present, OnceMessage only delivers live and does not persist for later late listeners.
static bool Test_EquivOnceLiveDeliveryDoesNotPersist()
{
	GMP_TEST_BEGIN("T-EQ2b.once live delivery does not persist (gate-agnostic)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.EQ.OnceLive");
	const FSigSource Source(Src);

	int32 LiveHits = 0, LiveGot = 0;
	FSigHandle HLive;
	Hub()->ListenObjectMessage(Key, Source, &HLive, [&](int32 V) { ++LiveHits; LiveGot = V; });

	Hub()->OnceObjectMessage(Key, Source, int32(33));
	GMP_TEST_CHECK(LiveHits == 1);
	GMP_TEST_CHECK(LiveGot == 33);

	int32 LateHits = 0;
	FSigHandle HLate;
	Hub()->ListenObjectMessage(Key, Source, &HLate, [&](int32) { ++LateHits; });
	GMP_TEST_CHECK(LateHits == 0);  // live delivery consumed the once send; no stored replay remains

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_EquivOnceLiveDeliveryDoesNotPersist, "GMP.Core.EquivOnceLiveDeliveryDoesNotPersist")

// T-EQ3: R/R round trip -- ScriptRequestMessage (gate-agnostic) + an FName responder-form listener; the seq mechanism is identical across gates.
static bool Test_EquivRequestResponse()
{
	GMP_TEST_BEGIN("T-EQ3.request/response round trip (gate-agnostic)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.EQ.RR");

	int32 SeenA = 0;
	FSigHandle HL;
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &HL, [&](int32 a, FGMPResponder& R) { ++SeenA; R.Response(a * 3); });

	int32 Got = 0, OnRspHits = 0;
	int32 ReqVal = 7;
	FTypedAddresses ReqParam{FGMPTypedAddr::MakeMsg(ReqVal)};
	FGMPKey RspKey = Hub()->ScriptRequestMessage(Key, ReqParam,
		[&](FMessageBody& RspBody) { ++OnRspHits; Got = RspBody.GetParam<int32>(0); },
		FSigSource(Src));
	GMP_TEST_CHECK(SeenA == 1);
	GMP_TEST_CHECK(OnRspHits == 1);   // response round-tripped (identical seq mechanism across gates)
	GMP_TEST_CHECK(Got == 21);
	GMP_TEST_CHECK(RspKey.IsValid());

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_EquivRequestResponse, "GMP.Core.EquivRequestResponse")

// ---- T-EQ3: zero-arg listen/send (gate-agnostic) ----------------------------------------
// Empty-payload messages are legal on the FName path; mirrors the direct-only Test_TypedZeroArgDirect.
static bool Test_EquivZeroArg()
{
	GMP_TEST_BEGIN("T-EQ4.zero-arg listen/send (gate-agnostic)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.EQ.ZeroArg");

	int32 Hits = 0;
	FSigHandle H;
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &H, [&]() { ++Hits; });
	Hub()->SendObjectMessage(Key, FSigSource(Src));
	GMP_TEST_CHECK(Hits == 1);
	Hub()->SendObjectMessage(Key, FSigSource(Src));
	GMP_TEST_CHECK(Hits == 2);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_EquivZeroArg, "GMP.Core.EquivZeroArg")

// ---- T-EQ4: store source isolation (gate-agnostic) --------------------------------------
// Two sources each store under the same key; a late listener on one source must see only that
// source's stored value. Mirrors the store-isolation half of direct-only Test_TypedStoreIsolationReduced.
static bool Test_EquivStoreSourceIsolation()
{
	GMP_TEST_BEGIN("T-EQ5.store source isolation (gate-agnostic)");
	UObject* SrcA = MakeProbe();
	UObject* SrcB = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.EQ.StoreIso");

	Hub()->StoreObjectMessage(Key, FSigSource(SrcA), int32(11));
	Hub()->StoreObjectMessage(Key, FSigSource(SrcB), int32(22));

	int32 GotA = 0, HitsA = 0;
	FSigHandle HA;
	Hub()->ListenObjectMessage(Key, FSigSource(SrcA), &HA, [&](int32 v) { ++HitsA; GotA = v; });
	GMP_TEST_CHECK(HitsA == 1);
	GMP_TEST_CHECK(GotA == 11);   // only SrcA's stored value, not SrcB's

	int32 GotB = 0, HitsB = 0;
	FSigHandle HB;
	Hub()->ListenObjectMessage(Key, FSigSource(SrcB), &HB, [&](int32 v) { ++HitsB; GotB = v; });
	GMP_TEST_CHECK(HitsB == 1);
	GMP_TEST_CHECK(GotB == 22);   // SrcB sees its own value

	Hub()->RemoveStoredObjectMessage(Key, FSigSource(SrcA));
	Hub()->RemoveStoredObjectMessage(Key, FSigSource(SrcB));
	SrcA->RemoveFromRoot();
	SrcB->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_EquivStoreSourceIsolation, "GMP.Core.EquivStoreSourceIsolation")

// ---- T-EQ5: single-struct store fast path (gate-agnostic) -------------------------------
// A single struct argument is stored as the struct itself (single-struct store path); a late
// listener taking that one struct must receive it. Mirrors direct-only Test_TypedStoreSingleStruct.
static bool Test_EquivStoreSingleStruct()
{
	GMP_TEST_BEGIN("T-EQ6.store single-struct fast path (gate-agnostic)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.EQ.Struct");

	Hub()->StoreObjectMessage(Key, FSigSource(Src), FIntPoint(3, 4));  // single struct arg

	FIntPoint Got(0, 0); int32 Hits = 0;
	FSigHandle H;
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &H, [&](FIntPoint P) { ++Hits; Got = P; });
	GMP_TEST_CHECK(Hits == 1);
	GMP_TEST_CHECK(Got.X == 3 && Got.Y == 4);

	Hub()->RemoveStoredObjectMessage(Key, FSigSource(Src));
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_EquivStoreSingleStruct, "GMP.Core.EquivStoreSingleStruct")

// ---- T-EQ6: native-interface param store/late-replay (gate-agnostic, R015) --------------
// Stores an IGMPTestInterface* and verifies a late listener gets the REAL interface subobject
// pointer (not the UObject*). The impl multiply-inherits so the interface ptr != UObject*; a
// wrong layout would read the UObject* and hit the wrong vtable. Mirrors direct-only
// Test_TypedStoreInterfaceParam, but through the gate-agnostic FName store path.
static bool Test_EquivStoreInterfaceParam()
{
	GMP_TEST_BEGIN("T-EQ7.store native-interface param (gate-agnostic, R015)");
	UObject* Src = MakeProbe();
	UGMPTestInterfaceImpl* Impl = NewObject<UGMPTestInterfaceImpl>(GetTransientPackage());
	Impl->AddToRoot();
	IGMPTestInterface* Iface = Impl;
	GMP_TEST_CHECK(static_cast<void*>(Iface) != static_cast<void*>(Impl));  // subobject ptr != UObject*

	const auto Key = MSGKEY("GMP.UT.EQ.Iface");
	Hub()->StoreObjectMessage(Key, FSigSource(Src), Iface);

	IGMPTestInterface* Got = nullptr; int32 Hits = 0; int32 Magic = 0; UObject* GotObj = nullptr;
	FSigHandle H;
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &H, [&](IGMPTestInterface* I) {
		++Hits;
		Got = I;
		if (I)
		{
			Magic = I->GMPTestMagic();
			GotObj = I->_getUObject();
		}
	});
	GMP_TEST_CHECK(Hits == 1);
	GMP_TEST_CHECK(Got == Iface);            // real interface subobject pointer
	GMP_TEST_CHECK(Magic == 4242);           // hit the real impl method
	GMP_TEST_CHECK(GotObj == Impl);

	Hub()->RemoveStoredObjectMessage(Key, FSigSource(Src));
	Impl->RemoveFromRoot();
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_EquivStoreInterfaceParam, "GMP.Core.EquivStoreInterfaceParam")

// ---- T-EQ7: native-interface param live-fire (gate-agnostic, R011) ----------------------
// Live-fire (no store) native-interface param: the listener reads the real interface subobject
// pointer (NOT the UObject*). Formerly Test_TypedLiveInterfaceParam inside the gate; it only ever
// used the FName Hub API, so it is gate-independent and now runs under ==0 too.
static bool Test_EquivLiveInterfaceParam()
{
	GMP_TEST_BEGIN("T-EQ8.live-fire native-interface param (gate-agnostic, R011)");
	UObject* Src = MakeProbe();
	UGMPTestInterfaceImpl* Impl = NewObject<UGMPTestInterfaceImpl>(GetTransientPackage());
	Impl->AddToRoot();
	IGMPTestInterface* Iface = Impl;
	GMP_TEST_CHECK(static_cast<void*>(Iface) != static_cast<void*>(Impl));

	const auto Key = MSGKEY("GMP.UT.EQ.LiveIface");
	IGMPTestInterface* Got = nullptr; int32 Hits = 0; int32 Magic = 0; UObject* GotObj = nullptr;
	FSigHandle H;
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &H, [&](IGMPTestInterface* I) {
		++Hits;
		Got = I;
		if (I)
		{
			Magic = I->GMPTestMagic();
			GotObj = I->_getUObject();
		}
	});

	Hub()->SendObjectMessage(Key, FSigSource(Src), Iface);  // live fire, no store

	GMP_TEST_CHECK(Hits == 1);
	GMP_TEST_CHECK(Got == Iface);
	GMP_TEST_CHECK(Magic == 4242);
	GMP_TEST_CHECK(GotObj == Impl);

	Impl->RemoveFromRoot();
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_EquivLiveInterfaceParam, "GMP.Core.EquivLiveInterfaceParam")

// ---- T-EQ9: ReqRsp round trip via the message layer (gate-agnostic) ----------------------
// Migrated from the former UGMPRpcProxy::BeginPlay() bTest sample (dead code). The ReqRsp half is
// pure GMP message layer (no network), so it runs headless: a responder-form listener (last param
// FGMPResponder&) acts as an async server; NotifyObjectMessage with a trailing callable acts as an
// async request whose callback receives the response. Multi-arg payload mirrors the original sample.
static bool Test_ReqRspProxyRoundTrip()
{
	GMP_TEST_BEGIN("T-EQ9.ReqRsp round trip (message layer, gate-agnostic)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.EQ.ReqRspProxy");

	int32 ServerHits = 0;
	FSigHandle HServer;
	// async server: the trailing FGMPResponder& makes this a responder-form listener; it echoes the
	// request payload back via Response(...). (Hub() overload takes an FName key, like the other T-EQ cases.)
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &HServer,
		[&](int32 v1, UObject* v2, const TArray<uint8>& v3, const TArray<UObject*>& v4, const TSubclassOf<AActor>& c, FGMPResponder& Info) {
			++ServerHits;
			Info.Response(v1, v2, v3, v4, c);
		});

	int32 ClientHits = 0, GotV1 = 0; UObject* GotV2 = nullptr; int32 GotV3Num = 0, GotV4Num = 0;
	TSubclassOf<AActor> ReqClass = AActor::StaticClass();
	// async request: the trailing callable makes NotifyObjectMessage a request; the callback fires on response.
	FMessageUtils::NotifyObjectMessage(Src, Key,
		123, Src, TArray<uint8>{0x1, 0x2, 0x3, 0x4}, TArray<UObject*>{Src}, ReqClass,
		[&](int32 v1, UObject* v2, const TArray<uint8>& v3, TArray<UObject*>& v4, const TSubclassOf<AActor>& c) {
			++ClientHits;
			GotV1 = v1; GotV2 = v2; GotV3Num = v3.Num(); GotV4Num = v4.Num();
		});

	GMP_TEST_CHECK(ServerHits == 1);   // responder server was invoked once
	GMP_TEST_CHECK(ClientHits == 1);   // response round-tripped back to the request callback
	GMP_TEST_CHECK(GotV1 == 123);      // echoed payload
	GMP_TEST_CHECK(GotV2 == Src);
	GMP_TEST_CHECK(GotV3Num == 4);
	GMP_TEST_CHECK(GotV4Num == 1);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ReqRspProxyRoundTrip, "GMP.Core.ReqRspProxyRoundTrip")

// RPC path compile-only smoke (NOT a runtime test, never called). The RPC helpers (RecvRPC/PostRPC)
// require a real PlayerController + PackageMap + net connection, so they cannot run under the headless
// commandlet/automation runner (every step ensureAlways(PC && PackageMap) -> automation FAIL). This
// function exists solely so the RPC templates from the former bTest sample stay instantiated and keep
// compiling; real RPC round-trip must be exercised in a PIE/DedicatedServer+Client environment.
[[maybe_unused]] static void GMPRpc_CompileSmoke(UGMPRpcProxy* Proxy, APlayerController* PC)
{
	using namespace GMP;
	if (Proxy == nullptr)  // always false at runtime via the (never-called) contract; guards against accidental use.
	{
		TSubclassOf<AActor> c = AActor::StaticClass();
		FRpcMessageUtils::RecvRPC(PC, Proxy, MSGKEY("RPC.Test"), Proxy,
			[](int v1, UObject* v2, const TArray<uint8>& v3, TArray<UObject*>& v4, const TSubclassOf<AActor>& c) {});
		FRpcMessageUtils::PostRPC(PC, Proxy, MSGKEY("RPC.Test"),
			123, (UObject*)Proxy, TArray<uint8>{0x1, 0x2, 0x3, 0x4}, TArray<UObject*>{(UObject*)Proxy}, c);
	}
}

#if GMP_WITH_DIRECT_SIGNAL
// ---- T11: typed direct zero-arg message ------------------------------------
// Empty payload messages are legal on the FName path; the direct helper must keep that surface available.
static bool Test_TypedZeroArgDirect()
{
	GMP_TEST_BEGIN("T11.typed direct zero-arg");
	UObject* Src = MakeProbe();
	FSigHandle H;
	int32 Hits = 0;
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedZeroArg");

	ListenObjectMessageDirect(Slot, FSigSource(Src), &H, [&]() { ++Hits; });
	SendObjectMessageDirect(Slot, FSigSource(Src));
	GMP_TEST_CHECK(Hits == 1);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedZeroArgDirect, "GMP.Typed.TypedZeroArgDirect")

// ---- T12: typed direct honors object-source isolation -----------------------
// The typed pass-through path must respect FSigSource isolation just like the FName path.
static bool Test_TypedSourceIsolation()
{
	GMP_TEST_BEGIN("T12.typed direct source isolation");
	UObject* SrcA = MakeProbe();
	UObject* SrcB = MakeProbe();
	FSigHandle HA, HB;
	int32 GotA = 0, GotB = 0;
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedIso");
	ListenObjectMessageDirect(Slot, FSigSource(SrcA), &HA, [&](int32 V) { GotA += V; });
	ListenObjectMessageDirect(Slot, FSigSource(SrcB), &HB, [&](int32 V) { GotB += V; });

	SendObjectMessageDirect(Slot, FSigSource(SrcA), int32(8));
	GMP_TEST_CHECK(GotA == 8);
	GMP_TEST_CHECK(GotB == 0);  // typed send to A doesn't reach B
	SendObjectMessageDirect(Slot, FSigSource(SrcB), int32(9));
	GMP_TEST_CHECK(GotB == 9);
	GMP_TEST_CHECK(GotA == 8);

	SrcA->RemoveFromRoot();
	SrcB->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedSourceIsolation, "GMP.Typed.TypedSourceIsolation")

// ---- T12b: typed direct leveled dispatch covers object + real world + global --
static bool Test_TypedLeveledDispatchWorldTier()
{
	GMP_TEST_BEGIN("T12b.typed direct leveled dispatch (object+world+global)");
	UWorld* World = nullptr;
	UGMPWorldProbe* Src = MakeWorldProbe(World);
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedLevelWorld");
	FSigHandle HObj, HWorld, HGlobal;
	int32 ObjHits = 0, WorldHits = 0, GlobalHits = 0;

	ListenObjectMessageDirect(Slot, FSigSource(Src), &HObj, [&](int32) { ++ObjHits; });
	ListenObjectMessageDirect(Slot, FSigSource(World), &HWorld, [&](int32) { ++WorldHits; });
	ListenObjectMessageDirect(Slot, FSigSource::NullSigSrc, &HGlobal, [&](int32) { ++GlobalHits; });

	SendObjectMessageDirect(Slot, FSigSource(Src), int32(1));
	GMP_TEST_CHECK(ObjHits == 1);
	GMP_TEST_CHECK(WorldHits == 1);
	GMP_TEST_CHECK(GlobalHits == 1);

	SendObjectMessageDirect(Slot, FSigSource(World), int32(1));
	GMP_TEST_CHECK(ObjHits == 1);
	GMP_TEST_CHECK(WorldHits == 2);
	GMP_TEST_CHECK(GlobalHits == 2);

	ReleaseWorldProbe(Src, World);
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedLeveledDispatchWorldTier, "GMP.Typed.TypedLeveledDispatchWorldTier")

#if GMP_WITH_SIGNAL_ORDER
// ---- T12c: typed direct leveled dispatch keeps explicit order ----------------
static bool Test_TypedLeveledDispatchOrder()
{
	GMP_TEST_BEGIN("T12c.typed direct leveled dispatch explicit order");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedLevelOrder");
	FSigHandle HGlobalLate, HObjEarly, HObjLate, HGlobalMid;
	TArray<int32> Seen;

	ListenObjectMessageDirect(Slot, FSigSource::NullSigSrc, &HGlobalLate, [&](int32) { Seen.Add(4); }, FGMPListenOptions(-1, 20));
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HObjEarly, [&](int32) { Seen.Add(1); }, FGMPListenOptions(-1, -10));
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HObjLate, [&](int32) { Seen.Add(3); }, FGMPListenOptions(-1, 10));
	ListenObjectMessageDirect(Slot, FSigSource::NullSigSrc, &HGlobalMid, [&](int32) { Seen.Add(2); }, FGMPListenOptions(-1, 0));

	SendObjectMessageDirect(Slot, FSigSource(Src), int32(1));
	GMP_TEST_CHECK(Seen.Num() == 4);
	if (Seen.Num() == 4)
	{
		GMP_TEST_CHECK(Seen[0] == 1);
		GMP_TEST_CHECK(Seen[1] == 2);
		GMP_TEST_CHECK(Seen[2] == 3);
		GMP_TEST_CHECK(Seen[3] == 4);
	}

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedLeveledDispatchOrder, "GMP.Typed.TypedLeveledDispatchOrder")

// ---- T12d: typed direct order across object + world + global tiers -----------
static bool Test_TypedLeveledDispatchWorldTierOrder()
{
	GMP_TEST_BEGIN("T12d.typed direct leveled dispatch explicit order (object+world+global)");
	UWorld* World = nullptr;
	UGMPWorldProbe* Src = MakeWorldProbe(World);
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedLevelWorldOrder");
	FSigHandle HGlobalLate, HObjMid, HWorldEarly;
	TArray<int32> Seen;

	ListenObjectMessageDirect(Slot, FSigSource::NullSigSrc, &HGlobalLate, [&](int32) { Seen.Add(3); }, FGMPListenOptions(-1, 30));
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HObjMid, [&](int32) { Seen.Add(2); }, FGMPListenOptions(-1, 10));
	ListenObjectMessageDirect(Slot, FSigSource(World), &HWorldEarly, [&](int32) { Seen.Add(1); }, FGMPListenOptions(-1, -10));

	SendObjectMessageDirect(Slot, FSigSource(Src), int32(1));
	GMP_TEST_CHECK(Seen.Num() == 3);
	if (Seen.Num() == 3)
	{
		GMP_TEST_CHECK(Seen[0] == 1);
		GMP_TEST_CHECK(Seen[1] == 2);
		GMP_TEST_CHECK(Seen[2] == 3);
	}

	ReleaseWorldProbe(Src, World);
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedLeveledDispatchWorldTierOrder, "GMP.Typed.TypedLeveledDispatchWorldTierOrder")
#endif

#if GMP_WITH_MSG_HOLDER
// ---- T13: typed StoreMessage -> late typed listener reads params via ParamOffsets (no FMessageBody) ----
// Store first (no listener yet), then a late typed listener must receive the stored params, read in place by offset.
// Multi-arg incl. bool to exercise param-order offset capture + native-bool in-place read.
static bool Test_TypedStoreLateDelivery()
{
	GMP_TEST_BEGIN("T13.typed StoreMessage late-delivery (offset replay)");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedStore");

	// store before anyone listens
	StoreObjectMessageDirect(Slot, FSigSource(Src), int32(11), 2.5f, true);

	// late typed listener: must get the stored params replayed immediately on listen
	int32 A = 0; float B = 0.f; bool C = false; int32 Hits = 0;
	FSigHandle H;
	ListenObjectMessageDirect(Slot, FSigSource(Src), &H, [&](int32 a, float b, bool c) { ++Hits; A = a; B = b; C = c; });
	GMP_TEST_CHECK(Hits == 1);   // late-delivered
	GMP_TEST_CHECK(A == 11);
	GMP_TEST_CHECK(B == 2.5f);
	GMP_TEST_CHECK(C == true);   // bool read in place by offset

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedStoreLateDelivery, "GMP.Typed.TypedStoreLateDelivery")

// ---- T14: typed StoreMessage source isolation + reduced-arg late listener ----
static bool Test_TypedStoreIsolationReduced()
{
	GMP_TEST_BEGIN("T14.typed store isolation + reduced-arg late");
	UObject* SrcA = MakeProbe();
	UObject* SrcB = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedStore2");

	StoreObjectMessageDirect(Slot, FSigSource(SrcA), int32(7), 1.5f);  // stored on A only

	// reduced-arg late listener on A: reads prefix (int only) from the stored block
	int32 GotA = 0; int32 HitsA = 0;
	FSigHandle HA;
	ListenObjectMessageDirect(Slot, FSigSource(SrcA), &HA, [&](int32 a) { ++HitsA; GotA = a; });
	GMP_TEST_CHECK(HitsA == 1);
	GMP_TEST_CHECK(GotA == 7);   // reduced-arg prefix read

	// late listener on B: nothing stored for B -> no late delivery
	int32 HitsB = 0;
	FSigHandle HB;
	ListenObjectMessageDirect(Slot, FSigSource(SrcB), &HB, [&](int32) { ++HitsB; });
	GMP_TEST_CHECK(HitsB == 0);  // source isolation: B has no stored message

	SrcA->RemoveFromRoot();
	SrcB->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedStoreIsolationReduced, "GMP.Typed.TypedStoreIsolationReduced")

// ---- T14b: typed StoreMessage follows source lifecycle -----------------------
static bool Test_TypedStoreRemovedWithSourceLifecycle()
{
	GMP_TEST_BEGIN("T14b.typed store removed with source lifecycle");
	UObject* Src = MakeProbe();
	const FSigSource Source(Src);
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedStoreLifecycle");

	StoreObjectMessageDirect(Slot, Source, int32(88));
	FSigSource::RemoveSource(Source);

	int32 Hits = 0;
	FSigHandle H;
	ListenObjectMessageDirect(Slot, Source, &H, [&](int32) { ++Hits; });
	GMP_TEST_CHECK(Hits == 0);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedStoreRemovedWithSourceLifecycle, "GMP.Typed.TypedStoreRemovedWithSourceLifecycle")

// ---- T15: typed Once consume + Remove-stored blocks late + explicit Unbind ----
static bool Test_TypedOnceRemoveUnbind()
{
	GMP_TEST_BEGIN("T15.typed once/remove/unbind");
	UObject* Src = MakeProbe();

	// (a) OnceObjectMessageDirect -> first late listener consumes it; a second late listener gets nothing.
	{
		auto Slot = MSGKEY_SLOT("GMP.UT.TypedOnce");
		OnceObjectMessageDirect(Slot, FSigSource(Src), int32(5));
		int32 G1 = 0, H1 = 0; FSigHandle L1;
		ListenObjectMessageDirect(Slot, FSigSource(Src), &L1, [&](int32 v) { ++H1; G1 = v; });
		GMP_TEST_CHECK(H1 == 1); GMP_TEST_CHECK(G1 == 5);  // consumed by first
		int32 H2 = 0; FSigHandle L2;
		ListenObjectMessageDirect(Slot, FSigSource(Src), &L2, [&](int32) { ++H2; });
		GMP_TEST_CHECK(H2 == 0);  // once already consumed -> second late listener gets nothing
	}

	// (b) RemoveStoredObjectMessageDirect drops a stored message before any listener -> no late delivery.
	{
		auto Slot = MSGKEY_SLOT("GMP.UT.TypedRm");
		StoreObjectMessageDirect(Slot, FSigSource(Src), int32(9));
		const int32 Removed = RemoveStoredObjectMessageDirect(Slot, FSigSource(Src));
		GMP_TEST_CHECK(Removed == 1);
		int32 H = 0; FSigHandle L;
		ListenObjectMessageDirect(Slot, FSigSource(Src), &L, [&](int32) { ++H; });
		GMP_TEST_CHECK(H == 0);  // removed -> nothing replayed
	}

	// (c) explicit UnbindMessageDirect by FGMPKey stops further delivery.
	{
		auto Slot = MSGKEY_SLOT("GMP.UT.TypedUnbind");
		int32 G = 0; FSigHandle L;
		const FGMPKey Key = ListenObjectMessageDirect(Slot, FSigSource(Src), &L, [&](int32 v) { G += v; });
		SendObjectMessageDirect(Slot, FSigSource(Src), int32(3));
		GMP_TEST_CHECK(G == 3);
		UnbindMessageDirect(Slot, Key);  // explicit imperative unbind
		SendObjectMessageDirect(Slot, FSigSource(Src), int32(3));
		GMP_TEST_CHECK(G == 3);  // no further delivery after unbind
	}

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedOnceRemoveUnbind, "GMP.Typed.TypedOnceRemoveUnbind")

// ---- T15b: typed Once with live listener does not persist --------------------
static bool Test_TypedOnceLiveDeliveryDoesNotPersist()
{
	GMP_TEST_BEGIN("T15b.typed once live delivery does not persist");
	UObject* Src = MakeProbe();
	const FSigSource Source(Src);
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedOnceLive");

	int32 LiveHits = 0, LiveGot = 0;
	FSigHandle HLive;
	ListenObjectMessageDirect(Slot, Source, &HLive, [&](int32 V) { ++LiveHits; LiveGot = V; });

	OnceObjectMessageDirect(Slot, Source, int32(44));
	GMP_TEST_CHECK(LiveHits == 1);
	GMP_TEST_CHECK(LiveGot == 44);

	int32 LateHits = 0;
	FSigHandle HLate;
	ListenObjectMessageDirect(Slot, Source, &HLate, [&](int32) { ++LateHits; });
	GMP_TEST_CHECK(LateHits == 0);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedOnceLiveDeliveryDoesNotPersist, "GMP.Typed.TypedOnceLiveDeliveryDoesNotPersist")

// ---- T16: single-struct fast path (GMP_WITH_SINGLE_STRUCT_STORE) typed store -> late listener ----
// InitAsMsgStore special-cases a single struct arg (stored as the struct itself). The accessor GetMessageParamOffsets
// synthesizes {0} for SingleStructStoreBit. Verify a typed listener taking that one struct receives it via offset-0 replay.
static bool Test_TypedStoreSingleStruct()
{
	GMP_TEST_BEGIN("T16.typed store single-struct fast path");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.TypedStruct");

	StoreObjectMessageDirect(Slot, FSigSource(Src), FIntPoint(3, 4));  // single struct arg -> single-struct store path

	FIntPoint Got(0, 0); int32 Hits = 0;
	FSigHandle H;
	ListenObjectMessageDirect(Slot, FSigSource(Src), &H, [&](FIntPoint P) { ++Hits; Got = P; });
	GMP_TEST_CHECK(Hits == 1);
	GMP_TEST_CHECK(Got.X == 3 && Got.Y == 4);  // struct read at offset 0

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedStoreSingleStruct, "GMP.Typed.TypedStoreSingleStruct")

// ---- T17: typed StoreMessage with a native interface param (R011/R015 fix) ----
// Once a known limitation (R015), now fixed: the store-replay rebuild lays out a 24B backing computed via FScriptInterface::GetInterface().
// A typed store interface param backing is a UE FScriptInterface (16B: ObjectPointer + InterfacePointer), while the read side
// GetParamImpl(DispatchInterface) reads a GMP TGMPNativeInterface (24B = TScriptInterface 16B + InterfaceType*& Ref @offset16)
// at offset16. The old unified body replay fed the 16B backing directly -> reading offset16 was out-of-bounds (ACCESS_VIOLATION).
// Fix (AsTypedAddresses, GMPHub.cpp): for FInterfaceProperty params, lay out a side 24B slot (FStoreReplayAddrs::FIfaceReplaySlot):
// [0..15]=FScriptInterface copy, IfaceVal=GetInterface() real interface ptr value, [16..23]=&IfaceVal; GetNativeAddr() returns that Ref,
// the listener copies it by value into IXxx*. Live-fire is unaffected. See regressions.md R015 (RESOLVED).
static bool Test_TypedStoreInterfaceParam()
{
	GMP_TEST_BEGIN("T17.typed store native-interface param (R011/R015 fix)");
	UObject* Src = MakeProbe();
	UGMPTestInterfaceImpl* Impl = NewObject<UGMPTestInterfaceImpl>(GetTransientPackage());
	Impl->AddToRoot();
	IGMPTestInterface* Iface = Impl;
	// Key precondition: the interface subobject ptr (IGMPTestInterface*) != UObject* (Impl multiply-inherits), so a wrong layout reads UObject* instead of the real interface ptr.
	GMP_TEST_CHECK(static_cast<void*>(Iface) != static_cast<void*>(Impl));

	auto Slot = MSGKEY_SLOT("GMP.UT.TypedIface");
	StoreObjectMessageDirect(Slot, FSigSource(Src), Iface);  // persists the FScriptInterface (16B) backing

	// late listener: the store lays out 24B via AsTypedAddresses -> late-replay gets the real interface ptr.
	IGMPTestInterface* Got = nullptr; int32 Hits = 0; int32 Magic = 0; UObject* GotObj = nullptr;
	FSigHandle H;
	ListenObjectMessageDirect(Slot, FSigSource(Src), &H, [&](IGMPTestInterface* I) {
		++Hits;
		Got = I;
		if (I)
		{
			Magic = I->GMPTestMagic();      // calls the real impl method; a wrong layout crashes or reads the wrong vtable
			GotObj = I->_getUObject();       // the interface ptr should recover the host UObject
		}
	});
	GMP_TEST_CHECK(Hits == 1);
	GMP_TEST_CHECK(Got == Iface);            // got back the real interface subobject ptr that was stored
	GMP_TEST_CHECK(Magic == 4242);           // hit the real UGMPTestInterfaceImpl::GMPTestMagic
	GMP_TEST_CHECK(GotObj == Impl);          // _getUObject() recovered the host object
	GMP_TEST_CHECK(Cast<UGMPTestInterfaceImpl>(GotObj) == Impl);

	Impl->RemoveFromRoot();
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedStoreInterfaceParam, "GMP.Typed.TypedStoreInterfaceParam")

// NOTE: native-interface live-fire (former T17b) moved OUT of the gate to the T-EQ block as
// Test_EquivLiveInterfaceParam -- it only ever used the gate-agnostic FName Hub API, so it
// belongs with the gate-independent tests and now runs under ==0 too.

#endif  // GMP_WITH_MSG_HOLDER

// ---- T18: typed direct live-Fire non-const reference writeback (matches original FName path) ----
// Send crosses the boundary by reference; a listener taking T&/UObject*& writes back to the SENDER's local variable.
// (Live Fire only -- StoreMessage late-replay is value-only since the sender is gone.)
static bool Test_TypedRefWriteback()
{
	GMP_TEST_BEGIN("T18.typed live ref writeback (int& / UObject*&)");
	UObject* Src = MakeProbe();

	// (a) int& writeback: listener doubles the value in place
	{
		auto Slot = MSGKEY_SLOT("GMP.UT.RefInt");
		FSigHandle H;
		ListenObjectMessageDirect(Slot, FSigSource(Src), &H, [](int32& V) { V *= 10; });
		int32 X = 7;
		SendObjectMessageDirect(Slot, FSigSource(Src), X);  // X passed by reference
		GMP_TEST_CHECK(X == 70);  // listener wrote back to the sender's X
	}

	// (b) UObject*& writeback: listener sets the pointer
	{
		auto Slot = MSGKEY_SLOT("GMP.UT.RefObj");
		FSigHandle H;
		UObject* Target = MakeProbe();
		ListenObjectMessageDirect(Slot, FSigSource(Src), &H, [Target](UObject*& O) { O = Target; });
		UObject* Out = nullptr;
		SendObjectMessageDirect(Slot, FSigSource(Src), Out);  // Out passed by reference
		GMP_TEST_CHECK(Out == Target);  // listener wrote the pointer back to the sender's Out
		Target->RemoveFromRoot();
	}

	// (c) by-value listener still works on a ref-ABI signal (adapter static_casts T& -> value)
	{
		auto Slot = MSGKEY_SLOT("GMP.UT.RefVal");
		FSigHandle H;
		int32 Seen = 0;
		ListenObjectMessageDirect(Slot, FSigSource(Src), &H, [&](int32 V) { Seen = V; });
		int32 Y = 5;
		SendObjectMessageDirect(Slot, FSigSource(Src), Y);
		GMP_TEST_CHECK(Seen == 5);   // by-value listener reads correctly
		GMP_TEST_CHECK(Y == 5);      // and does NOT alter the sender
	}

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedRefWriteback, "GMP.Typed.TypedRefWriteback")

// ---- T19: mixed qualifiers on ONE key are SAFE (adapter unifies the thunk ABI to reference) ----
// int32 / int32& / const int32& listeners on the same key: the listen-side adapter compiles every listener's thunk to
// the SAME reference ABI (remove_reference_t<CbArgs>& == int32& for all three; const only adds read-onlyness, not a
// different register ABI). So one fire reaches all three -- no SigHash isolation needed, no ABI corruption.
static bool Test_TypedMixedQualifiers()
{
	GMP_TEST_BEGIN("T19.mixed qualifiers same key (thunk unifies ABI)");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.MixedQual");
	FSigHandle HVal, HRef, HCRef;
	int32 SeenVal = 0, SeenCRef = 0;

	ListenObjectMessageDirect(Slot, FSigSource(Src), &HVal, [&](int32 V) { SeenVal = V; });          // by value
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HRef, [](int32& V) { V += 100; });             // writeback ref
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HCRef, [&](const int32& V) { SeenCRef = V; }); // const ref

	int32 X = 5;
	SendObjectMessageDirect(Slot, FSigSource(Src), X);  // one fire reaches all three mixed-qualifier listeners
	// All three were invoked (no isolation): the ref listener wrote +100 to the sender's X; the value/const-ref
	// listeners observed the value (order-dependent for the post-write reads, so just assert delivery + writeback).
	GMP_TEST_CHECK(X == 105);        // the int32& listener wrote back (proves the ref listener ran on the same key)
	GMP_TEST_CHECK(SeenVal != 0);    // the by-value listener also ran (read 5 before, or 105 after -- either way delivered)
	GMP_TEST_CHECK(SeenCRef != 0);   // the const-ref listener also ran

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedMixedQualifiers, "GMP.Typed.TypedMixedQualifiers")
#if !GMP_WITH_STATIC_STORE  // modular-only: slot handle lazy ResolvePtr + duplicate OwnerSlot de-dup
// ---- T20: lazy / on-demand slot resolution (slot used before EndOfEngineInit batch-bind) ----
// Simulate an unbound slot (Ptr==null, i.e. direct API used before the startup batch bind, or an Editor modular
// duplicate). ResolvePtr must lazily get/create the authoritative store, write Ptr back, set OwnerSlot, and deliver
// normally (a one-time warning is logged). Mirrors the FName path's lazy GetSig<true> availability.
static bool Test_LazySlotResolve()
{
	GMP_TEST_BEGIN("T20.lazy slot resolve (unbound -> on-demand)");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.LazySlot");

	// Force the unbound state: clear the cached store pointer + OwnerSlot back-link as if batch-bind had not run yet.
	if (Slot->Ptr)
		Slot->Ptr->OwnerSlot = nullptr;
	Slot->Ptr = nullptr;

	// Listen on the unbound slot: ListenObjectMessage(slot) -> Slot.ResolvePtr() must lazily bind (no ensure, no drop).
	int32 Got = 0, Hits = 0;
	FSigHandle H;
	ListenObjectMessageDirect(Slot, FSigSource(Src), &H, [&](int32 v) { ++Hits; Got = v; });
	GMP_TEST_CHECK(Slot->Ptr != nullptr);                       // lazily bound on listen
	GMP_TEST_CHECK(Slot->Ptr->OwnerSlot == &Slot.Slot);         // two-way binding wired (so later rebuild writes back)

	// Send on the now-bound slot: normal delivery.
	SendObjectMessageDirect(Slot, FSigSource(Src), int32(42));
	GMP_TEST_CHECK(Hits == 1);
	GMP_TEST_CHECK(Got == 42);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_LazySlotResolve, "GMP.Typed.LazySlotResolve")

// ---- T21: duplicate modular slot must not steal the canonical owner ----------
// Editor/modular builds can have more than one FStaticSignalSlot object for the same key. Resolving a duplicate must not
// leave the already-bound canonical slot unable to receive future store rebuild/destroy writebacks.
static bool Test_DuplicateSlotResolveKeepsCanonicalOwner()
{
	GMP_TEST_BEGIN("T21.duplicate slot resolve keeps canonical owner");
	auto Canonical = MSGKEY_SLOT("GMP.UT.DuplicateSlotOwner");
	FSignalStore* CanonicalStore = Canonical->ResolvePtr();
	GMP_TEST_CHECK(CanonicalStore != nullptr);
	GMP_TEST_CHECK(CanonicalStore->OwnerSlot == &Canonical.Slot);

	FStaticSignalSlot Duplicate("GMP.UT.DuplicateSlotOwner");
	FSignalStore* DuplicateStore = Duplicate.ResolvePtr();
	const bool bSameStore = DuplicateStore == CanonicalStore;
	const bool bCanonicalStillOwner = CanonicalStore && CanonicalStore->OwnerSlot == &Canonical.Slot;
	const bool bCanonicalPtrStillValid = Canonical->Ptr == CanonicalStore;
	const bool bDuplicatePtrResolved = Duplicate.Ptr == CanonicalStore;

	// Keep later tests and shutdown safe even when the assertion above exposes a stolen owner.
	if (CanonicalStore)
		CanonicalStore->OwnerSlot = &Canonical.Slot;

	GMP_TEST_CHECK(bSameStore);
	GMP_TEST_CHECK(bCanonicalStillOwner);
	GMP_TEST_CHECK(bCanonicalPtrStillValid);
	GMP_TEST_CHECK(bDuplicatePtrResolved);
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_DuplicateSlotResolveKeepsCanonicalOwner, "GMP.Typed.DuplicateSlotResolveKeepsCanonicalOwner")
#endif  // !GMP_WITH_STATIC_STORE (T20/T21 are modular handle/ResolvePtr/OwnerSlot mechanics; N/A under static-store)

// ---- T-RR1: typed-direct Request/Response basic round trip (zero-pack reply) ----
// Responder-form listener (last param FGMPResponder&) replies typed; OnRsp receives it typed. No FMessageBody either way.
static bool Test_TypedRequestBasic()
{
	GMP_TEST_BEGIN("T-RR1.typed request/response round trip");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.RR.Basic");

	FSigHandle HL;
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HL, [](int32 a, FGMPResponder& R) { R.Response(a * 2); });

	int32 Got = 0, Hits = 0;
	SendObjectMessageDirect(Slot, FSigSource(Src), int32(21), [&](int32 r) { ++Hits; Got = r; });
	GMP_TEST_CHECK(Hits == 1);
	GMP_TEST_CHECK(Got == 42);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedRequestBasic, "GMP.Request.TypedRequestBasic")

// ---- T-RR2: multi-arg request + multi-arg response ----
static bool Test_TypedRequestMultiArg()
{
	GMP_TEST_BEGIN("T-RR2.typed request/response multi-arg");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.RR.Multi");

	FSigHandle HL;
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HL, [](int32 a, float b, FGMPResponder& R) { R.Response(a + (int32)b, FString(TEXT("ok"))); });

	int32 GotI = 0; FString GotS; int32 Hits = 0;
	SendObjectMessageDirect(Slot, FSigSource(Src), int32(10), 2.0f, [&](int32 i, const FString& s) { ++Hits; GotI = i; GotS = s; });
	GMP_TEST_CHECK(Hits == 1);
	GMP_TEST_CHECK(GotI == 12);
	GMP_TEST_CHECK(GotS == TEXT("ok"));

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedRequestMultiArg, "GMP.Request.TypedRequestMultiArg")

// ---- T-RR3: source isolation (request to SrcA only hits SrcA's responder) ----
static bool Test_TypedRequestSourceIsolation()
{
	GMP_TEST_BEGIN("T-RR3.typed request source isolation");
	UObject* SrcA = MakeProbe();
	UObject* SrcB = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.RR.Iso");

	FSigHandle HA, HB;
	int32 SeenA = 0, SeenB = 0;
	ListenObjectMessageDirect(Slot, FSigSource(SrcA), &HA, [&](int32 a, FGMPResponder& R) { ++SeenA; R.Response(a); });
	ListenObjectMessageDirect(Slot, FSigSource(SrcB), &HB, [&](int32 a, FGMPResponder& R) { ++SeenB; R.Response(a); });

	int32 Got = 0;
	SendObjectMessageDirect(Slot, FSigSource(SrcA), int32(5), [&](int32 r) { Got = r; });
	GMP_TEST_CHECK(SeenA == 1);
	GMP_TEST_CHECK(SeenB == 0);   // B's responder not invoked
	GMP_TEST_CHECK(Got == 5);

	SrcA->RemoveFromRoot();
	SrcB->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedRequestSourceIsolation, "GMP.Request.TypedRequestSourceIsolation")

// ---- T-RR4: chained R/R (OnRsp issues a second request) ----
static bool Test_TypedRequestChained()
{
	GMP_TEST_BEGIN("T-RR4.typed request chained");
	UObject* Src = MakeProbe();
	auto Slot1 = MSGKEY_SLOT("GMP.UT.RR.Chain1");
	auto Slot2 = MSGKEY_SLOT("GMP.UT.RR.Chain2");

	FSigHandle H1, H2;
	ListenObjectMessageDirect(Slot1, FSigSource(Src), &H1, [](int32 a, FGMPResponder& R) { R.Response(a + 1); });
	ListenObjectMessageDirect(Slot2, FSigSource(Src), &H2, [](int32 a, FGMPResponder& R) { R.Response(a * 10); });

	int32 Final = 0, Hits1 = 0, Hits2 = 0;
	SendObjectMessageDirect(Slot1, FSigSource(Src), int32(4), [&](int32 r1) {
		++Hits1;  // r1 == 5
		SendObjectMessageDirect(Slot2, FSigSource(Src), int32(r1), [&](int32 r2) { ++Hits2; Final = r2; });
	});
	GMP_TEST_CHECK(Hits1 == 1);
	GMP_TEST_CHECK(Hits2 == 1);
	GMP_TEST_CHECK(Final == 50);  // (4+1)*10

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedRequestChained, "GMP.Request.TypedRequestChained")

// ---- T-RR5: deferred responder forwarding (copy FGMPResponder, reply later) ----
static bool Test_TypedRequestDeferred()
{
	GMP_TEST_BEGIN("T-RR5.typed request deferred responder");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.RR.Defer");

	FGMPResponder Saved;
	FSigHandle HL;
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HL, [&](int32 a, FGMPResponder& R) { Saved = R; });  // stash, no immediate reply

	int32 Got = 0, Hits = 0;
	SendObjectMessageDirect(Slot, FSigSource(Src), int32(7), [&](int32 r) { ++Hits; Got = r; });
	GMP_TEST_CHECK(Hits == 0);          // not replied yet
	GMP_TEST_CHECK((bool)Saved);        // responder captured
	Saved.Response(int32(70));          // reply later
	GMP_TEST_CHECK(Hits == 1);
	GMP_TEST_CHECK(Got == 70);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedRequestDeferred, "GMP.Request.TypedRequestDeferred")

// ---- T-RR6: once semantics (second Response on same responder is a no-op) ----
static bool Test_TypedRequestOnce()
{
	GMP_TEST_BEGIN("T-RR6.typed response once");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.RR.Once");

	FGMPResponder Saved;
	FSigHandle HL;
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HL, [&](int32 a, FGMPResponder& R) { Saved = R; });

	int32 Hits = 0;
	SendObjectMessageDirect(Slot, FSigSource(Src), int32(1), [&](int32) { ++Hits; });
	Saved.Response(int32(1));
	Saved.Response(int32(1));   // second take finds nothing -> no-op
	GMP_TEST_CHECK(Hits == 1);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedRequestOnce, "GMP.Request.TypedRequestOnce")

// ---- T-RR7: R/R fast path reaches mixed listeners (responder-form + plain body listener coexisting) ----
// The tri-arg R/R fast-path request fire must reach ALL listeners on the key like a normal fire, not just the responder.
// Verifies: on the same key/source, a responder-form listener (replies) + a plain non-responder listener (only receives the request args),
// a single SendObjectMessageDirect(request) reaches both, and the responder reply hits OnRsp.
// This covers the contract that after the R/R fast path bypasses FMessageBody, non-responder listeners are still reached (prior R/R cases only checked the responder).
static bool Test_TypedRequestMixedListeners()
{
	GMP_TEST_BEGIN("T-RR7.request fast-path reaches mixed listeners (responder + plain)");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.RR.Mixed");

	int32 PlainGot = 0, PlainHits = 0, RspHits = 0;
	FSigHandle HPlain, HResp;
	// plain listener (non-responder): only receives the request args.
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HPlain, [&](int32 a) { ++PlainHits; PlainGot = a; });
	// responder-form listener: receives the request args + replies.
	ListenObjectMessageDirect(Slot, FSigSource(Src), &HResp, [&](int32 a, FGMPResponder& R) { ++RspHits; R.Response(a * 2); });

	int32 OnRspGot = 0, OnRspHits = 0;
	SendObjectMessageDirect(Slot, FSigSource(Src), int32(21), [&](int32 r) { ++OnRspHits; OnRspGot = r; });

	GMP_TEST_CHECK(PlainHits == 1);    // plain listener reached by the request fire (non-responder still reached after the fast path bypasses body)
	GMP_TEST_CHECK(PlainGot == 21);
	GMP_TEST_CHECK(RspHits == 1);      // responder reached too
	GMP_TEST_CHECK(OnRspHits == 1);    // reply round-tripped
	GMP_TEST_CHECK(OnRspGot == 42);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_TypedRequestMixedListeners, "GMP.Request.TypedRequestMixedListeners")

// ---- T-SC: script tri-arg listen path -------------------------------------
// Verifies a tri-arg callback registered via ScriptListenMessageRaw is invoked correctly through ScriptNotifyMessage (FireMsgBodyAdapt tri-arg fire),
// with correct paddrs values and extra->Size/Key/TypeNames (TypeNames is populated by FireMsgBodyAdapt; previously nullptr).
// Note: the commandlet does not start lua; this uses a plain C++ lambda over the real script tri-arg ABI to prove the ABI only; v8/lua_pcall is not covered here.
// ScriptNotifyMessage validates the signature via VerifyScriptMessage (requires a real UObject SigSource + non-None key), hence FSigSource(Src).
static bool Test_ScriptRawBasic()
{
	GMP_TEST_BEGIN("T-SC1.script tri-arg basic (paddrs + extra->TypeNames)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.SC.Basic");

	bool bFired = false;
	int32 GotVal = 0, GotSize = -1;
	FName GotKey, GotTypeName0, GotAddrTypeName0;
	Hub()->ScriptListenMessageRaw(FSigSource(Src), Key, Src,
		[&](const FGMPTypedAddr* paddrs, const FGMPExtra* extra) {
			bFired = true;
			GotSize = extra->Size;
			GotKey = extra->Key;
			GotVal = paddrs[0].GetParam<int32>();
			GotTypeName0 = extra->TypeNames ? extra->TypeNames[0] : NAME_None;  // verify the send side populated TypeNames
#if GMP_WITH_TYPENAME
			GotAddrTypeName0 = paddrs[0].TypeName;
#endif
		});

	int32 Payload = 7;
	FTypedAddresses Param{FGMPTypedAddr::MakeMsg(Payload)};
	Hub()->ScriptNotifyMessage(Key, Param, FSigSource(Src));

	GMP_TEST_CHECK(bFired);
	GMP_TEST_CHECK(GotSize == 1);
	GMP_TEST_CHECK(GotVal == 7);
	GMP_TEST_CHECK(GotKey == Key);
#if GMP_WITH_TYPENAME
	GMP_TEST_CHECK(!GotTypeName0.IsNone());               // extra->TypeNames is non-empty (population took effect)
	GMP_TEST_CHECK(GotTypeName0 == GotAddrTypeName0);     // extra->TypeNames[0] cross-checks with paddrs[0].TypeName
#endif

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptRawBasic, "GMP.Script.ScriptRawBasic")

static bool Test_ScriptRawMultiArg()
{
	GMP_TEST_BEGIN("T-SC2.script tri-arg multi-arg (int,float,FString)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.SC.Multi");

	int32 GotA = 0; float GotB = 0.f; FString GotC; int32 GotSize = -1;
	bool bTypeNamesOk = true;
	Hub()->ScriptListenMessageRaw(FSigSource(Src), Key, Src,
		[&](const FGMPTypedAddr* paddrs, const FGMPExtra* extra) {
			GotSize = extra->Size;
			GotA = paddrs[0].GetParam<int32>();
			GotB = paddrs[1].GetParam<float>();
			GotC = paddrs[2].GetParam<FString>();
#if GMP_WITH_TYPENAME
			for (int32 i = 0; i < extra->Size; ++i)
			{
				if (!extra->TypeNames || extra->TypeNames[i].IsNone() || extra->TypeNames[i] != paddrs[i].TypeName)
					bTypeNamesOk = false;
			}
#endif
		});

	int32 A = 42; float B = 3.5f; FString C = TEXT("hello");
	FTypedAddresses Param{FGMPTypedAddr::MakeMsg(A), FGMPTypedAddr::MakeMsg(B), FGMPTypedAddr::MakeMsg(C)};
	Hub()->ScriptNotifyMessage(Key, Param, FSigSource(Src));

	GMP_TEST_CHECK(GotSize == 3);
	GMP_TEST_CHECK(GotA == 42);
	GMP_TEST_CHECK(GotB == 3.5f);
	GMP_TEST_CHECK(GotC == TEXT("hello"));
	GMP_TEST_CHECK(bTypeNamesOk);
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptRawMultiArg, "GMP.Script.ScriptRawMultiArg")

static bool Test_ScriptRawSourceIsolation()
{
	GMP_TEST_BEGIN("T-SC3.script tri-arg source isolation");
	UObject* SrcA = MakeProbe();
	UObject* SrcB = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.SC.Iso");

	int32 HitsA = 0, HitsB = 0;
	Hub()->ScriptListenMessageRaw(FSigSource(SrcA), Key, SrcA,
		[&](const FGMPTypedAddr*, const FGMPExtra*) { ++HitsA; });
	Hub()->ScriptListenMessageRaw(FSigSource(SrcB), Key, SrcB,
		[&](const FGMPTypedAddr*, const FGMPExtra*) { ++HitsB; });

	int32 Payload = 1;
	FTypedAddresses Param{FGMPTypedAddr::MakeMsg(Payload)};
	Hub()->ScriptNotifyMessage(Key, Param, FSigSource(SrcA));  // hits A only
	GMP_TEST_CHECK(HitsA == 1);
	GMP_TEST_CHECK(HitsB == 0);

	SrcA->RemoveFromRoot();
	SrcB->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptRawSourceIsolation, "GMP.Script.ScriptRawSourceIsolation")

// ---- T-SC3b: tri-arg listener reads extra->Source / extra->Seq directly ----
// Covers exactly the extra fields the blueprint by-key listener (UGMPBPLib::ListenMessageByKey, three-arg form)
// reads to forward to its FGMPScriptDelegate: extra->Source.TryGetUObject() / extra->Key / extra->Seq + the
// TArray(paddrs, extra->Size). T-SC1 already verifies Key/Size/paddrs; this pins Source and Seq too, so the BP
// path's three-arg lambda body (which the commandlet cannot drive directly -- it needs a real World + bound
// UFUNCTION delegate) is validated at the same uniform three-arg ABI it actually runs on.
static bool Test_ScriptRawSourceSeq()
{
	GMP_TEST_BEGIN("T-SC3b.script tri-arg reads extra->Source/Seq");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.SC.SrcSeq");

	bool bFired = false;
	const UObject* GotSource = nullptr;
	uint64 GotSeq = 0xFFFFFFFFull;
	FName GotKey;
	int32 GotVal = 0, GotSize = -1;
	Hub()->ScriptListenMessageRaw(FSigSource(Src), Key, Src,
		[&](const FGMPTypedAddr* paddrs, const FGMPExtra* extra) {
			// Mirror the blueprint by-key listener body (GMPBPLib.cpp): read the 4 fields it forwards to the delegate.
			bFired = true;
			GotSource = extra->Source.TryGetUObject();
			GotKey = extra->Key;
			GotSeq = (uint64)(int64)extra->Seq;
			TArray<FGMPTypedAddr> Arr(paddrs, extra->Size);
			GotSize = Arr.Num();
			GotVal = Arr.Num() > 0 ? Arr[0].GetParam<int32>() : 0;
		});

	int32 Payload = 42;
	FTypedAddresses Param{FGMPTypedAddr::MakeMsg(Payload)};
	Hub()->ScriptNotifyMessage(Key, Param, FSigSource(Src));

	GMP_TEST_CHECK(bFired);
	GMP_TEST_CHECK(GotSource == Src);   // extra->Source carries the sig source (what BP delegate gets as SigSource)
	GMP_TEST_CHECK(GotKey == Key);
	GMP_TEST_CHECK(GotSize == 1);
	GMP_TEST_CHECK(GotVal == 42);
	// Note: extra->Seq is the message sequence id. Its concrete value differs by environment -- editor fire
	// (GMP_MSGBODY_ON_STACK -> InitOnStack auto-generates GetNextSequenceID() when Id==0, so non-zero) vs the
	// non-editor direct path (FGMPExtra Seq = FGMPKey{} == 0). The BP delegate just forwards whatever Seq the
	// fire carries (same value the body-style Msg.Sequence() would yield), so we don't assert a fixed number;
	// we only confirm it was delivered (GotSeq was written, i.e. the listener ran and read extra->Seq).
	GMP_TEST_CHECK(GotSeq != 0xFFFFFFFFull);  // overwritten from its sentinel -> extra->Seq was read

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptRawSourceSeq, "GMP.Script.ScriptRawSourceSeq")

// ---- T-SC4: script pure-notify direct tri-arg fire -> reaches listeners of every kind ----
// Verifies ScriptNotifyMessageImpl going through NotifyMessageDirectImpl (bypassing FMessageBody/atomic/push): on the same key/source,
// a C++ body listener (ListenObjectMessage) + a C++ tri-arg listener (ScriptListenMessageRaw), via ScriptNotifyMessage,
// both receive the value in one fire. The body listener rebuilds body from paddrs+extra (Seq=0) via the ConnectBodySlot adapter thunk; pure notify reads no seq.
static bool Test_ScriptNotifyToCppListeners()
{
	GMP_TEST_BEGIN("T-SC4.script notify direct tri-arg -> body + tri-arg listeners");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.SC.ToCpp");

	int32 BodyGot = 0, BodyHits = 0;   // C++ body listener (rebuilds body via the ConnectBodySlot adapter thunk)
	int32 TriGot = 0, TriHits = 0;     // C++ tri-arg listener (reads paddrs directly via ConnectRawSlot)
	FSigHandle HBody;
	Hub()->ListenObjectMessage(Key, FSigSource(Src), &HBody, [&](int32 V) { ++BodyHits; BodyGot = V; });
	Hub()->ScriptListenMessageRaw(FSigSource(Src), Key, Src,
		[&](const FGMPTypedAddr* paddrs, const FGMPExtra* extra) { ++TriHits; TriGot = paddrs[0].GetParam<int32>(); });

	int32 Payload = 42;
	FTypedAddresses Param{FGMPTypedAddr::MakeMsg(Payload)};
	Hub()->ScriptNotifyMessage(Key, Param, FSigSource(Src));  // tri-arg fire via NotifyMessageDirectImpl

	GMP_TEST_CHECK(BodyHits == 1);   // body listener received via the adapter thunk
	GMP_TEST_CHECK(BodyGot == 42);
	GMP_TEST_CHECK(TriHits == 1);    // tri-arg listener received via direct read
	GMP_TEST_CHECK(TriGot == 42);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptNotifyToCppListeners, "GMP.Script.ScriptNotifyToCppListeners")

#if WITH_EDITOR
// ---- T-SC5: script notify must keep editor call history parity ---------------
// The old body path recorded debug call history through NotifyMessageImpl. Direct script notify should preserve that
// editor-facing behavior so GetCallInfos/graph diagnostics do not regress when GMP_WITH_DIRECT_SIGNAL is enabled.
static bool Test_ScriptNotifyRecordsEditorHistory()
{
	GMP_TEST_BEGIN("T-SC5.script notify records editor history");
	UObject* Src = MakeProbe();
	UObject* Listener = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.SC.History");
	if (!GIsEditor)
	{
		GMP_TEST_CHECK(true);
		Listener->RemoveFromRoot();
		Src->RemoveFromRoot();
		GMP_TEST_END();
	}

	int32 Hits = 0;
	Hub()->ListenObjectMessage(Key, FSigSource(Src), Listener, [&](int32) { ++Hits; });

	Hub()->SendObjectMessage(Key, FSigSource(Src), int32(1));
	TArray<FString> Before;
	const bool bBaselineQueryOk = Hub()->GetCallInfos(Listener, Key, Before);
	GMP_TEST_CHECK(bBaselineQueryOk);
	GMP_TEST_CHECK(Before.Num() >= 1);

	int32 Payload = 2;
	FTypedAddresses Param{FGMPTypedAddr::MakeMsg(Payload)};
	Hub()->ScriptNotifyMessage(Key, Param, FSigSource(Src));
	TArray<FString> After;
	const bool bAfterQueryOk = Hub()->GetCallInfos(Listener, Key, After);

	GMP_TEST_CHECK(Hits == 2);
	GMP_TEST_CHECK(bAfterQueryOk);
	GMP_TEST_CHECK(After.Num() > Before.Num());

	Listener->RemoveFromRoot();
	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptNotifyRecordsEditorHistory, "GMP.Script.ScriptNotifyRecordsEditorHistory")
#endif

// ---- T-SC6: direct typed send must provide metadata for script tri-arg bridges -
// Script bridges consume FGMPExtra::TypeNames as the stable metadata surface; typed direct send must provide it too.
static bool Test_DirectSendSuppliesScriptExtraTypeNames()
{
	GMP_TEST_BEGIN("T-SC6.direct send supplies script extra type names");
	UObject* Src = MakeProbe();
	auto Slot = MSGKEY_SLOT("GMP.UT.SC.DirectExtraTypeNames");
	const FName Key = Slot.GetKey();

	int32 Hits = 0;
	int32 Got = 0;
	FName GotTypeName = NAME_None;
#if GMP_WITH_TYPENAME
	FName GotAddrTypeName = NAME_None;
#endif
	Hub()->ScriptListenMessageRaw(FSigSource(Src), FMSGKEY(Key), Src,
		[&](const FGMPTypedAddr* paddrs, const FGMPExtra* extra) {
			++Hits;
			Got = paddrs[0].GetParam<int32>();
			GotTypeName = (extra && extra->TypeNames) ? extra->TypeNames[0] : NAME_None;
#if GMP_WITH_TYPENAME
			GotAddrTypeName = paddrs[0].TypeName;
#endif
		});

	int32 Payload = 17;
	SendObjectMessageDirect(Slot, FSigSource(Src), Payload);
	GMP_TEST_CHECK(Hits == 1);
	GMP_TEST_CHECK(Got == 17);
	GMP_TEST_CHECK(!GotTypeName.IsNone());
#if GMP_WITH_TYPENAME
	GMP_TEST_CHECK(GotTypeName == GotAddrTypeName);
#endif

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_DirectSendSuppliesScriptExtraTypeNames, "GMP.Script.DirectSendSuppliesScriptExtraTypeNames")

// ---- T-RR-SC: script Request/Response round trip (pins down the real script R/R seq behavior) ----------
// Background: C++ R/R generates seq explicitly via MakeSingleShotImpl GetNextSequenceID(), registers GMPResponses[seq],
//   and the responder replies from body.Sequence()=seq, self-consistent (T-RR1..6 PASS).
// script R/R goes ScriptRequestMessage (bare FGMPMessageSig OnRsp) -> FResponseSig (default InId=0) ->
//   RequestMessageImpl registers GMPResponses[0] using OnRsp.GetId()=0; but FMessageBody(Id=0) construction falls back to
//   GetNextSequenceID() yielding a non-zero SequenceId -> responder sees body.Sequence() != the registered key(0) -> suspected reply mismatch.
// This case uses a plain C++ lambda over the real script API (like T-SC1..4, no lua) to prove the ABI/seq mechanism only.
// The responder uses ScriptListenMessageCallback (adds CallbackMarks, the bExsitResponder precondition of RequestMessageImpl)
//   + a responder lambda that reads seq from Body.Sequence() and replies via ScriptResponseMessage (mimicking a real BP responder).
static bool Test_ScriptRequestBasic()
{
	GMP_TEST_BEGIN("T-RR-SC1.script request/response round trip (seq behavior)");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.RRSC.Basic");

	// responder: on request, read seq from body, reply response(a*2). Record the seq the responder saw.
	uint64 ResponderSeq = 0;
	int32 ResponderHits = 0;
	Hub()->ScriptListenMessageCallback(Key, Src,
		[&](FMessageBody& Body) {
			++ResponderHits;
			ResponderSeq = (uint64)(int64)Body.Sequence();
			int32 RspVal = Body.GetParam<int32>(0) * 2;
			FTypedAddresses RspParam{FGMPTypedAddr::MakeMsg(RspVal)};
			Hub()->ScriptResponseMessage(Body.Sequence(), RspParam, FSigSource(Src));
		},
		FGMPListenOptions{});

	// request: OnRsp receives the reply. Record the returned RspKey and whether OnRsp was called.
	int32 Got = 0, OnRspHits = 0;
	uint64 OnRspSeq = 0;
	int32 ReqVal = 21;
	FTypedAddresses ReqParam{FGMPTypedAddr::MakeMsg(ReqVal)};
	FGMPKey RspKey = Hub()->ScriptRequestMessage(Key, ReqParam,
		[&](FMessageBody& RspBody) {
			++OnRspHits;
			OnRspSeq = (uint64)(int64)RspBody.Sequence();
			Got = RspBody.GetParam<int32>(0);
		},
		FSigSource(Src));

	// diagnostics: log the real observations (see the truth whether PASS/FAIL, no assumptions).
	UE_LOG(LogGMPUnitTest, Display, TEXT("    [T-RR-SC1] RspKey=%llu ResponderHits=%d ResponderSeq=%llu OnRspHits=%d OnRspSeq=%llu Got=%d"),
		(uint64)(int64)RspKey, ResponderHits, ResponderSeq, OnRspHits, OnRspSeq, Got);

	// real contract assertions: the responder must be called once; OnRsp must receive the reply value 42.
	GMP_TEST_CHECK(ResponderHits == 1);   // request delivered to the responder
	GMP_TEST_CHECK(OnRspHits == 1);       // reply OnRsp called (a seq=0 mismatch would FAIL here = bug confirmed)
	GMP_TEST_CHECK(Got == 42);            // reply value correct
	GMP_TEST_CHECK(RspKey.IsValid());     // request returned a valid key

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptRequestBasic, "GMP.ScriptRequest.ScriptRequestBasic")

// ---- T-RR-SC2: two concurrent requests (different keys) each reply without cross-talk ----
// If script R/R all registered GMPResponses[0], the second request would hit ensureAlwaysMsgf(!Contains(0)) -> dropped/cross-talk.
static bool Test_ScriptRequestConcurrent()
{
	GMP_TEST_BEGIN("T-RR-SC2.script request concurrent (no seq=0 collision)");
	UObject* Src = MakeProbe();
	const auto Key1 = MSGKEY("GMP.UT.RRSC.Conc1");
	const auto Key2 = MSGKEY("GMP.UT.RRSC.Conc2");

	// responder1/2: each stashes its seq and defers the reply (so both requests are pending in GMPResponses at once).
	uint64 Seq1 = 0, Seq2 = 0;
	Hub()->ScriptListenMessageCallback(Key1, Src,
		[&](FMessageBody& Body) { Seq1 = (uint64)(int64)Body.Sequence(); }, FGMPListenOptions{});
	Hub()->ScriptListenMessageCallback(Key2, Src,
		[&](FMessageBody& Body) { Seq2 = (uint64)(int64)Body.Sequence(); }, FGMPListenOptions{});

	int32 Hits1 = 0, Hits2 = 0, Got1 = 0, Got2 = 0;
	int32 V1 = 1, V2 = 2;
	FTypedAddresses P1{FGMPTypedAddr::MakeMsg(V1)};
	FGMPKey K1 = Hub()->ScriptRequestMessage(Key1, P1,
		[&](FMessageBody& RspBody) { ++Hits1; Got1 = RspBody.GetParam<int32>(0); }, FSigSource(Src));
	FTypedAddresses P2{FGMPTypedAddr::MakeMsg(V2)};
	FGMPKey K2 = Hub()->ScriptRequestMessage(Key2, P2,
		[&](FMessageBody& RspBody) { ++Hits2; Got2 = RspBody.GetParam<int32>(0); }, FSigSource(Src));

	UE_LOG(LogGMPUnitTest, Display, TEXT("    [T-RR-SC2] K1=%llu K2=%llu Seq1=%llu Seq2=%llu"),
		(uint64)(int64)K1, (uint64)(int64)K2, Seq1, Seq2);

	// the two requests must get distinct seq (and returned key); otherwise a key=0 collision.
	GMP_TEST_CHECK(K1.IsValid());
	GMP_TEST_CHECK(K2.IsValid());
	GMP_TEST_CHECK(K1 != K2);             // different requests -> different keys (seq=0 would make both 0/invalid = FAIL)

	// now each replies using the seq the responder stashed.
	int32 RV1 = 11, RV2 = 22;
	FTypedAddresses R1{FGMPTypedAddr::MakeMsg(RV1)};
	Hub()->ScriptResponseMessage(FGMPKey(Seq1), R1, FSigSource(Src));
	FTypedAddresses R2{FGMPTypedAddr::MakeMsg(RV2)};
	Hub()->ScriptResponseMessage(FGMPKey(Seq2), R2, FSigSource(Src));

	GMP_TEST_CHECK(Hits1 == 1);
	GMP_TEST_CHECK(Hits2 == 1);
	GMP_TEST_CHECK(Got1 == 11);
	GMP_TEST_CHECK(Got2 == 22);

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptRequestConcurrent, "GMP.ScriptRequest.ScriptRequestConcurrent")

// ---- T-RR-SC3: script request multi-arg + multi-arg response (int,float -> int,FString) ----
// Verifies post-fix multi-arg request/response serialization and reply values are correct with consistent seq (script version of C++ T-RR2).
static bool Test_ScriptRequestMultiArg()
{
	GMP_TEST_BEGIN("T-RR-SC3.script request/response multi-arg");
	UObject* Src = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.RRSC.Multi");

	int32 RA = 0; float RB = 0.f; int32 ResponderHits = 0;
	Hub()->ScriptListenMessageCallback(Key, Src,
		[&](FMessageBody& Body) {
			++ResponderHits;
			RA = Body.GetParam<int32>(0);
			RB = Body.GetParam<float>(1);
			int32 Sum = RA + (int32)RB;
			FString Tag = TEXT("ok");
			FTypedAddresses RspParam{FGMPTypedAddr::MakeMsg(Sum), FGMPTypedAddr::MakeMsg(Tag)};
			Hub()->ScriptResponseMessage(Body.Sequence(), RspParam, FSigSource(Src));
		},
		FGMPListenOptions{});

	int32 GotI = 0; FString GotS; int32 OnRspHits = 0;
	int32 A = 10; float B = 2.0f;
	FTypedAddresses ReqParam{FGMPTypedAddr::MakeMsg(A), FGMPTypedAddr::MakeMsg(B)};
	FGMPKey RspKey = Hub()->ScriptRequestMessage(Key, ReqParam,
		[&](FMessageBody& RspBody) { ++OnRspHits; GotI = RspBody.GetParam<int32>(0); GotS = RspBody.GetParam<FString>(1); },
		FSigSource(Src));

	GMP_TEST_CHECK(ResponderHits == 1);
	GMP_TEST_CHECK(OnRspHits == 1);
	GMP_TEST_CHECK(GotI == 12);
	GMP_TEST_CHECK(GotS == TEXT("ok"));
	GMP_TEST_CHECK(RspKey.IsValid());

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptRequestMultiArg, "GMP.ScriptRequest.ScriptRequestMultiArg")

// ---- T-RR-SC4: script request source isolation + script<->C++ cross-boundary R/R ----
// The responder is a C++ responder-form listener (ListenObjectMessage + FGMPResponder&, bound to a specific source);
// a script request to SrcA hits only SrcAs responder. Verifies: (1) source isolation; (2) the fixed script-request seq
// lets the C++ FGMPResponder reply hit (consistent seq across the script/C++ boundary).
static bool Test_ScriptRequestSourceIsolation()
{
	GMP_TEST_BEGIN("T-RR-SC4.script request source isolation + cross-boundary");
	UObject* SrcA = MakeProbe();
	UObject* SrcB = MakeProbe();
	const auto Key = MSGKEY("GMP.UT.RRSC.Iso");

	int32 SeenA = 0, SeenB = 0;
	FSigHandle HA, HB;
	Hub()->ListenObjectMessage(Key, FSigSource(SrcA), &HA, [&](int32 a, FGMPResponder& R) { ++SeenA; R.Response(a); });
	Hub()->ListenObjectMessage(Key, FSigSource(SrcB), &HB, [&](int32 a, FGMPResponder& R) { ++SeenB; R.Response(a); });

	int32 Got = 0, OnRspHits = 0;
	int32 ReqVal = 5;
	FTypedAddresses ReqParam{FGMPTypedAddr::MakeMsg(ReqVal)};
	FGMPKey RspKey = Hub()->ScriptRequestMessage(Key, ReqParam,
		[&](FMessageBody& RspBody) { ++OnRspHits; Got = RspBody.GetParam<int32>(0); },
		FSigSource(SrcA));

	GMP_TEST_CHECK(SeenA == 1);       // SrcAs responder hit
	GMP_TEST_CHECK(SeenB == 0);       // SrcBs responder not hit (source isolation)
	GMP_TEST_CHECK(OnRspHits == 1);   // cross-boundary reply hit (script request -> C++ FGMPResponder -> script OnRsp)
	GMP_TEST_CHECK(Got == 5);
	GMP_TEST_CHECK(RspKey.IsValid());

	SrcA->RemoveFromRoot();
	SrcB->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptRequestSourceIsolation, "GMP.ScriptRequest.ScriptRequestSourceIsolation")

// ---- T-RR-SC5: chained script R/R (OnRsp starts a second script request) ----
// The two nested requests must each get a distinct atomic seq without key collision (pre-fix the inner one collided with the outers leaked [0]).
static bool Test_ScriptRequestChained()
{
	GMP_TEST_BEGIN("T-RR-SC5.script request chained (nested seq distinct)");
	UObject* Src = MakeProbe();
	const auto Key1 = MSGKEY("GMP.UT.RRSC.Chain1");
	const auto Key2 = MSGKEY("GMP.UT.RRSC.Chain2");

	// responder1: a+1; responder2: a*10. Each replies from body.Sequence().
	Hub()->ScriptListenMessageCallback(Key1, Src,
		[&](FMessageBody& Body) {
			int32 v = Body.GetParam<int32>(0) + 1;
			FTypedAddresses Rsp{FGMPTypedAddr::MakeMsg(v)};
			Hub()->ScriptResponseMessage(Body.Sequence(), Rsp, FSigSource(Src));
		}, FGMPListenOptions{});
	Hub()->ScriptListenMessageCallback(Key2, Src,
		[&](FMessageBody& Body) {
			int32 v = Body.GetParam<int32>(0) * 10;
			FTypedAddresses Rsp{FGMPTypedAddr::MakeMsg(v)};
			Hub()->ScriptResponseMessage(Body.Sequence(), Rsp, FSigSource(Src));
		}, FGMPListenOptions{});

	int32 Final = 0, Hits1 = 0, Hits2 = 0;
	int32 ReqVal = 4;
	FTypedAddresses P1{FGMPTypedAddr::MakeMsg(ReqVal)};
	Hub()->ScriptRequestMessage(Key1, P1,
		[&](FMessageBody& RspBody1) {
			++Hits1;
			int32 r1 = RspBody1.GetParam<int32>(0);  // 5
			FTypedAddresses P2{FGMPTypedAddr::MakeMsg(r1)};
			Hub()->ScriptRequestMessage(Key2, P2,
				[&](FMessageBody& RspBody2) { ++Hits2; Final = RspBody2.GetParam<int32>(0); },
				FSigSource(Src));
		}, FSigSource(Src));

	GMP_TEST_CHECK(Hits1 == 1);
	GMP_TEST_CHECK(Hits2 == 1);
	GMP_TEST_CHECK(Final == 50);   // (4+1)*10

	Src->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_ScriptRequestChained, "GMP.ScriptRequest.ScriptRequestChained")
#endif  // GMP_WITH_DIRECT_SIGNAL

// ============================================================================
// C++ -> Blueprint zero-copy FastCall (GMPBPFastCall.h). These are independent of
// GMP_WITH_DIRECT_SIGNAL (they exercise TGMPBPFastCall / InvokeBlueprintEvent, not
// the message hub), so they run under BOTH gate values. Targets are native UFUNCTIONs
// on UGMPTestProbe; native UFUNCTIONs read params from the frame (Stack.Locals) via the
// PropertyChainForCompiledIn + StepExplicitProperty path (Stack.h:485-499), write the
// return value to RESULT_PARAM, and write out-params via OutParms -- the exact ABI our
// FFrame setup mirrors from ProcessEventInternal (ScriptCore.cpp:2110-2208).
// ----------------------------------------------------------------------------

// Helper: a rooted concrete probe we can read members off of.
static UGMPTestProbe* MakeTypedProbe()
{
	UGMPTestProbe* Obj = NewObject<UGMPTestProbe>(GetTransientPackage(), UGMPTestProbe::StaticClass(), NAME_None, RF_Transient);
	Obj->AddToRoot();
	return Obj;
}

// ---- T20: all-POD + return value -> zero-copy fast path (tuple IS the frame) -----------
static bool Test_FastCallReturnValue()
{
	GMP_TEST_BEGIN("T20.FastCall POD return value (zero-copy)");
	UGMPTestProbe* Probe = MakeTypedProbe();
	UFunction* Func = Probe->FindFunction(TEXT("FastCallAddInts"));
	GMP_TEST_CHECK(Func != nullptr);
	if (Func)
	{
		// FastInvoke takes args by non-const lvalue ref (so out-params can write back), so
		// inputs must be named lvalues -- literals won't bind.
		int32 A = 12, B = 30, Ret = 0;
		TGMPBPFastCall<int32(int32, int32)>::FastInvoke(Probe, Func, A, B, Ret);
		GMP_TEST_CHECK(Ret == 42);            // return value written back into caller's R&
		GMP_TEST_CHECK(Probe->LastA == 12);   // param A read from the tuple-frame
		GMP_TEST_CHECK(Probe->LastB == 30);   // param B read from the tuple-frame
	}
	Probe->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_FastCallReturnValue, "GMP.FastCall.ReturnValue")

// ---- T21: out/ref param writeback (RefEvent: the only way a void event returns output) --
static bool Test_FastCallOutParm()
{
	GMP_TEST_BEGIN("T21.FastCall out-param writeback");
	UGMPTestProbe* Probe = MakeTypedProbe();
	UFunction* Func = Probe->FindFunction(TEXT("FastCallScaleOut"));
	GMP_TEST_CHECK(Func != nullptr);
	if (Func)
	{
		int32 In = 21, OutDouble = -1;
		TGMPBPFastCall<void(int32, int32&)>::FastInvoke(Probe, Func, In, OutDouble);
		GMP_TEST_CHECK(OutDouble == 42);      // OutParm redirected straight to caller's int32&
		GMP_TEST_CHECK(Probe->LastA == 21);   // in-param read from the tuple-frame
	}
	Probe->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_FastCallOutParm, "GMP.FastCall.OutParm")

// ---- T22: void, zero args (the void/zero-arg overload; empty parameter frame) ----------
static bool Test_FastCallVoidZeroArg()
{
	GMP_TEST_BEGIN("T22.FastCall void zero-arg");
	UGMPTestProbe* Probe = MakeTypedProbe();
	UFunction* Func = Probe->FindFunction(TEXT("FastCallTick"));
	GMP_TEST_CHECK(Func != nullptr);
	if (Func)
	{
		const int32 Before = Probe->TickCount;
		TGMPBPFastCall<void()>::FastInvoke(Probe, Func);
		TGMPBPFastCall<void()>::FastInvoke(Probe, Func);
		GMP_TEST_CHECK(Probe->TickCount == Before + 2);
	}
	Probe->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_FastCallVoidZeroArg, "GMP.FastCall.VoidZeroArg")

// ---- T23: deliberate layout mismatch -> safe reflection fallback (no crash, no ensure) --
// Call FastCallAddInts(int32,int32) through a wrong-SIZE template signature (int64 first
// arg). sizeof(tuple<int64,int32,int32>) != Function->ParmsSize, so VerifyTupleLayout
// short-circuits false on the very first size check -> NOT eligible -> the reflection slow
// path runs. The slow path copies each arg through its FProperty (CopyCompleteValue copies
// the property's element size = 4 bytes from the int64 arg's low word on little-endian),
// so the call is memory-safe and the function still runs. This proves a bad layout can
// never corrupt the frame -- it degrades to the safe path. (No GMP_WITH_DYNAMIC_CALL_CHECK
// ensure fires: VerifyArgNames runs only on the fast path, which we do not take here.)
static bool Test_FastCallLayoutMismatchFallback()
{
	GMP_TEST_BEGIN("T23.FastCall layout-mismatch -> reflection fallback");
	UGMPTestProbe* Probe = MakeTypedProbe();
	UFunction* Func = Probe->FindFunction(TEXT("FastCallAddInts"));
	GMP_TEST_CHECK(Func != nullptr);
	if (Func)
	{
		Probe->LastA = Probe->LastB = 0;
		int64 A64 = 5; int32 B = 7, Ret = 0;
		// int64 first arg -> tuple is larger than ParmsSize -> forced slow path.
		TGMPBPFastCall<int32(int64, int32)>::FastInvoke(Probe, Func, A64, B, Ret);
		GMP_TEST_CHECK(Probe->LastA == 5);  // low word of the int64 reached the int32 slot
		GMP_TEST_CHECK(Probe->LastB == 7);
		GMP_TEST_CHECK(Ret == 12);          // function still produced a correct result
	}
	Probe->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_FastCallLayoutMismatchFallback, "GMP.FastCall.LayoutMismatchFallback")

// ---- T24: non-POD params + non-POD ref writeback + non-POD return value -----------------
// FastCallAppendStr(const FString&, FString&) -> FString. bAllPOD == false, so this takes
// the placement-construct fast-path branch: the param tuple<FString,FString,FString> is
// placement-NEW-constructed into the frame (NEVER assigned into zero memory), the call runs,
// the FString ref is written back through OutParm redirection, the non-POD return value is
// written into the caller's already-constructed FString&, and finally the tuple is C++-destroyed
// (~TupType). This is the branch that would leak or double-free if construction/destruction
// were wrong, so all three FStrings are checked against distinct initial values.
static bool Test_FastCallNonPodRefAndReturn()
{
	GMP_TEST_BEGIN("T24.FastCall non-POD ref writeback + return value");
	UGMPTestProbe* Probe = MakeTypedProbe();
	UFunction* Func = Probe->FindFunction(TEXT("FastCallAppendStr"));
	GMP_TEST_CHECK(Func != nullptr);
	if (Func)
	{
		FString In = TEXT("ab");
		FString Out = TEXT("X");        // distinct initial value
		FString Ret = TEXT("?");        // distinct initial value
		TGMPBPFastCall<FString(const FString&, FString&)>::FastInvoke(Probe, Func, In, Out, Ret);
		GMP_TEST_CHECK(Out == TEXT("Xab"));     // non-POD ref appended in place, written back
		GMP_TEST_CHECK(Ret == TEXT("R:ab"));    // non-POD return value written into caller's FString&
		GMP_TEST_CHECK(Probe->LastStr == TEXT("ab"));  // non-POD in-param read from the tuple-frame
		GMP_TEST_CHECK(In == TEXT("ab"));       // caller's input is unchanged (const ref, not clobbered)
	}
	Probe->RemoveFromRoot();
	GMP_TEST_END();
}
GMP_IMPLEMENT_AUTOMATION_TEST(Test_FastCallNonPodRefAndReturn, "GMP.FastCall.NonPodRefAndReturn")

// ---- Benchmark (moved here from GMPHub.cpp; benchmarks belong in the test file) ----
// Per-broadcast cost: native MulticastDelegate vs GMP FName-send vs GMP slot-send vs GMP typed pass-through.
// Run: -run=GMPUnitTest -Bench   (default 8 listeners x 1,000,000 iters). Uses an object source (no world needed).
static volatile int64 GBenchSink = 0;
DECLARE_MULTICAST_DELEGATE_OneParam(FBenchDelegate, int32);

static void RunBenchmark()
{
	UObject* Src = MakeProbe();
	const int32 NumListeners = 8;
	const int64 Iters = 1000000;
	auto* H = Hub();

	// baseline 1: native MulticastDelegate
	FBenchDelegate NativeDel;
	for (int32 i = 0; i < NumListeners; ++i)
		NativeDel.AddLambda([](int32 V) { GBenchSink += V; });
	double T0 = FPlatformTime::Seconds();
	for (int64 i = 0; i < Iters; ++i)
		NativeDel.Broadcast(1);
	const double NativeNs = (FPlatformTime::Seconds() - T0) / Iters * 1e9;

	// baseline 2: GMP FName send (separate handles to avoid de-dup)
	static FSigHandle FNameHandles[16];
	for (int32 i = 0; i < NumListeners; ++i)
		H->ListenObjectMessage(MSGKEY("GMP.Bench.FName"), Src, &FNameHandles[i], [](int32 V) { GBenchSink += V; });
	double T1 = FPlatformTime::Seconds();
	for (int64 i = 0; i < Iters; ++i)
		H->SendObjectMessage(MSGKEY("GMP.Bench.FName"), Src, int32(1));
	const double FNameNs = (FPlatformTime::Seconds() - T1) / Iters * 1e9;

	double SlotNs = -1.0, TypedNs = -1.0;
#if GMP_WITH_DIRECT_SIGNAL
	// baseline 3: GMP slot-direct send (FName listeners, slot-resolved send)
	static FSigHandle SlotHandles[16];
	for (int32 i = 0; i < NumListeners; ++i)
		H->ListenObjectMessage(MSGKEY("GMP.Bench.Slot"), Src, &SlotHandles[i], [](int32 V) { GBenchSink += V; });
	auto BenchSlot = MSGKEY_SLOT("GMP.Bench.Slot");
	double T2 = FPlatformTime::Seconds();
	for (int64 i = 0; i < Iters; ++i)
		SendObjectMessageDirect(BenchSlot, FSigSource(Src), int32(1));
	SlotNs = (FPlatformTime::Seconds() - T2) / Iters * 1e9;

	// baseline 4: GMP typed pass-through (no body, strongly typed TSignal Fire == native delegate)
	static FSigHandle TypedHandles[16];
	auto TypedSlot = MSGKEY_SLOT("GMP.Bench.Typed");
	for (int32 i = 0; i < NumListeners; ++i)
		ListenObjectMessageDirect(TypedSlot, FSigSource(Src), &TypedHandles[i], [](int32 V) { GBenchSink += V; });
	double T3 = FPlatformTime::Seconds();
	for (int64 i = 0; i < Iters; ++i)
		SendObjectMessageDirect(TypedSlot, FSigSource(Src), int32(1));
	TypedNs = (FPlatformTime::Seconds() - T3) / Iters * 1e9;
#endif

	UE_LOG(LogGMPUnitTest, Display,
		TEXT("[Bench] listeners=%d iters=%lld | native=%.1fns  gmpFName=%.1fns(%.2fx)  gmpSlot=%.1fns(%.2fx)  gmpTyped=%.1fns(%.2fx)"),
		NumListeners, Iters, NativeNs,
		FNameNs, FNameNs / NativeNs,
		SlotNs, SlotNs > 0 ? SlotNs / NativeNs : -1.0,
		TypedNs, TypedNs > 0 ? TypedNs / NativeNs : -1.0);

	Src->RemoveFromRoot();
}


// Convenience headless entry shared with the commandlet (RunAllGMPTests). Registers every Test_*
// gated by the same switches; returns the failed-case count (0 = all pass) for the commandlet exit code.
int32 RunAllGMPTests(const FString& Params)
{
	const bool bNoDirect = FParse::Param(*Params, TEXT("NoDirect"));

	UE_LOG(LogGMPUnitTest, Display, TEXT("==== GMP Unit Tests (GMP_WITH_DIRECT_SIGNAL=%d, NoDirect=%d) ===="),
		(int)GMP_WITH_DIRECT_SIGNAL, (int)bNoDirect);

	GNumRun = 0; GNumFail = 0;

	Test_FNameBasic();
	Test_FlexSignalGmpStoragePolicy();  // FlexSignal policy 注入 GMPFunction 存储(gate-independent)

	// C++->BP FastCall (GMPBPFastCall.h) -- gate-independent, runs under both ==0 and ==1.
	Test_FastCallReturnValue();
	Test_FastCallOutParm();
	Test_FastCallVoidZeroArg();
	Test_FastCallLayoutMismatchFallback();
	Test_FastCallNonPodRefAndReturn();
#if GMP_WITH_DIRECT_SIGNAL
	if (!bNoDirect)
	{
		Test_SlotDirect();
		Test_TypedEntryAutoSlot();
		Test_TypedDirect();
		Test_TypedCoexistenceNoMisfire();
	}
#endif
	Test_LeveledDispatch();
	Test_LeveledDispatchWorldTier();
#if GMP_WITH_SIGNAL_ORDER
	Test_LeveledDispatchOrder();
	Test_LeveledDispatchWorldTierOrder();
#endif
	Test_DisconnectAndGC();
#if GMP_ENABLE_STATIC_DISCONNECT
	Test_StaticDisconnectByKey();
#endif
	Test_AutoInvalidationPurgesStaleListenerImmediately();

	// FSigSource forms
	Test_SourceObjectIsolation();
	Test_SourceWeakAndObjectPtr();
	Test_SourceISigSource();
	Test_SourceKeyed();
	Test_SourceAnyWildcard();
	// ==1/==0 equivalence cases (gate-agnostic; run under both gates to verify identical behavior)
	Test_EquivStoreLateDelivery();
	Test_EquivStoreRemovedWithSourceLifecycle();
	Test_EquivOnceConsumed();
	Test_EquivOnceLiveDeliveryDoesNotPersist();
	Test_EquivRequestResponse();
	// gate-agnostic equivalents of direct-only coverage (zero-arg / store isolation / single-struct /
	// interface store+live), so ==0 covers these two-gate-shared semantics too.
	Test_EquivZeroArg();
	Test_EquivStoreSourceIsolation();
	Test_EquivStoreSingleStruct();
	Test_EquivStoreInterfaceParam();
	Test_EquivLiveInterfaceParam();
	Test_ReqRspProxyRoundTrip();  // migrated from UGMPRpcProxy::BeginPlay bTest sample (ReqRsp half)
#if GMP_WITH_DIRECT_SIGNAL
	if (!bNoDirect)
	{
		Test_TypedZeroArgDirect();
		Test_TypedSourceIsolation();
		Test_TypedLeveledDispatchWorldTier();
#if GMP_WITH_SIGNAL_ORDER
		Test_TypedLeveledDispatchOrder();
		Test_TypedLeveledDispatchWorldTierOrder();
#endif
#if GMP_WITH_MSG_HOLDER
		Test_TypedStoreLateDelivery();
		Test_TypedStoreIsolationReduced();
		Test_TypedStoreRemovedWithSourceLifecycle();
		Test_TypedOnceRemoveUnbind();
		Test_TypedOnceLiveDeliveryDoesNotPersist();
		Test_TypedStoreSingleStruct();
		Test_TypedStoreInterfaceParam();
#endif
		Test_TypedRefWriteback();
		Test_TypedMixedQualifiers();
#if !GMP_WITH_STATIC_STORE  // modular-only mechanics (lazy ResolvePtr / duplicate OwnerSlot)
		Test_LazySlotResolve();
		Test_DuplicateSlotResolveKeepsCanonicalOwner();
#endif
		Test_TypedRequestBasic();
		Test_TypedRequestMultiArg();
		Test_TypedRequestSourceIsolation();
		Test_TypedRequestChained();
		Test_TypedRequestDeferred();
		Test_TypedRequestOnce();
		Test_TypedRequestMixedListeners();
		// script tri-arg listen path
		Test_ScriptRawBasic();
		Test_ScriptRawMultiArg();
		Test_ScriptRawSourceIsolation();
		Test_ScriptRawSourceSeq();
		Test_ScriptNotifyToCppListeners();
#if WITH_EDITOR
		Test_ScriptNotifyRecordsEditorHistory();
#endif
		Test_DirectSendSuppliesScriptExtraTypeNames();
		// script R/R path (pins down seq behavior + scenario coverage)
		Test_ScriptRequestBasic();
		Test_ScriptRequestConcurrent();
		Test_ScriptRequestMultiArg();
		Test_ScriptRequestSourceIsolation();
		Test_ScriptRequestChained();
	}
#endif

	UE_LOG(LogGMPUnitTest, Display, TEXT("==== GMP Unit Tests: %d run, %d failed ===="), GNumRun, GNumFail);
	if (GNumFail == 0)
	{
		UE_LOG(LogGMPUnitTest, Display, TEXT("ALL PASS"));
	}
	else
	{
		UE_LOG(LogGMPUnitTest, Error, TEXT("%d CASE(S) FAILED"), GNumFail);
	}

	// Optional micro-benchmark (slow: ~1M iters per path); opt-in via -Bench.
	if (FParse::Param(*Params, TEXT("Bench")))
	{
		UE_LOG(LogGMPUnitTest, Display, TEXT("---- running benchmark (-Bench) ----"));
		RunBenchmark();
	}

	return GNumFail;  // exit code: 0 = all pass
}

}  // namespace GMPUnitTest
