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

	static void print_mismatch(const char * const op, const size_t iter, const size_t reg,
		const mpz_t & got, const mpz_t & expect)
	{
		std::cerr << "[SELFTEST] MISMATCH iter=" << iter
			<< " op=" << op << " reg=R" << reg << std::endl;
		std::cerr << "[SELFTEST] got    = 0x" << mpz_hex_prefix(got) << std::endl;
		std::cerr << "[SELFTEST] expect = 0x" << mpz_hex_prefix(expect) << std::endl;
	}

	static void mod_norm(mpz_t & rop, const mpz_t & op, const mpz_t & mod)
	{
		mpz_mod(rop, op, mod);
		if (mpz_sgn(rop) < 0) mpz_add(rop, rop, mod);
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

	static void require_equal(engine * const eng, const engine::Reg reg, const mpz_t & expect,
		const char * const op, const size_t iter)
	{
		mpz_t got;
		mpz_init(got);
		eng->get_mpz(got, reg);
		if (mpz_cmp(got, expect) != 0)
		{
			print_mismatch(op, iter, size_t(reg), got, expect);
			mpz_clear(got);
			throw std::runtime_error("engine self-test failed");
		}
		mpz_clear(got);
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
		ss << "  -T             deep arithmetic engine self-test against GMP" << std::endl;
		ss << "Environment:" << std::endl;
		ss << "  MARIN_ENGINE_SELFTEST=1        enable self-test without -T" << std::endl;
		ss << "  MARIN_ENGINE_SELFTEST_ITERS=N  number of randomized iterations (default 200)" << std::endl;
		ss << "  MARIN_ENGINE_SELFTEST_SEED=N   RNG seed (default 1)" << std::endl;
		return ss.str();
	}

	static void selftest(const uint32_t p, const size_t device)
	{
		if (p == 0) throw std::runtime_error("self-test requires -p <prime exponent>.");

		const uint64_t seed = env_u64("MARIN_ENGINE_SELFTEST_SEED", 1);
		const size_t iterations = size_t(env_u64("MARIN_ENGINE_SELFTEST_ITERS", 200));
		const size_t reg_count = 32;

		std::unique_ptr<engine> eng(
#if defined(GPU)
			engine::create_gpu(p, reg_count, device, true)
#else
			engine::create_cpu(p, reg_count)
#endif
		);
		if (!eng) throw std::runtime_error("cannot create engine.");

		std::cout << "[SELFTEST] p=" << p << " transform_size=" << eng->get_size()
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
			const uint32_t k4 = uint32_t(((seed + iter * 59u) % 509u) + 1u);

			eng->set_mpz(R0, A);
			eng->set_mpz(R1, B);
			eng->set_mpz(R2, C);
			eng->set_mpz(R3, D);

			eng->get_mpz(got, R0);
			if (mpz_cmp(got, A) != 0) { print_mismatch("roundtrip A", iter, R0, got, A); throw std::runtime_error("engine self-test failed"); }
			eng->get_mpz(got, R1);
			if (mpz_cmp(got, B) != 0) { print_mismatch("roundtrip B", iter, R1, got, B); throw std::runtime_error("engine self-test failed"); }

			eng->copy(R4, R0);
			require_equal(eng.get(), R4, A, "copy", iter);

			eng->copy(R4, R0);
			eng->add(R4, R1);
			mpz_add(E, A, B);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "add", iter);

			eng->copy(R4, R0);
			eng->sub_reg(R4, R1);
			mpz_sub(E, A, B);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "sub_reg", iter);

			eng->copy(R4, R0);
			eng->sub(R4, k1);
			mpz_sub_ui(E, A, k1);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "sub_const", iter);

			eng->addsub(R4, R5, R0, R1);
			mpz_add(E, A, B);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "addsub sum", iter);
			mpz_sub(E, A, B);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R5, E, "addsub diff", iter);

			eng->addsub_copy(R4, R5, R6, R7, R0, R1);
			mpz_add(E, A, B);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "addsub_copy sum", iter);
			require_equal(eng.get(), R6, E, "addsub_copy sum_copy", iter);
			mpz_sub(E, A, B);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R5, E, "addsub_copy diff", iter);
			require_equal(eng.get(), R7, E, "addsub_copy diff_copy", iter);

			eng->copy(R4, R0);
			eng->set_multiplicand(R8, R1);
			eng->mul(R4, R8, k2);
			mpz_mul_ui(E, B, k2);
			mpz_mul(E, E, A);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "mul", iter);

			eng->copy(R4, R0);
			eng->square_mul(R4, k3);
			mpz_mul(E, A, A);
			mpz_mul_ui(E, E, k3);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "square_mul", iter);

			eng->copy(R4, R0);
			eng->square_mul_copy(R4, R6, k4);
			mpz_mul(E, A, A);
			mpz_mul_ui(E, E, k4);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "square_mul_copy", iter);
			require_equal(eng.get(), R6, E, "square_mul_copy copy", iter);

			eng->copy(R4, R0);
			eng->set_multiplicand(R8, R1);
			eng->mul_add(R4, R8, R2, k2);
			mpz_mul_ui(E, B, k2);
			mpz_mul(E, E, A);
			mpz_add(E, E, C);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "mul_add", iter);

			eng->copy(R4, R0);
			eng->set_multiplicand(R8, R1);
			eng->mul_copy(R4, R8, R6, k1);
			mpz_mul_ui(E, B, k1);
			mpz_mul(E, E, A);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "mul_copy", iter);
			require_equal(eng.get(), R6, E, "mul_copy copy", iter);

			eng->copy(R4, R0);
			eng->copy(R5, R2);
			eng->copy(R8, R1);
			eng->copy(R9, R3);
			eng->mul_pair_unit(R4, R8, R5, R9);
			mpz_mul(E, A, B);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "mul_pair_unit first", iter);
			mpz_mul(E, C, D);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R5, E, "mul_pair_unit second", iter);

			eng->copy(R4, R0);
			eng->copy(R5, R2);
			eng->set_multiplicand(R8, R1);
			eng->set_multiplicand(R9, R3);
			eng->mul_pair_prepared(R4, R8, R5, R9, k1, k2);
			mpz_mul_ui(E, B, k1);
			mpz_mul(E, E, A);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R4, E, "mul_pair_prepared first", iter);
			mpz_mul_ui(E, D, k2);
			mpz_mul(E, E, C);
			mod_norm(E, E, Mp);
			require_equal(eng.get(), R5, E, "mul_pair_prepared second", iter);

			eng->copy(R4, R0);
			if (!eng->get_data(blob, R4)) throw std::runtime_error("get_data failed");
			eng->set(R6, uint32_t(0));
			if (!eng->set_data(R6, blob)) throw std::runtime_error("set_data failed");
			require_equal(eng.get(), R6, A, "get_data/set_data", iter);

			eng->copy(R10, R0);
			eng->copy(R11, R1);
			if (!eng->get_checkpoint(ckpt)) throw std::runtime_error("get_checkpoint failed");
			eng->set(R10, uint32_t(0));
			eng->set(R11, uint32_t(0));
			if (!eng->set_checkpoint(ckpt)) throw std::runtime_error("set_checkpoint failed");
			require_equal(eng.get(), R10, A, "checkpoint R10", iter);
			require_equal(eng.get(), R11, B, "checkpoint R11", iter);

			if ((iter <= 5) || (iter % 25 == 0) || (iter == iterations))
			{
				std::cout << "[SELFTEST] iter " << iter << "/" << iterations << " OK" << std::endl;
			}
		}

		mpz_clears(A, B, C, D, E, F, got, Mp, nullptr);
		gmp_randclear(rs);

		std::cout << "[SELFTEST] all tests passed." << std::endl;
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
		bool deep_selftest = env_flag("MARIN_ENGINE_SELFTEST");

		// parse args
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
				deep_selftest = true;
			}
		}

		if (deep_selftest)
		{
			selftest(p, device);
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
	std::setvbuf(stderr, nullptr, _IONBF, 0);	// no buffer

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
