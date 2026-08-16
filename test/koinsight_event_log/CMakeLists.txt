add_executable(KoInsightEventLogTest
  KoInsightEventLogTest.cpp
)

target_link_libraries(KoInsightEventLogTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(KoInsightEventLogTest PRIVATE
  ${REPO_ROOT}/lib/KoInsightSync
)

gtest_discover_tests(KoInsightEventLogTest)
