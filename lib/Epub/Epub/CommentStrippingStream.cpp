#include "CommentStrippingStream.h"

#include <Logging.h>

namespace {
constexpr char COMMENT_OPEN[] = "<!--";
constexpr size_t COMMENT_OPEN_LEN = sizeof(COMMENT_OPEN) - 1;
}  // namespace

void CommentStrippingStream::flushRun(const uint8_t* data) {
  if (runLen_ == 0) return;
  if (!failed_ && out_.write(data + runStart_, runLen_) != runLen_) {
    failed_ = true;
  }
  runLen_ = 0;
}

void CommentStrippingStream::emitLiteral(const char* p, const size_t n, const uint8_t* data) {
  flushRun(data);
  if (!failed_ && out_.write(reinterpret_cast<const uint8_t*>(p), n) != n) {
    failed_ = true;
  }
}

size_t CommentStrippingStream::write(const uint8_t* data, const size_t size) {
  runStart_ = 0;
  runLen_ = 0;

  for (size_t i = 0; i < size; i++) {
    const uint8_t c = data[i];

    if (inComment_) {
      // Closing delimiter is "-->"; extra dashes before the '>' still close it.
      if (c == '>' && closeMatch_ >= 2) {
        inComment_ = false;
        closeMatch_ = 0;
      } else if (c == '-') {
        if (closeMatch_ < 2) closeMatch_++;
      } else {
        closeMatch_ = 0;
      }
      continue;
    }

    if (c == static_cast<uint8_t>(COMMENT_OPEN[openMatch_])) {
      // Hold the byte back: it may turn out to open a comment.
      if (++openMatch_ == COMMENT_OPEN_LEN) {
        openMatch_ = 0;
        inComment_ = true;
        closeMatch_ = 0;
      }
      continue;
    }

    if (openMatch_ > 0) {
      // Not a comment after all. The held bytes are by construction a prefix of
      // "<!--", so they can be written from the literal rather than buffered.
      emitLiteral(COMMENT_OPEN, openMatch_, data);
      openMatch_ = 0;
      if (c == static_cast<uint8_t>(COMMENT_OPEN[0])) {
        openMatch_ = 1;
        continue;
      }
    }

    // Extend the current pass-through run, or start a new one after a gap.
    if (runLen_ > 0 && runStart_ + runLen_ == i) {
      runLen_++;
    } else {
      flushRun(data);
      runStart_ = i;
      runLen_ = 1;
    }
  }

  flushRun(data);
  // Always report full consumption: bytes are dropped by design, and the caller
  // (ZipFile::readFileToStream) treats a short write as an extraction failure.
  return size;
}

bool CommentStrippingStream::finish() {
  if (inComment_) {
    LOG_ERR("CSS", "Source ended inside a comment");
    return false;
  }
  if (openMatch_ > 0) {
    // Trailing "<", "<!" or "<!-" is literal content, not a comment.
    if (!failed_ && out_.write(reinterpret_cast<const uint8_t*>(COMMENT_OPEN), openMatch_) != openMatch_) {
      failed_ = true;
    }
    openMatch_ = 0;
  }
  return !failed_;
}
