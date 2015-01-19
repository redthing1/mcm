/*	MCM file compressor

	Copyright (C) 2015, Google Inc.
	Authors: Mathieu Chartier

	LICENSE

    This file is part of the MCM file compressor.

    MCM is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MCM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MCM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _TURBO_CM_HPP_
#define _TURBO_CM_HPP_

#include <cstdlib>
#include <vector>
#include "Detector.hpp"
#include "DivTable.hpp"
#include "Entropy.hpp"
#include "Huffman.hpp"
#include "Log.hpp"
#include "MatchModel.hpp"
#include "Memory.hpp"
#include "Mixer.hpp"
#include "Model.hpp"
#include "Range.hpp"
#include "StateMap.hpp"
#include "Util.hpp"
#include "WordModel.hpp"

template <const uint32_t level = 6>
class TurboCM : public Compressor {
public:
	// SS table
	static const uint32_t shift = 12;
	static const uint32_t max_value = 1 << shift;
	typedef ss_table<short, max_value, -2 * int(KB), 2 * int(KB), 8> SSTable;
	SSTable table;
	typedef fastBitModel<int, shift, 9, 30> StationaryModel;
	static const int kEOFChar = 233;

	// Contexts
	uint32_t owhash; // Order of sizeof(uint32_t) hash.

	// Rotating buffer.
	CyclicBuffer<byte> buffer;

	// Probs
	StationaryModel probs[8][256];
	StationaryModel eof_model;
	std::vector<StationaryModel> isse1;
	std::vector<StationaryModel> isse2;

	// CM state table.
	static const uint32_t num_states = 256;
	byte state_trans[num_states][2];

	// Fixed models
	byte order0[256];
	byte order1[256 * 256];

	// Hash table
	uint32_t hash_mask;
	MemMap hash_storage;
	byte* hash_table;

	// Learn rate
	uint32_t byte_count;

	// Range encoder
	Range7 ent;

	// Memory usage
	uint32_t mem_usage;

	// Word model.
	WordModel word_model;

	// Optimization variable.
	uint32_t opt_var;

	// Mixer
	typedef Mixer<int, 4, 17, 1> CMMixer;
	CMMixer mixer;

	TurboCM() : mem_usage(0), opt_var(0) {
	}


	void setOpt(uint32_t var) {
		opt_var = var;
	}

	void setMemUsage(uint32_t level) {
		mem_usage = level;
	}

	void init() {
		for (auto& c : order0) c = 0;
		for (auto& c : order1) c = 0;
		table.build(0);
	
		hash_mask = ((2 * MB) << mem_usage) / sizeof(hash_table[0]) - 1;
		hash_storage.resize(hash_mask + 1); // Add extra space for ctx.
		std::cout << hash_mask + 1 << std::endl;
		hash_table = reinterpret_cast<byte*>(hash_storage.getData());

		NSStateMap<12> sm;
		sm.build();
		
		// Optimization
		for (uint32_t i = 0; i < num_states; ++i) {
			// Pre stretch state map.
			for (uint32_t j = 0; j < 2; ++j) {
				state_trans[i][j] = sm.getTransition(i, j);
			}
		}

		unsigned short initial_probs[][256] = {
			{1890,1430,689,498,380,292,171,165,155,137,96,101,70,81,92,72,64,85,76,57,100,65,33,44,49,40,69,39,63,29,46,55,41,63,33,38,35,24,33,32,30,28,33,51,66,28,52,2,15,0,0,1,0,797,442,182,242,194,201,183,153,135,124,171,93,85,122,67,71,93,222,32,631,896,980,647,895,164,93,1375,693,566,497,420,414,411,357,356,319,1740,649,683,681,1717,1170,1025,892,834,835,685,1727,1259,1612,1588,1778,1999,2524,2197,2613,2781,2957,2181,1664,1735,1496,1061,913,2521,1524,1935,2155,2733,1500,1282,2906,3093,2337,3337,2392,1647,3113,2435,1885,3332,2614,3384,3394,3437,3606,3432,3669,3473,2090,1598,3186,3578,3713,3533,1936,1525,2864,3689,3624,3782,3769,2293,3974,2654,3953,2693,3088,2302,1967,2558,2865,1563,2458,1805,3255,3700,1576,2840,2649,3017,1472,2467,2018,3157,3024,2338,3011,3377,3361,3394,3515,1715,3653,2480,1370,3695,3593,2812,2561,3709,3827,3780,3787,3799,3850,3817,3773,3863,3943,1946,3922,3946,3954,3952,4089,2316,4095,2515,4087,3067,2107,4095,2986,2806,3420,2255,3818,3269,3614,2293,2541,3370,3583,3572,2147,2845,2940,2999,3591,3507,1981,3072,2950,2851,3629,3890,3891,3867,2290,3846,3637,2856,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{1857,973,217,167,164,78,79,74,72,51,64,34,19,35,23,40,22,33,36,13,24,14,15,22,23,22,33,19,19,20,20,43,16,18,5,17,9,9,13,20,20,6,12,14,15,10,14,0,0,0,0,7,0,1936,466,259,266,243,282,142,161,196,93,211,103,81,143,61,92,146,545,87,1240,1846,2578,984,2556,40,27,1543,810,503,584,430,398,451,431,342,372,2337,731,726,643,1747,1264,1050,1102,999,932,736,1896,1399,1823,1733,1657,2082,2214,2133,2367,2744,2989,2493,1734,1602,1257,1158,1082,2579,1420,2305,1886,2700,1398,1266,3112,3146,2189,3268,2025,1537,2998,2650,2254,3212,2568,3283,3419,3521,3455,3317,3531,3538,1863,1524,3069,3606,3657,3473,1612,1431,2731,3589,3663,3672,3745,1839,3998,1977,3952,2104,2984,2163,2378,2655,3006,2231,2589,2609,3838,3982,1622,2114,3226,3414,2098,2708,1699,3499,3578,2265,3513,3530,3574,3777,3722,1506,3726,1863,1004,3788,3889,2675,1455,3658,3684,3796,3894,3925,3921,3962,4014,4039,4004,1350,3989,4051,3995,4061,4090,1814,4093,1804,4093,1796,1896,4089,1846,2872,2695,2539,3663,2624,2818,2843,655,1994,2834,2804,2512,1868,1897,1868,2729,2467,1893,1824,1908,1908,2845,3474,3486,3509,2946,3272,2921,2581,4067,4067,4063,4079,4077,4075,3493,4081,4095,2048,},
			{1928,1055,592,381,231,275,181,187,173,84,93,111,80,40,39,84,78,64,62,63,40,70,32,43,46,48,47,52,36,34,34,84,25,47,29,19,24,18,24,39,24,30,30,17,55,22,37,0,8,0,8,0,0,1705,354,205,289,245,199,161,131,137,120,161,150,94,112,105,72,109,713,14,1057,2128,2597,1086,2813,13,42,1580,745,591,576,476,469,312,353,337,273,2435,711,592,611,1967,1135,903,844,773,901,728,2080,1275,1745,1424,1779,2062,2495,2482,2608,2723,2862,2358,1455,1514,1034,924,883,2791,1310,2219,2045,2914,1314,1125,3152,3105,2162,3460,2070,1481,3238,2380,1521,3288,2417,3437,3513,3641,3661,3447,3616,3605,1667,1272,2988,3687,3732,3501,1518,1445,2688,3578,3710,3754,3768,1682,3993,2011,3971,2135,3125,2647,2400,2908,3211,2364,2629,2868,3613,3796,1412,3199,2994,3720,1754,2437,1452,3660,3631,2151,3707,3645,3750,3883,3732,1600,3847,1657,1028,3822,3894,2361,1321,3836,3783,3911,3876,3922,3972,3876,3911,3953,3952,1524,3890,3969,4017,4019,4091,2015,4094,1902,4073,1951,2250,4088,2141,2824,2674,2455,3349,2378,2556,2648,884,2596,2657,2597,2257,1715,1289,1369,2479,2341,2127,1726,1551,1737,2608,3368,3455,3255,2681,3104,2864,2564,4055,4057,4069,4063,4082,4070,3234,4062,4094,2048,},
			{1819,1089,439,350,201,174,135,170,86,105,95,113,81,76,52,87,44,50,72,74,30,35,49,38,35,36,47,69,9,52,41,43,43,45,43,24,27,15,24,9,27,21,11,14,19,11,27,0,0,0,8,1,1,1888,488,287,315,278,211,218,181,207,131,242,118,139,124,126,117,148,1082,115,1565,2380,2600,1110,2813,17,25,1670,736,581,578,444,435,429,367,352,282,2492,693,652,723,2026,1049,918,976,799,873,730,1923,1309,1731,1505,1823,1941,2321,2644,2678,2695,2941,2286,1278,1422,1152,1075,835,2730,1357,2679,1833,2886,1257,1188,3044,3193,2161,3468,1789,1463,3177,2291,1510,3261,2357,3373,3464,3638,3658,3415,3552,3529,1546,1188,2974,3555,3625,3528,1351,1395,2705,3512,3653,3681,3664,1428,3948,1856,3969,1835,3103,2445,2405,2840,3268,2555,2827,2908,3684,3778,1415,3138,3156,3499,1791,2488,1529,3612,3552,2027,3606,3646,3663,3850,3772,1623,3818,1655,1142,3845,3894,2040,1560,3759,3825,3878,3908,3961,3964,3908,3896,3962,3988,1403,3976,4019,4015,4013,4065,1911,4077,1896,4086,1986,2082,4074,2098,2771,2465,2430,3109,2338,1966,2641,1065,2461,2369,2555,2228,1795,952,1041,2300,2285,2348,2143,1962,2110,2375,3121,2929,2835,2503,2659,2444,2491,4062,4048,4050,4084,4063,4078,3311,4087,4095,2048,},
			{1948,1202,284,256,184,174,132,75,73,90,65,72,52,55,63,89,38,21,76,21,44,32,37,41,6,22,21,51,22,21,15,7,12,25,31,48,15,23,8,26,9,15,5,18,9,34,20,8,0,0,0,0,0,1715,393,271,335,330,342,261,179,183,160,203,118,156,129,117,91,130,874,100,1616,2406,2460,1074,2813,31,43,1474,718,552,532,422,527,393,363,295,319,2535,710,587,668,1854,1133,927,1005,753,865,758,1905,1216,1563,1561,1743,1906,2353,2623,2737,2632,2838,2247,1349,1570,1169,1100,757,2828,1419,2407,1929,2975,1187,1245,3109,3115,2147,3428,1829,1288,3116,2360,1408,3313,2425,3483,3513,3614,3671,3409,3441,3438,1607,1312,2966,3525,3568,3480,1426,1293,2730,3536,3704,3726,3727,1407,3882,1663,3958,1619,3117,2467,2512,2749,3109,2350,2753,2869,3858,3831,1534,3170,3254,3634,1940,2749,1538,3668,3705,2097,3616,3668,3707,3827,3698,1592,3762,1773,1108,3858,3828,2178,1566,3790,3748,3868,3942,3950,3968,3928,3912,4001,3972,1497,3986,4008,3982,4027,4069,1891,4082,1815,4063,1998,2097,4090,2055,2680,2411,2533,3155,2209,1840,2669,1118,2618,2266,2532,2327,1671,972,937,2204,2292,2277,1647,1606,1915,2324,2915,2908,2820,2443,2551,2357,2404,4087,4073,4074,4062,4078,4070,3348,4072,4095,2048,},
			{1866,1128,214,197,89,82,95,53,68,74,50,69,24,30,41,55,15,32,73,30,31,32,28,38,31,32,20,13,10,17,6,3,19,14,12,25,9,8,8,3,1,8,9,7,11,37,9,0,1,8,1,8,0,1488,439,297,309,299,278,205,191,197,162,250,126,122,160,117,125,122,680,60,1411,2169,2543,958,2873,63,78,1489,613,548,580,457,467,346,348,290,326,2625,639,740,649,1859,997,859,909,662,826,720,1853,1226,1625,1487,1671,1998,2374,2485,2632,2680,2943,2067,1383,1494,1090,1232,926,2641,1378,2214,2042,2836,1242,1218,3049,3069,2240,3483,1880,1239,3097,2355,1657,3334,2365,3460,3444,3676,3574,3423,3526,3489,1613,1446,2961,3606,3570,3555,1258,1041,2737,3610,3701,3759,3815,1498,3917,1425,3898,1405,3202,2528,2489,2565,3054,2362,2947,2758,3879,3891,1540,2945,3258,3531,2190,2745,1659,3571,3639,2321,3670,3628,3660,3786,3726,1605,3793,2112,1033,3827,3821,2677,1462,3785,3742,3823,3887,3926,3894,3900,4005,3997,4034,1612,4038,4042,4033,4056,4069,1901,4071,1774,4061,1823,2033,4065,2061,2744,2308,2606,3314,2279,2115,2756,980,2620,2315,2566,2265,1672,1166,1217,2286,2410,2248,1346,1413,1638,2400,3061,3171,3038,2609,2675,2467,2368,4084,4085,4092,4079,4082,4088,3658,4083,4095,2048,},
			{1890,1430,689,498,380,292,171,165,155,137,96,101,70,81,92,72,64,85,76,57,100,65,33,44,49,40,69,39,63,29,46,55,41,63,33,38,35,24,33,32,30,28,33,51,66,28,52,2,15,0,0,1,0,797,442,182,242,194,201,183,153,135,124,171,93,85,122,67,71,93,222,32,631,896,980,647,895,164,93,1375,693,566,497,420,414,411,357,356,319,1740,649,683,681,1717,1170,1025,892,834,835,685,1727,1259,1612,1588,1778,1999,2524,2197,2613,2781,2957,2181,1664,1735,1496,1061,913,2521,1524,1935,2155,2733,1500,1282,2906,3093,2337,3337,2392,1647,3113,2435,1885,3332,2614,3384,3394,3437,3606,3432,3669,3473,2090,1598,3186,3578,3713,3533,1936,1525,2864,3689,3624,3782,3769,2293,3974,2654,3953,2693,3088,2302,1967,2558,2865,1563,2458,1805,3255,3700,1576,2840,2649,3017,1472,2467,2018,3157,3024,2338,3011,3377,3361,3394,3515,1715,3653,2480,1370,3695,3593,2812,2561,3709,3827,3780,3787,3799,3850,3817,3773,3863,3943,1946,3922,3946,3954,3952,4089,2316,4095,2515,4087,3067,2107,4095,2986,2806,3420,2255,3818,3269,3614,2293,2541,3370,3583,3572,2147,2845,2940,2999,3591,3507,1981,3072,2950,2851,3629,3890,3891,3867,2290,3846,3637,2856,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
			{1890,1430,689,498,380,292,171,165,155,137,96,101,70,81,92,72,64,85,76,57,100,65,33,44,49,40,69,39,63,29,46,55,41,63,33,38,35,24,33,32,30,28,33,51,66,28,52,2,15,0,0,1,0,797,442,182,242,194,201,183,153,135,124,171,93,85,122,67,71,93,222,32,631,896,980,647,895,164,93,1375,693,566,497,420,414,411,357,356,319,1740,649,683,681,1717,1170,1025,892,834,835,685,1727,1259,1612,1588,1778,1999,2524,2197,2613,2781,2957,2181,1664,1735,1496,1061,913,2521,1524,1935,2155,2733,1500,1282,2906,3093,2337,3337,2392,1647,3113,2435,1885,3332,2614,3384,3394,3437,3606,3432,3669,3473,2090,1598,3186,3578,3713,3533,1936,1525,2864,3689,3624,3782,3769,2293,3974,2654,3953,2693,3088,2302,1967,2558,2865,1563,2458,1805,3255,3700,1576,2840,2649,3017,1472,2467,2018,3157,3024,2338,3011,3377,3361,3394,3515,1715,3653,2480,1370,3695,3593,2812,2561,3709,3827,3780,3787,3799,3850,3817,3773,3863,3943,1946,3922,3946,3954,3952,4089,2316,4095,2515,4087,3067,2107,4095,2986,2806,3420,2255,3818,3269,3614,2293,2541,3370,3583,3572,2147,2845,2940,2999,3591,3507,1981,3072,2950,2851,3629,3890,3891,3867,2290,3846,3637,2856,4009,3996,3989,4032,4007,4023,2937,4008,4095,2048,},
		};

		for (uint32_t j = 0; j < 8;++j) {
			for (uint32_t k = 0; k < num_states; ++k) {
				auto& pr = probs[j][k];
				pr.init();
				pr.setP(std::max(initial_probs[j][k], static_cast<unsigned short>(1)));
			}
		}

		isse1.resize(256 * 256);
		for (auto& pr : isse1) pr.init();
		isse2.resize(256 * 256);
		for (auto& pr : isse2) pr.init();

		eof_model.init();

		word_model.init();

		mixer.init();

		owhash = 0;
		byte_count = 0;
	}

	forceinline byte nextState(byte t, uint32_t bit, uint32_t smi = 0) {
		return state_trans[t][bit];
	}

	template <const bool kDecode, typename TStream>
	uint32_t processByte(TStream& stream, uint32_t c = 0) {
		uint32_t p0 = owhash & 0xFF;
		byte* o0ptr = &order0[0];
		byte* o1ptr = &order1[p0 << 8];
		uint32_t o2h = ((owhash & 0xFFFF) * 256) & hash_mask;
		uint32_t o3h = ((owhash & 0xFFFFFF) * 3413763181) & hash_mask;
		uint32_t o4h = (owhash * 798765431) & hash_mask;

		uint32_t learn_rate = 4 +
			(byte_count > KB) +
			(byte_count > 16 * KB) +
			(byte_count > 256 * KB) +
			(byte_count > MB);

		const short* no_alias st = table.getStretchPtr();
		uint32_t code = 0;
		if (!kDecode) {
			code = c << (sizeof(uint32_t) * 8 - 8);
		}
		uint32_t len = word_model.getLength();
		int ctx = 1;
		for (uint32_t i = 0; i < 8; ++i) {
			byte
				*no_alias s0 = nullptr, *no_alias s1 = nullptr, *no_alias s2 = nullptr, *no_alias s3 = nullptr, 
				*no_alias s4 = nullptr, *no_alias s5 = nullptr, *no_alias s6 = nullptr, *no_alias s7 = nullptr;

			s0 = &o1ptr[ctx];
			s1 = &hash_table[o2h ^ ctx];
			s2 = &hash_table[o4h ^ ctx];
			// s3 = len > 4 ? &hash_table[(word_model.getHash() & hash_mask) ^ ctx] : &hash_table[o3h ^ ctx];
			s3 = &hash_table[(word_model.getHash() & hash_mask) ^ ctx];

			auto& pr0 = probs[0][*s0];
			auto& pr1 = probs[1][*s1];
			auto& pr2 = probs[2][*s2];
			auto& pr3 = probs[3][*s3];
#if 0
			uint32_t p = table.sq(
				(
				3 * table.st(pr0.getP()) +
				3 * table.st(pr1.getP()) +
				7 * table.st(pr2.getP()) +
				8 * table.st(pr3.getP())) / 16);
#elif 0
			int p0 = table.st(pr0.getP());
			int p1 = table.st(pr1.getP());
			int p2 = table.st(pr2.getP());
			int p3 = table.st(pr3.getP());
			int p = table.sq(mixer.p(p0, p1, p2, p3));
#else
			int p = table.sq((table.st(pr0.getP()) + table.st(pr1.getP()) + table.st(pr2.getP()) + table.st(pr3.getP())) / 4) ;
#endif

			uint32_t bit;
			if (kDecode) { 
				bit = ent.getDecodedBit(p, shift);
			} else {
				bit = code >> (sizeof(uint32_t) * 8 - 1);
				code <<= 1;
				ent.encode(stream, bit, p, shift);
			}

			ctx = ctx * 2 + bit;

			// mixer.update(p0, p1, p2, p3, 0, 0, 0, 0, p, bit);
			pr0.update(bit, learn_rate);
			pr1.update(bit, learn_rate);
			pr2.update(bit, learn_rate);
			pr3.update(bit, learn_rate);
			*s0 = nextState(*s0, bit, 0);
			*s1 = nextState(*s1, bit, 1);
			*s2 = nextState(*s2, bit, 2);
			*s3 = nextState(*s3, bit, 3);
			
			// Encode the bit / decode at the last second.
			if (kDecode) {
				ent.Normalize(stream);
			}
		}

		return ctx ^ 256;
	}

	void update(char c) {
		owhash = (owhash << 8) | static_cast<byte>(c);
		word_model.updateUTF(c);
		++byte_count;
	}

	void compress(Stream* in_stream, Stream* out_stream) {
		BufferedStreamReader<4 * KB> sin(in_stream);
		BufferedStreamWriter<4 * KB> sout(out_stream);
		assert(in_stream != nullptr);
		assert(out_stream != nullptr);
		init();
		ent.init();
		for (;;) {
			int c = sin.get();
			if (c == EOF) break;
			processByte<false>(sout, c);
			if (c == kEOFChar) {
				ent.encodeBit(sout, 0);
			}
			update(c);
		}
		processByte<false>(sout, kEOFChar);
		ent.encodeBit(sout, 1);
		ent.flush(sout);
	}

	void decompress(Stream* in_stream, Stream* out_stream) {
		BufferedStreamReader<4 * KB> sin(in_stream);
		BufferedStreamWriter<4 * KB> sout(out_stream);
		assert(in_stream != nullptr);
		assert(out_stream != nullptr);
		init();
		ent.initDecoder(sin);
		for (;;) {
			int c = processByte<true>(sin);
			if (c == kEOFChar) {
				if (ent.decodeBit(sin) == 1) {
					break;
				}
			}
			sout.put(c);
			update(c);
		}
	}	
};

#endif
