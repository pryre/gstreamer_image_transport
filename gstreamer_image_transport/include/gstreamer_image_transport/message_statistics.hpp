#pragma once

#include <rcl/time.h>
#include <string>
#include <span>
#include <chrono>

#include <rclcpp/time.hpp>
#include <rclcpp/duration.hpp>

using namespace std::chrono_literals;

namespace gstreamer_image_transport
{

const auto duration_zero = rclcpp::Duration(0, 0);
const double message_rate_filter = 0.2;
// constexpr double rate_filter_inv = 1.0 - _rate_filter;

struct MessageStatistics {
    std::string name;
    rclcpp::Time stamp_last;
    double rate = 0.0;
    rclcpp::Duration delay = duration_zero;

    size_t count = 0;
    size_t dropped = 0;

    MessageStatistics(const std::string statistic_name, rcl_clock_type_t clock_type = rcl_clock_type_t::RCL_ROS_TIME) :
    name(statistic_name),
    stamp_last(0, 0, clock_type)
    {
    }

    inline void log(const rclcpp::Time now, const rclcpp::Time stamp, bool was_dropped = false) {
        const auto delta_real = now - stamp;
        auto delta_stamp = stamp - stamp_last;

        //Reset our tracker if we have stale data
        if((delta_real > 10s) || (delta_stamp > 10s)) {
            rate = 0.0;
            delta_stamp = duration_zero;
        }

        count++;
        if(was_dropped) {
            dropped++;
        } else {
            const double raw_rate = delta_stamp == duration_zero ? 0.0 : std::chrono::nanoseconds::period().den / double(delta_stamp.nanoseconds());
                    // y[i] := y[i-1] + α * (x[i] - y[i-1])
            rate += message_rate_filter * (raw_rate - rate);

            const rcl_duration_value_t diff = message_rate_filter * (delta_real - delay).nanoseconds();
            delay = rclcpp::Duration::from_nanoseconds(delay.nanoseconds() + diff);

            stamp_last = stamp;
        }
    }

    inline double rate_clean(rclcpp::Time now) const {
        auto delta = now - stamp_last;

        return delta > 10s ? 0.0 : rate;
    }

    inline rclcpp::Duration delay_clean(rclcpp::Time now) const {
        auto delta = now - stamp_last;

        return delta > 10s ? duration_zero : delay;
    }

    inline double drop_rate() const {
        return count ? ((double)dropped)/count : 0.0;
    }

    inline double success_rate() const {
        return 1.0 - drop_rate();
    }

};

};
