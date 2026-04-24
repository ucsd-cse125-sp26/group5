#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include <iostream>
#include <vector>
#include <algorithm>

namespace shared {
namespace profiler {

inline std::unordered_map<std::string, double> frame_stats;
inline std::chrono::time_point<std::chrono::high_resolution_clock> frame_start;

class ScopeTimer {
public:
    ScopeTimer(const char* name) : name_(name) {
        start_ = std::chrono::high_resolution_clock::now();
    }
    ~ScopeTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start_;
        frame_stats[name_] += elapsed.count();
    }
private:
    const char* name_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

inline void start_frame() {
    frame_start = std::chrono::high_resolution_clock::now();
}

inline void end_frame(const char* context = "Frame") {
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - frame_start;
    
    // Add the total time over the 60 frames.
    static double cumulative_frame_time = 0.0;
    cumulative_frame_time += elapsed.count();

    static int frame_count = 0;
    frame_count++;
    
    // Print stats every 60 frames
    if (frame_count % 60 == 0) { 
        std::cout << "\n=== [ " << context << " ] Profiling Stats (Average per Frame over last 60 Frames) ===\n";
        double average_frame_time = cumulative_frame_time / 60.0;
        std::cout << "Avg Frame Time: " << average_frame_time << " ms\n";
        
        std::vector<std::pair<std::string, double>> stats(frame_stats.begin(), frame_stats.end());
        std::sort(stats.begin(), stats.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        for (const auto& stat : stats) {
            double avg_stat_time = stat.second / 60.0;
            double percent = average_frame_time > 0 ? (avg_stat_time / average_frame_time) * 100.0 : 0.0;
            std::cout << " - " << stat.first << ": " << avg_stat_time << " ms (" << percent << "%)\n";
        }
        std::cout << "========================================================\n";
        
        // Reset accumulation for the next 60 frames
        frame_stats.clear();
        cumulative_frame_time = 0.0;
    }
}

} // profiler
} // shared

#ifdef ENABLE_PROFILING
    #define SIMPLE_PROFILE_SCOPE(name) shared::profiler::ScopeTimer __timer_##__LINE__(name)
    #define SIMPLE_PROFILE_FRAME_START() shared::profiler::start_frame()
    #define SIMPLE_PROFILE_FRAME_END(context) shared::profiler::end_frame(context)
#else
    #define SIMPLE_PROFILE_SCOPE(name)
    #define SIMPLE_PROFILE_FRAME_START()
    #define SIMPLE_PROFILE_FRAME_END(context)
#endif
