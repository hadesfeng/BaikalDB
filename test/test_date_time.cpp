#include <gtest/gtest.h>
#include <climits>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "common.h"
#include "datetime.h"
#include "expr_value.h"

int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace baikaldb {

//extern std::string timestamp_to_str(time_t timestamp);
//extern time_t str_to_timestamp(const char* str_time);
//extern std::string datetime_to_str(uint64_t datetime);
//extern uint64_t str_to_datetime(const char* str_time);
//extern time_t datetime_to_timestamp(uint64_t datetime);
//extern uint64_t timestamp_to_datetime(time_t timestamp);

TEST(test_stamp_to_str, case_all) {
    EXPECT_EQ(timestamp_to_str(1512300524), "2017-12-03 19:28:44");
    EXPECT_EQ(timestamp_to_str(1512300480), "2017-12-03 19:28:00");
    EXPECT_EQ(timestamp_to_str(1512230400), "2017-12-03 00:00:00");
}

TEST(test_str_to_stamp, case_all) {
    EXPECT_EQ(str_to_timestamp("2017-12-03 19:28:44hahahaha"), 1512300524);
    EXPECT_EQ(str_to_timestamp("2017-12-03 19:28:44"), 1512300524);
    EXPECT_EQ(str_to_timestamp("2017-12-03 19:28:"), 1512300480);
    EXPECT_EQ(str_to_timestamp("2017-12-03"), 1512230400);
    EXPECT_NE(str_to_timestamp("hahahahah"), 1512259200);
}

TEST(test_str_stamp, case_all) {
    EXPECT_EQ(timestamp_to_str(str_to_timestamp("2017-12-03 19:28:4400")), "2017-12-03 19:28:44");
    EXPECT_EQ(timestamp_to_str(str_to_timestamp("2017-12-03 19:283:44")), "2017-12-03 19:28:00");
    EXPECT_EQ(timestamp_to_str(str_to_timestamp("2017-12-03 192:28:44")), "2017-12-03 19:00:00");
    //EXPECT_EQ(timestamp_to_str(str_to_timestamp("2017-122-03 19:28:44")), "2017-12-00 00:00:00");
    //EXPECT_EQ(timestamp_to_str(str_to_timestamp("20179-12-03 19:28:44")), "2017-12-00 00:00:00");
    EXPECT_EQ(timestamp_to_str(str_to_timestamp("2017-12-03 19:28:")), "2017-12-03 19:28:00");
    EXPECT_EQ(timestamp_to_str(str_to_timestamp("2017-12-03 19:")), "2017-12-03 19:00:00");
}

TEST(test_datetime_str, case_all) {
    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 19:28:44")), "2017-12-03 19:28:44");
    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 19:28:44.")), "2017-12-03 19:28:44");
    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 19:")), "2017-12-03 19:00:00");
    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 19")), "2017-12-03 19:00:00");
    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 19:28:44.000")), "2017-12-03 19:28:44");
    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 19:28:44.1234567")), "2017-12-03 19:28:44.123456");
    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 19:28:44.123")), "2017-12-03 19:28:44.123000");

    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 19:28:44.000123")), "2017-12-03 19:28:44.000123");
    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 19:28:44.000123456")), "2017-12-03 19:28:44.000123");

    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 192:28:44")), "2017-12-03 19:00:00");
    EXPECT_EQ(datetime_to_str(str_to_datetime("2017-12-03 19:284:44")), "2017-12-03 19:28:00");
}
int32_t str_to_time2(const char* str_time) {
    int hour = 0;
    int minute = 0;
    int second = 0;
    int32_t time = 0;
    bool minus = false;

    sscanf(str_time, "%d:%2u:%2u",
         &hour, &minute, &second);
    if (hour < 0) {
        hour = -hour;
        minus = true;
    }
    time |= second;
    time |= (minute << 6);
    time |= (hour << 12);
    if (minus) {
        time = -time;
    }
    std::cout << hour << minute << second << time << std::endl;
    return time;
}
std::string time_to_str2(int32_t time) {
    bool minus = false;
    if (time < 0) {
        minus = true;
        time = -time;
    }
    int hour = (time >> 12) & 0x3FF;
    int min = (time >> 6) & 0x3F;
    int sec = time & 0x3F;
    if (minus) {
        hour = -hour;
    }
    std::cout << hour << min << sec << std::endl;
    char buf[20] = {0};
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, min, sec);
    return std::string(buf);
}

uint64_t time_to_datetime2(int32_t time) {
    ExprValue tmp(pb::TIMESTAMP);
    time_t now = ::time(NULL);
    std::cout << now << std::endl;
    now = ((now + 28800) / 86400) * 86400; // 去除时分秒
    std::cout << now << std::endl;

    bool minus = false;
    if (time < 0) {
        minus = true;
        time = -time;
    }
    uint32_t hour = (time >> 12) & 0x3FF;
    uint32_t min = (time >> 6) & 0x3F;
    uint32_t sec = time & 0x3F;
    int32_t delta_sec = hour * 3600 + min * 60 + sec;
    if (minus) {
        delta_sec = -delta_sec;
    }
    now -= 28800;
    now += delta_sec;
    std::cout << delta_sec << std::endl;
    std::cout << now << std::endl;

    return timestamp_to_datetime(now);
}
TEST(test_datetime_time, case_all) {
    EXPECT_EQ(time_to_str(datetime_to_time(str_to_datetime("2017-12-03 19:28:44.123456"))), "19:28:44");
    EXPECT_EQ(time_to_str(str_to_time("19:28:44")), "19:28:44");
    EXPECT_EQ(time_to_str(str_to_time("-19:28:44")), "-19:28:44");
    EXPECT_EQ(time_to_str(str_to_time("-119:28:44")), "-119:28:44");
    EXPECT_EQ(time_to_str(str_to_time("-119:28:44.124")), "-119:28:44");
    EXPECT_EQ(time_to_str(str_to_time("199:28:44")), "199:28:44");
    EXPECT_EQ(datetime_to_str(time_to_datetime(str_to_time("19:28:44"))), ExprValue::Now().cast_to(pb::DATE).get_string() + " 19:28:44");
    EXPECT_EQ(datetime_to_str(time_to_datetime(str_to_time("01:28:44"))), ExprValue::Now().cast_to(pb::DATE).get_string() + " 01:28:44");
    //EXPECT_EQ(datetime_to_str(time_to_datetime2(str_to_time("-19:28:44"))), ExprValue::Now().cast_to(pb::DATE).get_string() + " 19:28:44.000000");
    EXPECT_EQ(time_to_str(seconds_to_time(3601)), "01:00:01");
    EXPECT_EQ(time_to_str(seconds_to_time(-3601)), "-01:00:01");
}


TEST(test_datetime_timestamp, case_all) {
    EXPECT_EQ(timestamp_to_str(datetime_to_timestamp(str_to_datetime("2017-12-03 19:28:44.123456"))), "2017-12-03 19:28:44");
    EXPECT_EQ(datetime_to_str(timestamp_to_datetime(str_to_timestamp("2017-12-03 19:28:44"))), "2017-12-03 19:28:44");
}

}  // namespace baikal
