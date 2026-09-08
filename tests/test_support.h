#pragma once

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef WEPBR_FIXTURES_DIR
#	define WEPBR_FIXTURES_DIR "tests/fixtures"
#endif

namespace test
{
	inline int failures = 0;
	inline int checks = 0;

	inline bool Near(float a, float b, float eps = 1e-4f)
	{
		return std::fabs(a - b) <= eps;
	}

	inline void Check(bool ok, const std::string& what)
	{
		++checks;
		if (!ok) {
			++failures;
			std::printf("FAIL: %s\n", what.c_str());
		}
	}

	inline void Skip(const std::string& what)
	{
		++checks;
		++failures;
		std::printf("SKIP (counted as failure): %s\n", what.c_str());
	}

	inline std::filesystem::path Fixtures()
	{
		return std::filesystem::path{ WEPBR_FIXTURES_DIR };
	}

	inline std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	inline bool WriteFile(const std::filesystem::path& a_path, std::string_view a_text)
	{
		std::ofstream out(a_path, std::ios::binary);
		out << a_text;
		return static_cast<bool>(out);
	}

	inline int Finish(const char* a_name)
	{
		if (failures == 0) {
			std::printf("all %d %s checks passed\n", checks, a_name);
		} else {
			std::printf("%d of %d %s checks failed\n", failures, checks, a_name);
		}
		return failures == 0 ? 0 : 1;
	}
}
