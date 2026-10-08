#pragma once

#include "CoreMinimal.h"

class ISlateStyle;
struct FTextBlockStyle;

namespace UE::AgentMcp::Chat
{
	/**
	 * Converts the Markdown models write (headings, lists, quotes, tables, bold, italic, inline code, code blocks, links) to Slate rich
	 * text markup for FRichTextLayoutMarshaller. Anything it does not know stays as plain text.
	 */
	FString MarkdownToRichText(const FString& Markdown);

	/** Text styles the markup names: B, I, Code, H1, H2, H3, Quote. */
	const ISlateStyle& GetChatTextStyles();

	/** Body text style of chat messages. */
	const FTextBlockStyle& GetChatBodyTextStyle();
}
