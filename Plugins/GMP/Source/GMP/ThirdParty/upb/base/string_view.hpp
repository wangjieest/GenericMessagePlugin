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
    StringView(upb_StringView strview) : strview_{ strview } {}
    StringView(const char* data = "") : strview_(upb_StringView_FromString(data)) {}
    StringView(const char* data, size_t size) : strview_(upb_StringView_FromDataAndSize(data, size)) {}
    StringView(const std::string& str) : StringView(str.data(), str.size()) {}
    operator upb_StringView() const { return strview_; }

    bool operator == (const StringView& other) const { return strview_.size == other.strview_.size && strncmp(strview_.data, strview_.data, strview_.size) == 0; }
    bool operator == (const std::string& other) const { return strview_.size == other.size() && strncmp(strview_.data, other.data(), strview_.size) == 0; }
    bool operator == (const char* data) const { return data && strview_.size == strlen(data) && strncmp(strview_.data, data, strview_.size) == 0; }
    
    const char * c_str() const { return strview_.data; }
    const char * data() const { return strview_.data; }
    size_t size() const { return strview_.size; }
    operator const char * () const { return c_str(); }
    operator size_t () const { return strview_.size; }
    
    upb_StringView Dump(upb_Arena* Arena) const
	{
		auto Buf = (char*)upb_Arena_Malloc(Arena, strview_.size + 1);
		memcpy(Buf, strview_.data, strview_.size);
        Buf[strview_.size] = 0;
		return upb_StringView(Buf, strview_.size);
	}

#if defined(__UNREAL__)
    StringView(const FAnsiStringView& str) : StringView(str.GetData(), str.Len()) {}
    StringView(const FStringView& str) : StringView(TCHAR_TO_UTF8(*str.GetData())) {}

    StringView(const FString& str) : StringView(TCHAR_TO_UTF8(*str)) {}
    FString ToFString() const { return  FString(c_str());  }
    operator FString() const { return ToFString(); }
    bool operator==(const FString& str) const { return operator==(TCHAR_TO_UTF8(*str)); }

    StringView(const FName& name) : StringView(TCHAR_TO_UTF8(*name.ToString())) {}
    FName ToFName(EFindName Type = FNAME_Add) const { return FName(c_str(), Type); }
    operator FName() const { return ToFName(); }
    bool operator==(const FName& name) const { return operator==(TCHAR_TO_UTF8(*name.ToString())); }

    struct StringViewData
    {
        StringViewData(const StringView& InStrView) : StrData(InStrView.ToFString()) {}
        operator const TCHAR*() const { return *StrData; }

    protected:
        FString StrData;
    };
    StringViewData ToFStringData() const { return StringViewData(*this); }
#endif

  protected:
    upb_StringView strview_;
  private:
    void* operator new(size_t);
    void operator delete(void*);
    void* operator new[](size_t);
    void operator delete[](void*);
  };
}

#endif /* UPB_BASE_STRING_VIEW_HPP_ */
