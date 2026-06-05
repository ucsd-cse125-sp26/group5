#include <gtest/gtest.h>

#include "shared/puzzles/decrypt/defaults.h"

TEST(DecryptAnswer, NormalizesCaseAndSpacing) {
  EXPECT_EQ(shared::decrypt::normalizeAnswer("  SO FAR  TO GO  "),
            "so far to go");
}

TEST(DecryptAnswer, AcceptsCurlyApostrophe) {
  EXPECT_TRUE(shared::decrypt::answersMatch("so far to go, but so far you've gone"));
  EXPECT_TRUE(shared::decrypt::answersMatch(
      "So Far To Go, But So Far You\u2019ve Gone"));
}

TEST(DecryptAnswer, RejectsWrongPhrase) {
  EXPECT_FALSE(shared::decrypt::answersMatch("so far together"));
  EXPECT_FALSE(shared::decrypt::answersMatch(""));
}

TEST(DecryptAnswer, MatchesCanonicalAnswer) {
  EXPECT_TRUE(shared::decrypt::answersMatch(shared::decrypt::kExpectedAnswer));
}
