/*
 * MVKPerfICount.h
 *
 * Enshrouded perf debug (D12, for C4): how many of the Metal draws MoltenVK encodes for
 * vkCmdDraw[Indexed]IndirectCount are real.
 *
 * MoltenVK encodes maxDrawCount Metal indirect draws on the CPU for every such command; a conversion
 * kernel then zeroes the ones at or beyond the GPU-side count. With MVK_PERF_ICOUNT_STATS=1 the
 * conversion kernels record, per command, the real count min(count, maxDrawCount) into a small shared
 * buffer (slots below), and the CPU keeps the encoded totals and a histogram of maxDrawCount values.
 * Every 20,000 commands a summary goes to stderr as "[mvk-perf] icount" lines.
 */

#pragma once

#import <Metal/Metal.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

/** Slots of the GPU statistics buffer. Must match the count-conversion kernels. */
enum MVKPerfICountStat : uint32_t {
	kMVKPerfICountRealDraws,		// sum of min(count, maxDrawCount), wraps
	kMVKPerfICountMaxReal,			// largest real count in one command
	kMVKPerfICountMaxFillPerMille,	// largest real count / maxDrawCount, in 1/1000
	kMVKPerfICountEmptyCommands,	// commands whose count was 0
	kMVKPerfICountOverMax,			// commands whose count exceeded maxDrawCount
	kMVKPerfICountStatCount
};

/**
 * Enshrouded perf C4: the most Metal draws to encode for one vkCmdDraw[Indexed]IndirectCount.
 * MoltenVK encodes maxDrawCount draws on the CPU and zeroes the unused ones on the GPU; Enshrouded asks
 * for up to 10,368 per command but uses at most ~101 (D12, 25 Oct 2026). MVK_PERF_ICOUNT_MAX_DRAWS caps
 * maxDrawCount (default 512; 0 = no cap). With MVK_PERF_ICOUNT_STATS=1, "count > max" in the icount lines
 * then counts commands the cap truncated.
 */
inline uint32_t mvkPerfICountCap(uint32_t maxDrawCount) {
	static const uint32_t cap = [] {
		const char* s = getenv("MVK_PERF_ICOUNT_MAX_DRAWS");
		long n = s ? strtol(s, nullptr, 0) : 512;
		uint32_t c = n > 0 ? (uint32_t)n : 0;
		if (c) { fprintf(stderr, "[mvk-perf] C4 indirect-count draws capped at %u per command\n", c); }
		else   { fprintf(stderr, "[mvk-perf] C4 indirect-count cap off\n"); }
		return c;
	}();
	return cap ? std::min(maxDrawCount, cap) : maxDrawCount;
}

inline bool mvkPerfICountStatsEnabled() {
	static const bool on = [] { const char* s = getenv("MVK_PERF_ICOUNT_STATS"); return s && atoi(s) != 0; }();
	return on;
}

inline id<MTLBuffer> mvkPerfICountStatsBuffer(id<MTLDevice> mtlDevice) {
	static id<MTLBuffer> buff = [] (id<MTLDevice> dev) {
		id<MTLBuffer> b = [dev newBufferWithLength: kMVKPerfICountStatCount * sizeof(uint32_t)
										   options: MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
		memset(b.contents, 0, b.length);
		return b;
	}(mtlDevice);
	return buff;
}

/** Called once per encoded IndirectCount command, with its maxDrawCount. */
inline void mvkPerfICountRecord(id<MTLBuffer> buff, uint32_t maxDrawCount) {
	static std::mutex lock;
	static uint64_t commands = 0, encoded = 0, lastCommands = 0, lastEncoded = 0;
	static uint32_t lastReal = 0;
	static std::map<uint32_t, uint64_t> histogram;

	std::lock_guard<std::mutex> guard(lock);
	commands++; encoded += maxDrawCount; histogram[maxDrawCount]++;
	if (commands % 20000 != 0) { return; }

	const volatile uint32_t* stats = (const volatile uint32_t*)buff.contents;
	uint32_t real = stats[kMVKPerfICountRealDraws];		// GPU lags the CPU a little; fine for a summary
	uint64_t dCmd = commands - lastCommands, dEnc = encoded - lastEncoded, dReal = (uint32_t)(real - lastReal);
	fprintf(stderr, "[mvk-perf] icount: last %llu commands encoded %llu Metal draws, %llu real (%.2f%%); "
			"max real per command %u, max fill %.1f%%, empty commands %u, count > max %u (totals so far)\n",
			(unsigned long long)dCmd, (unsigned long long)dEnc, (unsigned long long)dReal, dEnc ? 100.0 * dReal / dEnc : 0.0,
			stats[kMVKPerfICountMaxReal], stats[kMVKPerfICountMaxFillPerMille] / 10.0,
			stats[kMVKPerfICountEmptyCommands], stats[kMVKPerfICountOverMax]);
	std::vector<std::pair<uint64_t, uint32_t>> v;
	for (auto& kv : histogram) { v.emplace_back(kv.second, kv.first); }
	std::sort(v.rbegin(), v.rend());
	fprintf(stderr, "[mvk-perf] icount: maxDrawCount values (commands so far):");
	for (size_t i = 0; i < v.size() && i < 12; i++) { fprintf(stderr, " %u x%llu", v[i].second, (unsigned long long)v[i].first); }
	fprintf(stderr, "\n");
	fflush(stderr);
	lastCommands = commands; lastEncoded = encoded; lastReal = real;
}
