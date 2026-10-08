# 도구 만들기

[English](WritingTools.md)

에디터 모듈의 의존성에 `AgentMcpToolset`을 추가하고, `UAgentMcpToolset` 하위 클래스에 static 함수를 선언합니다.

```cpp
#include "AgentMcpToolset.h"

#include "MyTools.generated.h"

USTRUCT(BlueprintType)
struct FMyGreeting
{
    GENERATED_BODY()

    UPROPERTY()
    FString Message;
};

UCLASS(meta = (McpToolset = "my"))
class UMyTools : public UAgentMcpToolset
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "My Tools", meta = (AICallable, McpAccess = "Read", BlueprintInternalUseOnly = "true"))
    static FMyGreeting Greet(const FString& Name = TEXT("world"));
};
```

이 함수는 선택 문자열 인자 `name`을 받는 `my_greet` 도구가 됩니다.

- 툴셋 클래스는 자동으로 찾습니다. 도구 이름은 `<McpToolset>_<snake_case 함수 이름>`이고, 주석이 설명이 됩니다.
- `McpAccess`는 `Read`, `Write`, `Destructive`, `Control` 중 하나입니다. 지정하지 않으면 `Write`로 취급합니다.
- `BlueprintCallable`을 빼지 마세요. Unreal Engine 5.5는 블루프린트에서 호출 가능한 함수에만 C++ 기본 인자 값을 기록합니다.
- `USTRUCT(BlueprintType)`를 반환하세요. 필드가 JSON 결과가 됩니다.
- 실패는 `UE::AgentMcp::RaiseToolError(TEXT("CODE"), TEXT("Message"), TEXT("Hint"))`로 알립니다.
- 객체 인자(`UObject*`, `AActor*`, `UClass*`)는 오브젝트 경로를 받고, 액터는 레이블도 받습니다.
- 다음 프레임 이후에 끝나는 작업은 `UAgentMcpAsyncResult::Create(TimeoutSeconds, PollFunction)`를 반환합니다. Read와 Control 도구만 가능합니다.
- 이미지를 반환하려면 결과 구조체에 `FAgentMcpImage` 필드를 추가합니다.
