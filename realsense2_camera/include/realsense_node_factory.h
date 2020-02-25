// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2018 Intel Corporation. All Rights Reserved

#pragma once

#include <pluginlib/class_list_macros.h>
#include <nodelet/nodelet.h>
#include <image_transport/image_transport.h>
#include <ros/ros.h>
#include <ros/package.h>
#include <cv_bridge/cv_bridge.h>
#include <csignal>
#include <eigen3/Eigen/Geometry>
#include <fstream>
#include <thread>

#include <any_librealsense2/rs.hpp>
#include <any_librealsense2/rsutil.h>
#include <any_librealsense2/hpp/rs_processing.hpp>
#include <any_librealsense2/rs_advanced_mode.hpp>

#include <constants.h>
#include <any_realsense2_camera/Extrinsics.h>
#include <any_realsense2_camera/IMUInfo.h>

using namespace any_realsense2_camera;

namespace realsense2_camera
{
    const stream_index_pair COLOR{RS2_STREAM_COLOR, 0};
    const stream_index_pair DEPTH{RS2_STREAM_DEPTH, 0};
    const stream_index_pair INFRA1{RS2_STREAM_INFRARED, 1};
    const stream_index_pair INFRA2{RS2_STREAM_INFRARED, 2};
    const stream_index_pair FISHEYE{RS2_STREAM_FISHEYE, 0};
    const stream_index_pair FISHEYE1{RS2_STREAM_FISHEYE, 1};
    const stream_index_pair FISHEYE2{RS2_STREAM_FISHEYE, 2};
    const stream_index_pair GYRO{RS2_STREAM_GYRO, 0};
    const stream_index_pair ACCEL{RS2_STREAM_ACCEL, 0};
    const stream_index_pair POSE{RS2_STREAM_POSE, 0};
    

    const std::vector<stream_index_pair> IMAGE_STREAMS = {DEPTH, INFRA1, INFRA2,
                                                          COLOR,
                                                          FISHEYE,
                                                          FISHEYE1, FISHEYE2};

    const std::vector<stream_index_pair> HID_STREAMS = {GYRO, ACCEL, POSE};

    class InterfaceRealSenseNode
    {
    public:
        virtual void publishTopics() = 0;
        virtual void registerDynamicReconfigCb(ros::NodeHandle& nh) = 0;
        virtual ~InterfaceRealSenseNode() = default;
    };

    class RealSenseNodeFactory : public nodelet::Nodelet
    {
    public:
        RealSenseNodeFactory();
        virtual ~RealSenseNodeFactory();

    private:
        void closeDevice();
        void StartDevice();
        void change_device_callback(rs2::event_information& info);
        void getDevice(rs2::device_list list);
        virtual void onInit() override;
        void tryGetLogSeverity(rs2_log_severity& severity) const;

        rs2::device _device;
        std::unique_ptr<InterfaceRealSenseNode> _realSenseNode;
        rs2::context _ctx;
        std::string _serial_no;
        std::string _usb_port_id;
        std::string _device_type;
        bool _initial_reset;
        std::thread _query_thread;

    };
}//end namespace
