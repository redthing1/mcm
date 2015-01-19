#ifndef _WORD_MODEL_HPP_
#define _WORD_MODEL_HPP_

#include "UTF8.hpp"

class WordModel {
public:
	// Hashes.
	uint32_t prev;
	uint32_t h1, h2;

	// UTF decoder.
	UTF8Decoder<false> decoder;

	// Length of the model.
	uint32_t len;

	// Transform table.
	static const uint32_t transform_table_size = 256;
	uint32_t transform[transform_table_size];

	uint32_t opt_var;
	void setOpt(uint32_t n) {
		opt_var = n;
	}

	forceinline uint32_t& trans(char c) {
		uint32_t index = (uint32_t)(uint8_t)c;
		check(index < transform_table_size);
		return transform[index];
	}

	WordModel() : opt_var(0) { 
	}

	void init() {
		uint32_t index = 0;
		for (auto& t : transform) t = transform_table_size;
		for (uint32_t i = 'a'; i <= 'z'; ++i) {
			transform[i] = index++;
		}
		for (uint32_t i = 'A'; i <= 'Z'; ++i) {
			transform[i] = transform[(byte)lower_case((char)i)];
		}
#if 0
		for (uint32_t i = '0'; i <= '9'; ++i) {
			transform[i] = index++;
		}
#endif
		// 34 38
		trans('"') = index++;
		trans('&') = index++;
		trans('<') = index++;
		trans('{') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;

		trans('�') = trans('�') = index++;

		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;

		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;

		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;

		trans('�') = index++;
		trans('�') = index++;
		trans('�') = trans('�') = index++;

		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;

		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
			
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;
		trans('�') = trans('�') = index++;

		len = 0;
		prev = 0;
		reset();
		decoder.init();
	}

	forceinline void reset() {
		h1 = 0x1F20239A;
		h2 = 0xBE5FD47A;
		len = 0;
	}

	forceinline uint32_t getHash() const {
		return h1 + h2;
	}

	forceinline uint32_t getPrevHash() const {
		return prev;
	}

	forceinline uint32_t getLength() const {
		return len;
	}

	void update(uint8_t c) {
		uint32_t cur = transform[c];
		if (cur != transform_table_size || (cur >= 128 && cur != transform_table_size)) {
			h1 = (h1 + cur) * 54;
			h2 = h1 >> 7;
			++len;
		} else {
			prev = rotate_left(getHash(), 13);
			reset();
		}
	}

	forceinline uint32_t hashFunc(uint32_t c, uint32_t h) {
		h *= 61;
		h += c;
		h += rotate_left(h, 10);
		return h ^ (h >> 8);
	}

	void updateUTF(char c) {
		decoder.update(c);
		uint32_t cur = decoder.getAcc();
		if (decoder.done()) {
			if (cur < 256) cur = transform[cur];
			if (LIKELY(cur != transform_table_size)) {
				h1 = hashFunc(cur, h1);
				h2 = h1 * 8;
				++len;
			} else {
				if (len) {
					prev = rotate_left(getHash(), 13);
					reset();
				}
				return;
			}
		} else {
			h2 = hashFunc(cur, h2);
		}
	}
};

#endif