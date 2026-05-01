#include "prototypes/DOTSPrototype.hpp"
#include <iostream>
#include <random>
#include <chrono>
#include <cmath>
#include <string>

namespace dots_prototype {

void run_prototype(int argc, char** argv) {
    std::size_t N = 2000;
    int steps = 5;
    if (argc > 1) {
        try { N = static_cast<std::size_t>(std::stoul(argv[1])); } catch(...) {}
    }
    if (argc > 2) {
        try { steps = std::stoi(argv[2]); } catch(...) {}
    }

    ThreadPool pool(std::max<std::size_t>(1, std::thread::hardware_concurrency()));
    ParticleSoA p;
    p.resize(N);

    std::mt19937 rng(424242u);
    std::uniform_real_distribution<float> posd(-1.0f, 1.0f);
    std::uniform_real_distribution<float> veld(-0.01f, 0.01f);
    std::uniform_real_distribution<float> massd(0.5f, 2.0f);
    for (std::size_t i = 0; i < N; ++i) {
        p.x[i] = posd(rng);
        p.y[i] = posd(rng);
        p.z[i] = posd(rng);
        p.vx[i] = veld(rng);
        p.vy[i] = veld(rng);
        p.vz[i] = veld(rng);
        p.mass[i] = massd(rng);
    }

    std::vector<float> ax(N), ay(N), az(N);
    const float dt = 0.01f;
    const float soften = 1e-6f;

    std::cout << "DOTS prototype: N=" << N << " steps=" << steps << " threads=" << std::thread::hardware_concurrency() << std::endl;

    for (int step = 0; step < steps; ++step) {
        auto t0 = std::chrono::high_resolution_clock::now();

        // compute accelerations (naive O(N^2) - prototype only)
        pool.parallel_for(0, N, [&](std::size_t i) {
            float xi = p.x[i], yi = p.y[i], zi = p.z[i];
            float axi = 0.0f, ayi = 0.0f, azi = 0.0f;
            for (std::size_t j = 0; j < N; ++j) {
                if (j == i) continue;
                float dx = p.x[j] - xi;
                float dy = p.y[j] - yi;
                float dz = p.z[j] - zi;
                float r2 = dx*dx + dy*dy + dz*dz + soften;
                float invr = 1.0f / std::sqrt(r2);
                float invr3 = invr * invr * invr;
                float m = p.mass[j];
                axi += m * dx * invr3;
                ayi += m * dy * invr3;
                azi += m * dz * invr3;
            }
            ax[i] = axi; ay[i] = ayi; az[i] = azi;
        }, 32);

        // integrate
        pool.parallel_for(0, N, [&](std::size_t i) {
            p.vx[i] += ax[i] * dt;
            p.vy[i] += ay[i] * dt;
            p.vz[i] += az[i] * dt;
            p.x[i] += p.vx[i] * dt;
            p.y[i] += p.vy[i] * dt;
            p.z[i] += p.vz[i] * dt;
        }, 64);

        auto t1 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> dur = t1 - t0;
        std::cout << " step=" << step << " wall=" << dur.count() << "s" << std::endl;
    }
}

} // namespace dots_prototype

int main(int argc, char** argv) {
    dots_prototype::run_prototype(argc, argv);
    return 0;
}
