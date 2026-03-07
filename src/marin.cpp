/*
Copyright 2025, Yves Gallot

marin is free source code. You can redistribute, use and/or modify it.
Please give feedback to the authors if improvement is realized. It is distributed in the hope that it will be useful.
*/

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <cstring>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <signal.h>
#endif

#if defined(GPU)
#include "ocl.h"
#endif
#include "mersenne.h"
#include "engine.h"

class application
{
private:
	struct deleter { void operator()(const application * const p) { delete p; } };

	struct zint
	{
		mpz_t v;
		zint() { mpz_init(v); }
		zint(const zint & other) { mpz_init_set(v, other.v); }
		zint & operator=(const zint & other) { if (this != &other) mpz_set(v, other.v); return *this; }
		~zint() { mpz_clear(v); }
	};

private:
	static void quit(int)
	{
		Mersenne::get_instance().quit();
	}

private:
#if defined(_WIN32)
	static BOOL WINAPI HandlerRoutine(DWORD)
	{
		quit(1);
		return TRUE;
	}
#endif

public:
	application()
	{
#if defined(_WIN32)
		SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#else
		signal(SIGTERM, quit);
		signal(SIGINT, quit);
#endif
	}

	virtual ~application() {}

	static application & get_instance()
	{
		static std::unique_ptr<application, deleter> instance(new application());
		return *instance;
	}

private:
	static std::string header(const std::vector<std::string> & args)
	{
		const char * const sys_ver =
#if defined(_WIN64)
			"win64";
#elif defined(_WIN32)
			"win32";
#elif defined(__linux__)
#if defined(__x86_64)
			"linux x64";
#elif defined(__aarch64__)
			"linux arm64";
#else
			"linux x86";
#endif
#elif defined(__APPLE__)
#if defined(__aarch64__)
			"macOS arm64";
#else
			"macOS x64";
#endif
#else
			"unknown";
#endif

		std::ostringstream comp_ver;
#if defined(__clang__)
		comp_ver << ", clang-" << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
#elif defined(__GNUC__)
		comp_ver << ", gcc-" << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
#endif

		const char * const ext =
#if defined(GPU)
			"";
#else
			"_cpu";
#endif

		std::ostringstream ss;
		ss << "marin" << ext << " version 25.09.1 (" << sys_ver << comp_ver.str() << ")" << std::endl;
		ss << "Copyright (c) 2025, Yves Gallot" << std::endl;
		ss << "marin is free source code, under the MIT license." << std::endl;
		ss << std::endl << "Command line: '";
		bool first = true;
		for (const std::string & arg : args)
		{
			if (first) first = false; else ss << " ";
			ss << arg;
		}
		ss << "'" << std::endl;
		return ss.str();
	}

	static uint64_t env_u64(const char * const name, const uint64_t default_value)
	{
		const char * const s = std::getenv(name);
		if ((s == nullptr) || (*s == '\0')) return default_value;
		char * end = nullptr;
		const unsigned long long v = std::strtoull(s, &end, 10);
		return ((end != nullptr) && (*end == '\0')) ? uint64_t(v) : default_value;
	}

	static bool env_flag(const char * const name)
	{
		const char * const s = std::getenv(name);
		if (s == nullptr) return false;
		const std::string v(s);
		return (v == "1") || (v == "true") || (v == "TRUE") || (v == "yes") || (v == "YES") || (v == "on") || (v == "ON");
	}

	static std::string mpz_hex_prefix(const mpz_t & z, const size_t max_chars = 64)
	{
		char * raw = mpz_get_str(nullptr, 16, z);
		std::string s = (raw != nullptr) ? std::string(raw) : std::string("<null>");
		void (*freefunc)(void *, size_t);
		mp_get_memory_functions(nullptr, nullptr, &freefunc);
		if (raw != nullptr) freefunc(raw, std::strlen(raw) + 1);
		if (s.size() > max_chars) s = s.substr(0, max_chars) + "...";
		return s;
	}

	static std::string mpz_hex_edge(const mpz_t & z, const size_t edge = 24)
	{
		char * raw = mpz_get_str(nullptr, 16, z);
		std::string s = (raw != nullptr) ? std::string(raw) : std::string("<null>");
		void (*freefunc)(void *, size_t);
		mp_get_memory_functions(nullptr, nullptr, &freefunc);
		if (raw != nullptr) freefunc(raw, std::strlen(raw) + 1);

		if (s.size() <= 2 * edge) return s;
		return s.substr(0, edge) + "..." + s.substr(s.size() - edge);
	}
	static void print_diff_window(const mpz_t & got, const mpz_t & expect)
	{
		mpz_t diff;
		mpz_init(diff);
		mpz_xor(diff, got, expect);

		if (mpz_sgn(diff) == 0)
		{
			std::cerr << "[SELFTEST] no diff\n";
			mpz_clear(diff);
			return;
		}

		const size_t bit = mpz_scan1(diff, 0);          // premier bit différent (LSB)
		const size_t hi  = mpz_sizeinbase(diff, 2) - 1; // bit différent le plus haut

		std::cerr << "[SELFTEST] first_diff_bit=" << bit
				<< " highest_diff_bit=" << hi << '\n';

		mpz_clear(diff);
	}
	static std::string mpz_hex_window_around_bit(const mpz_t & z, const size_t bit, const size_t span_hex = 16)
	{
		char * raw = mpz_get_str(nullptr, 16, z);
		std::string s = (raw != nullptr) ? std::string(raw) : std::string("<null>");
		void (*freefunc)(void *, size_t);
		mp_get_memory_functions(nullptr, nullptr, &freefunc);
		if (raw != nullptr) freefunc(raw, std::strlen(raw) + 1);

		if (s == "<null>") return s;

		const size_t total_hex = s.size();
		const size_t hex_from_right = bit / 4;
		const size_t pos = (hex_from_right < total_hex) ? (total_hex - 1 - hex_from_right) : 0;

		const size_t begin = (pos > span_hex) ? (pos - span_hex) : 0;
		const size_t end = std::min(total_hex, pos + span_hex + 1);

		return s.substr(begin, end - begin);
	}
	static void print_mismatch(const char * const test_name, const char * const op,
		const size_t iter, const size_t step, const size_t reg,
		const mpz_t & got, const mpz_t & expect)
	{
		mpz_t diff;
		mpz_init(diff);
		mpz_xor(diff, got, expect);

		const size_t got_bits = mpz_sizeinbase(got, 2);
		const size_t exp_bits = mpz_sizeinbase(expect, 2);
		const size_t diff_bits = (mpz_sgn(diff) == 0) ? 0 : mpz_sizeinbase(diff, 2);
		const long msb_diff = (mpz_sgn(diff) == 0) ? -1 : long(diff_bits - 1);

		std::cerr << "[SELFTEST][" << test_name << "] MISMATCH iter=" << iter;
		if (step != 0) std::cerr << " step=" << step;
		std::cerr << " op=" << op << " reg=R" << reg << '\n';

		std::cerr << "[SELFTEST] got    = 0x" << mpz_hex_edge(got) << '\n';
		std::cerr << "[SELFTEST] expect = 0x" << mpz_hex_edge(expect) << '\n';
		std::cerr << "[SELFTEST] xor    = 0x" << mpz_hex_edge(diff) << '\n';
		std::cerr << "[SELFTEST] got_bits=" << got_bits
				<< " expect_bits=" << exp_bits
				<< " diff_bits=" << diff_bits
				<< " msb_diff=" << msb_diff << '\n';

		print_diff_window(got, expect);

		//mpz_t diff;
		mpz_init(diff);
		mpz_xor(diff, got, expect);
		if (mpz_sgn(diff) != 0)
		{
			const size_t hi = mpz_sizeinbase(diff, 2) - 1;
			std::cerr << "[SELFTEST] got@diff    = 0x" << mpz_hex_window_around_bit(got, hi) << '\n';
			std::cerr << "[SELFTEST] expect@diff = 0x" << mpz_hex_window_around_bit(expect, hi) << '\n';
		}
		mpz_clear(diff);
	}

	static void mod_norm(mpz_t & rop, const mpz_t & op, const mpz_t & mod)
	{
		mpz_mod(rop, op, mod);
		if (mpz_sgn(rop) < 0) mpz_add(rop, rop, mod);
	}

	static void addm(mpz_t & rop, const mpz_t & a, const mpz_t & b, const mpz_t & mod)
	{
		mpz_add(rop, a, b);
		mod_norm(rop, rop, mod);
	}

	static void subm(mpz_t & rop, const mpz_t & a, const mpz_t & b, const mpz_t & mod)
	{
		mpz_sub(rop, a, b);
		mod_norm(rop, rop, mod);
	}

	static void mulm(mpz_t & rop, const mpz_t & a, const mpz_t & b, const mpz_t & mod)
	{
		mpz_mul(rop, a, b);
		mod_norm(rop, rop, mod);
	}

	static void sqrm(mpz_t & rop, const mpz_t & a, const mpz_t & mod)
	{
		mpz_mul(rop, a, a);
		mod_norm(rop, rop, mod);
	}

	static void random_mod(mpz_t & rop, gmp_randstate_t state, const unsigned long bits, const mpz_t & mod, const uint64_t salt)
	{
		switch (salt % 10)
		{
			case 0: mpz_set_ui(rop, 0); return;
			case 1: mpz_set_ui(rop, 1); return;
			case 2: mpz_set_ui(rop, 2); return;
			case 3: mpz_sub_ui(rop, mod, 1); return;
			case 4:
			{
				mpz_set_ui(rop, 0);
				const unsigned long b = (bits == 0) ? 0ul : static_cast<unsigned long>(salt % bits);
				mpz_setbit(rop, mp_bitcnt_t(b));
				mod_norm(rop, rop, mod);
				return;
			}
			default:
				mpz_urandomb(rop, state, mp_bitcnt_t(std::max(1ul, bits)));
				mod_norm(rop, rop, mod);
				return;
		}
	}

	static void random_non_zero_mod(mpz_t & rop, gmp_randstate_t state, const unsigned long bits, const mpz_t & mod, const uint64_t salt)
	{
		random_mod(rop, state, bits, mod, salt);
		if (mpz_sgn(rop) == 0) mpz_set_ui(rop, 1);
	}

	static void read_reg(engine * const eng, const engine::Reg reg, mpz_t & out, const mpz_t & mod)
	{
		eng->get_mpz(out, reg);
		mod_norm(out, out, mod);
	}

	static void write_reg(engine * const eng, const engine::Reg reg, const mpz_t & value)
	{
		mpz_t tmp;
		mpz_init_set(tmp, value);
		eng->set_mpz(reg, tmp);
		mpz_clear(tmp);
	}

	static void require_equal(engine * const eng, const engine::Reg reg, const mpz_t & expect,
		const mpz_t & mod, const char * const test_name, const char * const op,
		const size_t iter, const size_t step)
	{
		mpz_t got;
		mpz_init(got);
		read_reg(eng, reg, got, mod);
		if (mpz_cmp(got, expect) != 0)
		{
			print_mismatch(test_name, op, iter, step, size_t(reg), got, expect);
			mpz_clear(got);
			throw std::runtime_error("engine self-test failed");
		}
		mpz_clear(got);
	}

	static void set_expect_and_reg(engine * const eng, const engine::Reg reg, const mpz_t & value,
		std::vector<zint> & expect, const mpz_t & mod)
	{
		mod_norm(expect[size_t(reg)].v, value, mod);
		write_reg(eng, reg, expect[size_t(reg)].v);
	}

	static void prepare_mul(engine * const eng, const engine::Reg dst, const engine::Reg src,
		std::vector<zint> & expect)
	{
		eng->set_multiplicand(dst, src);
		mpz_set(expect[size_t(dst)].v, expect[size_t(src)].v);
	}

	static std::string usage()
	{
		const char * const ext =
#if defined(GPU)
			"";
#else
			"_cpu";
#endif
		std::ostringstream ss;
		ss << "Usage: marin" << ext << " [options]  options may be specified in any order" << std::endl;
		ss << "  -p <p>         exponent of the Mersenne number 2^p - 1 (7 <= p <= 1509949421)" << std::endl;
		ss << "  -LL            perform Lucas-Lehmer primality test" << std::endl;
#if defined(GPU)
		ss << "  -d <n>         set the device number (default 0)" << std::endl;
#endif
		ss << "  -h             validate hardware (quick Gerbicz-Li error checking for each size)" << std::endl;
		ss << "  -T             run all deep self-tests (basic + TE + Montgomery)" << std::endl;
		ss << "  -Tbasic        run basic arithmetic engine self-test against GMP" << std::endl;
		ss << "  -Tte           run Twisted Edwards ECM-like sequence self-test" << std::endl;
		ss << "  -Tmont         run Montgomery xDBLADD ECM-like sequence self-test" << std::endl;
		ss << "Environment:" << std::endl;
		ss << "  MARIN_ENGINE_SELFTEST=1         enable all self-tests without -T" << std::endl;
		ss << "  MARIN_ENGINE_SELFTEST_ITERS=N   iterations for each test (default 200 basic, 50 seq)" << std::endl;
		ss << "  MARIN_ENGINE_SELFTEST_SEED=N    RNG seed (default 1)" << std::endl;
		ss << "  MARIN_ENGINE_SELFTEST_CHAIN=N   chained TE/Montgomery steps per iteration (default 32)" << std::endl;
		return ss.str();
	}

	static std::unique_ptr<engine> create_engine_checked(const uint32_t p, const size_t reg_count, const size_t device)
	{
		std::unique_ptr<engine> eng(
#if defined(GPU)
			engine::create_gpu(p, reg_count, device, true)
#else
			engine::create_cpu(p, reg_count)
#endif
		);
		if (!eng) throw std::runtime_error("cannot create engine.");
		return eng;
	}

	static void selftest_basic(const uint32_t p, const size_t device)
	{
		if (p == 0) throw std::runtime_error("self-test requires -p <prime exponent>.");
		const uint64_t seed = env_u64("MARIN_ENGINE_SELFTEST_SEED", 1);
		const size_t iterations = size_t(env_u64("MARIN_ENGINE_SELFTEST_ITERS", 200));
		const size_t reg_count = 32;
		std::unique_ptr<engine> eng = create_engine_checked(p, reg_count, device);

		std::cout << "[SELFTEST][basic] p=" << p << " transform_size=" << eng->get_size()
			<< " iterations=" << iterations << " seed=" << seed << std::endl;

		gmp_randstate_t rs;
		gmp_randinit_mt(rs);
		gmp_randseed_ui(rs, static_cast<unsigned long>(seed));

		mpz_t Mp;
		mpz_init(Mp);
		mpz_ui_pow_ui(Mp, 2u, p);
		mpz_sub_ui(Mp, Mp, 1u);

		mpz_t A, B, C, D, E, F, got;
		mpz_inits(A, B, C, D, E, F, got, nullptr);

		const engine::Reg R0  = 0;
		const engine::Reg R1  = 1;
		const engine::Reg R2  = 2;
		const engine::Reg R3  = 3;
		const engine::Reg R4  = 4;
		const engine::Reg R5  = 5;
		const engine::Reg R6  = 6;
		const engine::Reg R7  = 7;
		const engine::Reg R8  = 8;
		const engine::Reg R9  = 9;
		const engine::Reg R10 = 10;
		const engine::Reg R11 = 11;

		std::vector<char> blob(eng->get_register_data_size());
		std::vector<char> ckpt(eng->get_checkpoint_size());

		for (size_t iter = 1; iter <= iterations; ++iter)
		{
			random_mod(A, rs, p, Mp, seed + 11 * iter + 0);
			random_mod(B, rs, p, Mp, seed + 11 * iter + 1);
			random_mod(C, rs, p, Mp, seed + 11 * iter + 2);
			random_mod(D, rs, p, Mp, seed + 11 * iter + 3);

			const uint32_t k1 = uint32_t(((seed + iter * 17u) % 97u) + 1u);
			const uint32_t k2 = uint32_t(((seed + iter * 29u) % 193u) + 1u);
			const uint32_t k3 = uint32_t(((seed + iter * 43u) % 251u) + 1u);

			write_reg(eng.get(), R0, A);
			write_reg(eng.get(), R1, B);
			write_reg(eng.get(), R2, C);
			write_reg(eng.get(), R3, D);

			read_reg(eng.get(), R0, got, Mp);
			if (mpz_cmp(got, A) != 0) { print_mismatch("basic", "roundtrip A", iter, 0, R0, got, A); throw std::runtime_error("engine self-test failed"); }
			read_reg(eng.get(), R1, got, Mp);
			if (mpz_cmp(got, B) != 0) { print_mismatch("basic", "roundtrip B", iter, 0, R1, got, B); throw std::runtime_error("engine self-test failed"); }

			eng->copy(R4, R0);
			require_equal(eng.get(), R4, A, Mp, "basic", "copy", iter, 0);

			eng->copy(R4, R0);
			eng->add(R4, R1);
			addm(E, A, B, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "add", iter, 0);

			eng->copy(R4, R0);
			eng->sub_reg(R4, R1);
			subm(E, A, B, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "sub_reg", iter, 0);

			eng->copy(R4, R0);
			eng->sub(R4, k1);
			mpz_sub_ui(E, A, k1);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "sub_const", iter, 0);

			eng->addsub(R4, R5, R0, R1);
			addm(E, A, B, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "addsub sum", iter, 0);
			subm(E, A, B, Mp);
			require_equal(eng.get(), R5, E, Mp, "basic", "addsub diff", iter, 0);

			eng->addsub_copy(R4, R5, R6, R7, R0, R1);
			addm(E, A, B, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "addsub_copy sum", iter, 0);
			require_equal(eng.get(), R6, E, Mp, "basic", "addsub_copy sum_copy", iter, 0);
			subm(E, A, B, Mp);
			require_equal(eng.get(), R5, E, Mp, "basic", "addsub_copy diff", iter, 0);
			require_equal(eng.get(), R7, E, Mp, "basic", "addsub_copy diff_copy", iter, 0);

			eng->copy(R4, R0);
			eng->square_mul(R4, k2);
			sqrm(E, A, Mp);
			if (k2 != 1u)
			{
				mpz_mul_ui(E, E, k2);
				mod_norm(E, E, Mp);
			}
			require_equal(eng.get(), R4, E, Mp, "basic", "square_mul", iter, 0);

			eng->copy(R4, R0);
			eng->square_mul_copy(R4, R5, k3);
			sqrm(E, A, Mp);
			if (k3 != 1u)
			{
				mpz_mul_ui(E, E, k3);
				mod_norm(E, E, Mp);
			}
			require_equal(eng.get(), R4, E, Mp, "basic", "square_mul_copy src", iter, 0);
			require_equal(eng.get(), R5, E, Mp, "basic", "square_mul_copy copy", iter, 0);

			eng->copy(R4, R0);
			eng->set_multiplicand(R11, R1);
			eng->mul(R4, R11);
			mulm(E, A, B, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "set_multiplicand+mul", iter, 0);

			eng->copy(R4, R0);
			eng->set_multiplicand(R11, R1);
			eng->mul_copy(R4, R11, R5);
			mulm(E, A, B, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "mul_copy dst", iter, 0);
			require_equal(eng.get(), R5, E, Mp, "basic", "mul_copy copy", iter, 0);

			eng->copy(R4, R0);
			eng->set_multiplicand(R11, R1);
			eng->mul_add(R4, R11, R2);
			mulm(E, A, B, Mp);
			addm(E, E, C, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "mul_add", iter, 0);

			/*eng->copy(R4, R0);
			eng->copy(R5, R2);
			eng->mul_pair_unit(R4, R1, R5, R3);
			mulm(E, A, B, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "mul_pair_unit dst0", iter, 0);
			mulm(E, C, D, Mp);
			require_equal(eng.get(), R5, E, Mp, "basic", "mul_pair_unit dst1", iter, 0);*/

			eng->copy(R4, R0);
			eng->copy(R5, R2);
			eng->copy(R8, R1);
			eng->copy(R9, R3);
			eng->mul_pair_unit(R4, R8, R5, R9);
			mulm(E, A, B, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "mul_pair_unit dst0", iter, 0);
			mulm(E, C, D, Mp);
			require_equal(eng.get(), R5, E, Mp, "basic", "mul_pair_unit dst1", iter, 0);

			eng->copy(R4, R0);
			eng->copy(R5, R2);
			eng->set_multiplicand(R8, R1);
			eng->set_multiplicand(R9, R3);
			eng->mul_pair_prepared(R4, R8, R5, R9);
			mulm(E, A, B, Mp);
			require_equal(eng.get(), R4, E, Mp, "basic", "mul_pair_prepared dst0", iter, 0);
			mulm(E, C, D, Mp);
			require_equal(eng.get(), R5, E, Mp, "basic", "mul_pair_prepared dst1", iter, 0);
			if (!eng->get_data(blob, R0)) throw std::runtime_error("get_data failed");
			eng->set(R10, uint32_t(0));
			if (!eng->set_data(R10, blob)) throw std::runtime_error("set_data failed");
			require_equal(eng.get(), R10, A, Mp, "basic", "data roundtrip", iter, 0);

			eng->copy(R10, R0);
			eng->copy(R11, R1);
			if (!eng->get_checkpoint(ckpt)) throw std::runtime_error("get_checkpoint failed");
			eng->set(R10, uint32_t(0));
			eng->set(R11, uint32_t(0));
			if (!eng->set_checkpoint(ckpt)) throw std::runtime_error("set_checkpoint failed");
			require_equal(eng.get(), R10, A, Mp, "basic", "checkpoint R10", iter, 0);
			require_equal(eng.get(), R11, B, Mp, "basic", "checkpoint R11", iter, 0);

			if ((iter <= 5) || (iter % 25 == 0) || (iter == iterations))
			{
				std::cout << "[SELFTEST][basic] iter " << iter << "/" << iterations << " OK" << std::endl;
			}
		}

		mpz_clears(A, B, C, D, E, F, got, Mp, nullptr);
		gmp_randclear(rs);
		std::cout << "[SELFTEST][basic] all tests passed." << std::endl;
	}

	static void selftest_te_sequence(const uint32_t p, const size_t device)
	{
		if (p == 0) throw std::runtime_error("self-test requires -p <prime exponent>.");
		const uint64_t seed = env_u64("MARIN_ENGINE_SELFTEST_SEED", 1);
		const size_t iterations = size_t(env_u64("MARIN_ENGINE_SELFTEST_ITERS", 50));
		const size_t chain = size_t(env_u64("MARIN_ENGINE_SELFTEST_CHAIN", 32));
		const size_t reg_count = 64;
		std::unique_ptr<engine> eng = create_engine_checked(p, reg_count, device);

		std::cout << "[SELFTEST][te] p=" << p << " transform_size=" << eng->get_size()
			<< " iterations=" << iterations << " chain=" << chain << " seed=" << seed << std::endl;

		gmp_randstate_t rs;
		gmp_randinit_mt(rs);
		gmp_randseed_ui(rs, static_cast<unsigned long>(seed ^ 0x9E3779B9u));

		mpz_t Mp;
		mpz_init(Mp);
		mpz_ui_pow_ui(Mp, 2u, p);
		mpz_sub_ui(Mp, Mp, 1u);

		std::vector<zint> R(64);
		std::vector<char> ckpt(eng->get_checkpoint_size());
		mpz_t tmp0, tmp1, tmp2, tmp3, tmp4;
		mpz_inits(tmp0, tmp1, tmp2, tmp3, tmp4, nullptr);

		for (size_t iter = 1; iter <= iterations; ++iter)
		{
			for (size_t i = 0; i < R.size(); ++i) mpz_set_ui(R[i].v, 0);

			random_non_zero_mod(tmp0, rs, p, Mp, seed + 101 * iter + 1); // a
			random_non_zero_mod(tmp1, rs, p, Mp, seed + 101 * iter + 2); // d
			random_non_zero_mod(tmp2, rs, p, Mp, seed + 101 * iter + 3); // X1
			random_non_zero_mod(tmp3, rs, p, Mp, seed + 101 * iter + 4); // Y1
			random_non_zero_mod(tmp4, rs, p, Mp, seed + 101 * iter + 5); // X2
			set_expect_and_reg(eng.get(), 16, tmp0, R, Mp);
			set_expect_and_reg(eng.get(), 29, tmp1, R, Mp);
			set_expect_and_reg(eng.get(), 3, tmp2, R, Mp);
			set_expect_and_reg(eng.get(), 4, tmp3, R, Mp);
			eng->set(1, 1u); mpz_set_ui(R[1].v, 1u);
			mulm(tmp0, tmp2, tmp3, Mp); set_expect_and_reg(eng.get(), 5, tmp0, R, Mp); // T1 = X1*Y1
			set_expect_and_reg(eng.get(), 6, tmp4, R, Mp);
			random_non_zero_mod(tmp4, rs, p, Mp, seed + 101 * iter + 6); // Y2
			set_expect_and_reg(eng.get(), 7, tmp4, R, Mp);
			mulm(tmp0, R[6].v, R[7].v, Mp); set_expect_and_reg(eng.get(), 9, tmp0, R, Mp); // T2 = X2*Y2

			//prepare_mul_and_check(eng.get(), 43, 16, R, Mp, "te", "prepare a", iter, 0);
			//prepare_mul_and_check(eng.get(), 45, 29, R, Mp, "te", "prepare d", iter, 0);
			//prepare_mul_and_check(eng.get(), 46, 9,  R, Mp, "te", "prepare T2", iter, 0);
			prepare_mul(eng.get(), 43, 16, R);
			prepare_mul(eng.get(), 45, 29, R);
			prepare_mul(eng.get(), 46, 9,  R);
			if (!eng->get_checkpoint(ckpt)) throw std::runtime_error("get_checkpoint failed");

			auto tr_copy = [&](const size_t dst, const size_t src, const char * const op, const size_t step)
			{
				eng->copy(dst, src);
				mpz_set(R[dst].v, R[src].v);
				require_equal(eng.get(), dst, R[dst].v, Mp, "te", op, iter, step);
			};
			auto tr_add = [&](const size_t dst, const size_t src, const char * const op, const size_t step)
			{
				eng->add(dst, src);
				addm(R[dst].v, R[dst].v, R[src].v, Mp);
				require_equal(eng.get(), dst, R[dst].v, Mp, "te", op, iter, step);
			};
			auto tr_sub = [&](const size_t dst, const size_t src, const char * const op, const size_t step)
			{
				eng->sub_reg(dst, src);
				subm(R[dst].v, R[dst].v, R[src].v, Mp);
				require_equal(eng.get(), dst, R[dst].v, Mp, "te", op, iter, step);
			};
			auto tr_addsub = [&](const size_t sum, const size_t diff, const size_t a, const size_t b, const char * const op, const size_t step)
			{
				eng->addsub(sum, diff, a, b);
				addm(R[sum].v, R[a].v, R[b].v, Mp);
				subm(R[diff].v, R[a].v, R[b].v, Mp);
				require_equal(eng.get(), sum,  R[sum].v,  Mp, "te", op, iter, step);
				require_equal(eng.get(), diff, R[diff].v, Mp, "te", op, iter, step);
			};
			auto tr_mul = [&](const size_t dst, const size_t src, const char * const op, const size_t step)
			{
				eng->mul(dst, src);
				mulm(R[dst].v, R[dst].v, R[src].v, Mp);
				require_equal(eng.get(), dst, R[dst].v, Mp, "te", op, iter, step);
			};
			auto tr_square = [&](const size_t dst, const char * const op, const size_t step)
			{
				eng->square_mul(dst);
				sqrm(R[dst].v, R[dst].v, Mp);
				require_equal(eng.get(), dst, R[dst].v, Mp, "te", op, iter, step);
			};
			auto tr_mul_copy = [&](const size_t dst, const size_t src, const size_t dcopy, const char * const op, const size_t step)
			{
				eng->mul_copy(dst, src, dcopy);
				mulm(R[dst].v, R[dst].v, R[src].v, Mp);
				mpz_set(R[dcopy].v, R[dst].v);
				require_equal(eng.get(), dst, R[dst].v, Mp, "te", op, iter, step);
				require_equal(eng.get(), dcopy, R[dcopy].v, Mp, "te", op, iter, step);
			};
			auto tr_setmul = [&](const size_t dst, const size_t src, const char * const /*op*/, const size_t /*step*/)
			{
				prepare_mul(eng.get(), (engine::Reg)dst, (engine::Reg)src, R);
			};
			auto hadamard = [&](const size_t a, const size_t b, const size_t s, const size_t d, const char * const op, const size_t step)
			{
				tr_addsub(s, d, a, b, op, step);
			};

			auto eDBL_XYTZ = [&](const size_t RX, const size_t RY, const size_t RZ, const size_t RT, const size_t step)
			{
				tr_setmul(11, RZ, "eDBL setmul Z", step);
				tr_mul(RT, 11, "eDBL mul TZ", step);
				tr_add(RT, RT, "eDBL add E2", step);
				tr_square(RZ, "eDBL sqr Z", step);
				tr_add(RZ, RZ, "eDBL add C2", step);
				tr_square(RX, "eDBL sqr X", step);
				tr_square(RY, "eDBL sqr Y", step);
				tr_mul(RX, 43, "eDBL mul aA", step);
				hadamard(RX, RY, 23, 25, "eDBL hadamard GH", step);
				tr_copy(24, 23, "eDBL copy G", step);
				tr_sub(24, RZ, "eDBL sub F", step);
				tr_setmul(11, 24, "eDBL setmul F", step);
				tr_copy(RX, RT, "eDBL copy E->X", step);
				tr_mul(RX, 11, "eDBL mul XF", step);
				tr_copy(RZ, 23, "eDBL copy G->Z", step);
				tr_mul(RZ, 11, "eDBL mul ZF", step);
				tr_setmul(11, 25, "eDBL setmul H", step);
				tr_copy(RY, 23, "eDBL copy G->Y", step);
				tr_mul(RY, 11, "eDBL mul YH", step);
				tr_mul(RT, 11, "eDBL mul TH", step);
			};

			auto eADD_RP = [&](const size_t step)
			{
				tr_addsub(34, 35, 4, 3, "eADD addsub S1D1", step);
				tr_addsub(36, 37, 7, 6, "eADD addsub S2D2", step);
				tr_copy(30, 3, "eADD copy X1", step);
				tr_setmul(11, 6, "eADD setmul X2", step);
				tr_mul_copy(30, 11, 39, "eADD mul_copy X1X2", step);
				tr_copy(31, 4, "eADD copy Y1", step);
				tr_setmul(11, 7, "eADD setmul Y2", step);
				tr_mul(31, 11, "eADD mul Y1Y2", step);
				tr_copy(32, 5, "eADD copy T1", step);
				tr_mul(32, 46, "eADD mul T1T2", step);
				tr_mul(32, 45, "eADD mul dT1T2", step);
				hadamard(1, 32, 42, 41, "eADD hadamard DC", step);
				tr_copy(38, 34, "eADD copy S1", step);
				tr_setmul(11, 36, "eADD setmul S2", step);
				tr_mul(38, 11, "eADD mul S1S2", step);
				require_equal(eng.get(), 38, R[38].v, Mp, "te", "before sub X1X2", iter, step);
				require_equal(eng.get(), 30, R[30].v, Mp, "te", "check X1X2", iter, step);
				require_equal(eng.get(), 31, R[31].v, Mp, "te", "check Y1Y2", iter, step);
				//tr_sub(38, 30, "eADD sub X1X2", step);
				tr_sub(38, 30, "eADD sub X1X2", step);
				require_equal(eng.get(), 38, R[38].v, Mp, "te", "after sub X1X2", iter, step);
				//tr_sub(38, 31, "eADD sub Y1Y2", step);
				tr_sub(38, 31, "eADD sub Y1Y2", step);
				tr_mul(39, 43, "eADD mul aX1X2", step);
				tr_copy(40, 31, "eADD copy Y1Y2", step);
				tr_sub(40, 39, "eADD sub H", step);
				tr_copy(3, 38, "eADD copy X3", step);
				tr_setmul(11, 41, "eADD setmul D-C", step);
				tr_mul(3, 11, "eADD mul X3", step);
				tr_copy(1, 42, "eADD copy Z3", step);
				tr_mul(1, 11, "eADD mul Z3", step);
				tr_setmul(11, 40, "eADD setmul H", step);
				tr_copy(4, 42, "eADD copy Y3", step);
				tr_mul(4, 11, "eADD mul Y3", step);
				tr_copy(5, 38, "eADD copy T3", step);
				tr_mul(5, 11, "eADD mul T3", step);
			};

			for (size_t step = 1; step <= chain; ++step)
			{
				eDBL_XYTZ(3, 4, 1, 5, step);
				eADD_RP(step);
			}

			if ((iter <= 5) || (iter % 10 == 0) || (iter == iterations))
			{
				std::cout << "[SELFTEST][te] iter " << iter << "/" << iterations << " OK" << std::endl;
			}
		}

		mpz_clears(tmp0, tmp1, tmp2, tmp3, tmp4, Mp, nullptr);
		gmp_randclear(rs);
		std::cout << "[SELFTEST][te] all tests passed." << std::endl;
	}

	static void selftest_mont_sequence(const uint32_t p, const size_t device)
	{
		if (p == 0) throw std::runtime_error("self-test requires -p <prime exponent>.");
		const uint64_t seed = env_u64("MARIN_ENGINE_SELFTEST_SEED", 1);
		const size_t iterations = size_t(env_u64("MARIN_ENGINE_SELFTEST_ITERS", 50));
		const size_t chain = size_t(env_u64("MARIN_ENGINE_SELFTEST_CHAIN", 32));
		const size_t reg_count = 64;
		std::unique_ptr<engine> eng = create_engine_checked(p, reg_count, device);

		std::cout << "[SELFTEST][mont] p=" << p << " transform_size=" << eng->get_size()
			<< " iterations=" << iterations << " chain=" << chain << " seed=" << seed << std::endl;

		gmp_randstate_t rs;
		gmp_randinit_mt(rs);
		gmp_randseed_ui(rs, static_cast<unsigned long>(seed ^ 0x85EBCA6Bu));

		mpz_t Mp;
		mpz_init(Mp);
		mpz_ui_pow_ui(Mp, 2u, p);
		mpz_sub_ui(Mp, Mp, 1u);

		std::vector<zint> R(64);
		mpz_t tmp0, tmp1, tmp2, tmp3, tmp4;
		mpz_inits(tmp0, tmp1, tmp2, tmp3, tmp4, nullptr);

		for (size_t iter = 1; iter <= iterations; ++iter)
		{
			for (size_t i = 0; i < R.size(); ++i) mpz_set_ui(R[i].v, 0);

			random_non_zero_mod(tmp0, rs, p, Mp, seed + 211 * iter + 1); // X1
			random_non_zero_mod(tmp1, rs, p, Mp, seed + 211 * iter + 2); // Z1
			random_non_zero_mod(tmp2, rs, p, Mp, seed + 211 * iter + 3); // X2
			random_non_zero_mod(tmp3, rs, p, Mp, seed + 211 * iter + 4); // Z2
			random_non_zero_mod(tmp4, rs, p, Mp, seed + 211 * iter + 5); // A24
			set_expect_and_reg(eng.get(), 3, tmp0, R, Mp);
			set_expect_and_reg(eng.get(), 1, tmp1, R, Mp);
			set_expect_and_reg(eng.get(), 6, tmp2, R, Mp);
			set_expect_and_reg(eng.get(), 7, tmp3, R, Mp);
			set_expect_and_reg(eng.get(), 12, tmp4, R, Mp); // raw A24 then overwritten as multiplicand
			random_non_zero_mod(tmp4, rs, p, Mp, seed + 211 * iter + 6); // xD
			set_expect_and_reg(eng.get(), 13, tmp4, R, Mp); // raw xD then overwritten as multiplicand

			//prepare_mul_and_check(eng.get(), 12, 12, R, Mp, "mont", "prepare A24", iter, 0);
			//prepare_mul_and_check(eng.get(), 13, 13, R, Mp, "mont", "prepare xD", iter, 0);
			prepare_mul(eng.get(), (engine::Reg)12, (engine::Reg)12, R);
			prepare_mul(eng.get(), (engine::Reg)13, (engine::Reg)13, R);
			auto tr_copy = [&](const size_t dst, const size_t src, const char * const op, const size_t step)
			{
				eng->copy(dst, src);
				mpz_set(R[dst].v, R[src].v);
				require_equal(eng.get(), dst, R[dst].v, Mp, "mont", op, iter, step);
			};
			auto tr_add = [&](const size_t dst, const size_t src, const char * const op, const size_t step)
			{
				eng->add(dst, src);
				addm(R[dst].v, R[dst].v, R[src].v, Mp);
				require_equal(eng.get(), dst, R[dst].v, Mp, "mont", op, iter, step);
			};
			auto tr_sub = [&](const size_t dst, const size_t src, const char * const op, const size_t step)
			{
				eng->sub_reg(dst, src);
				subm(R[dst].v, R[dst].v, R[src].v, Mp);
				require_equal(eng.get(), dst, R[dst].v, Mp, "mont", op, iter, step);
			};
			auto tr_addsub = [&](const size_t sum, const size_t diff, const size_t a, const size_t b, const char * const op, const size_t step)
			{
				eng->addsub(sum, diff, a, b);
				addm(R[sum].v, R[a].v, R[b].v, Mp);
				subm(R[diff].v, R[a].v, R[b].v, Mp);
				require_equal(eng.get(), sum,  R[sum].v,  Mp, "mont", op, iter, step);
				require_equal(eng.get(), diff, R[diff].v, Mp, "mont", op, iter, step);
			};
			auto tr_addsub_copy = [&](const size_t sum, const size_t diff, const size_t sum_copy, const size_t diff_copy,
				const size_t a, const size_t b, const char * const op, const size_t step)
			{
				eng->addsub_copy(sum, diff, sum_copy, diff_copy, a, b);
				addm(R[sum].v, R[a].v, R[b].v, Mp);
				subm(R[diff].v, R[a].v, R[b].v, Mp);
				mpz_set(R[sum_copy].v, R[sum].v);
				mpz_set(R[diff_copy].v, R[diff].v);
				require_equal(eng.get(), sum, R[sum].v, Mp, "mont", op, iter, step);
				require_equal(eng.get(), diff, R[diff].v, Mp, "mont", op, iter, step);
				require_equal(eng.get(), sum_copy, R[sum_copy].v, Mp, "mont", op, iter, step);
				require_equal(eng.get(), diff_copy, R[diff_copy].v, Mp, "mont", op, iter, step);
			};
			auto tr_mul = [&](const size_t dst, const size_t src, const char * const op, const size_t step)
			{
				eng->mul(dst, src);
				mulm(R[dst].v, R[dst].v, R[src].v, Mp);
				require_equal(eng.get(), dst, R[dst].v, Mp, "mont", op, iter, step);
			};
			auto tr_square = [&](const size_t dst, const char * const op, const size_t step)
			{
				eng->square_mul(dst);
				sqrm(R[dst].v, R[dst].v, Mp);
				require_equal(eng.get(), dst, R[dst].v, Mp, "mont", op, iter, step);
			};
			auto tr_square_copy = [&](const size_t src, const size_t dcopy, const char * const op, const size_t step)
			{
				eng->square_mul_copy(src, dcopy);
				sqrm(R[src].v, R[src].v, Mp);
				mpz_set(R[dcopy].v, R[src].v);
				require_equal(eng.get(), src, R[src].v, Mp, "mont", op, iter, step);
				require_equal(eng.get(), dcopy, R[dcopy].v, Mp, "mont", op, iter, step);
			};
			auto tr_mul_copy = [&](const size_t dst, const size_t src, const size_t dcopy, const char * const op, const size_t step)
			{
				eng->mul_copy(dst, src, dcopy);
				mulm(R[dst].v, R[dst].v, R[src].v, Mp);
				mpz_set(R[dcopy].v, R[dst].v);
				require_equal(eng.get(), dst, R[dst].v, Mp, "mont", op, iter, step);
				require_equal(eng.get(), dcopy, R[dcopy].v, Mp, "mont", op, iter, step);
			};
			auto tr_mul_add = [&](const size_t dst, const size_t mul_src, const size_t add_src, const char * const op, const size_t step)
			{
				eng->mul_add(dst, mul_src, add_src);
				mulm(R[dst].v, R[dst].v, R[mul_src].v, Mp);
				addm(R[dst].v, R[dst].v, R[add_src].v, Mp);
				require_equal(eng.get(), dst, R[dst].v, Mp, "mont", op, iter, step);
			};
			auto tr_setmul = [&](const size_t dst, const size_t src, const char * const /*op*/, const size_t /*step*/)
			{
				prepare_mul(eng.get(), (engine::Reg)dst, (engine::Reg)src, R);
			};
			auto hadamard = [&](const size_t a, const size_t b, const size_t s, const size_t d, const char * const op, const size_t step)
			{
				tr_addsub(s, d, a, b, op, step);
			};
			auto hadamard_copy = [&](const size_t a, const size_t b, const size_t s, const size_t d,
				const size_t s_copy, const size_t d_copy, const char * const op, const size_t step)
			{
				tr_addsub_copy(s, d, s_copy, d_copy, a, b, op, step);
			};

			auto xDBLADD_strict = [&](const size_t X1, const size_t Z1, const size_t X2, const size_t Z2, const size_t step)
			{
				hadamard_copy(X1, Z1, 25, 24, 10, 9, "xDBLADD hadamard_copy P", step);
				hadamard(X2, Z2, 8, 7, "xDBLADD hadamard Q", step);
				tr_setmul(11, 8, "xDBLADD setmul S2", step);
				tr_mul(9, 11, "xDBLADD mul t1", step);
				tr_setmul(11, 7, "xDBLADD setmul D2", step);
				tr_mul(10, 11, "xDBLADD mul t2", step);
				hadamard(9, 10, X2, Z2, "xDBLADD hadamard t1t2", step);
				tr_square(X2, "xDBLADD sqr X2", step);
				tr_square(Z2, "xDBLADD sqr Z2", step);
				tr_mul(Z2, 13, "xDBLADD mul xD", step);
				tr_square_copy(25, X1, "xDBLADD sqr_copy U", step);
				tr_square(24, "xDBLADD sqr V", step);
				tr_sub(25, 24, "xDBLADD sub E", step);
				tr_setmul(15, 24, "xDBLADD setmul V", step);
				tr_mul(X1, 15, "xDBLADD mul X1", step);
				tr_setmul(15, 25, "xDBLADD setmul E", step);
				tr_mul_add(25, 12, 24, "xDBLADD mul_add A24E+V", step);
				tr_mul_copy(25, 15, Z1, "xDBLADD mul_copy Z1", step);
			};

			for (size_t step = 1; step <= chain; ++step)
			{
				xDBLADD_strict(3, 1, 6, 7, step);
			}

			if ((iter <= 5) || (iter % 10 == 0) || (iter == iterations))
			{
				std::cout << "[SELFTEST][mont] iter " << iter << "/" << iterations << " OK" << std::endl;
			}
		}

		mpz_clears(tmp0, tmp1, tmp2, tmp3, tmp4, Mp, nullptr);
		gmp_randclear(rs);
		std::cout << "[SELFTEST][mont] all tests passed." << std::endl;
	}

	static void run_selected_selftests(const uint32_t p, const size_t device,
		const bool do_basic, const bool do_te, const bool do_mont)
	{
		if (!do_basic && !do_te && !do_mont) throw std::runtime_error("no self-test selected.");
		if (do_basic) selftest_basic(p, device);
		if (do_te) selftest_te_sequence(p, device);
		if (do_mont) selftest_mont_sequence(p, device);
	}

public:
	void run(int argc, char * argv[])
	{
		std::vector<std::string> args;
		for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

		std::cout << header(args) << std::endl;

		uint32_t p = 0;
		bool isLL = false;
		size_t device = 0;
		bool valid = false;
		bool do_basic = false;
		bool do_te = false;
		bool do_mont = false;

		if (env_flag("MARIN_ENGINE_SELFTEST"))
		{
			do_basic = true;
			do_te = true;
			do_mont = true;
		}

		for (size_t i = 0, size = args.size(); i < size; ++i)
		{
			const std::string & arg = args[i];

			if (arg.substr(0, 2) == "-p")
			{
				const std::string str = ((arg == "-p") && (i + 1 < size)) ? args[++i] : arg.substr(2);
				p = uint32_t(std::atoi(str.c_str()));
				bool isprime = (p % 2 != 0);
				for (uint32_t d = 3; p / d >= d; d += 2) if (p % d == 0) { isprime = false; break; }
				if (!isprime) throw std::runtime_error("p must be an odd prime.");
				if ((p < 7) || (p > 1509949421)) throw std::runtime_error("p is out of range.");
			}
			if (arg.substr(0, 3) == "-LL")
			{
				isLL = true;
			}
#if defined(GPU)
			if (arg.substr(0, 2) == "-d")
			{
				const std::string str = ((arg == "-d") && (i + 1 < size)) ? args[++i] : arg.substr(2);
				device = size_t(std::atoi(str.c_str()));
			}
#endif
			if (arg.substr(0, 2) == "-h")
			{
				valid = true;
			}
			if ((arg == "-T") || (arg == "-selftest") || (arg == "--selftest"))
			{
				do_basic = true;
				do_te = true;
				do_mont = true;
			}
			if ((arg == "-Tbasic") || (arg == "--Tbasic") || (arg == "--selftest-basic")) do_basic = true;
			if ((arg == "-Tte")    || (arg == "--Tte")    || (arg == "--selftest-te"))    do_te = true;
			if ((arg == "-Tmont")  || (arg == "--Tmont")  || (arg == "--selftest-mont"))  do_mont = true;
		}

		if (do_basic || do_te || do_mont)
		{
			run_selected_selftests(p, device, do_basic, do_te, do_mont);
			return;
		}

		Mersenne & mersenne = Mersenne::get_instance();
		if (valid) { mersenne.valid_all(device); return; }

		if (p == 0)
		{
			std::cout << usage() << std::endl;
#if defined(GPU)
			ocl::platform pfm;
			if (pfm.display_devices() == 0) throw std::runtime_error("No OpenCL device.");
#endif
			return;
		}

		if (isLL) mersenne.checkLL(p, device);
		else mersenne.check(p, device);
	}
};

int main(int argc, char * argv[])
{
	std::setvbuf(stderr, nullptr, _IONBF, 0);

	try
	{
		application & app = application::get_instance();
		app.run(argc, argv);
	}
	catch (const std::runtime_error & e)
	{
		std::cerr << e.what() << std::endl << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
