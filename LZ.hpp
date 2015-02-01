/*	MCM file compressor

	Copyright (C) 2013, Google Inc.
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

#ifndef _LZ_HPP_
#define _LZ_HPP_

#include "BoundedQueue.hpp"
#include "CyclicBuffer.hpp"
#include "Model.hpp"
#include "Range.hpp"

class Match {
public:
	forceinline Match() {
		reset();

	}
	forceinline void reset() {
		dist_ = 0;
		length_ = 0;
	}
	forceinline size_t getDist() const {
		return dist_;
	}
	forceinline void setDist(size_t dist) {
		dist_ = dist;
	}
	forceinline size_t getLength() const {
		return length_;
	}
	forceinline void setLength(size_t length) {
		length_ = length;
	}

private:
	size_t dist_;
	size_t length_;
};

class MatchEncoder {
public:
	virtual void encodeMatch(const Match& match) = 0;
};

class MatchFinder {
public:
	virtual Match findNextMatch() = 0;
	virtual size_t getNonMatchLen() const = 0;
	virtual bool done() const = 0;
};

class LZ {
	BoundedQueue<byte> lookahead;
	CyclicBuffer<byte> buffer;

	void tryMatch(uint32_t& pos, uint32_t& len) {
		
	}

	void update(byte c) {
		buffer.push(c);
	}
public:
	static const uint32_t version = 0;
	void setMemUsage(uint32_t n) {}

	typedef safeBitModel<unsigned int, 12> BitModel;
	typedef bitContextModel<BitModel, 1 << 8> CtxModel;

	Range7 ent;
	CtxModel mdl;

	void init() {
		mdl.init();
	}

	template <typename TOut, typename TIn>
	uint32_t Compress(TOut& sout, TIn& sin) {
		init();
		ent.init();
		for (;;) {
			int c = sin.read();
			if (c == EOF) break;
			mdl.encode(ent, sout, c);
		}
		ent.flush(sout);
		return (uint32_t)sout.getTotal();
	}

	template <typename TOut, typename TIn>
	bool DeCompress(TOut& sout, TIn& sin) {
		init();
		ent.initDecoder(sin);
		for (;;) {
			int c = mdl.decode(ent, sin);
			if (sin.eof()) break;
			sout.write(c);
		}
		return true;
	}
};

class MemoryLZ : public MemoryCompressor {
public:
	// Assumes 8 bytes buffer overrun possible per run.
	size_t getMatchLen(byte* m1, byte* m2, byte* limit1);
};

// variable order rolz.
class VRolz {
	static const uint32_t kMinMatch = 2U;
	static const uint32_t kMaxMatch = 0xFU + kMinMatch;
public:
	template<uint32_t kSize>
	class RolzTable {
	public:
		RolzTable() {
			init();
		}

		void init() {
			pos_ = 0;
			for (auto& s : slots_) {
				s = 0;
			}
		}

		forceinline uint32_t add(uint32_t pos) {
			uint32_t old = slots_[pos_];
			if (++pos_ == kSize) {
				pos_ = 0;
			}
			slots_[pos_] = pos;
			return old;
		}

		forceinline uint32_t operator[](uint32_t index) {
			return slots_[index];
		}

		forceinline uint32_t size() {
			return kSize;
		}

	private:
		uint32_t pos_;
		uint32_t slots_[kSize];
	};

	RolzTable<16> order1[0x100];
	RolzTable<16> order2[0x10000];

	void addHash(byte* in, uint32_t pos, uint32_t prev);
	uint32_t getMatchLen(byte* m1, byte* m2, uint32_t max_len);
	virtual size_t getMaxExpansion(size_t in_size);
	virtual size_t compressBytes(byte* in, byte* out, size_t count);
	virtual void decompressBytes(byte* in, byte* out, size_t count);
};

class MemoryMatchFinder : public MatchFinder {
public:
	forceinline virtual bool done() const {
		return in_ >= limit_;
	}
	forceinline size_t getNonMatchLen() const {
		return non_match_len_;
	}
	forceinline const byte* getNonMatchPtr() const {
		return non_match_ptr_;
	}
	forceinline const byte* getLimit() const {
		return limit_;
	}
	void backtrack(size_t len) {
		in_ptr_ -= len;
	}
	void init(byte* in, const byte* limit);
	
protected:
	const uint8_t* in_;
	const uint8_t* in_ptr_;
	const uint8_t* limit_;
	const uint8_t* non_match_ptr_;
	size_t non_match_len_;
};

class GreedyMatchFinder : public MemoryMatchFinder {
public:
	uint32_t opt;

	void init(byte* in, const byte* limit);
	GreedyMatchFinder();
	Match findNextMatch();
	forceinline hash_t hashFunc(uint32_t a, hash_t b) {
		b += a;
		b += rotate_left(b, 6);
		return b ^ (b >> 23);
	}

private:
	static const uint32_t kMinMatch = 4;
	static const uint32_t kMaxDist = 0xFFFF;
	// This is probably not very efficient.
	class Entry {
	public:
		Entry() {
			init();
		}
		void init() {
			pos_ = std::numeric_limits<uint32_t>::max() - kMaxDist * 2;
			hash_ = 0;
		}
		forceinline static uint32_t getHash(uint32_t word, uint32_t slot) {
			return slot | (word & ~0xFFU);
		}
		forceinline static uint32_t getLen(uint32_t word) {
			return word & 0xFF;
		}
		forceinline static uint32_t buildWord(uint32_t h, uint32_t len) {
			return (h & ~0xFFU) | len;
		}
		forceinline void setHash(uint32_t h) {
			hash_ = h;
		}

		uint32_t pos_;
		uint32_t hash_;
	};
	;
	std::vector<Entry> hash_storage_;
	uint32_t hash_mask_;
	Entry* hash_table_;
};

class FastMatchFinder : public MemoryMatchFinder {
public:
	void init(byte* in, const byte* limit);
	FastMatchFinder();
	Match findNextMatch() {
		non_match_ptr_ = in_ptr_;
		const byte* match_ptr = nullptr;
		auto lookahead = *reinterpret_cast<const uint32_t*>(in_ptr_);
		size_t dist;
		while (true) {
			const uint32_t hash_index = (lookahead * 190807U) >> 16;
			// const uint32_t hash_index = (lookahead * 2654435761U) >> 20;
			assert(hash_index <= hash_mask_);
			match_ptr = in_ + hash_table_[hash_index];
			hash_table_[hash_index] = static_cast<uint32_t>(in_ptr_ - in_);
			dist = in_ptr_ - match_ptr;
			if (dist - kMinMatch <= kMaxDist - kMinMatch) {
				if (*reinterpret_cast<const uint32_t*>(match_ptr) == lookahead) {
					break;
				}
			}
			lookahead = (lookahead >> 8) | (static_cast<uint32_t>(*(in_ptr_ + sizeof(lookahead))) << 24);
			if (++in_ptr_ >= limit_) {
				// assert(in_ptr_ == limit_);
				non_match_len_ = in_ptr_ - non_match_ptr_;
				return Match();
			}
		}
		non_match_len_ = in_ptr_ - non_match_ptr_;
		// Improve the match.
		size_t len = sizeof(lookahead);
		while (len < dist) {
			typedef unsigned long ulong;
			auto diff = *reinterpret_cast<const ulong*>(in_ptr_ + len) ^ *reinterpret_cast<const ulong*>(match_ptr + len);
			if (UNLIKELY(diff)) {
				ulong idx = 0;
#ifdef WIN32
				// TODO
				_BitScanForward(&idx, diff);
#endif
				len += idx >> 3;
				break;
			}
			len += sizeof(diff);
		}
		len = std::min(len, dist);
		len = std::min(len, static_cast<size_t>(limit_ - in_ptr_));
		assert(match_ptr + len <= in_ptr_);
		assert(in_ptr_ + len <= limit_);
		// Verify match.
		for (uint32_t i = 0; i < len; ++i) {
			assert(in_ptr_[i] == match_ptr[i]);
		}
		Match match;
		match.setDist(dist);
		match.setLength(len);
		in_ptr_ += len;
		return match;
	}

private:
	static const uint32_t kMaxDist = 0xFFFF;
	static const uint32_t kMatchCount = 1;
	static const uint32_t kMinMatch = 4;

	std::vector<uint32_t> hash_storage_;
	uint32_t hash_mask_;
	uint32_t* hash_table_;
};

class LZFast : public MemoryLZ {
public:
	uint32_t opt;
	LZFast() : opt(0) {
	}
	virtual void setOpt(uint32_t new_opt) {
		opt = new_opt;
	}
	byte* flushNonMatch(byte* out_ptr, const byte* in_ptr, size_t non_match_len);
	virtual size_t getMaxExpansion(size_t in_size);
	virtual size_t compressBytes(byte* in, byte* out, size_t count);
	virtual void decompressBytes(byte* in, byte* out, size_t count);
	template<uint32_t pos>
	ALWAYS_INLINE static byte* processMatch(byte matches, uint32_t lengths, byte* out, byte** in);

private:
	static const bool kCountMatches = true;
	GreedyMatchFinder match_finder_;
	std::vector<uint32_t> non_matches_;
	// Match format:
	// <byte> top bit = set -> match
	// match len = low bits
	static const size_t kMatchShift = 7;
	static const size_t kMatchFlag = 1U << kMatchShift;
	static const size_t kMinMatch = 4;
	// static const uint32_t kMaxMatch = (0xFF ^ kMatchFlag) + kMinMatch;
	static const size_t kMinNonMatch = 1;
#if 1
	static const size_t kMaxMatch = 0x7F + kMinMatch;
	static const size_t kMaxNonMatch = 0x7F + kMinNonMatch;
#elif 0
	static const size_t kMaxMatch = 16; // 0xF + kMinMatch;
	static const size_t kMaxNonMatch = 16;
#else
	static const size_t kMaxMatch = 16 + kMinMatch;
	static const size_t kMaxNonMatch = 16 + kMinNonMatch;
#endif
	static const size_t non_match_bits = 2;
	static const size_t extra_match_bits = 5;
};

class LZ4 : public MemoryCompressor {
public:
	virtual uint32_t getMaxExpansion(uint32_t in_size);
	virtual uint32_t compressBytes(byte* in, byte* out, uint32_t count);
	virtual void decompressBytes(byte* in, byte* out, uint32_t count);
};

#endif
