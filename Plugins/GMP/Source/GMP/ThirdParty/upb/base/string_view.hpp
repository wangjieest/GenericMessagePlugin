// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google LLC nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef UPB_BASE_STRING_VIEW_HPP_
#define UPB_BASE_STRING_VIEW_HPP_

// Must be last.
#include "upb/base/string_view.h"
#include "upb/generated_code_macros.h"

#include <string>

#if defined(__UNREAL__)
#include "Containers/UnrealString.h"
#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "UObject/NameTypes.h"
#endif

namespace upb {
  class StringView {
  public:
    StringView(std::nullptr_t) : strview_{ nullptr, 0 } {}
    StringView(upb_StringView strview, upb_Arena* Arena = nullptr) : strview_(DumpOrRef(strview.data, strview.size, Arena)) {}
    StringView(const char* data, size_t size, upb_Arena* Arena = nullptr) : strview_(DumpOrRef(data, size, Arena)) {}
    StringView(const char* data = "", upb_Arena* Arena = nullptr) : strview_(DumpOrRef(data, strlen(data), Arena)) {}

    operator upb_StringView() const { return strview_; }

    bool operator==(const StringView& other) const { return Equal(other.strview_.data, other.strview_.size); }
    bool operator==(const std::string& other) const { return Equal(other.data(), other.size()); }
    bool operator==(const char* data) const { return data && Equal(data, strlen(data)); }
    
    const char * c_str() const { return strview_.data; }
    const char * data() const { return strview_.data; }
    size_t size() const { return strview_.size; }
    operator const char * () const { return c_str(); }
    operator size_t () const { return strview_.size; }

    StringView(const std::string& str, upb_Arena* Arena DEFAULT_ARENA_PARAMETER ) : StringView(DumpOrRef(str.data(), str.size(), GetAneraChecked(Arena))) {}
    upb_StringView Dump(upb_Arena* Arena DEFAULT_ARENA_PARAMETER ) const { return DumpOrRef(strview_.data, strview_.size, GetAneraChecked(Arena)); }

    bool StartsWith(const char* suffix) const
    {
        auto SuffixLen = strlen(suffix);
        return size() >= SuffixLen && strncmp(c_str(), suffix, SuffixLen) == 0;
    }
    bool EndsWith(const char* suffix) const
    {
        auto SuffixLen = strlen(suffix);
        return size() >= SuffixLen && strncmp(c_str() + size() - SuffixLen, suffix, SuffixLen) == 0;
    }

#if defined(__UNREAL__)
    friend uint32 GetTypeHash(const StringView& In) { return FCrc::StrCrc32(In.strview_.data, In.strview_.size); }

    StringView(const FAnsiStringView& str) : StringView(str.GetData(), str.Len()) {}
    bool operator==(const FAnsiStringView& str) const { return Equal(str.GetData(), str.Len()); }
    bool operator==(const FStringView& str) const { return Equal(str); }

    bool operator==(TConstArrayView<uint8> str) const { return Equal(str.GetData(), str.Num()); }
    bool operator==(TArrayView<uint8> str) const { return Equal(str.GetData(), str.Num()); }
    bool operator==(const TArray<const uint8>& str) const { return Equal(str.GetData(), str.Num()); }
    bool operator==(const TArray<uint8>& str) const { return Equal(str.GetData(), str.Num()); }
    StringView(const TConstArrayView<uint8>& str) : StringView((const char*)str.GetData(), str.Num()) {}
    TConstArrayView<uint8> ToArrayView() const { return MakeArrayView((const uint8*)c_str(), size()); }
    TArray<uint8> ToArray() const
    {
        TArray<uint8> Ret;
        Ret.AddUninitialized(size());
        FMemory::Memcpy(Ret.GetData(), c_str(), size());
        return Ret;
    }
    operator TArray<uint8>() const { return ToArray(); }


    StringView(const FStringView& str, upb_Arena* Arena DEFAULT_ARENA_PARAMETER ) : StringView(AllocStringView(str, GetAneraChecked(Arena))) {}
    StringView(const FString& str, upb_Arena* Arena DEFAULT_ARENA_PARAMETER ) : StringView(AllocStringView(str, GetAneraChecked(Arena))) {}

    FString ToFString() const { return  FString(size(), c_str());  }
    operator FString() const { return ToFString(); }
    bool operator==(const FString& str) const { return Equal(str); }

    StringView(const FName& name, upb_Arena* Arena) : StringView(AllocStringView(name.ToString(), Arena)) {}
    FName ToFName(EFindName Type = FNAME_Add) const { return FName(size(), c_str(), Type); }
    operator FName() const { return ToFName(); }
    bool operator==(const FName& name) const { return Equal(name.ToString()); }

    struct FStringViewData
    {
        FStringViewData(const StringView& InStrView) : StrData(InStrView.ToFString()) {}
        operator const TCHAR*() const { return *StrData; }
        const TCHAR* operator *() const { return *StrData; }
    protected:
        FString StrData;
    };
    FStringViewData ToFStringData() const { return FStringViewData(*this); }

    struct FStringViewRef
    {
        FStringViewRef(const FStringView& View)
        {
            auto Size = FTCHARToUTF8_Convert::ConvertedLength(View.GetData(), View.Len());
            Builder.AddUninitialized(Size);
            FTCHARToUTF8_Convert::Convert(Builder.GetData(), Size, View.GetData(), View.Len());
        }

        operator StringView() const { return StringView(Builder.GetData(), Builder.Num()); }
    protected:
        TArray<char, TInlineAllocator<1024>> Builder;
    };

    static FStringViewRef Ref(const FStringView& str) { return FStringViewRef(str); }
    static FStringViewRef Ref(FName name) { return FStringViewRef(name.ToString()); }

  protected:
    bool Equal(const void* Buf, size_t Len) const
    {
        return strview_.size == Len && strncmp(strview_.data, (const char*)Buf, strview_.size) == 0;
    }
    bool Equal(const FStringView& View) const
    {
        TStringBuilderWithBuffer<char, 1024> Builder;
        auto Size = FTCHARToUTF8_Convert::ConvertedLength(View.GetData(), View.Len());
        Builder.AddUninitialized(Size + 1);
        Builder.GetData()[Size] = '\0';
        FTCHARToUTF8_Convert::Convert(Builder.GetData(), Size, View.GetData(), View.Len());
        return operator==(StringView(Builder.GetData(), Size));
    }
    static StringView AllocStringView(const FStringView& View, upb_Arena* Arena)
    {
        check(Arena);
        auto Size = FTCHARToUTF8_Convert::ConvertedLength(View.GetData(), View.Len());
        auto Buf = (char*)upb_Arena_Malloc(Arena, Size + 1);
        Buf[Size] = 0;
        FTCHARToUTF8_Convert::Convert(Buf, Size, View.GetData(), View.Len());
        return StringView(Buf, Size);
    }
#endif
    static upb_Arena* GetAneraChecked(upb_Arena * Arena) { UPB_VALID_ARENA(Arena); return Arena; }

  protected:
    upb_StringView strview_;
    static upb_StringView DumpOrRef(const char* val, size_t size, upb_Arena* Arena)
    {
        upb_StringView ret;
        if (Arena)
        {
            auto buf = (char *)upb_Arena_Malloc(Arena, size + 1);
            memcpy(buf, val, size);
            buf[size] = 0;
            ret = upb_StringView_FromDataAndSize(buf, size);
        }
        else
        {
            ret = upb_StringView_FromDataAndSize(val, size);
        }
        return ret;
    }
  private:
    void* operator new(size_t) = delete;
    void operator delete(void*) = delete;
    void* operator new[](size_t) = delete;
    void operator delete[](void*) = delete;
  };
}  // namespace upb

static_assert(sizeof(upb_StringView) == sizeof(upb::StringView), "upb::StringView must be the same size as upb_StringView");
#endif /* UPB_BASE_STRING_VIEW_HPP_ */
