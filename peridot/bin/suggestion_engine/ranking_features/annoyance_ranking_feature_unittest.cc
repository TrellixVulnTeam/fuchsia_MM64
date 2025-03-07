// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/annoyance_ranking_feature.h"

#include "gtest/gtest.h"

namespace modular {
namespace {

class AnnoyanceRankingFeatureTest : public ::testing::Test {
 protected:
  SuggestionPrototype GetSuggestionPrototype(
      fuchsia::modular::AnnoyanceType annoyance) {
    fuchsia::modular::SuggestionDisplay display;
    display.annoyance = annoyance;
    fuchsia::modular::Proposal proposal;
    proposal.display = std::move(display);
    SuggestionPrototype prototype;
    prototype.proposal = std::move(proposal);
    return prototype;
  }
  AnnoyanceRankingFeature annoyance_ranking_feature;
  fuchsia::modular::UserInput query;
};

TEST_F(AnnoyanceRankingFeatureTest, ComputeFeatureAnnoyance) {
  auto prototype =
      GetSuggestionPrototype(fuchsia::modular::AnnoyanceType::INTERRUPT);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;
  double value = annoyance_ranking_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, kMaxConfidence);
}

TEST_F(AnnoyanceRankingFeatureTest, ComputeFeatureNonAnnoyance) {
  auto prototype =
      GetSuggestionPrototype(fuchsia::modular::AnnoyanceType::NONE);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;
  double value = annoyance_ranking_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, kMinConfidence);
}

}  // namespace
}  // namespace modular
