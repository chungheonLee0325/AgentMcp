#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "AgentMcpTestbedGameMode.generated.h"

/** Shows the fixture HUD (WBP_AgentMcpBound) when play begins, so smoke tests can check a UI in a play session. */
UCLASS()
class AGENTMCPTESTBED_API AAgentMcpTestbedGameMode : public AGameModeBase
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	void ShowHud();
};
