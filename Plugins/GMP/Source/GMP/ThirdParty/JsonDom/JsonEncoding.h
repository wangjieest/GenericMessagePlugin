//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Encoding policy for JsonDom. The public headers stay free of rapidjson; this file only selects
// UTF-16 (UE native, TCHAR=wchar_t) vs UTF-8 (standalone, TCHAR=char). JsonDom.inl is the sole
// place that maps this choice onto a concrete rapidjson encoding.
//   - UE build:         default UTF-16 (JSONDOM_ENCODING_UTF8 stays 0)
//   - standalone build: predefine JSONDOM_ENCODING_UTF8=1 in the Compat layer
#pragma once

// Encoding selection: UTF-16 (default) vs UTF-8. Predefine before including any JsonDom header.
#ifndef JSONDOM_ENCODING_UTF8
#define JSONDOM_ENCODING_UTF8 0
#endif

// Namespace configuration: external code can drop JsonDom into its own namespace / short alias by
// predefining these before including any JsonDom header. Defaults keep `jsondom` + `sj`.
#ifndef JSONDOM_NAMESPACE
#define JSONDOM_NAMESPACE jsondom
#endif
#ifndef JSONDOM_ALIAS
#define JSONDOM_ALIAS sj
#endif

// Implementation mode. Default: inline header-only — JsonSerializer.h auto-includes JsonDom.inl and
// its functions are inline, so any TU including the header gets the parse impl (rapidjson comes with
// it). Predefine JSONDOM_ISOLATED_IMPL to keep rapidjson out of the public headers: the impl is
// non-inline and a single host TU must #include "JsonDom/JsonDom.inl" exactly once.
#ifdef JSONDOM_ISOLATED_IMPL
#define JSONDOM_IMPL_INLINE
#else
#define JSONDOM_IMPL_INLINE inline
#endif
