#pragma once

#include <Print.h>

#include <cstddef>
#include <cstdint>

// Print filter that drops XML/HTML comment spans (<!-- ... -->) from a stream on
// its way to disk, used when a chapter's XHTML is inflated into the HTML cache.
//
// Why: expat holds a comment as one indivisible token, so it cannot report the
// comment -- and cannot advance its buffer -- until it has the closing "-->".
// XML_GetBuffer therefore grows its buffer to span the whole comment, doubling it
// and allocating the new block before freeing the old (xmlparse.c). A Word-exported
// chapter carrying five 34KB <!--[if gte mso 9]> blocks pushes that to a 64KB
// allocation on top of a live 32KB one, which fails mid-build on this device and
// stops the chapter dead. Nothing downstream wants the bytes: comments reach
// ChapterHtmlSlimParser::defaultHandlerExpand, which discards anything that is not
// an entity, and the parser has no <style> element handling, so no CSS hides in a
// comment either. Pagination is therefore identical with or without this filter --
// no section cache version bump is needed.
//
// Not a general HTML sanitiser: a literal "<!--" inside a CDATA section would be
// treated as a comment open. EPUB content does not do this, and an unescaped "<" in
// an attribute value is not well-formed XML in the first place.
class CommentStrippingStream final : public Print {
 public:
  explicit CommentStrippingStream(Print& out) : out_(out) {}

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t size) override;

  // Call once the source is exhausted: emits any held partial "<!--" prefix (a file
  // ending in "<!-" is not a comment). Returns false if the stream ends inside a
  // comment, which means the source was truncated or malformed.
  bool finish();

  // True if a write to the wrapped Print failed at any point. Always check this:
  // write() reports the full input size so the caller's "short write" check stays
  // meaningful, since dropping bytes is the point.
  bool failed() const { return failed_; }

 private:
  void flushRun(const uint8_t* data);
  void emitLiteral(const char* p, size_t n, const uint8_t* data);

  Print& out_;
  size_t runStart_ = 0;  // pass-through run within the current write() buffer
  size_t runLen_ = 0;
  uint8_t openMatch_ = 0;   // bytes of "<!--" matched but not yet emitted
  uint8_t closeMatch_ = 0;  // consecutive '-' seen while inside a comment (capped at 2)
  bool inComment_ = false;
  bool failed_ = false;
};
