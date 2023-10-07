//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "GMPArchive.h"
#include "GMPUnion.h"

namespace GMP
{
namespace Serializer
{
	extern GMP_API const FLazyName NAME_Text;
	extern GMP_API const FLazyName NAME_DateTime;
	extern GMP_API const FLazyName NAME_Guid;
	extern GMP_API const FLazyName NAME_Color;
	extern GMP_API const FLazyName NAME_LinearColor;
	extern GMP_API const FLazyName NAME_MemResVersion;
	extern GMP_API const TCHAR* Str_Ticks;
	extern GMP_API const TCHAR* Str_FutureNow;
	extern GMP_API const TCHAR* Str_Max;
	extern GMP_API const TCHAR* Str_Min;

	namespace Traits
	{
		template<typename Lambda, typename SrcType, typename ValType>
		struct TForEachPropOperation
		{
		protected:
			Lambda Op;
			TMap<uint64, TUniqueFunction<bool(SrcType&, FProperty*, ValType*)>> DispatchMap;

		public:
			TForEachPropOperation(Lambda InOp)
				: Op(InOp)
			{
#define INSERT_PROP_IMPL(TestType, ImplType) DispatchMap.Emplace(TestType::StaticClassCastFlags(), [this](SrcType& Src, FProperty* Prop, ValType* Value) { return Op(Src, CastFieldChecked<ImplType>(Prop), Value); });
#define INSERT_PROP(TYPE) INSERT_PROP_IMPL(TYPE, TYPE)
				INSERT_PROP(FStructProperty)
				INSERT_PROP(FArrayProperty)
				INSERT_PROP(FSetProperty)
				INSERT_PROP(FMapProperty)
				INSERT_PROP(FStrProperty)
				INSERT_PROP(FNameProperty)
				INSERT_PROP(FTextProperty)
				INSERT_PROP(FBoolProperty)
				INSERT_PROP(FEnumProperty)
				INSERT_PROP(FInt8Property)
				INSERT_PROP(FInt16Property)
				INSERT_PROP(FIntProperty)
				INSERT_PROP(FInt64Property)
				INSERT_PROP(FByteProperty)
				INSERT_PROP(FUInt16Property)
				INSERT_PROP(FUInt32Property)
				INSERT_PROP(FUInt64Property)
				INSERT_PROP(FFloatProperty)
				INSERT_PROP(FDoubleProperty)
				INSERT_PROP_IMPL(FSoftObjectProperty, FSoftObjectProperty)
				INSERT_PROP_IMPL(FSoftClassProperty, FSoftObjectProperty)
#undef INSERT_PROP_IMPL
#undef INSERT_PROP
			}

			bool operator()(SrcType& Src, FProperty* Property, ValType* Value) const
			{
				if (auto Find = DispatchMap.Find(Property->GetCastFlags()))
				{
					return (*Find)(Src, Property, Value);
				}
				else
				{
					return Op(Src, Property, Value);
				}
			}
		};
		template<typename Lambda, typename SrcType, typename ValType>
		auto ForeachProp(Lambda&& Op, SrcType& Src, FProperty* Property, ValType* Value)
		{
			static const TForEachPropOperation<Lambda, SrcType, ValType> ForEach{std::forward<Lambda>(Op)};
			return ForEach(Src, Property, Value);
		}
	}  // namespace Traits
}  // namespace Serializer
}  // namespace GMP
