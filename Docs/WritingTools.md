# Writing tools

[한국어](WritingTools.ko.md)

Add `AgentMcpToolset` to the dependencies of an editor module, then declare static functions on a `UAgentMcpToolset` subclass:

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

This becomes the tool `my_greet` with an optional string argument `name`.

- Toolset classes are found automatically. Tool names are `<McpToolset>_<function name in snake_case>`, and comments provide descriptions.
- `McpAccess` is `Read`, `Write`, `Destructive` or `Control`; a missing value is treated as `Write`.
- Keep `BlueprintCallable`: Unreal Engine 5.5 records C++ default argument values only for Blueprint-callable functions.
- Return a `USTRUCT(BlueprintType)`; its fields become the JSON result.
- Report failures with `UE::AgentMcp::RaiseToolError(TEXT("CODE"), TEXT("Message"), TEXT("Hint"))`.
- Object arguments (`UObject*`, `AActor*`, `UClass*`) accept object paths, and actors also accept labels.
- For work that finishes on a later frame, return `UAgentMcpAsyncResult::Create(TimeoutSeconds, PollFunction)`; only Read and Control tools can do this.
- To return an image, add an `FAgentMcpImage` field to the result struct.
