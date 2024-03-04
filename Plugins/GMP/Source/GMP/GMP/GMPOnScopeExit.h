//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include <type_traits>
#include <utility>
namespace GMP
{
namespace OnScopeExit
{
	enum class EState
	{
		ON_SUCCESS = 1,
		ON_FAILURE = 2,
		RUN_ALWAYS = 3,
		RUN_NEVER = 4,
	};

	template<typename Fun>
	class TScopeGuard
	{
	public:
		TScopeGuard(Fun&& f, EState state)
			: fun_(std::move(f))
			, state_(state)
		{
		}

		~TScopeGuard()
		{
#if ((defined(_MSVC_LANG) && _MSVC_LANG >= 201703L) || __cplusplus >= 201703L)
			int exceptions = std::uncaught_exceptions();
#else
			int exceptions = std::uncaught_exception() ? 1 : 0;
#endif
			if (state_ == EState::RUN_ALWAYS || (state_ == EState::ON_FAILURE && exceptions > 0) || (state_ == EState::ON_SUCCESS && exceptions == 0))
			{
				fun_();
			}
		}

		void dismiss(bool reset = false)
		{
			state_ = EState::RUN_NEVER;
			// 		if (reset)
			// 			fun_ = nullptr;
		}

		TScopeGuard() = delete;
		TScopeGuard(const TScopeGuard&) = delete;
		TScopeGuard& operator=(const TScopeGuard&) = delete;

		TScopeGuard(TScopeGuard&& rhs)
			: fun_(std::move(rhs.fun_))
			, state_(rhs.state_)
		{
			rhs.dismiss(true);
		}

	private:
		Fun fun_;
		EState state_;
	};
	template<typename Fun>
	inline TScopeGuard<Fun> operator+(EState state, Fun&& fn)
	{
		return TScopeGuard<Fun>(std::forward<Fun>(fn), state);
	}
}  // namespace OnScopeExit
}  // namespace GMP

#define Z_SCOPEGUARD_CONCATENATE_IMPL__(s1, s2) s1##s2
#define Z_SCOPEGUARD_CONCATENATE__(s1, s2) Z_SCOPEGUARD_CONCATENATE_IMPL__(s1, s2)

// Helper macro
#define GMP_SCOPE_EXIT auto Z_SCOPEGUARD_CONCATENATE__(OnScopeExit_, __LINE__) = GMP::OnScopeExit::EState(GMP::OnScopeExit ::EState::RUN_ALWAYS) + [&]()

#define GMP_SCOPE_SUCCESS auto Z_SCOPEGUARD_CONCATENATE__(OnScopeExit_, __LINE__) = GMP::OnScopeExit ::EState(GMP::OnScopeExit ::EState::ON_SUCCESS) + [&]()

#define GMP_SCOPE_FAIL auto Z_SCOPEGUARD_CONCATENATE__(OnScopeExit_, __LINE__) = GMP::OnScopeExit::EState(GMP::OnScopeExit ::EState::ON_FAILURE) + [&]()
