#include "AgentMcpChatMarkdown.h"

#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateTypes.h"

namespace UE::AgentMcp::Chat
{
	namespace MarkdownPrivate
	{
		FString Escape(const FString& Text)
		{
			FString Result = Text.Replace(TEXT("&"), TEXT("&amp;"));
			Result.ReplaceInline(TEXT("<"), TEXT("&lt;"));
			Result.ReplaceInline(TEXT(">"), TEXT("&gt;"));
			Result.ReplaceInline(TEXT("\""), TEXT("&quot;"));
			return Result;
		}

		FString Wrap(const TCHAR* Style, const FString& Text)
		{
			return Text.IsEmpty() ? FString() : FString::Printf(TEXT("<%s>%s</>"), Style, *Escape(Text));
		}

		/**
		 * Inline Markdown of one line: `code`, **bold**, *italic* and [text](url). Slate rich text cannot nest runs, so code inside
		 * bold ends the bold run. Underscores are left alone because asset and C++ names are full of them.
		 */
		FString ConvertInline(const FString& Line)
		{
			FString Result;
			FString Plain;
			auto FlushPlain = [&Result, &Plain]()
			{
				Result += Escape(Plain);
				Plain.Reset();
			};

			int32 Index = 0;
			while (Index < Line.Len())
			{
				const TCHAR Character = Line[Index];
				if (Character == TEXT('`'))
				{
					const int32 End = Line.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
					if (End != INDEX_NONE)
					{
						FlushPlain();
						Result += Wrap(TEXT("Code"), Line.Mid(Index + 1, End - Index - 1));
						Index = End + 1;
						continue;
					}
				}
				else if (Character == TEXT('*') && Index + 1 < Line.Len() && Line[Index + 1] == TEXT('*'))
				{
					const int32 End = Line.Find(TEXT("**"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 2);
					if (End != INDEX_NONE)
					{
						FlushPlain();
						// Strip code ticks inside bold; the run shows them as bold text.
						Result += Wrap(TEXT("B"), Line.Mid(Index + 2, End - Index - 2).Replace(TEXT("`"), TEXT("")));
						Index = End + 2;
						continue;
					}
				}
				else if (Character == TEXT('*') && Index + 1 < Line.Len() && !FChar::IsWhitespace(Line[Index + 1]))
				{
					const int32 End = Line.Find(TEXT("*"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
					if (End != INDEX_NONE && End > Index + 1)
					{
						FlushPlain();
						Result += Wrap(TEXT("I"), Line.Mid(Index + 1, End - Index - 1));
						Index = End + 1;
						continue;
					}
				}
				else if (Character == TEXT('['))
				{
					const int32 Close = Line.Find(TEXT("]("), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
					const int32 End = Close == INDEX_NONE ? INDEX_NONE : Line.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Close + 2);
					if (End != INDEX_NONE)
					{
						// The link text, then the address so it can be copied.
						Plain += Line.Mid(Index + 1, Close - Index - 1) + TEXT(" (") + Line.Mid(Close + 2, End - Close - 2) + TEXT(")");
						Index = End + 1;
						continue;
					}
				}
				Plain.AppendChar(Character);
				++Index;
			}
			FlushPlain();
			return Result;
		}

		bool IsTableSeparator(const FString& Line)
		{
			if (!Line.StartsWith(TEXT("|")))
			{
				return false;
			}
			for (const TCHAR Character : Line)
			{
				if (Character != TEXT('|') && Character != TEXT('-') && Character != TEXT(':') && Character != TEXT(' '))
				{
					return false;
				}
			}
			return true;
		}

		/** The indentation of a list item, so nested items keep their level. */
		FString Indent(const FString& RawLine)
		{
			int32 Spaces = 0;
			while (Spaces < RawLine.Len() && RawLine[Spaces] == TEXT(' '))
			{
				++Spaces;
			}
			return FString::ChrN(Spaces, TEXT(' '));
		}
	}

	FString MarkdownToRichText(const FString& Markdown)
	{
		using namespace MarkdownPrivate;

		TArray<FString> Lines;
		Markdown.Replace(TEXT("\r"), TEXT("")).ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);

		TArray<FString> Output;
		bool bInCode = false;
		for (const FString& RawLine : Lines)
		{
			const FString Line = RawLine.TrimStartAndEnd();

			if (Line.StartsWith(TEXT("```")))
			{
				bInCode = !bInCode;
				continue;
			}
			if (bInCode)
			{
				Output.Add(RawLine.IsEmpty() ? FString() : Wrap(TEXT("Code"), TEXT("    ") + RawLine));
				continue;
			}

			if (Line.IsEmpty())
			{
				Output.Add(FString());
			}
			else if (Line.StartsWith(TEXT("### ")))
			{
				Output.Add(Wrap(TEXT("H3"), Line.RightChop(4).Replace(TEXT("**"), TEXT("")).Replace(TEXT("`"), TEXT(""))));
			}
			else if (Line.StartsWith(TEXT("## ")))
			{
				Output.Add(Wrap(TEXT("H2"), Line.RightChop(3).Replace(TEXT("**"), TEXT("")).Replace(TEXT("`"), TEXT(""))));
			}
			else if (Line.StartsWith(TEXT("# ")))
			{
				Output.Add(Wrap(TEXT("H1"), Line.RightChop(2).Replace(TEXT("**"), TEXT("")).Replace(TEXT("`"), TEXT(""))));
			}
			else if (Line == TEXT("---") || Line == TEXT("***") || Line == TEXT("___"))
			{
				Output.Add(Wrap(TEXT("Quote"), FString::ChrN(40, TEXT('-'))));
			}
			else if (Line.StartsWith(TEXT("> ")))
			{
				Output.Add(Wrap(TEXT("Quote"), TEXT("| ") + Line.RightChop(2)));
			}
			else if (IsTableSeparator(Line))
			{
				continue;
			}
			else if (Line.StartsWith(TEXT("|")))
			{
				TArray<FString> Cells;
				Line.ParseIntoArray(Cells, TEXT("|"), /*InCullEmpty=*/false);
				TArray<FString> Converted;
				for (const FString& Cell : Cells)
				{
					if (!Cell.TrimStartAndEnd().IsEmpty() || Converted.Num() > 0)
					{
						Converted.Add(ConvertInline(Cell.TrimStartAndEnd()));
					}
				}
				while (Converted.Num() > 0 && Converted.Last().IsEmpty())
				{
					Converted.Pop();
				}
				Output.Add(FString::Join(Converted, TEXT("   |   ")));
			}
			else if (Line.StartsWith(TEXT("- ")) || Line.StartsWith(TEXT("* ")) || Line.StartsWith(TEXT("+ ")))
			{
				Output.Add(Indent(RawLine) + TEXT("  • ") + ConvertInline(Line.RightChop(2)));
			}
			else
			{
				// Numbered items keep their number; other lines are paragraphs.
				Output.Add(Indent(RawLine) + ConvertInline(Line));
			}
		}

		// Collapse runs of blank lines.
		TArray<FString> Compact;
		for (const FString& Line : Output)
		{
			if (Line.IsEmpty() && (Compact.Num() == 0 || Compact.Last().IsEmpty()))
			{
				continue;
			}
			Compact.Add(Line);
		}
		while (Compact.Num() > 0 && Compact.Last().IsEmpty())
		{
			Compact.Pop();
		}
		return FString::Join(Compact, TEXT("\n"));
	}

	namespace MarkdownPrivate
	{
		FTextBlockStyle MakeStyle(const FTextBlockStyle& Base, FSlateFontInfo Font, const FSlateColor& Color)
		{
			FTextBlockStyle Style = Base;
			Style.SetFont(Font);
			Style.SetColorAndOpacity(Color);
			return Style;
		}

		struct FChatStyles
		{
			FChatStyles()
				: Set(MakeShared<FSlateStyleSet>(TEXT("AgentMcpChatText")))
			{
				const FTextBlockStyle& Base = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
				const FSlateColor Foreground = FSlateColor::UseForeground();
				Body = MakeStyle(Base, FCoreStyle::GetDefaultFontStyle("Regular", 10), Foreground);
				Set->Set("B", MakeStyle(Base, FCoreStyle::GetDefaultFontStyle("Bold", 10), Foreground));
				Set->Set("I", MakeStyle(Base, FCoreStyle::GetDefaultFontStyle("Italic", 10), Foreground));
				Set->Set("Code", MakeStyle(Base, FCoreStyle::GetDefaultFontStyle("Mono", 9), FLinearColor(0.95f, 0.78f, 0.50f)));
				Set->Set("H1", MakeStyle(Base, FCoreStyle::GetDefaultFontStyle("Bold", 14), Foreground));
				Set->Set("H2", MakeStyle(Base, FCoreStyle::GetDefaultFontStyle("Bold", 12), Foreground));
				Set->Set("H3", MakeStyle(Base, FCoreStyle::GetDefaultFontStyle("Bold", 11), Foreground));
				Set->Set("Quote", MakeStyle(Base, FCoreStyle::GetDefaultFontStyle("Italic", 10), FSlateColor::UseSubduedForeground()));
			}

			TSharedRef<FSlateStyleSet> Set;
			FTextBlockStyle Body;
		};

		const FChatStyles& GetStyles()
		{
			static const FChatStyles Styles;
			return Styles;
		}
	}

	const ISlateStyle& GetChatTextStyles()
	{
		return *MarkdownPrivate::GetStyles().Set;
	}

	const FTextBlockStyle& GetChatBodyTextStyle()
	{
		return MarkdownPrivate::GetStyles().Body;
	}
}
