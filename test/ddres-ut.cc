// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "ddres.h"
#include "loghandle.hpp"

#include <iostream>

extern "C" {
#include <stdarg.h>
}

namespace ddprof {

TEST(DDRes, Size) {
  DDRes ddres = ddres_init();
  ASSERT_TRUE(sizeof(ddres) == sizeof(int32_t));
}

TEST(DDRes, InitOK) {
  {
    DDRes ddres1 = {};
    DDRes ddres2 = ddres_init();
    DDRes ddres3;
    InitDDResOK(ddres3);

    ASSERT_TRUE(ddres_equal(ddres1, ddres2));
    ASSERT_TRUE(ddres_equal(ddres1, ddres3));

    ASSERT_FALSE(IsDDResNotOK(ddres2));
    ASSERT_TRUE(IsDDResOK(ddres2));
  }
}

extern "C" {
static int s_call_counter = 0;

DDRes mock_fatal_generator() {
  ++s_call_counter;
  DDRES_RETURN_ERROR_LOG(DD_WHAT_UNITTEST,
                         "Test the log and return function %d", 42);
}

DDRes mock_fatal_default_message() {
  ++s_call_counter;
  DDRES_RETURN_ERROR_LOG(DD_WHAT_UNITTEST, DDRES_NOLOG);
}

DDRes dderr_wrapper() {
  DDRES_CHECK_FWD(mock_fatal_generator());
  return ddres_init();
}

int minus_one_generator(void) { return -1; }

bool false_generator(void) { return false; }
}

TEST(DDRes, FillFatal) {
  {
    DDRes ddres = ddres_error(DD_WHAT_UNITTEST);
    ASSERT_TRUE(IsDDResNotOK(ddres));
    ASSERT_TRUE(IsDDResFatal(ddres));
  }
  {
    LogHandle handle;
    {
      DDRes ddres = mock_fatal_generator();
      ASSERT_TRUE(ddres_equal(ddres, ddres_error(DD_WHAT_UNITTEST)));
    }
    EXPECT_EQ(s_call_counter, 1);

    {
      DDRes ddres = dderr_wrapper();
      ASSERT_TRUE(ddres_equal(ddres, ddres_error(DD_WHAT_UNITTEST)));
    }
    EXPECT_EQ(s_call_counter, 2);
    {
      DDRes ddres = mock_fatal_default_message();
      ASSERT_TRUE(ddres_equal(ddres, ddres_error(DD_WHAT_UNITTEST)));
    }
    EXPECT_EQ(s_call_counter, 3);
  }
}

void mock_except1() { throw DDException(DD_SEVERROR, DD_WHAT_UNITTEST); }

void mock_except2() { throw std::bad_alloc(); }

DDRes mock_wrapper(int idx) {
  try {
    if (idx == 1) {
      mock_except1();
    } else if (idx == 2) {
      mock_except2();
    } else if (idx == 3) {
      DDRES_CHECK_INT(minus_one_generator(), DD_WHAT_UNITTEST,
                      "minus one returned");
    } else if (idx == 4) {
      LG_NTC("all good");
    } else if (idx == 5) {
      DDRES_CHECK_BOOL(false_generator(), DD_WHAT_UNITTEST,
                       "False returned from generator");
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}

// Check that an exception can be caught and converted back to a C result
TEST(DDRes, ConvertException) {
  LogHandle handle;
  try {
    DDRes ddres = mock_wrapper(1);
    ASSERT_EQ(ddres, ddres_create(DD_SEVERROR, DD_WHAT_UNITTEST));
    ddres = mock_wrapper(2);
    ASSERT_EQ(ddres, ddres_create(DD_SEVERROR, DD_WHAT_BADALLOC));
    ddres = mock_wrapper(3);
    ASSERT_EQ(ddres, ddres_create(DD_SEVERROR, DD_WHAT_UNITTEST));
    ddres = mock_wrapper(4);
    ASSERT_TRUE(IsDDResOK(ddres));

    ddres = mock_wrapper(5);
    ASSERT_EQ(ddres, ddres_create(DD_SEVERROR, DD_WHAT_UNITTEST));

  } catch (...) { ASSERT_TRUE(false); }
}

TEST(DDRes, ErrorMessageCheck) {
  LogHandle handle;
  for (int i = DD_COMMON_START_RANGE; i < COMMON_ERROR_SIZE; ++i) {
    printf("Id = %d \n", i);
    LOG_ERROR_DETAILS(LG_NTC, i);
  }

  EXPECT_TRUE(strcmp(ddres_error_message(DD_WHAT_BADALLOC),
                     "BADALLOC: allocation error") == 0);

  for (int i = DD_NATIVE_START_RANGE; i < NATIVE_ERROR_SIZE; ++i) {
    printf("Id = %d \n", i);
    LOG_ERROR_DETAILS(LG_NTC, i);
  }

  EXPECT_TRUE(strcmp(ddres_error_message(DD_WHAT_UNITTEST),
                     "UNITTEST: unit test error") == 0);
}
} // namespace ddprof
