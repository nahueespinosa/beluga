#include <benchmark/benchmark.h>

#include <range/v3/view.hpp>

#include <beluga/tuple_vector.h>
#include <beluga/type_traits.h>

namespace {

constexpr std::size_t kParticleCount = 1'000'000;

struct State {
  double x = 0.;
  double y = 0.;
  double theta = 0.;
};

using Particle = std::tuple<State, double, std::size_t>;
using StructureOfArrays = beluga::TupleVector<Particle>;
using ArrayOfStructures = beluga::Vector<Particle>;

struct Arrays {
  std::vector<State> states;
  std::vector<double> weights;
  std::vector<std::size_t> clusters;

  [[nodiscard]] std::size_t size() const noexcept { return states.size(); }

  void clear() noexcept {
    states.clear();
    weights.clear();
    clusters.clear();
  }

  void reserve(std::size_t new_cap) {
    states.reserve(new_cap);
    weights.reserve(new_cap);
    clusters.reserve(new_cap);
  }

  void resize(std::size_t count) {
    states.resize(count);
    weights.resize(count);
    clusters.resize(count);
  }
};

double update_weight(const State& state) {
  return state.x * state.y * state.theta;
}

void BM_Update_Baseline_StructureOfArrays(benchmark::State& state) {
  Arrays arrays;
  arrays.resize(kParticleCount);
  for (auto _ : state) {
    for (std::size_t i = 0; i < kParticleCount; ++i) {
      arrays.weights[i] = update_weight(arrays.states[i]);
    }
  }
}

void BM_Update_Baseline_ArrayOfStructures(benchmark::State& state) {
  std::vector<Particle> particles;
  particles.resize(kParticleCount);
  for (auto _ : state) {
    for (std::size_t i = 0; i < kParticleCount; ++i) {
      auto&& particle = particles[i];
      std::get<1>(particle) = update_weight(std::get<0>(particle));
    }
  }
}

template <class Container>
void BM_Update(benchmark::State& state) {
  auto container = Container{};
  container.resize(kParticleCount);
  for (auto _ : state) {
    auto&& states = beluga::views::states(container);
    auto&& weights = beluga::views::weights(container);
    std::transform(std::begin(states), std::end(states), std::begin(weights), update_weight);
  }
}

BENCHMARK(BM_Update_Baseline_StructureOfArrays)->MinWarmUpTime(1);
BENCHMARK(BM_Update<StructureOfArrays>)->MinWarmUpTime(1);
BENCHMARK(BM_Update_Baseline_ArrayOfStructures)->MinWarmUpTime(1);
BENCHMARK(BM_Update<ArrayOfStructures>)->MinWarmUpTime(1);

void BM_PushBack_Baseline_StructureOfArrays(benchmark::State& state) {
  Arrays arrays;
  arrays.resize(kParticleCount);
  Arrays new_arrays;
  new_arrays.reserve(kParticleCount);
  for (auto _ : state) {
    new_arrays.clear();
    for (std::size_t i = 0; i < kParticleCount; ++i) {
      new_arrays.states.push_back(State{});
      new_arrays.weights.push_back(0);
      new_arrays.clusters.push_back(0);
    }
  }
}

void BM_PushBack_Baseline_ArrayOfStructures(benchmark::State& state) {
  std::vector<Particle> particles;
  particles.resize(kParticleCount);
  std::vector<Particle> new_particles;
  new_particles.reserve(kParticleCount);
  for (auto _ : state) {
    new_particles.clear();
    for (std::size_t i = 0; i < kParticleCount; ++i) {
      new_particles.emplace_back(std::get<0>(particles[i]), 0, 0);
    }
  }
}

template <class Container>
void BM_PushBack(benchmark::State& state) {
  auto container = Container{};
  container.resize(kParticleCount);
  auto new_container = Container{};
  new_container.reserve(kParticleCount);
  for (auto _ : state) {
    new_container.clear();
    auto&& states = beluga::views::states(container);
    std::transform(std::begin(states), std::end(states), std::back_inserter(new_container), [](const State& state) {
      return std::make_tuple(state, 0, 0);
    });
  }
}

BENCHMARK(BM_PushBack_Baseline_StructureOfArrays)->MinWarmUpTime(1);
BENCHMARK(BM_PushBack<StructureOfArrays>)->MinWarmUpTime(1);
BENCHMARK(BM_PushBack_Baseline_ArrayOfStructures)->MinWarmUpTime(1);
BENCHMARK(BM_PushBack<ArrayOfStructures>)->MinWarmUpTime(1);

void BM_Assign_Baseline_ArrayOfStructures(benchmark::State& state) {
  std::vector<Particle> particles;
  particles.resize(kParticleCount);
  std::vector<Particle> new_particles;
  new_particles.resize(kParticleCount);
  for (auto _ : state) {
    new_particles.clear();
    for (std::size_t i = 0; i < kParticleCount; ++i) {
      new_particles[i] = Particle{std::get<0>(particles[i]), 0, 0};
    }
  }
}

void BM_Assign_Baseline_StructureOfArrays(benchmark::State& state) {
  Arrays arrays;
  arrays.resize(kParticleCount);
  Arrays new_arrays;
  new_arrays.resize(kParticleCount);
  for (auto _ : state) {
    for (std::size_t i = 0; i < kParticleCount; ++i) {
      new_arrays.states[i] = arrays.states[i];
      new_arrays.weights[i] = 0;
      new_arrays.clusters[i] = 0;
    }
  }
}

template <class Container>
void BM_Assign(benchmark::State& state) {
  auto container = Container{};
  container.resize(kParticleCount);
  auto new_container = Container{};
  new_container.resize(kParticleCount);
  for (auto _ : state) {
    auto&& states = beluga::views::states(container);
    auto&& new_particles = beluga::views::all(new_container);
    std::transform(std::begin(states), std::end(states), std::begin(new_particles), [](const State& state) {
      return std::make_tuple(state, 0, 0);
    });
  }
}

BENCHMARK(BM_Assign_Baseline_StructureOfArrays)->MinWarmUpTime(1);
BENCHMARK(BM_Assign<StructureOfArrays>)->MinWarmUpTime(1);
BENCHMARK(BM_Assign_Baseline_ArrayOfStructures)->MinWarmUpTime(1);
BENCHMARK(BM_Assign<ArrayOfStructures>)->MinWarmUpTime(1);

}  // namespace
