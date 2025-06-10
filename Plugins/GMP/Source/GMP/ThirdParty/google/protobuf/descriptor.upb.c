#include <stddef.h>
#include "upb/generated_code_support.h"
#include "google/protobuf/descriptor.upb.h"
#include "upb/port/def.inc"
static upb_Arena* upb_BootstrapArena() {
  static upb_Arena* arena = NULL;
  if (!arena) arena = upb_Arena_New();
  return arena;
}

const upb_MiniTable* UPB_DESC(_FileDescriptorSet_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 1), UPB_DESC(_FileDescriptorProto_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_FileDescriptorProto_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$11EGGGG33<<1a4";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 4), UPB_DESC(_DescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 5), UPB_DESC(_EnumDescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 6), UPB_DESC(_ServiceDescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 7), UPB_DESC(_FieldDescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 8), UPB_DESC(_FileOptions_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 9), UPB_DESC(_SourceCodeInfo_msg_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 14), UPB_DESC(Edition_enum_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_DescriptorProto_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$1GGGGG3GGE";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 2), UPB_DESC(_FieldDescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 6), UPB_DESC(_FieldDescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 3), UPB_DESC(_DescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 4), UPB_DESC(_EnumDescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 5), UPB_DESC(_DescriptorProto__ExtensionRange_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 8), UPB_DESC(_OneofDescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 7), UPB_DESC(_MessageOptions_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 9), UPB_DESC(_DescriptorProto__ReservedRange_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_DescriptorProto__ExtensionRange_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$((3";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 3), UPB_DESC(_ExtensionRangeOptions_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_DescriptorProto__ReservedRange_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$((";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_ExtensionRangeOptions_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$PaG4n`3t|G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 999), UPB_DESC(_UninterpretedOption_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 2), UPB_DESC(_ExtensionRangeOptions__Declaration_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 50), UPB_DESC(_FeatureSet_msg_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 3), UPB_DESC(ExtensionRangeOptions_VerificationState_enum_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_ExtensionRangeOptions__Declaration_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$(11a//";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_FieldDescriptorProto_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$11(44113(1f/";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 4), UPB_DESC(FieldDescriptorProto_Label_enum_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 5), UPB_DESC(FieldDescriptorProto_Type_enum_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 8), UPB_DESC(_FieldOptions_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_OneofDescriptorProto_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$13";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 2), UPB_DESC(_OneofOptions_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_EnumDescriptorProto_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$1G3GE";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 2), UPB_DESC(_EnumValueDescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 3), UPB_DESC(_EnumOptions_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 4), UPB_DESC(_EnumDescriptorProto__EnumReservedRange_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_EnumDescriptorProto__EnumReservedRange_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$((";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_EnumValueDescriptorProto_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$1(3";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 3), UPB_DESC(_EnumValueOptions_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_ServiceDescriptorProto_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$1G3";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 2), UPB_DESC(_MethodDescriptorProto_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 3), UPB_DESC(_ServiceOptions_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_MethodDescriptorProto_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$1113//";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 4), UPB_DESC(_MethodOptions_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_FileOptions_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$P1f14/1d///a/b/c/c/d11a111/a11d3t|G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 9), UPB_DESC(FileOptions_OptimizeMode_enum_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 50), UPB_DESC(_FeatureSet_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 999), UPB_DESC(_UninterpretedOption_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_MessageOptions_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$P///c/c/3z}G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 12), UPB_DESC(_FeatureSet_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 999), UPB_DESC(_UninterpretedOption_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_FieldOptions_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$P4//a/4c/d//4aHG3q}G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 1), UPB_DESC(FieldOptions_CType_enum_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 6), UPB_DESC(FieldOptions_JSType_enum_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 17), UPB_DESC(FieldOptions_OptionRetention_enum_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 19), UPB_DESC(FieldOptions_OptionTargetType_enum_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 20), UPB_DESC(_FieldOptions__EditionDefault_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 21), UPB_DESC(_FeatureSet_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 999), UPB_DESC(_UninterpretedOption_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_FieldOptions__EditionDefault_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$a14";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 3), UPB_DESC(Edition_enum_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_OneofOptions_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$P3e~G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 1), UPB_DESC(_FeatureSet_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 999), UPB_DESC(_UninterpretedOption_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_EnumOptions_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$Pa//b/3_~G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 7), UPB_DESC(_FeatureSet_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 999), UPB_DESC(_UninterpretedOption_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_EnumValueOptions_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$P/3/c~G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 2), UPB_DESC(_FeatureSet_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 999), UPB_DESC(_UninterpretedOption_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_ServiceOptions_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$P``/3d}G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 34), UPB_DESC(_FeatureSet_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 999), UPB_DESC(_UninterpretedOption_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_MethodOptions_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$P``/43c}G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 34), UPB_DESC(MethodOptions_IdempotencyLevel_enum_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 35), UPB_DESC(_FeatureSet_msg_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 999), UPB_DESC(_UninterpretedOption_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_UninterpretedOption_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$aG1,+ 01";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 2), UPB_DESC(_UninterpretedOption__NamePart_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_UninterpretedOption__NamePart_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$1N/N";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_FeatureSet_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$P444444";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 1), UPB_DESC(FeatureSet_FieldPresence_enum_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 2), UPB_DESC(FeatureSet_EnumType_enum_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 3), UPB_DESC(FeatureSet_RepeatedFieldEncoding_enum_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 4), UPB_DESC(FeatureSet_Utf8Validation_enum_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 5), UPB_DESC(FeatureSet_MessageEncoding_enum_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 6), UPB_DESC(FeatureSet_JsonFormat_enum_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_FeatureSetDefaults_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$Gb44";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 1), UPB_DESC(_FeatureSetDefaults__FeatureSetEditionDefault_msg_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 4), UPB_DESC(Edition_enum_init)());
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 5), UPB_DESC(Edition_enum_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_FeatureSetDefaults__FeatureSetEditionDefault_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$a34";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 3), UPB_DESC(Edition_enum_init)());
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 2), UPB_DESC(_FeatureSet_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_SourceCodeInfo_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 1), UPB_DESC(_SourceCodeInfo__Location_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_SourceCodeInfo__Location_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$<M<M11aE";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_GeneratedCodeInfo_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$G";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubMessage(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 1), UPB_DESC(_GeneratedCodeInfo__Annotation_msg_init)());
  return mini_table;
}

const upb_MiniTable* UPB_DESC(_GeneratedCodeInfo__Annotation_msg_init)() {
  static upb_MiniTable* mini_table = NULL;
  static const char* mini_descriptor = "$<M1((4";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTable_Build(mini_descriptor, strlen(mini_descriptor),
                          upb_BootstrapArena(), NULL);
  upb_MiniTable_SetSubEnum(mini_table, (upb_MiniTableField*)upb_MiniTable_FindFieldByNumber(mini_table, 5), UPB_DESC(GeneratedCodeInfo_Annotation_Semantic_enum_init)());
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(Edition_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)`~)qt_b)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(ExtensionRangeOptions_VerificationState_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!$";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FeatureSet_EnumType_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FeatureSet_FieldPresence_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!1";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FeatureSet_JsonFormat_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FeatureSet_MessageEncoding_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FeatureSet_RepeatedFieldEncoding_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FeatureSet_Utf8Validation_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FieldDescriptorProto_Label_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!0";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FieldDescriptorProto_Type_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!@AA1";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FieldOptions_CType_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FieldOptions_JSType_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FieldOptions_OptionRetention_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FieldOptions_OptionTargetType_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!AA";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(FileOptions_OptimizeMode_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!0";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(GeneratedCodeInfo_Annotation_Semantic_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}

const upb_MiniTableEnum* UPB_DESC(MethodOptions_IdempotencyLevel_enum_init)() {
  static const upb_MiniTableEnum* mini_table = NULL;
  static const char* mini_descriptor = "!)";
  if (mini_table) return mini_table;
  mini_table =
      upb_MiniTableEnum_Build(mini_descriptor, strlen(mini_descriptor),
                              upb_BootstrapArena(), NULL);
  return mini_table;
}
#include "upb/port/undef.inc"
