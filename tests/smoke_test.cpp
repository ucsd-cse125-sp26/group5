#include <gtest/gtest.h>

#include "shared/protocol.h"

TEST(SmokeMath, OnePlusOneEqualsTwo) {
    EXPECT_EQ(1 + 1, 2);
}

TEST(SmokeProtocol, InputPacketExists) {
    EXPECT_GT(sizeof(shared::InputPacket), 0u);
}

// TEMPORARY: verifies CI fails when a test fails. Revert before merging.
TEST(CiGate, IntentionalFailure) {
    FAIL() << "intentional failure to verify CI gating";
}
