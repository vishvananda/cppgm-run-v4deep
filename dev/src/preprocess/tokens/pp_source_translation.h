// Translation phases 1 and 2 for preprocessing-token recognition.
//
// Phase 1 maps the physical source characters to the course source character
// set (UTF-8) and replaces trigraphs and universal-character-names.  Phase 2
// splices physical lines.  The result feeds preprocessing-token recognition in
// phase 3, which lives in pp_tokenizer.h.
//
// The pipeline is a cursor rather than a materialized stream: the tokenizer
// pulls translated code points as it needs them, so a translation unit costs
// its source buffer, a line index and a bounded lookahead window.  Raw string
// literals read the buffer directly, because [lex.pptoken]/3 reverts the phase
// 1 and 2 rewrites between their opening and closing quotes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace preprocess
{

// Raised for source text that the phases of translation reject.
class SourceError : public std::runtime_error
{
public:
	explicit SourceError(const std::string& message)
		: std::runtime_error(message)
	{}
};

// A physical position in the source buffer.  `line` and `column` count from 1;
// the column counts bytes from the start of the line.
struct SourceLocation
{
	std::size_t line;
	std::size_t column;
};

// Phase 1 and 2 over one immutable source buffer.
class TranslatedSource
{
public:
	// Takes ownership of the source bytes, removes a leading byte order mark
	// and appends the line feed the last source line must end with.
	explicit TranslatedSource(std::string bytes);

	// Code point `ahead` translated code points beyond the cursor, or -1 when
	// the stream is exhausted.
	int CodeAt(std::size_t ahead)
	{
		if (pending_.size() - front_ <= ahead)
			Refill(ahead);
		std::size_t index = front_ + ahead;
		return index < pending_.size() ? pending_[index].code_point : -1;
	}

	// Byte offset in the source buffer where that code point begins.  Past the
	// end this reports the buffer size.
	std::size_t ByteOffsetAt(std::size_t ahead) const
	{
		std::size_t index = front_ + ahead;
		return index < pending_.size() ? pending_[index].byte_offset : buffer_.size();
	}

	// Drops `count` code points from the front of the cursor.
	void Advance(std::size_t count)
	{
		front_ += count;
		if (front_ > pending_.size())
			front_ = pending_.size();
		if (front_ >= kCompactThreshold)
			Compact();
	}

	// Byte offset at the cursor, where a raw string literal resumes reading the
	// untranslated source.
	std::size_t RawByteOffset() const { return ByteOffsetAt(0); }

	// Decodes and validates the UTF-8 character at `byte_offset`.  Returns false
	// at the end of the buffer.
	bool DecodeAt(std::size_t byte_offset, int& code_point, std::size_t& next) const;

	std::size_t BufferSize() const { return buffer_.size(); }
	const std::string& Buffer() const { return buffer_; }

	// Continues the pipeline at `byte_offset`, which is where a raw string
	// literal ended.  The translation state there is the same as at any token
	// boundary, so no rewrite can span the resume point.
	void ResumeAt(std::size_t byte_offset);

	// Physical position of a byte offset.  Calls arrive in non-decreasing byte
	// order, so the line is found by walking the index forward.
	SourceLocation LocationOf(std::size_t byte_offset);

private:
	struct Entry
	{
		std::size_t byte_offset;
		int code_point;
	};

	static const std::size_t kCompactThreshold = 64;

	void Compact();
	void Refill(std::size_t wanted);
	void Produce();
	bool EffectiveAt(std::size_t at, int& code_point, std::size_t& next) const;
	bool ReadEscapeDigits(std::size_t at, std::size_t count, int& value, std::size_t& next) const;
	bool ReadUniversalCharacterName(std::size_t at, int& value, std::size_t& end) const;
	void Push(int code_point, std::size_t byte_offset);
	void DropLast();
	void BuildLineIndex();

	std::string buffer_;
	std::vector<Entry> pending_;
	std::vector<std::size_t> line_starts_;
	std::size_t front_;
	std::size_t next_byte_;
	std::size_t location_line_;
	bool exhausted_;
};

// Appends the UTF-8 encoding of one code point.
void AppendCodePointUtf8(int code_point, std::string& out);

} // namespace preprocess
} // namespace cppgm
