//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPGenericInvokerCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Application/SlateApplication.h"
#include "IPropertyUtilities.h"
#include "K2Node_GMPGenericInvoker.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GMPGenericInvokerCustomization"

TSharedRef<IDetailCustomization> FGMPGenericInvokerCustomization::MakeInstance()
{
	return MakeShared<FGMPGenericInvokerCustomization>();
}

void FGMPGenericInvokerCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	PropertyUtilities = DetailLayout.GetPropertyUtilities();

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailLayout.GetObjectsBeingCustomized(Objects);
	for (const TWeakObjectPtr<UObject>& Obj : Objects)
	{
		if (auto* Node = Cast<UK2Node_GMPGenericInvoker>(Obj.Get()))
		{
			NodePtr = Node;
			break;
		}
	}
	UK2Node_GMPGenericInvoker* Node = NodePtr.Get();
	if (!Node)
		return;

	Node->ResolveChain();

	IDetailCategoryBuilder& Category = DetailLayout.EditCategory("Generic Invoker", LOCTEXT("CategoryName", "Generic Invoker"));

	// Member chain rows (always shown; empty chain => Target is the endpoint).
	const int32 NumRows = FMath::Max(Node->MemberChain.Num(), 1);
	for (int32 Level = 0; Level < NumRows; ++Level)
		BuildChainRow(Category, Level);

	BuildEndpointRows(Category);
}

void FGMPGenericInvokerCustomization::BuildChainRow(IDetailCategoryBuilder& Category, int32 Level)
{
	const FText RowLabel = FText::Format(LOCTEXT("LevelLabel", "Member {0}"), FText::AsNumber(Level));
	Category.AddCustomRow(RowLabel)
		.NameContent()
		[
			SNew(STextBlock).Text(RowLabel).Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(220.0f)
		[
			SNew(SComboButton)
			.OnGetMenuContent(this, &FGMPGenericInvokerCustomization::MakeChainMenu, Level)
			.ContentPadding(2.0f)
			.ButtonContent()
			[
				SNew(STextBlock).Text(this, &FGMPGenericInvokerCustomization::GetChainText, Level).Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
}

void FGMPGenericInvokerCustomization::BuildEndpointRows(IDetailCategoryBuilder& Category)
{
	UK2Node_GMPGenericInvoker* Node = NodePtr.Get();
	if (!Node)
		return;

	// Endpoint mode toggle (two radio-style checkboxes).
	Category.AddCustomRow(LOCTEXT("EndpointLabel", "Endpoint"))
		.NameContent()
		[
			SNew(STextBlock).Text(LOCTEXT("EndpointLabel", "Endpoint")).Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(240.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "RadioButton")
				.IsChecked_Lambda([this]() {
					UK2Node_GMPGenericInvoker* N = NodePtr.Get();
					return (N && N->EndpointMode == EGMPInvokeEndpoint::GetMember) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) {
					if (UK2Node_GMPGenericInvoker* N = NodePtr.Get())
					{
						const FScopedTransaction T(LOCTEXT("ModeGet", "Endpoint: Get Member"));
						N->SetEndpointMode(EGMPInvokeEndpoint::GetMember);
						if (PropertyUtilities.IsValid()) PropertyUtilities->RequestForceRefresh();
					}
				})
				[ SNew(STextBlock).Text(LOCTEXT("GetMember", "Get Member")) ]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "RadioButton")
				.IsChecked_Lambda([this]() {
					UK2Node_GMPGenericInvoker* N = NodePtr.Get();
					return (N && N->EndpointMode == EGMPInvokeEndpoint::CallFunction) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState) {
					if (UK2Node_GMPGenericInvoker* N = NodePtr.Get())
					{
						const FScopedTransaction T(LOCTEXT("ModeCall", "Endpoint: Call Function"));
						N->SetEndpointMode(EGMPInvokeEndpoint::CallFunction);
						if (PropertyUtilities.IsValid()) PropertyUtilities->RequestForceRefresh();
					}
				})
				[ SNew(STextBlock).Text(LOCTEXT("CallFunction", "Call Function")) ]
			]
		];

	// Function dropdown only in CallFunction mode.
	if (Node->EndpointMode == EGMPInvokeEndpoint::CallFunction)
	{
		Category.AddCustomRow(LOCTEXT("FunctionLabel", "Function"))
			.NameContent()
			[
				SNew(STextBlock).Text(LOCTEXT("FunctionLabel", "Function")).Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			.MinDesiredWidth(240.0f)
			[
				SNew(SComboButton)
				.OnGetMenuContent(this, &FGMPGenericInvokerCustomization::MakeFunctionMenu)
				.ContentPadding(2.0f)
				.ButtonContent()
				[
					SNew(STextBlock).Text(this, &FGMPGenericInvokerCustomization::GetFunctionText).Font(IDetailLayoutBuilder::GetDetailFont())
				]
			];
	}
}

FText FGMPGenericInvokerCustomization::GetChainText(int32 Level) const
{
	UK2Node_GMPGenericInvoker* Node = NodePtr.Get();
	if (!Node || !Node->MemberChain.IsValidIndex(Level))
		return LOCTEXT("SelectMember", "(select member)");
	const FGMPMemberChainLink& Link = Node->MemberChain[Level];
	const FName Name = Link.MemberRef.GetMemberName();
	if (Name.IsNone())
		return LOCTEXT("SelectMember", "(select member)");
	if (Link.bResolveFailed)
		return FText::Format(LOCTEXT("MissingMember", "{0} (missing)"), FText::FromName(Name));
	return FText::FromName(Name);
}

FText FGMPGenericInvokerCustomization::GetFunctionText() const
{
	UK2Node_GMPGenericInvoker* Node = NodePtr.Get();
	if (!Node)
		return FText::GetEmpty();
	const FName Name = Node->FunctionRef.GetMemberName();
	if (Name.IsNone())
		return LOCTEXT("SelectFunc", "(select function)");
	if (Node->bResolveFailed)
		return FText::Format(LOCTEXT("MissingFunc", "{0} (missing)"), FText::FromName(Name));
	return FText::FromName(Name);
}

TSharedRef<SWidget> FGMPGenericInvokerCustomization::MakeChainMenu(int32 Level)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	UK2Node_GMPGenericInvoker* Node = NodePtr.Get();
	UStruct* Owner = Node ? Node->GetOwnerStructForLevel(Level) : nullptr;
	if (!Owner)
	{
		Box->AddSlot().AutoHeight().Padding(8.0f, 4.0f)
			[ SNew(STextBlock).Text(LOCTEXT("NoMembers", "No members (connect Target?)")) ];
		return Box;
	}

	struct FOpt { FName Name; FText Display; };
	TArray<FOpt> Options;
	for (TFieldIterator<FProperty> It(Owner); It; ++It)
	{
		FProperty* Prop = *It;
		if (!GMPMemberChainUtils::IsExposedToBlueprint(Prop))
			continue;
		const bool bDescend = (GMPMemberChainUtils::ClassifyKind(Prop) != EGMPChainNodeKind::Leaf);
		FOpt Opt;
		Opt.Name = Prop->GetFName();
		Opt.Display = FText::Format(LOCTEXT("OptFmt", "{0}{1}"), FText::FromName(Prop->GetFName()), bDescend ? LOCTEXT("Desc", "  >") : FText::GetEmpty());
		Options.Add(Opt);
	}
	Options.Sort([](const FOpt& A, const FOpt& B) { return A.Name.LexicalLess(B.Name); });

	if (Options.Num() == 0)
	{
		Box->AddSlot().AutoHeight().Padding(8.0f, 4.0f)
			[ SNew(STextBlock).Text(LOCTEXT("NoExposed", "No blueprint-visible members")) ];
		return Box;
	}

	for (const FOpt& Opt : Options)
	{
		const FName MemberName = Opt.Name;
		Box->AddSlot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.HAlign(HAlign_Left)
				.ContentPadding(FMargin(8.0f, 2.0f))
				.OnClicked_Lambda([this, Level, MemberName]() {
					UK2Node_GMPGenericInvoker* N = NodePtr.Get();
					if (N)
					{
						UStruct* Owner = N->GetOwnerStructForLevel(Level);
						FProperty* Picked = Owner ? FindFProperty<FProperty>(Owner, MemberName) : nullptr;
						if (Picked)
						{
							const FScopedTransaction T(LOCTEXT("PickMember", "Pick Member"));
							N->SetMemberAtLevel(Level, Picked);
						}
					}
					FSlateApplication::Get().DismissAllMenus();
					if (PropertyUtilities.IsValid()) PropertyUtilities->RequestForceRefresh();
					return FReply::Handled();
				})
				[ SNew(STextBlock).Text(Opt.Display) ]
			];
	}

	return SNew(SBox).MaxDesiredHeight(400.0f)[ Box ];
}

TSharedRef<SWidget> FGMPGenericInvokerCustomization::MakeFunctionMenu()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	UK2Node_GMPGenericInvoker* Node = NodePtr.Get();
	UClass* EndClass = Node ? Node->GetEndpointClass() : nullptr;
	if (!EndClass)
	{
		Box->AddSlot().AutoHeight().Padding(8.0f, 4.0f)
			[ SNew(STextBlock).Text(LOCTEXT("NoEndpoint", "Connect Target (endpoint class unknown)")) ];
		return Box;
	}

	TArray<UFunction*> Funcs;
	for (TFieldIterator<UFunction> It(EndClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		UFunction* Fn = *It;
		if (Fn->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure) && !Fn->HasMetaData(TEXT("DeprecatedFunction")))
			Funcs.Add(Fn);
	}
	Funcs.Sort([](const UFunction& A, const UFunction& B) { return A.GetFName().LexicalLess(B.GetFName()); });

	if (Funcs.Num() == 0)
	{
		Box->AddSlot().AutoHeight().Padding(8.0f, 4.0f)
			[ SNew(STextBlock).Text(LOCTEXT("NoFuncs", "No callable functions")) ];
		return Box;
	}

	for (UFunction* Fn : Funcs)
	{
		TWeakObjectPtr<UFunction> WeakFn(Fn);
		Box->AddSlot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.HAlign(HAlign_Left)
				.ContentPadding(FMargin(8.0f, 2.0f))
				.OnClicked_Lambda([this, WeakFn]() {
					UK2Node_GMPGenericInvoker* N = NodePtr.Get();
					UFunction* Picked = WeakFn.Get();
					if (N && Picked)
					{
						const FScopedTransaction T(LOCTEXT("PickFunc", "Pick Function"));
						N->SetFunction(Picked);
					}
					FSlateApplication::Get().DismissAllMenus();
					if (PropertyUtilities.IsValid()) PropertyUtilities->RequestForceRefresh();
					return FReply::Handled();
				})
				[ SNew(STextBlock).Text(FText::FromName(Fn->GetFName())) ]
			];
	}

	return SNew(SBox).MaxDesiredHeight(400.0f)[ Box ];
}

#undef LOCTEXT_NAMESPACE
