// GTest verifying that vsomeip::application::init(const std::string&)
// initializes the application from an in-memory JSON string without reading or
// writing any configuration file on the filesystem.

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include <vsomeip/vsomeip.hpp>

#include "json_config_sample.hpp"

namespace {

// RAII helper that switches the process working directory to a freshly created,
// empty temporary directory for the duration of a test, then restores it. This
// guarantees there is no vsomeip.json next to the process that init() could
// silently fall back to.
class scoped_clean_working_directory {
public:
	scoped_clean_working_directory() : previous_(std::filesystem::current_path()) {
		auto unique = std::string("vsomeip_json_cfg_test_") + std::to_string(::testing::UnitTest::GetInstance()->random_seed())
				+ "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
		temp_dir_ = std::filesystem::temp_directory_path() / unique;
		std::filesystem::create_directories(temp_dir_);
		std::filesystem::current_path(temp_dir_);
	}

	~scoped_clean_working_directory() {
		std::error_code ec;
		std::filesystem::current_path(previous_, ec);
		std::filesystem::remove_all(temp_dir_, ec);
	}

	const std::filesystem::path& path() const { return temp_dir_; }

private:
	std::filesystem::path previous_;
	std::filesystem::path temp_dir_;
};

} // namespace

// Initializing from the in-memory JSON string must succeed even though there is
// no vsomeip.json in the working directory.
TEST(in_memory_json_api_tests, init_from_string_succeeds_without_config_file) {
	scoped_clean_working_directory clean_cwd;
	ASSERT_FALSE(std::filesystem::exists(clean_cwd.path() / "vsomeip.json"));

	auto app = vsomeip::runtime::get()->create_application("JsonConfigApp");
	ASSERT_TRUE(app != nullptr);

	EXPECT_TRUE(app->init(kInMemoryVsomeipJson));
}

// The client id configured only inside the in-memory JSON string must be the
// one applied to the application. This proves the string was parsed and used
// rather than vsomeip falling back to a file or auto-assigning an id.
TEST(in_memory_json_api_tests, init_from_string_applies_in_memory_client_id) {
	scoped_clean_working_directory clean_cwd;

	auto app = vsomeip::runtime::get()->create_application("JsonConfigApp");
	ASSERT_TRUE(app != nullptr);
	ASSERT_TRUE(app->init(kInMemoryVsomeipJson));

	EXPECT_EQ(app->get_client(), static_cast<vsomeip::client_t>(SAMPLE_CLIENT_ID));
}

// init(const std::string&) must not create a vsomeip.json (or any temporary
// configuration file) in the working directory.
TEST(in_memory_json_api_tests, init_from_string_creates_no_config_file) {
	scoped_clean_working_directory clean_cwd;

	auto app = vsomeip::runtime::get()->create_application("JsonConfigApp");
	ASSERT_TRUE(app != nullptr);
	ASSERT_TRUE(app->init(kInMemoryVsomeipJson));

	EXPECT_FALSE(std::filesystem::exists(clean_cwd.path() / "vsomeip.json"));
}

// Malformed JSON must not be silently accepted: init(const std::string&) is
// documented to return false when the configuration string cannot be parsed.
// A unique application name is used so the configuration plugin's per-name
// cache cannot mask the failure with a previously loaded valid configuration.
TEST(in_memory_json_api_tests, init_from_malformed_string_fails) {
	scoped_clean_working_directory clean_cwd;

	auto app = vsomeip::runtime::get()->create_application("JsonConfigMalformedApp");
	ASSERT_TRUE(app != nullptr);

	const std::string malformed_json = R"json({ "unicast": "127.0.0.1", )json";
	EXPECT_FALSE(app->init(malformed_json));
}
