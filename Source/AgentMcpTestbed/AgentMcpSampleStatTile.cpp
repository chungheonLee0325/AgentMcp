#include "AgentMcpSampleStatTile.h"

#include "Components/TextBlock.h"

void UAgentMcpSampleStatTile::SetValue(const FText& InValue)
{
	Value = InValue;
	if (ValueText)
	{
		ValueText->SetText(Value);
	}
}

void UAgentMcpSampleStatTile::NativePreConstruct()
{
	Super::NativePreConstruct();

	if (LabelText)
	{
		LabelText->SetText(Label);
	}
	if (ValueText)
	{
		ValueText->SetText(Value);
	}
}
