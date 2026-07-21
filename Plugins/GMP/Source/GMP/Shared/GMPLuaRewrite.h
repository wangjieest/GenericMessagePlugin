//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

// Backend-agnostic load-time lua source rewriter, shared by the UnLua and slua GMP backends:
//   NotifyObjectMessage(sender, "Tag", ...) -> Notify_<id>(sender, ...)
// so script authors keep writing NotifyObjectMessage while each call reaches the per-tag key-baked SB (Notify_<id>).
// Robust lexer: skips lua short/long strings and line/block comments. SanitizeInto mirrors GMPScriptCodeGen::SanitizeIdent
// (non [0-9A-Za-z_] -> '_'). Pure text over FString; the function is stateless so slua's capture-less raw loadFileDelegate
// can call it too.
namespace GMPLuaRewrite
{
inline void SanitizeInto(FString& Out, const FString& Key)
{
	Out.Reserve(Out.Len() + Key.Len());
	for (TCHAR C : Key)
		Out.AppendChar(((C >= '0' && C <= '9') || (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == '_') ? C : TEXT('_'));
}

// [==[ ... ]==] level at S[i]=='['; returns eq-count and validity.
inline bool LongBracketLevel(const FString& S, int32 i, int32& OutLevel)
{
	const int32 N = S.Len();
	if (i >= N || S[i] != '[')
		return false;
	int32 k = i + 1, eq = 0;
	while (k < N && S[k] == '=') { ++eq; ++k; }
	if (k < N && S[k] == '[') { OutLevel = eq; return true; }
	return false;
}

inline int32 SkipLong(const FString& S, int32 from, int32 level)
{
	FString Close = TEXT("]");
	for (int32 e = 0; e < level; ++e) Close.AppendChar('=');
	Close.AppendChar(']');
	const int32 idx = S.Find(Close, ESearchCase::CaseSensitive, ESearchDir::FromStart, from);
	return idx >= 0 ? idx + Close.Len() : S.Len();
}

// Parse args of a call whose '(' is at OpenParen; fills top-level arg substrings, returns index just after ')'.
inline bool SplitArgs(const FString& S, int32 OpenParen, TArray<FString>& OutArgs, int32& OutAfter)
{
	const int32 N = S.Len();
	int32 i = OpenParen + 1, depth = 0;
	FString Cur;
	while (i < N)
	{
		const TCHAR c = S[i];
		if (c == '"' || c == '\'')
		{
			const TCHAR q = c; int32 j = i + 1;
			while (j < N) { if (S[j] == '\\') { j += 2; continue; } if (S[j] == q) { ++j; break; } ++j; }
			Cur += S.Mid(i, j - i); i = j; continue;
		}
		if (c == '(' || c == '[' || c == '{') { ++depth; Cur.AppendChar(c); ++i; continue; }
		if (c == ')' || c == ']' || c == '}')
		{
			if (c == ')' && depth == 0) { OutArgs.Add(Cur); OutAfter = i + 1; return true; }
			--depth; Cur.AppendChar(c); ++i; continue;
		}
		if (c == ',' && depth == 0) { OutArgs.Add(Cur); Cur.Reset(); ++i; continue; }
		Cur.AppendChar(c); ++i;
	}
	return false;
}

inline bool StringLiteralValue(const FString& In, FString& OutVal)
{
	FString s = In.TrimStartAndEnd();
	if (s.Len() >= 2 && (s[0] == '"' || s[0] == '\'') && s[s.Len() - 1] == s[0])
	{
		OutVal = s.Mid(1, s.Len() - 2);
		return true;
	}
	return false;
}

inline FString Rewrite(const FString& Src)
{
	const int32 N = Src.Len();
	FString Out;
	Out.Reserve(N + 64);
	int32 i = 0;
	while (i < N)
	{
		const TCHAR c = Src[i];
		if (c == '-' && i + 1 < N && Src[i + 1] == '-')  // comment
		{
			int32 lvl = 0;
			if (i + 2 < N && Src[i + 2] == '[' && LongBracketLevel(Src, i + 2, lvl))
			{
				const int32 e = SkipLong(Src, i + 2 + 1 + lvl + 1, lvl);
				Out += Src.Mid(i, e - i); i = e; continue;
			}
			int32 e = Src.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i);
			if (e < 0) e = N;
			Out += Src.Mid(i, e - i); i = e; continue;
		}
		int32 lvl = 0;
		if (c == '[' && LongBracketLevel(Src, i, lvl))  // long string
		{
			const int32 e = SkipLong(Src, i + 1 + lvl + 1, lvl);
			Out += Src.Mid(i, e - i); i = e; continue;
		}
		if (c == '"' || c == '\'')  // short string
		{
			const TCHAR q = c; int32 j = i + 1;
			while (j < N) { if (Src[j] == '\\') { j += 2; continue; } if (Src[j] == q) { ++j; break; } ++j; }
			Out += Src.Mid(i, j - i); i = j; continue;
		}
		const bool bIdentStart = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
		if (bIdentStart)
		{
			int32 j = i + 1;
			while (j < N) { const TCHAR d = Src[j]; if ((d >= '0' && d <= '9') || (d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z') || d == '_') ++j; else break; }
			const FString Word = Src.Mid(i, j - i);
			if (Word == TEXT("NotifyObjectMessage"))
			{
				int32 k = j;
				while (k < N && (Src[k] == ' ' || Src[k] == '\t')) ++k;
				if (k < N && Src[k] == '(')
				{
					TArray<FString> Args; int32 After = 0;
					if (SplitArgs(Src, k, Args, After) && Args.Num() >= 2)
					{
						FString Key;
						if (StringLiteralValue(Args[1], Key))
						{
							FString Rep = TEXT("Notify_");
							SanitizeInto(Rep, Key);
							Rep.AppendChar('(');
							Rep += Args[0].TrimStartAndEnd();
							for (int32 a = 2; a < Args.Num(); ++a) { Rep += TEXT(", "); Rep += Args[a].TrimStartAndEnd(); }
							Rep.AppendChar(')');
							Out += Rep; i = After; continue;
						}
					}
				}
			}
			Out += Word; i = j; continue;
		}
		Out.AppendChar(c); ++i;
	}
	return Out;
}
}  // namespace GMPLuaRewrite
