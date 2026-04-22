#include <gtest/gtest.h>
#include "shelterops/infrastructure/CancellationToken.h"

using namespace shelterops::infrastructure;

TEST(CancellationToken, NotCancelledInitially) {
    CancellationSource src;
    auto tok = src.Token();
    EXPECT_FALSE(tok.IsCancelled());
}

TEST(CancellationToken, CancelledAfterCancel) {
    CancellationSource src;
    auto tok = src.Token();
    src.Cancel();
    EXPECT_TRUE(tok.IsCancelled());
}

TEST(CancellationToken, ThrowIfCancelledThrows) {
    CancellationSource src;
    auto tok = src.Token();
    src.Cancel();
    EXPECT_THROW(tok.ThrowIfCancelled(), OperationCancelledError);
}

TEST(CancellationToken, ThrowIfCancelledNoThrowWhenActive) {
    CancellationSource src;
    auto tok = src.Token();
    EXPECT_NO_THROW(tok.ThrowIfCancelled());
}

TEST(CancellationToken, MultipleTokensShareState) {
    CancellationSource src;
    auto tok1 = src.Token();
    auto tok2 = src.Token();
    src.Cancel();
    EXPECT_TRUE(tok1.IsCancelled());
    EXPECT_TRUE(tok2.IsCancelled());
}

TEST(CancellationSource, SourceIsCancelledAfterCancel) {
    CancellationSource src;
    EXPECT_FALSE(src.IsCancelled());
    src.Cancel();
    EXPECT_TRUE(src.IsCancelled());
}
