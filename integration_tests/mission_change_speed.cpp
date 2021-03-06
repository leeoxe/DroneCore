#include <iostream>
#include <functional>
#include <memory>
#include <atomic>
#include <cmath>
#include "integration_test_helper.h"
#include "dronecore.h"
#include "plugins/telemetry/telemetry.h"
#include "plugins/action/action.h"
#include "plugins/mission/mission.h"

using namespace dronecore;
using namespace std::placeholders; // for `_1`

static void receive_upload_mission_result(Mission::Result result);
static void receive_start_mission_result(Mission::Result result);
static void receive_mission_progress(int current, int total);

std::shared_ptr<MissionItem> only_set_speed(float speed_m_s);
std::shared_ptr<MissionItem> add_waypoint(double latitude_deg, double longitude_deg,
                                          float relative_altitude_m, float speed_m_s);

float current_speed(std::shared_ptr<Telemetry> &telemetry);

static std::atomic<bool> _mission_sent_ok {false};
static std::atomic<bool> _mission_started_ok {false};
static std::atomic<int> _current_item {0};

const static float speeds[4] = {10.0f, 3.0f, 8.0f, 5.0f};

// Test to check speed set for mission items.

TEST_F(SitlTest, MissionChangeSpeed)
{
    DroneCore dc;

    ConnectionResult ret = dc.add_udp_connection();
    ASSERT_EQ(ret, ConnectionResult::SUCCESS);

    // Wait for device to connect via heartbeat.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    Device &device = dc.device();
    auto telemetry = std::make_shared<Telemetry>(device);
    auto mission = std::make_shared<Mission>(device);
    auto action = std::make_shared<Action>(device);

    while (!telemetry->health_all_ok()) {
        std::cout << "waiting for device to be ready" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Device ready, let's start" << std::endl;

    std::vector<std::shared_ptr<MissionItem>> mission_items;

    mission_items.push_back(only_set_speed(speeds[0]));
    mission_items.push_back(add_waypoint(47.398262509933957, 8.5456324815750122, 10, speeds[1]));
    mission_items.push_back(add_waypoint(47.39824201089737, 8.5447561722784542, 10, speeds[2]));
    mission_items.push_back(add_waypoint(47.397733642793433, 8.5447776308767516, 10, speeds[3]));

    mission->upload_mission_async(mission_items,
                                  std::bind(&receive_upload_mission_result, _1));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_TRUE(_mission_sent_ok);

    ActionResult result = action->arm();
    ASSERT_EQ(result, ActionResult::SUCCESS);

    mission->subscribe_progress(std::bind(&receive_mission_progress, _1, _2));

    mission->start_mission_async(std::bind(&receive_start_mission_result, _1));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_TRUE(_mission_started_ok);

    int last_item = -1;
    while (_current_item < static_cast<int>(mission_items.size())) {

        if (last_item != _current_item) {
            // Don't check the first because it's just a speed command and neither the second
            // because we're still taking off.
            if (_current_item >= 2) {
                // Time to accelerate
                std::this_thread::sleep_for(std::chrono::seconds(6));
                const float speed_correct = speeds[_current_item - 1];
                const float speed_actual = current_speed(telemetry);
                LogWarn() << "speed check, should be: " << speed_correct << " m/s, "
                          << "actually: " << speed_actual << " m/s";
                EXPECT_GT(speed_actual, speed_correct - 1.0f);
                EXPECT_LT(speed_actual, speed_correct + 1.0f);
            }
            last_item = _current_item;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LogInfo() << "mission done";

    result = action->return_to_launch();
    ASSERT_EQ(result, ActionResult::SUCCESS);

    while (!mission->mission_finished()) {
        std::cout << "waiting until mission is done" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    while (telemetry->in_air()) {
        std::cout << "waiting until landed" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    result = action->disarm();
    ASSERT_EQ(result, ActionResult::SUCCESS);
}

void receive_upload_mission_result(Mission::Result result)
{
    EXPECT_EQ(result, Mission::Result::SUCCESS);

    if (result == Mission::Result::SUCCESS) {
        _mission_sent_ok = true;
    } else {
        std::cerr << "Error: mission send result: " << unsigned(result) << std::endl;
    }
}

void receive_start_mission_result(Mission::Result result)
{
    EXPECT_EQ(result, Mission::Result::SUCCESS);

    if (result == Mission::Result::SUCCESS) {
        _mission_started_ok = true;
    } else {
        std::cerr << "Error: mission start result: " << unsigned(result) << std::endl;
    }
}

std::shared_ptr<MissionItem>
only_set_speed(float speed_m_s)
{
    auto new_item = std::make_shared<MissionItem>();
    new_item->set_speed(speed_m_s);
    return new_item;
}

std::shared_ptr<MissionItem>
add_waypoint(double latitude_deg, double longitude_deg, float relative_altitude_m, float speed_m_s)
{
    auto new_item = std::make_shared<MissionItem>();
    new_item->set_position(latitude_deg, longitude_deg);
    new_item->set_relative_altitude(relative_altitude_m);
    new_item->set_speed(speed_m_s);
    return new_item;
}

float current_speed(std::shared_ptr<Telemetry> &telemetry)
{
    return std::sqrt(telemetry->ground_speed_ned().velocity_north_m_s *
                     telemetry->ground_speed_ned().velocity_north_m_s +
                     telemetry->ground_speed_ned().velocity_east_m_s *
                     telemetry->ground_speed_ned().velocity_east_m_s);
}

void receive_mission_progress(int current, int total)
{
    std::cout << "Mission status update: " << current << " / " << total << std::endl;
    _current_item = current;
}
