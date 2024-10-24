// Copyright 2022-2023 Ekumen, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <gtest/gtest-death-test.h>
#include <algorithm>
#include <array>
#include <ios>
#include <limits>
#include <numeric>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/repeat_n.hpp>
#include <range/v3/view/take_exactly.hpp>
#include <range/v3/view/transform.hpp>
#include <vector>

#include <Eigen/Core>
#include <beluga/random/multivariate_normal_distribution.hpp>
#include <sophus/average.hpp>
#include <sophus/common.hpp>
#include <sophus/se2.hpp>
#include <sophus/se3.hpp>
#include <sophus/so2.hpp>
#include <sophus/so3.hpp>

#include "beluga/algorithm/estimation.hpp"
#include "beluga/testing/sophus_matchers.hpp"
#include "beluga/views/sample.hpp"

namespace {

using beluga::testing::SE2Near;
using beluga::testing::Vector3Near;

using Constants = Sophus::Constants<double>;
using Eigen::Vector2d;
using Eigen::Vector3d;
using Sophus::SE2d;
using Sophus::SE3d;
using Sophus::SO2d;
using Sophus::SO3d;

struct CovarianceCalculation : public testing::Test {};

TEST_F(CovarianceCalculation, UniformWeightOverload) {
  // Covariance matrix calculated for items with uniform weights.
  // The following Octave code was used to validate the results:
  /*
      translations = [ 0 2 0 2 0 2 0 2 0 2; 0 2 0 2 0 0 2 2 2 0 ]';
      cov_matrix = cov(translations)
  */
  const auto translation_vector = std::vector{
      Vector2d{0, 0}, Vector2d{2, 2}, Vector2d{0, 0}, Vector2d{2, 2}, Vector2d{0, 0},
      Vector2d{2, 0}, Vector2d{0, 2}, Vector2d{2, 2}, Vector2d{0, 2}, Vector2d{2, 0},
  };
  const auto translation_mean = Vector2d{1, 1};
  const auto covariance = beluga::covariance(translation_vector, translation_mean);
  ASSERT_NEAR(covariance(0, 0), 1.1111, 0.001);
  ASSERT_NEAR(covariance(0, 1), 0.2222, 0.001);
  ASSERT_NEAR(covariance(1, 0), 0.2222, 0.001);
  ASSERT_NEAR(covariance(1, 1), 1.1111, 0.001);
}

TEST_F(CovarianceCalculation, NonUniformWeightOverload) {
  // Covariance matrix calculated with non-uniform weights.
  // The following Octave code was used to validate the results:
  /*
      translations = [ 0 2 0 2 0 2 0 2 0 2; 0 2 0 2 0 0 2 2 2 0 ]';
      weights = [0 1 2 1 0 1 2 1 0 1]';
      normalized_weight = weights ./ sum(weights);
      weighted_mean = sum(normalized_weight .* translations)
      deviations = translations - weighted_mean;
      weighted_cov_matrix =  (normalized_weight .* deviations)' * deviations ./ (1 - sum(normalized_weight.^2))
  */
  const auto translation_vector = std::vector{
      Vector2d{0, 0}, Vector2d{2, 2}, Vector2d{0, 0}, Vector2d{2, 2}, Vector2d{0, 0},
      Vector2d{2, 0}, Vector2d{0, 2}, Vector2d{2, 2}, Vector2d{0, 2}, Vector2d{2, 0},
  };
  auto weights = std::vector{0.0, 1.0, 2.0, 1.0, 0.0, 1.0, 2.0, 1.0, 0.0, 1.0};
  const auto total_weight = std::accumulate(weights.begin(), weights.end(), 0.0);
  std::for_each(weights.begin(), weights.end(), [total_weight](auto& weight) { weight /= total_weight; });
  const auto translation_mean = Vector2d{1.1111, 1.1111};
  const auto covariance = beluga::covariance(translation_vector, weights, translation_mean);
  ASSERT_NEAR(covariance(0, 0), 1.1765, 0.001);
  ASSERT_NEAR(covariance(0, 1), 0.1176, 0.001);
  ASSERT_NEAR(covariance(1, 0), 0.1176, 0.001);
  ASSERT_NEAR(covariance(1, 1), 1.1765, 0.001);
}

struct PoseCovarianceEstimation : public testing::Test {
  // The following Octave code can be used to validate the results in the tests that derive from this fixture:
  /*
     # inputs
     translations = [ x1 y1 rot1; x2 y2 rot2; x3 y3 yaw3; ... xn yn rotn ] ;
     weights = [w1 w2 w3 ... wn]; ]';

     # auxiliar variables
     xy_translation = translations(:, 1:2);
     complex_rotation = exp(i* translations(:, 3));
     normalized_weight = weights ./ sum(weights);
     complex_rotation_mean = sum(normalized_weight .* complex_rotation);
     R = abs(complex_rotation_mean);
     # mean estimations
     xy_mean = sum(normalized_weight .* xy_translation);
     rot_mean = imag(log(complex_rotation_mean / abs(complex_rotation_mean)));
     # covariance estimations
     xy_deviations = xy_translation - xy_mean;
     xy_cov_matrix =  (normalized_weight .* xy_deviations)' * xy_deviations ./ (1 - sum(normalized_weight.^2));
     rot_cov = -2 * log(R);
     # results
     means = [ xy_mean, rot_mean ]
     covariance_matrix = [xy_cov_matrix, [0; 0]; [0 0], rot_cov]
  */
};

TEST_F(PoseCovarianceEstimation, PureTranslation) {
  // test the mean and covariance estimations for states that have different translations but the same rotation
  const auto states = std::vector{SE2d{SO2d{0.0}, Vector2d{1.0, 2.0}}, SE2d{SO2d{0.0}, Vector2d{0.0, 0.0}}};
  const auto weights = std::vector(states.size(), 1.0);
  constexpr double kTolerance = 0.001;
  const auto [pose, covariance] = beluga::estimate(states, weights);
  ASSERT_THAT(pose, SE2Near(SO2d{0.0}, Vector2d{0.5, 1.0}, kTolerance));
  ASSERT_THAT(covariance.col(0).eval(), Vector3Near({0.5, 1.0, 0.0}, kTolerance));
  ASSERT_THAT(covariance.col(1).eval(), Vector3Near({1.0, 2.0, 0.0}, kTolerance));
  ASSERT_THAT(covariance.col(2).eval(), Vector3Near({0.0, 0.0, 0.0}, kTolerance));
}

TEST_F(PoseCovarianceEstimation, PureRotation) {
  // test the mean and covariance estimations for states that have different rotations but the same translation
  const auto states =
      std::vector{SE2d{SO2d{-Constants::pi() / 2}, Vector2d{0.0, 0.0}}, SE2d{SO2d{0.0}, Vector2d{0.0, 0.0}}};
  const auto weights = std::vector(states.size(), 1.0);
  constexpr double kTolerance = 0.001;
  const auto [pose, covariance] = beluga::estimate(states, weights);
  ASSERT_THAT(pose, SE2Near(SO2d{-Constants::pi() / 4}, Vector2d{0.0, 0.0}, kTolerance));
  ASSERT_THAT(covariance.col(0).eval(), Vector3Near({0.000, 0.000, 0.000}, kTolerance));
  ASSERT_THAT(covariance.col(1).eval(), Vector3Near({0.000, 0.000, 0.000}, kTolerance));
  ASSERT_THAT(covariance.col(2).eval(), Vector3Near({0.000, 0.000, 0.693}, kTolerance));
}

TEST_F(PoseCovarianceEstimation, JointTranslationAndRotation) {
  // test the mean and covariance estimations for states that have different translations and rotations
  const auto states = std::vector{
      SE2d{SO2d{Constants::pi() / 6}, Vector2d{0.0, -3.0}}, SE2d{SO2d{Constants::pi() / 2}, Vector2d{1.0, -2.0}},
      SE2d{SO2d{Constants::pi() / 3}, Vector2d{2.0, -1.0}}, SE2d{SO2d{0.0}, Vector2d{3.0, 0.0}}};
  const auto weights = std::vector(states.size(), 1.0);
  constexpr double kTolerance = 0.001;
  const auto [pose, covariance] = beluga::estimate(states, weights);
  ASSERT_THAT(pose, SE2Near(SO2d{Constants::pi() / 4}, Vector2d{1.5, -1.5}, kTolerance));
  ASSERT_THAT(covariance.col(0).eval(), Vector3Near({1.666, 1.666, 0.000}, kTolerance));
  ASSERT_THAT(covariance.col(1).eval(), Vector3Near({1.666, 1.666, 0.000}, kTolerance));
  ASSERT_THAT(covariance.col(2).eval(), Vector3Near({0.000, 0.000, 0.357}, kTolerance));
}

TEST_F(PoseCovarianceEstimation, CancellingOrientations) {
  // test mean and covariance for two states with opposite angles that cause a singularity in angular covariance
  // estimation
  const auto states = std::vector{
      SE2d{SO2d{Constants::pi() / 2}, Vector2d{0.0, 0.0}}, SE2d{SO2d{-Constants::pi() / 2}, Vector2d{0.0, 0.0}}};
  const auto weights = std::vector(states.size(), 1.0);
  constexpr double kTolerance = 0.001;
  constexpr double kInf = std::numeric_limits<double>::infinity();
  const auto [pose, covariance] = beluga::estimate(states, weights);
  ASSERT_THAT(pose, SE2Near(SO2d{0.0}, Vector2d{0.0, 0.0}, kTolerance));
  ASSERT_THAT(covariance.col(0).eval(), Vector3Near({0.0, 0.0, 0.0}, kTolerance));
  ASSERT_THAT(covariance.col(1).eval(), Vector3Near({0.0, 0.0, 0.0}, kTolerance));
  ASSERT_THAT(covariance.col(2).eval(), Vector3Near({0.0, 0.0, kInf}, kTolerance));
  ASSERT_EQ(covariance(2, 2), std::numeric_limits<double>::infinity());
}

TEST_F(PoseCovarianceEstimation, RandomWalkWithSmoothRotationWithUniformWeights) {
  // test the mean and covariance estimations for states with random variations of translation and rotation
  const auto states = std::vector{
      SE2d{SO2d{Constants::pi() * 0.1}, Vector2d{0.0, -2.0}},  //
      SE2d{SO2d{Constants::pi() * 0.2}, Vector2d{1.0, -1.0}},  //
      SE2d{SO2d{Constants::pi() * 0.3}, Vector2d{2.0, 1.0}},   //
      SE2d{SO2d{Constants::pi() * 0.2}, Vector2d{3.0, 2.0}},   //
      SE2d{SO2d{Constants::pi() * 0.2}, Vector2d{2.0, 1.0}},   //
      SE2d{SO2d{Constants::pi() * 0.2}, Vector2d{1.0, -1.0}},  //
      SE2d{SO2d{Constants::pi() * 0.3}, Vector2d{2.0, -2.0}},  //
      SE2d{SO2d{Constants::pi() * 0.4}, Vector2d{3.0, -1.0}},  //
      SE2d{SO2d{Constants::pi() * 0.5}, Vector2d{2.0, 1.0}},   //
      SE2d{SO2d{Constants::pi() * 0.4}, Vector2d{1.0, 2.0}},   //
  };
  const auto weights = std::vector(states.size(), 1.0);
  constexpr double kTolerance = 0.001;
  const auto [pose, covariance] = beluga::estimate(states, weights);
  ASSERT_THAT(pose, SE2Near(SO2d{0.8762}, Vector2d{1.700, 0.0}, kTolerance));
  ASSERT_THAT(covariance.col(0).eval(), Vector3Near({0.9000, 0.5556, 0.0000}, kTolerance));
  ASSERT_THAT(covariance.col(1).eval(), Vector3Near({0.5556, 2.4444, 0.0000}, kTolerance));
  ASSERT_THAT(covariance.col(2).eval(), Vector3Near({0.0000, 0.0000, 0.1355}, kTolerance));
}

TEST_F(PoseCovarianceEstimation, WeightsCanSingleOutOneSample) {
  // test the weights have effect by selecting a few states and ignoring others
  const auto states = std::vector{
      SE2d{SO2d{Constants::pi() / 6}, Vector2d{0.0, -3.0}},  //
      SE2d{SO2d{Constants::pi() / 2}, Vector2d{1.0, -2.0}},  //
      SE2d{SO2d{Constants::pi() / 3}, Vector2d{2.0, -1.0}},  //
      SE2d{SO2d{Constants::pi() / 2}, Vector2d{1.0, -2.0}},  //
  };
  const auto weights = std::vector{0.0, 1.0, 0.0, 1.0};
  constexpr double kTolerance = 0.001;
  const auto [pose, covariance] = beluga::estimate(states, weights);
  ASSERT_THAT(pose, SE2Near(SO2d{Constants::pi() / 2}, Vector2d{1.0, -2.0}, kTolerance));
  ASSERT_THAT(covariance.col(0).eval(), Vector3Near({0.0, 0.0, 0.0}, kTolerance));
  ASSERT_THAT(covariance.col(1).eval(), Vector3Near({0.0, 0.0, 0.0}, kTolerance));
  ASSERT_THAT(covariance.col(2).eval(), Vector3Near({0.0, 0.0, 0.0}, kTolerance));
}

TEST_F(PoseCovarianceEstimation, RandomWalkWithSmoothRotationAndNonUniformWeights) {
  // test the mean and covariance estimations for states with random variations of translation, rotation and weights
  const auto states = std::vector{
      SE2d{SO2d{Constants::pi() * 0.1}, Vector2d{0.0, -2.0}},  //
      SE2d{SO2d{Constants::pi() * 0.2}, Vector2d{1.0, -1.0}},  //
      SE2d{SO2d{Constants::pi() * 0.3}, Vector2d{2.0, 1.0}},   //
      SE2d{SO2d{Constants::pi() * 0.2}, Vector2d{3.0, 2.0}},   //
      SE2d{SO2d{Constants::pi() * 0.2}, Vector2d{2.0, 1.0}},   //
      SE2d{SO2d{Constants::pi() * 0.2}, Vector2d{1.0, -1.0}},  //
      SE2d{SO2d{Constants::pi() * 0.3}, Vector2d{2.0, -2.0}},  //
      SE2d{SO2d{Constants::pi() * 0.4}, Vector2d{3.0, -1.0}},  //
      SE2d{SO2d{Constants::pi() * 0.5}, Vector2d{2.0, 1.0}},   //
      SE2d{SO2d{Constants::pi() * 0.4}, Vector2d{1.0, 2.0}},   //
  };
  const auto weights = std::vector{0.1, 0.4, 0.7, 0.1, 0.9, 0.2, 0.2, 0.4, 0.1, 0.4};
  constexpr double kTolerance = 0.001;
  const auto [pose, covariance] = beluga::estimate(states, weights);
  ASSERT_THAT(pose, SE2Near(SO2d{0.8687}, Vector2d{1.800, 0.3143}, kTolerance));
  ASSERT_THAT(covariance.col(0).eval(), Vector3Near({0.5946, 0.0743, 0.0000}, kTolerance));
  ASSERT_THAT(covariance.col(1).eval(), Vector3Near({0.0743, 1.8764, 0.0000}, kTolerance));
  ASSERT_THAT(covariance.col(2).eval(), Vector3Near({0.0000, 0.0000, 0.0855}, kTolerance));
}

struct ScalarEstimation : public testing::Test {};

TEST_F(ScalarEstimation, UniformWeightOverload) {
  // Mean and variance estimation with uniform weights.
  const auto states = std::vector{0.0, 1.0, 1.0, 2.0, 2.0, 3.0, 4.0, 4.0, 5.0, 5.0, 6.0, 7.0, 7.0, 8.0, 9.0};
  const auto weights = std::vector(states.size(), 1.0);
  const auto [mean, variance] = beluga::estimate(states, weights);
  const auto standard_deviation = std::sqrt(variance);
  constexpr double kTolerance = 0.001;
  ASSERT_NEAR(mean, 4.266, kTolerance);
  ASSERT_NEAR(standard_deviation, 2.763, kTolerance);
}

TEST_F(ScalarEstimation, NonUniformWeightOverload) {
  // Mean and variance estimation with non-uniform weights.
  const auto states = std::vector{0.0, 1.0, 1.0, 2.0, 2.0, 3.0, 4.0, 4.0, 5.0, 5.0, 6.0, 7.0, 7.0, 8.0, 9.0};
  const auto weights = std::vector{0.1, 0.15, 0.15, 0.3, 0.3, 0.4, 0.8, 0.8, 0.4, 0.4, 0.35, 0.3, 0.3, 0.15, 0.1};
  const auto [mean, variance] = beluga::estimate(states, weights);
  const auto standard_deviation = std::sqrt(variance);
  constexpr double kTolerance = 0.001;
  ASSERT_NEAR(mean, 4.300, kTolerance);
  ASSERT_NEAR(standard_deviation, 2.055, kTolerance);
}

TEST_F(PoseCovarianceEstimation, MultiVariateNormalSE3) {
  constexpr double kTolerance = 0.01;
  const auto expected_mean =
      Sophus::SE3d{Sophus::SO3d::exp(Eigen::Vector3d{-0.17, 0.25, 0.1}), Eigen::Vector3d{1.0, 2.0, 3.0}};
  const Eigen::Matrix<double, 6, 6> expected_cov = Eigen::Matrix<double, 6, 6>::Identity() * 2e-1;
  auto distribution = beluga::MultivariateNormalDistribution{expected_mean, expected_cov};
  const auto samples = beluga::views::sample(distribution) |   //
                       ranges::views::take_exactly(100'000) |  //
                       ranges::to<std::vector>;
  const auto [mean, cov] =
      beluga::estimate(samples, ranges::views::repeat_n(1.0, static_cast<std::ptrdiff_t>(samples.size())));
  ASSERT_TRUE(expected_mean.matrix().isApprox(mean.matrix(), kTolerance));
  ASSERT_TRUE(((cov - expected_cov).array().abs() < kTolerance).all()) << std::fixed << (cov - expected_cov);
}

TEST_F(PoseCovarianceEstimation, WeightedSE3) {
  constexpr double kTolerance = 0.001;
  const auto states = std::vector{
      Sophus::SE3d::rotZ(0.5),
      Sophus::SE3d::rotZ(0.0),
      Sophus::SE3d::rotZ(-.5),
  };

  {
    const auto [mean, cov] = beluga::estimate(states, std::array{1., 1., 1.});
    ASSERT_TRUE(mean.matrix().isApprox(Sophus::SE3d{}.matrix(), kTolerance));
  }
  {
    const auto [mean, cov] = beluga::estimate(states, std::array{0.01, 0.01, 500.0});
    ASSERT_TRUE(mean.matrix().isApprox(Sophus::SE3d::rotZ(-.5).matrix(), kTolerance)) << mean.matrix();
  }
}

TEST(AverageQuaternion, AgainstSophusImpl) {
  const auto quaternions = std::vector{
      Eigen::Quaterniond::UnitRandom(),
      Eigen::Quaterniond::UnitRandom(),
      Eigen::Quaterniond::UnitRandom(),
  };

  {
    auto quats_as_so3_view =
        quaternions | ranges::views::transform([](const Eigen::Quaterniond& q) { return SO3d{q}; });
    const auto avg_quaternion = beluga::mean(quaternions, std::array{1., 1., 1.});
    const auto avg_quat_sophus = Sophus::details::averageUnitQuaternion(quats_as_so3_view | ranges::to<std::vector>);
    ASSERT_TRUE(avg_quaternion.isApprox(avg_quat_sophus));
  }

  {
    constexpr double kTolerance = 0.01;
    auto quats_as_so3_view =
        quaternions | ranges::views::transform([](const Eigen::Quaterniond& q) { return SO3d{q}; });
    const auto avg_quaternion = beluga::mean(quaternions, std::array{1e-3, 1e-3, 1. - 2e-3});
    const auto avg_quat_sophus = Sophus::details::averageUnitQuaternion(quats_as_so3_view | ranges::to<std::vector>);
    ASSERT_FALSE(avg_quaternion.isApprox(avg_quat_sophus));
    ASSERT_TRUE(avg_quaternion.isApprox(quaternions.back(), kTolerance));
  }
}

TEST_F(PoseCovarianceEstimation, SE3EquallyWeighted) {
  constexpr double kTolerance = 0.001;
  const auto states = std::vector{
      Sophus::SE3d::rotZ(0.5),
      Sophus::SE3d::rotZ(0.0),
      Sophus::SE3d::rotZ(-.5),
  };

  {
    const auto [mean, cov] = beluga::estimate(states);
    ASSERT_TRUE(mean.matrix().isApprox(Sophus::SE3d{}.matrix(), kTolerance));
  }
}

TEST_F(PoseCovarianceEstimation, SE3BadArguments) {
  ASSERT_DEBUG_DEATH(beluga::estimate(std::array{Sophus::SE3d{}}, std::array{1., 1., 1.}), ".*");
  ASSERT_DEBUG_DEATH(beluga::estimate(std::array{Sophus::SE3d{}, Sophus::SE3d{}}, std::array{1., 1., 1.}), ".*");
  ASSERT_DEBUG_DEATH(beluga::estimate(std::vector<Sophus::SE3d>{}, std::array{1., 1., 1.}), ".*");
}

}  // namespace
