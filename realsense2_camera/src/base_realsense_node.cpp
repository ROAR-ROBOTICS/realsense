#include "../include/base_realsense_node.h"
#include "../include/sr300_node.h"
#include "assert.h"
#include <boost/algorithm/string.hpp>
#include <algorithm>

using namespace realsense2_camera;

std::string BaseRealSenseNode::getNamespaceStr()
{
    auto ns = ros::this_node::getNamespace();
    ns.erase(std::remove(ns.begin(), ns.end(), '/'), ns.end());
    return ns;
}

BaseRealSenseNode::BaseRealSenseNode(ros::NodeHandle& nodeHandle,
                                     ros::NodeHandle& privateNodeHandle,
                                     rs2::device dev,
                                     const std::string& serial_no) :
    _dev(dev),  _node_handle(nodeHandle),
    _pnh(privateNodeHandle), _json_file_path(""),
    _serial_no(serial_no), _base_frame_id(""),
    _intialize_time_base(false),
    _namespace(getNamespaceStr()),
    _image_counter(0)
{
    // Types for depth stream
    _is_frame_arrived[DEPTH] = false;
    _format[DEPTH] = RS2_FORMAT_Z16;   // libRS type
    _image_format[DEPTH] = CV_16UC1;    // CVBridge type
    _encoding[DEPTH] = sensor_msgs::image_encodings::TYPE_16UC1; // ROS message type
    _unit_step_size[DEPTH] = sizeof(uint16_t); // sensor_msgs::ImagePtr row step size
    _stream_name[DEPTH] = "depth";
    _depth_aligned_encoding[DEPTH] = sensor_msgs::image_encodings::TYPE_16UC1;

    // Infrared stream - Left
    _is_frame_arrived[INFRA1] = false;
    _format[INFRA1] = RS2_FORMAT_Y8;   // libRS type
    _image_format[INFRA1] = CV_8UC1;    // CVBridge type
    _encoding[INFRA1] = sensor_msgs::image_encodings::TYPE_8UC1; // ROS message type
    _unit_step_size[INFRA1] = sizeof(uint8_t); // sensor_msgs::ImagePtr row step size
    _stream_name[INFRA1] = "infra1";
    _depth_aligned_encoding[INFRA1] = sensor_msgs::image_encodings::TYPE_16UC1;

    // Infrared stream - Right
    _is_frame_arrived[INFRA2] = false;
    _format[INFRA2] = RS2_FORMAT_Y8;   // libRS type
    _image_format[INFRA2] = CV_8UC1;    // CVBridge type
    _encoding[INFRA2] = sensor_msgs::image_encodings::TYPE_8UC1; // ROS message type
    _unit_step_size[INFRA2] = sizeof(uint8_t); // sensor_msgs::ImagePtr row step size
    _stream_name[INFRA2] = "infra2";
    _depth_aligned_encoding[INFRA2] = sensor_msgs::image_encodings::TYPE_16UC1;

    // Types for color stream
    _is_frame_arrived[COLOR] = false;
    _format[COLOR] = RS2_FORMAT_RGB8;   // libRS type
    _image_format[COLOR] = CV_8UC3;    // CVBridge type
    _encoding[COLOR] = sensor_msgs::image_encodings::RGB8; // ROS message type
    _unit_step_size[COLOR] = 3; // sensor_msgs::ImagePtr row step size
    _stream_name[COLOR] = "color";
    _depth_aligned_encoding[COLOR] = sensor_msgs::image_encodings::TYPE_16UC1;

    // Types for fisheye stream
    _is_frame_arrived[FISHEYE] = false;
    _format[FISHEYE] = RS2_FORMAT_RAW8;   // libRS type
    _image_format[FISHEYE] = CV_8UC1;    // CVBridge type
    _encoding[FISHEYE] = sensor_msgs::image_encodings::TYPE_8UC1; // ROS message type
    _unit_step_size[FISHEYE] = sizeof(uint8_t); // sensor_msgs::ImagePtr row step size
    _stream_name[FISHEYE] = "fisheye";
    _depth_aligned_encoding[FISHEYE] = sensor_msgs::image_encodings::TYPE_16UC1;

    // Types for Motion-Module streams
    _is_frame_arrived[GYRO] = false;
    _format[GYRO] = RS2_FORMAT_MOTION_XYZ32F;   // libRS type
    _image_format[GYRO] = CV_8UC1;    // CVBridge type
    _encoding[GYRO] = sensor_msgs::image_encodings::TYPE_8UC1; // ROS message type
    _unit_step_size[GYRO] = sizeof(uint8_t); // sensor_msgs::ImagePtr row step size
    _stream_name[GYRO] = "gyro";

    _is_frame_arrived[ACCEL] = false;
    _format[ACCEL] = RS2_FORMAT_MOTION_XYZ32F;   // libRS type
    _image_format[ACCEL] = CV_8UC1;    // CVBridge type
    _encoding[ACCEL] = sensor_msgs::image_encodings::TYPE_8UC1; // ROS message type
    _unit_step_size[ACCEL] = sizeof(uint8_t); // sensor_msgs::ImagePtr row step size
    _stream_name[ACCEL] = "accel";
}

void BaseRealSenseNode::toggleSensors(bool enabled)
{
    for (auto it=_sensors.begin(); it != _sensors.end(); it++)
    {
        auto& sens = _sensors[it->first];
        try
        {
            if (enabled)
                sens.start(_syncer);
            else
                sens.stop();
        }
        catch(const rs2::wrong_api_call_sequence_error& ex)
        {
            ROS_DEBUG_STREAM("toggleSensors: " << ex.what());
        }
    }
}

void BaseRealSenseNode::publishTopics()
{
    getParameters();
    setupDevice();
    setupPublishers();
    setupStreams();
    setupFilters();
    publishStaticTransforms();
    ROS_INFO_STREAM("RealSense Node Is Up!");
}

void BaseRealSenseNode::registerDynamicReconfigCb()
{
    ROS_INFO("Dynamic reconfig parameters is not implemented in the base node.");
}

rs2_stream BaseRealSenseNode::rs2_string_to_stream(std::string str)
{
    if (str == "RS2_STREAM_ANY")
        return RS2_STREAM_ANY;
    if (str == "RS2_STREAM_COLOR")
        return RS2_STREAM_COLOR;
    if (str == "RS2_STREAM_INFRARED")
        return RS2_STREAM_INFRARED;
    if (str == "RS2_STREAM_FISHEYE")
        return RS2_STREAM_FISHEYE;
    throw std::runtime_error("Unknown stream string " + str);
}

void BaseRealSenseNode::getParameters()
{
    ROS_INFO("getParameters...");

    _pnh.param("align_depth", _align_depth, ALIGN_DEPTH);
    _pnh.param("enable_pointcloud", _pointcloud, POINTCLOUD);
    std::string pc_texture_stream("");
    int pc_texture_idx;
    _pnh.param("pointcloud_texture_stream", pc_texture_stream, std::string("RS2_STREAM_COLOR"));
    _pnh.param("pointcloud_texture_index", pc_texture_idx, 0);
    _pointcloud_texture = stream_index_pair{rs2_string_to_stream(pc_texture_stream), pc_texture_idx};

    _pnh.param("filters", _filters_str, DEFAULT_FILTERS);
    _pointcloud |= (_filters_str.find("pointcloud") != std::string::npos);

    _pnh.param("enable_sync", _sync_frames, SYNC_FRAMES);
    if (_pointcloud || _align_depth || _filters_str.size() > 0)
        _sync_frames = true;
    _pnh.param("ros_time_offset", _ros_time_offset, DEFAULT_ROS_TIME_OFFSET);

    _pnh.param("json_file_path", _json_file_path, std::string(""));

    _pnh.param("depth_width", _width[DEPTH], DEPTH_WIDTH);
    _pnh.param("depth_height", _height[DEPTH], DEPTH_HEIGHT);
    _pnh.param("depth_fps", _fps[DEPTH], DEPTH_FPS);
    _pnh.param("enable_depth", _enable[DEPTH], ENABLE_DEPTH);

    _pnh.param("infra1_width", _width[INFRA1], INFRA1_WIDTH);
    _pnh.param("infra1_height", _height[INFRA1], INFRA1_HEIGHT);
    _pnh.param("infra1_fps", _fps[INFRA1], INFRA1_FPS);
    _pnh.param("enable_infra1", _enable[INFRA1], ENABLE_INFRA1);

    _pnh.param("infra2_width", _width[INFRA2], INFRA2_WIDTH);
    _pnh.param("infra2_height", _height[INFRA2], INFRA2_HEIGHT);
    _pnh.param("infra2_fps", _fps[INFRA2], INFRA2_FPS);
    _pnh.param("enable_infra2", _enable[INFRA2], ENABLE_INFRA2);

    _pnh.param("color_width", _width[COLOR], COLOR_WIDTH);
    _pnh.param("color_height", _height[COLOR], COLOR_HEIGHT);
    _pnh.param("color_fps", _fps[COLOR], COLOR_FPS);
    _pnh.param("enable_color", _enable[COLOR], ENABLE_COLOR);

    _pnh.param("fisheye_width", _width[FISHEYE], FISHEYE_WIDTH);
    _pnh.param("fisheye_height", _height[FISHEYE], FISHEYE_HEIGHT);
    _pnh.param("fisheye_fps", _fps[FISHEYE], FISHEYE_FPS);
    _pnh.param("enable_fisheye", _enable[FISHEYE], ENABLE_FISHEYE);

    _pnh.param("gyro_fps", _fps[GYRO], GYRO_FPS);
    _pnh.param("accel_fps", _fps[ACCEL], ACCEL_FPS);
    _pnh.param("enable_imu", _enable[GYRO], ENABLE_IMU);
    _pnh.param("enable_imu", _enable[ACCEL], ENABLE_IMU);

    _pnh.param("base_frame_id", _base_frame_id, DEFAULT_BASE_FRAME_ID);
    _pnh.param("depth_frame_id", _frame_id[DEPTH], DEFAULT_DEPTH_FRAME_ID);
    _pnh.param("infra1_frame_id", _frame_id[INFRA1], DEFAULT_INFRA1_FRAME_ID);
    _pnh.param("infra2_frame_id", _frame_id[INFRA2], DEFAULT_INFRA2_FRAME_ID);
    _pnh.param("color_frame_id", _frame_id[COLOR], DEFAULT_COLOR_FRAME_ID);
    _pnh.param("fisheye_frame_id", _frame_id[FISHEYE], DEFAULT_FISHEYE_FRAME_ID);
    _pnh.param("imu_gyro_frame_id", _frame_id[GYRO], DEFAULT_IMU_FRAME_ID);
    _pnh.param("imu_accel_frame_id", _frame_id[ACCEL], DEFAULT_IMU_FRAME_ID);

    _pnh.param("depth_optical_frame_id", _optical_frame_id[DEPTH], DEFAULT_DEPTH_OPTICAL_FRAME_ID);
    _pnh.param("infra1_optical_frame_id", _optical_frame_id[INFRA1], DEFAULT_INFRA1_OPTICAL_FRAME_ID);
    _pnh.param("infra2_optical_frame_id", _optical_frame_id[INFRA2], DEFAULT_INFRA2_OPTICAL_FRAME_ID);
    _pnh.param("color_optical_frame_id", _optical_frame_id[COLOR], DEFAULT_COLOR_OPTICAL_FRAME_ID);
    _pnh.param("fisheye_optical_frame_id", _optical_frame_id[FISHEYE], DEFAULT_FISHEYE_OPTICAL_FRAME_ID);
    _pnh.param("gyro_optical_frame_id", _optical_frame_id[GYRO], DEFAULT_GYRO_OPTICAL_FRAME_ID);
    _pnh.param("accel_optical_frame_id", _optical_frame_id[ACCEL], DEFAULT_ACCEL_OPTICAL_FRAME_ID);

    _pnh.param("aligned_depth_to_color_frame_id",   _depth_aligned_frame_id[COLOR],   DEFAULT_ALIGNED_DEPTH_TO_COLOR_FRAME_ID);
    _pnh.param("aligned_depth_to_infra1_frame_id",  _depth_aligned_frame_id[INFRA1],  DEFAULT_ALIGNED_DEPTH_TO_INFRA1_FRAME_ID);
    _pnh.param("aligned_depth_to_infra2_frame_id",  _depth_aligned_frame_id[INFRA2],  DEFAULT_ALIGNED_DEPTH_TO_INFRA2_FRAME_ID);
    _pnh.param("aligned_depth_to_fisheye_frame_id", _depth_aligned_frame_id[FISHEYE], DEFAULT_ALIGNED_DEPTH_TO_FISHEYE_FRAME_ID);

    std::string inter_cam_sync_mode_param;
    _pnh.param("inter_cam_sync_mode", inter_cam_sync_mode_param, INTER_CAM_SYNC_MODE);
    std::transform(inter_cam_sync_mode_param.begin(), inter_cam_sync_mode_param.end(),
                   inter_cam_sync_mode_param.begin(), ::tolower);
    // note: added a "none" mode, as not all sensor types/firmware versions allow setting of the sync mode.
    //       Use "none" if nothing is specified or an error occurs.
    //       Default (mode = 0) here refers to the default sync mode as per Intel whitepaper,
    //       which corresponds to master mode but no trigger output on Pin 5.
    //       Master (mode = 1) activates trigger signal output on Pin 5.
    //       Slave (mode = 2) causes the realsense to listen to a trigger signal on pin 5.
    if(inter_cam_sync_mode_param == "default"){ _inter_cam_sync_mode = inter_cam_sync_default; }
    else if(inter_cam_sync_mode_param == "master") { _inter_cam_sync_mode = inter_cam_sync_master; }
    else if(inter_cam_sync_mode_param == "slave"){ _inter_cam_sync_mode = inter_cam_sync_slave; }
    else if(inter_cam_sync_mode_param == "none") { _inter_cam_sync_mode = inter_cam_sync_none; }
    else
    {
        _inter_cam_sync_mode = inter_cam_sync_none;
        ROS_WARN_STREAM("Invalid inter cam sync mode (" << inter_cam_sync_mode_param << ")! Not using inter cam sync mode.");
    }
}

void BaseRealSenseNode::setupDevice()
{
    ROS_INFO("setupDevice...");
    try{
        if (!_json_file_path.empty())
        {
            if (_dev.is<rs400::advanced_mode>())
            {
                std::stringstream ss;
                std::ifstream in(_json_file_path);
                if (in.is_open())
                {
                    ss << in.rdbuf();
                    std::string json_file_content = ss.str();

                    auto adv = _dev.as<rs400::advanced_mode>();
                    adv.load_json(json_file_content);
                    ROS_INFO_STREAM("JSON file is loaded! (" << _json_file_path << ")");
                }
                else
                    ROS_WARN_STREAM("JSON file provided doesn't exist! (" << _json_file_path << ")");
            }
            else
                ROS_WARN("Device does not support advanced settings!");
        }
        else
            ROS_INFO("JSON file is not provided");

        ROS_INFO_STREAM("ROS Node Namespace: " << _namespace);

        auto camera_name = _dev.get_info(RS2_CAMERA_INFO_NAME);
        ROS_INFO_STREAM("Device Name: " << camera_name);

        ROS_INFO_STREAM("Device Serial No: " << _serial_no);

        auto fw_ver = _dev.get_info(RS2_CAMERA_INFO_FIRMWARE_VERSION);
        ROS_INFO_STREAM("Device FW version: " << fw_ver);

        auto pid = _dev.get_info(RS2_CAMERA_INFO_PRODUCT_ID);
        ROS_INFO_STREAM("Device Product ID: 0x" << pid);

        ROS_INFO_STREAM("Enable PointCloud: " << ((_pointcloud)?"On":"Off"));
        ROS_INFO_STREAM("Align Depth: " << ((_align_depth)?"On":"Off"));
        ROS_INFO_STREAM("Sync Mode: " << ((_sync_frames)?"On":"Off"));

        auto dev_sensors = _dev.query_sensors();

        ROS_INFO_STREAM("Device Sensors: ");
        for(auto&& elem : dev_sensors)
        {
            std::string module_name = elem.get_info(RS2_CAMERA_INFO_NAME);
            if ("Stereo Module" == module_name)
            {
                _sensors[DEPTH] = elem;
                _sensors[INFRA1] = elem;
                _sensors[INFRA2] = elem;
            }
            else if ("Coded-Light Depth Sensor" == module_name)
            {
                _sensors[DEPTH] = elem;
                _sensors[INFRA1] = elem;
            }
            else if ("RGB Camera" == module_name)
            {
                _sensors[COLOR] = elem;
            }
            else if ("Wide FOV Camera" == module_name)
            {
                _sensors[FISHEYE] = elem;
            }
            else if ("Motion Module" == module_name)
            {
                _sensors[GYRO] = elem;
                _sensors[ACCEL] = elem;
            }
            else
            {
                ROS_ERROR_STREAM("Module Name \"" << module_name << "\" isn't supported by LibRealSense! Terminating RealSense Node...");
                ros::shutdown();
                exit(1);
            }
            ROS_INFO_STREAM(std::string(elem.get_info(RS2_CAMERA_INFO_NAME)) << " was found.");
        }

        // Update "enable" map
        std::vector<std::vector<stream_index_pair>> streams(IMAGE_STREAMS);
        streams.insert(streams.end(), HID_STREAMS.begin(), HID_STREAMS.end());
        for (auto& elem : streams)
        {
            for (auto& stream_index : elem)
            {
                if (_enable[stream_index] && _sensors.find(stream_index) == _sensors.end()) // check if device supports the enabled stream
                {
                    ROS_INFO_STREAM("(" << rs2_stream_to_string(stream_index.first) << ", " << stream_index.second << ") sensor isn't supported by current device! -- Skipping...");
                    _enable[stream_index] = false;
                }
            }
        }

        // set cam sync mode
        if(_inter_cam_sync_mode != inter_cam_sync_none)
        {
            _sensors[DEPTH].set_option(RS2_OPTION_INTER_CAM_SYNC_MODE, _inter_cam_sync_mode);
            ROS_INFO_STREAM("Inter cam sync mode set to " << _inter_cam_sync_mode);
        }

    }
    catch(const std::exception& ex)
    {
        ROS_ERROR_STREAM("An exception has been thrown: " << ex.what());
        throw;
    }
    catch(...)
    {
        ROS_ERROR_STREAM("Unknown exception has occured!");
        throw;
    }
}

void BaseRealSenseNode::setupPublishers()
{
    ROS_INFO("setupPublishers...");
    image_transport::ImageTransport image_transport(_node_handle);

    std::vector<stream_index_pair> image_stream_types;
    for (auto& stream_vec : IMAGE_STREAMS)
    {
        for (auto& stream : stream_vec)
        {
            image_stream_types.push_back(stream);
        }
    }

    for (auto& stream : image_stream_types)
    {
        if (_enable[stream])
        {

            if(!_counter_enabled){
                _counter_publisher = _node_handle.advertise<timestamp_corrector_msgs::IntStamped>("/depth/counter", 1);
                _counter_enabled = true;
            }

            std::stringstream image_raw, camera_info;
            bool rectified_image = false;
            if (stream == DEPTH || stream == INFRA1 || stream == INFRA2)
                rectified_image = true;

            image_raw << _stream_name[stream] << "/image_" << ((rectified_image)?"rect_":"") << "raw";
            camera_info << _stream_name[stream] << "/camera_info";

            std::shared_ptr<FrequencyDiagnostics> frequency_diagnostics(new FrequencyDiagnostics(_fps[stream], _stream_name[stream], _serial_no));
            _image_publishers[stream] = {image_transport.advertise(image_raw.str(), 1), frequency_diagnostics};
            _info_publisher[stream] = _node_handle.advertise<sensor_msgs::CameraInfo>(camera_info.str(), 1);

            if (_align_depth && (stream != DEPTH))
            {
                std::stringstream aligned_image_raw, aligned_camera_info;
                aligned_image_raw << "aligned_depth_to_" << _stream_name[stream] << "/image_raw";
                aligned_camera_info << "aligned_depth_to_" << _stream_name[stream] << "/camera_info";

                std::string aligned_stream_name = "aligned_depth_to_" + _stream_name[stream];
                std::shared_ptr<FrequencyDiagnostics> frequency_diagnostics(new FrequencyDiagnostics(_fps[stream], aligned_stream_name, _serial_no));
                _depth_aligned_image_publishers[stream] = {image_transport.advertise(aligned_image_raw.str(), 1), frequency_diagnostics};
                _depth_aligned_info_publisher[stream] = _node_handle.advertise<sensor_msgs::CameraInfo>(aligned_camera_info.str(), 1);
            }

            if (stream == DEPTH && _pointcloud)
            {
                _pointcloud_publisher = _node_handle.advertise<sensor_msgs::PointCloud2>("depth/color/points", 1);
            }
        }
    }

    if (_enable[FISHEYE] &&
        _enable[DEPTH])
    {
        _depth_to_other_extrinsics_publishers[FISHEYE] = _node_handle.advertise<Extrinsics>("extrinsics/depth_to_fisheye", 1, true);
    }

    if (_enable[COLOR] &&
        _enable[DEPTH])
    {
        _depth_to_other_extrinsics_publishers[COLOR] = _node_handle.advertise<Extrinsics>("extrinsics/depth_to_color", 1, true);
    }

    if (_enable[INFRA1] &&
        _enable[DEPTH])
    {
        _depth_to_other_extrinsics_publishers[INFRA1] = _node_handle.advertise<Extrinsics>("extrinsics/depth_to_infra1", 1, true);
    }

    if (_enable[INFRA2] &&
        _enable[DEPTH])
    {
        _depth_to_other_extrinsics_publishers[INFRA2] = _node_handle.advertise<Extrinsics>("extrinsics/depth_to_infra2", 1, true);
    }

    if (_enable[GYRO])
    {
        _imu_publishers[GYRO] = _node_handle.advertise<sensor_msgs::Imu>("gyro/sample", 100);
        _info_publisher[GYRO] = _node_handle.advertise<IMUInfo>("gyro/imu_info", 1, true);
    }

    if (_enable[ACCEL])
    {
        _imu_publishers[ACCEL] = _node_handle.advertise<sensor_msgs::Imu>("accel/sample", 100);
        _info_publisher[ACCEL] = _node_handle.advertise<IMUInfo>("accel/imu_info", 1, true);
    }
}

void BaseRealSenseNode::updateIsFrameArrived(std::map<stream_index_pair, bool>& is_frame_arrived,
                                             rs2_stream stream_type, int stream_index)
{
    try
    {
        is_frame_arrived.at({stream_type, stream_index}) = true;
    }
    catch (std::out_of_range)
    {
        ROS_ERROR_STREAM("Stream type is not supported! (" << stream_type << ", " << stream_index << ")");
    }
}

void BaseRealSenseNode::publishAlignedDepthToOthers(rs2::frameset frames, const ros::Time& t)
{
    for (auto it = frames.begin(); it != frames.end(); ++it)
    {
        auto frame = (*it);
        auto stream_type = frame.get_profile().stream_type();

        if (RS2_STREAM_DEPTH == stream_type)
            continue;

        auto stream_index = frame.get_profile().stream_index();
        stream_index_pair sip{stream_type, stream_index};
        auto& info_publisher = _depth_aligned_info_publisher.at(sip);
        auto& image_publisher = _depth_aligned_image_publishers.at(sip);

        if(0 != info_publisher.getNumSubscribers() ||
           0 != image_publisher.first.getNumSubscribers())
        {
            rs2::align align(stream_type);
            rs2::frameset processed = frames.apply_filter(align);
            rs2::depth_frame aligned_depth_frame = processed.get_depth_frame();

            publishFrame(aligned_depth_frame, t, sip,
                         _depth_aligned_image,
                         _depth_aligned_info_publisher,
                         _depth_aligned_image_publishers, _depth_aligned_seq,
                         _depth_aligned_camera_info, _optical_frame_id,
                         _depth_aligned_encoding);
        }
    }
}

void BaseRealSenseNode::enable_devices()
{
	for (auto& streams : IMAGE_STREAMS)
	{
		for (auto& elem : streams)
		{
			if (_enable[elem])
			{
				auto& sens = _sensors[elem];
				auto profiles = sens.get_stream_profiles();
				for (auto& profile : profiles)
				{
					auto video_profile = profile.as<rs2::video_stream_profile>();
					ROS_DEBUG_STREAM("Sensor profile: " <<
									 "Format: " << video_profile.format() <<
									 ", Width: " << video_profile.width() <<
									 ", Height: " << video_profile.height() <<
									 ", FPS: " << video_profile.fps());

					if (video_profile.format() == _format[elem] &&
						(_width[elem] == 0 || video_profile.width() == _width[elem]) &&
						(_height[elem] == 0 || video_profile.height() == _height[elem]) &&
						(_fps[elem] == 0 || video_profile.fps() == _fps[elem]) &&
						video_profile.stream_index() == elem.second)
					{
						_width[elem] = video_profile.width();
						_height[elem] = video_profile.height();
						_fps[elem] = video_profile.fps();

						_enabled_profiles[elem].push_back(profile);

						_image[elem] = cv::Mat(_height[elem], _width[elem], _image_format[elem], cv::Scalar(0, 0, 0));

						ROS_INFO_STREAM(_stream_name[elem] << " stream is enabled - width: " << _width[elem] << ", height: " << _height[elem] << ", fps: " << _fps[elem]);
						break;
					}
				}
				if (_enabled_profiles.find(elem) == _enabled_profiles.end())
				{
					ROS_WARN_STREAM("Given stream configuration is not supported by the device! " <<
						" Stream: " << rs2_stream_to_string(elem.first) <<
						", Stream Index: " << elem.second <<
						", Format: " << _format[elem] <<
						", Width: " << _width[elem] <<
						", Height: " << _height[elem] <<
						", FPS: " << _fps[elem]);
					_enable[elem] = false;
				}
			}
		}
	}
	if (_align_depth)
	{
		for (auto& profiles : _enabled_profiles)
		{
			_depth_aligned_image[profiles.first] = cv::Mat(_height[DEPTH], _width[DEPTH], _image_format[DEPTH], cv::Scalar(0, 0, 0));
		}
	}
}

void BaseRealSenseNode::setupFilters()
{
    std::vector<std::string> filters_str;
    boost::split(filters_str, _filters_str, [](char c){return c == ',';});
    bool use_disparity_filter(false);
    bool use_colorizer_filter(false);
    for (std::vector<std::string>::const_iterator s_iter=filters_str.begin(); s_iter!=filters_str.end(); s_iter++)
    {
        if ((*s_iter) == "colorizer")
        {
            use_colorizer_filter = true;
        }
        else if ((*s_iter) == "disparity")
        {
            use_disparity_filter = true;
        }
        else if ((*s_iter) == "spatial")
        {
            ROS_INFO("Add Filter: spatial");
            _filters.push_back(NamedFilter("spatial", std::make_shared<rs2::spatial_filter>()));
        }
        else if ((*s_iter) == "temporal")
        {
            ROS_INFO("Add Filter: temporal");
            _filters.push_back(NamedFilter("temporal", std::make_shared<rs2::temporal_filter>()));
        }
        else if ((*s_iter) == "decimation")
        {
            ROS_INFO("Add Filter: decimation");
            _filters.push_back(NamedFilter("decimation", std::make_shared<rs2::decimation_filter>()));
        }
        else if ((*s_iter) == "pointcloud")
        {
            assert(_pointcloud); // For now, it is set in getParameters()..
        }
        else if ((*s_iter).size() > 0)
        {
            ROS_ERROR_STREAM("Unknown Filter: " << (*s_iter));
            throw;
        }
    }
    if (use_disparity_filter)
    {
        ROS_INFO("Add Filter: disparity");
        _filters.insert(_filters.begin(), NamedFilter("disparity_start", std::make_shared<rs2::disparity_transform>()));
        _filters.push_back(NamedFilter("disparity_end", std::make_shared<rs2::disparity_transform>(false)));
        ROS_INFO("Done Add Filter: disparity");
    }
    if (use_colorizer_filter)
    {
        ROS_INFO("Add Filter: colorizer");
        _filters.push_back(NamedFilter("colorizer", std::make_shared<rs2::colorizer>()));

        // Types for depth stream
        _format[DEPTH] = _format[COLOR];   // libRS type
        _image_format[DEPTH] = _image_format[COLOR];    // CVBridge type
        _encoding[DEPTH] = _encoding[COLOR]; // ROS message type
        _unit_step_size[DEPTH] = _unit_step_size[COLOR]; // sensor_msgs::ImagePtr row step size

        _width[DEPTH] = _width[COLOR];
        _height[DEPTH] = _height[COLOR];
        _image[DEPTH] = cv::Mat(_height[DEPTH], _width[DEPTH], _image_format[DEPTH], cv::Scalar(0, 0, 0));
    }
    if (_pointcloud)
    {
    	ROS_INFO("Add Filter: pointcloud");
        _filters.push_back(NamedFilter("pointcloud", std::make_shared<rs2::pointcloud>(_pointcloud_texture.first, _pointcloud_texture.second)));
    }
    ROS_INFO("num_filters: %d", static_cast<int>(_filters.size()));
}

void BaseRealSenseNode::setupStreams()
{
	ROS_INFO("setupStreams...");
	enable_devices();
    try{
		// Publish image stream info
        for (auto& profiles : _enabled_profiles)
        {
            for (auto& profile : profiles.second)
            {
                auto video_profile = profile.as<rs2::video_stream_profile>();
                updateStreamCalibData(video_profile);
            }
        }

        auto frame_callback = [this](rs2::frame frame)
        {
            try{
                // We compute a ROS timestamp which is based on an initial ROS time at point of first frame,
                // and the incremental timestamp from the camera.
                // In sync mode the timestamp is based on ROS time
                if (false == _intialize_time_base)
                {
                    if (RS2_TIMESTAMP_DOMAIN_SYSTEM_TIME == frame.get_frame_timestamp_domain())
                        ROS_WARN("Frame metadata isn't available! (frame_timestamp_domain = RS2_TIMESTAMP_DOMAIN_SYSTEM_TIME)");

                    _intialize_time_base = true;
                    _ros_time_base = ros::Time::now();
                    _camera_time_base = frame.get_timestamp();
                }

                ros::Time t;
                if (_sync_frames)
                    t = ros::Time::now() + ros::Duration(_ros_time_offset);
                else
                    t = ros::Time(_ros_time_base.toSec()+ (/*ms*/ frame.get_timestamp() - /*ms*/ _camera_time_base) / /*ms to seconds*/ 1000);

                std::map<stream_index_pair, bool> is_frame_arrived(_is_frame_arrived);
                if (frame.is<rs2::frameset>())
                {
                    ROS_DEBUG("Frameset arrived.");
                    bool is_depth_arrived = false;
                    rs2::frame depth_frame;
                    auto frameset = frame.as<rs2::frameset>();
                    ROS_DEBUG("List of frameset before applying filters: size: %d", static_cast<int>(frameset.size()));
                    for (auto it = frameset.begin(); it != frameset.end(); ++it)
                    {
                        auto f = (*it);
                        auto stream_type = f.get_profile().stream_type();
                        auto stream_index = f.get_profile().stream_index();
                        auto stream_format = f.get_profile().format();
                        auto stream_unique_id = f.get_profile().unique_id();
                        updateIsFrameArrived(is_frame_arrived, stream_type, stream_index);

                        ROS_DEBUG("Frameset contain (%s, %d, %s %d) frame. frame_number: %llu ; frame_TS: %f ; ros_TS(NSec): %lu",
                                  rs2_stream_to_string(stream_type), stream_index, rs2_format_to_string(stream_format), stream_unique_id, frame.get_frame_number(), frame.get_timestamp(), t.toNSec());
                    }
                    ROS_DEBUG("num_filters: %d", static_cast<int>(_filters.size()));
                    for (std::vector<NamedFilter>::const_iterator filter_it = _filters.begin(); filter_it != _filters.end(); filter_it++)
                    {
                        ROS_DEBUG("Applying filter: %s", filter_it->_name.c_str());
                        frameset = filter_it->_filter->process(frameset);
                    }

                    ROS_DEBUG("List of frameset after applying filters: size: %d", static_cast<int>(frameset.size()));
                    for (auto it = frameset.begin(); it != frameset.end(); ++it)
                    {
                        auto f = (*it);
                        auto stream_type = f.get_profile().stream_type();
                        auto stream_index = f.get_profile().stream_index();
                        auto stream_format = f.get_profile().format();
                        auto stream_unique_id = f.get_profile().unique_id();

                        ROS_DEBUG("Frameset contain (%s, %d, %s %d) frame. frame_number: %llu ; frame_TS: %f ; ros_TS(NSec): %lu",
                                  rs2_stream_to_string(stream_type), stream_index, rs2_format_to_string(stream_format), stream_unique_id, frame.get_frame_number(), frame.get_timestamp(), t.toNSec());
                    }
                    ROS_DEBUG("END OF LIST");
                    ROS_DEBUG_STREAM("Remove streams with same type and index:");
                    // TODO - Fix the following issue:
                    // Currently publishers are set using a map of stream type and index only.
                    // It means that colorized depth image <DEPTH, 0, Z16> and colorized depth image <DEPTH, 0, RGB>
                    // use the same publisher.
                    // As a workaround we remove the earlier one, the original one, assuming that if colorizer filter is
                    // set it means that that's what the client wants.
                    // However, that procedure also eliminates the pointcloud <DEPTH, 0, XYZ32F>, although it uses
                    // another publisher.
                    // That's why currently it can't send both pointcloud and colorized depth image.
                    //
                    bool points_in_set(false);
                    std::vector<rs2::frame> frames_to_publish;
                    std::vector<stream_index_pair> is_in_set;
                    for (auto it = frameset.begin(); it != frameset.end(); ++it)
                    {
                        auto f = (*it);
                        auto stream_type = f.get_profile().stream_type();
                        auto stream_index = f.get_profile().stream_index();
                        auto stream_format = f.get_profile().format();
                        if (f.is<rs2::points>())
                        {
                            if (!points_in_set)
                            {
                                points_in_set = true;
                                frames_to_publish.push_back(f);
                            }
                            continue;
                        }
                        stream_index_pair sip{stream_type,stream_index};
                        if (std::find(is_in_set.begin(), is_in_set.end(), sip) == is_in_set.end())
                        {
                            is_in_set.push_back(sip);
                            frames_to_publish.push_back(f);
                        }
                        if (_align_depth && stream_type == RS2_STREAM_DEPTH && stream_format == RS2_FORMAT_Z16)
                        {
                            depth_frame = f;
                            is_depth_arrived = true;
                        }
                    }

                    for (auto it = frames_to_publish.begin(); it != frames_to_publish.end(); ++it)
                    {
                        auto f = (*it);
                        auto stream_type = f.get_profile().stream_type();
                        auto stream_index = f.get_profile().stream_index();
                        auto stream_format = f.get_profile().format();

                        ROS_DEBUG("Frameset contain (%s, %d, %s) frame. frame_number: %llu ; frame_TS: %f ; ros_TS(NSec): %lu",
                                  rs2_stream_to_string(stream_type), stream_index, rs2_format_to_string(stream_format), frame.get_frame_number(), frame.get_timestamp(), t.toNSec());

                        if (f.is<rs2::points>())
                        {
                            if (0 != _pointcloud_publisher.getNumSubscribers())
                            {
                                ROS_DEBUG("Publish pointscloud");
                                publishPointCloud(f.as<rs2::points>(), t, frameset);
                            }
                            continue;
                        }
                        else
                        {
                            ROS_DEBUG("Not points");
                        }
                        stream_index_pair sip{stream_type,stream_index};
                        publishFrame(f, t,
                                     sip,
                                     _image,
                                     _info_publisher,
                                     _image_publishers, _seq,
                                     _camera_info, _optical_frame_id,
                                     _encoding);
                    }

                    if (_align_depth && is_depth_arrived)
                    {
                        ROS_DEBUG("publishAlignedDepthToOthers(...)");
                        publishAlignedDepthToOthers(frameset, t);
                    }
                }
                else
                {
                    auto stream_type = frame.get_profile().stream_type();
                    auto stream_index = frame.get_profile().stream_index();
                    updateIsFrameArrived(is_frame_arrived, stream_type, stream_index);
                    ROS_DEBUG("Single video frame arrived (%s, %d). frame_number: %llu ; frame_TS: %f ; ros_TS(NSec): %lu",
                              rs2_stream_to_string(stream_type), stream_index, frame.get_frame_number(), frame.get_timestamp(), t.toNSec());

                    stream_index_pair sip{stream_type,stream_index};
                    publishFrame(frame, t,
                                 sip,
                                 _image,
                                 _info_publisher,
                                 _image_publishers, _seq,
                                 _camera_info, _optical_frame_id,
                                 _encoding);
                }

                if(_counter_enabled && _send_counter){
                    timestamp_corrector_msgs::IntStamped image_counter_msg;

                    image_counter_msg.header.stamp = t;
                    image_counter_msg.counter = _image_counter;
                    _counter_publisher.publish(image_counter_msg);
                    ROS_DEBUG("Publishing Counter %d", _image_counter);
                    _image_counter++;
                    _send_counter = false;
                }


            }
            catch(const std::exception& ex)
            {
                ROS_ERROR_STREAM("An error has occurred during frame callback: " << ex.what());
            }
        }; // frame_callback

        // Streaming IMAGES
        for (auto& streams : IMAGE_STREAMS)
        {
            std::vector<rs2::stream_profile> profiles;
            for (auto& elem : streams)
            {
                if (!_enabled_profiles[elem].empty())
                {
                    profiles.insert(profiles.begin(),
                                    _enabled_profiles[elem].begin(),
                                    _enabled_profiles[elem].end());
                }
            }

            if (!profiles.empty())
            {
                auto stream = streams.front();
                auto& sens = _sensors[stream];
                sens.open(profiles);

                if (DEPTH == stream)
                {
                    auto depth_sensor = sens.as<rs2::depth_sensor>();
                    _depth_scale_meters = depth_sensor.get_depth_scale();
                }
                if (_sync_frames)
                {
                    sens.start(_syncer);
                }
                else
                {
                    sens.start(frame_callback);
                }
            }
        }//end for

        if (_sync_frames)
        {
            _syncer.start(frame_callback);
        }

        // Streaming HID
        for (const auto streams : HID_STREAMS)
        {
            for (auto& elem : streams)
            {
                if (_enable[elem])
                {
                    auto& sens = _sensors[elem];
                    auto profiles = sens.get_stream_profiles();
                    for (rs2::stream_profile& profile : profiles)
                    {
                        if (profile.fps() == _fps[elem] &&
                            profile.format() == _format[elem])
                        {
                            _enabled_profiles[elem].push_back(profile);
                            break;
                        }
                    }
                }
            }
        }

        auto gyro_profile = _enabled_profiles.find(GYRO);
        auto accel_profile = _enabled_profiles.find(ACCEL);

        if (gyro_profile != _enabled_profiles.end() &&
            accel_profile != _enabled_profiles.end())
        {
            std::vector<rs2::stream_profile> profiles;
            profiles.insert(profiles.begin(), gyro_profile->second.begin(), gyro_profile->second.end());
            profiles.insert(profiles.begin(), accel_profile->second.begin(), accel_profile->second.end());
            auto& sens = _sensors[GYRO];
            sens.open(profiles);

            sens.start([this](rs2::frame frame){
                auto stream = frame.get_profile().stream_type();
                if (false == _intialize_time_base)
                    return;

                ROS_DEBUG("Frame arrived: stream: %s ; index: %d ; Timestamp Domain: %s",
                          rs2_stream_to_string(frame.get_profile().stream_type()),
                          frame.get_profile().stream_index(),
                          rs2_timestamp_domain_to_string(frame.get_frame_timestamp_domain()));

                auto stream_index = (stream == GYRO.first)?GYRO:ACCEL;
                if (0 != _info_publisher[stream_index].getNumSubscribers() ||
                    0 != _imu_publishers[stream_index].getNumSubscribers())
                {
                    double elapsed_camera_ms = (/*ms*/ frame.get_timestamp() - /*ms*/ _camera_time_base) / /*ms to seconds*/ 1000;
                    ros::Time t(_ros_time_base.toSec() + elapsed_camera_ms);

                    auto imu_msg = sensor_msgs::Imu();
                    imu_msg.header.frame_id = _optical_frame_id[stream_index];
                    imu_msg.orientation.x = 0.0;
                    imu_msg.orientation.y = 0.0;
                    imu_msg.orientation.z = 0.0;
                    imu_msg.orientation.w = 0.0;
                    imu_msg.orientation_covariance = { -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

                    auto axes = *(reinterpret_cast<const float3*>(frame.get_data()));
                    if (GYRO == stream_index)
                    {
                        imu_msg.angular_velocity.x = axes.x;
                        imu_msg.angular_velocity.y = axes.y;
                        imu_msg.angular_velocity.z = axes.z;
                    }
                    else if (ACCEL == stream_index)
                    {
                        imu_msg.linear_acceleration.x = axes.x;
                        imu_msg.linear_acceleration.y = axes.y;
                        imu_msg.linear_acceleration.z = axes.z;
                    }
                    _seq[stream_index] += 1;
                    imu_msg.header.seq = _seq[stream_index];
                    imu_msg.header.stamp = t;
                    _imu_publishers[stream_index].publish(imu_msg);
                    ROS_DEBUG("Publish %s stream", rs2_stream_to_string(frame.get_profile().stream_type()));
                }
            });

            if (_enable[GYRO])
            {
                ROS_INFO_STREAM(_stream_name[GYRO] << " stream is enabled - " << "fps: " << _fps[GYRO]);
                auto gyroInfo = getImuInfo(GYRO);
                _info_publisher[GYRO].publish(gyroInfo);
            }

            if (_enable[ACCEL])
            {
                ROS_INFO_STREAM(_stream_name[ACCEL] << " stream is enabled - " << "fps: " << _fps[ACCEL]);
                auto accelInfo = getImuInfo(ACCEL);
                _info_publisher[ACCEL].publish(accelInfo);
            }
        }

        if (_enable[DEPTH] &&
            _enable[FISHEYE])
        {
            static const char* frame_id = "depth_to_fisheye_extrinsics";
            auto ex = getRsExtrinsics(DEPTH, FISHEYE);
            _depth_to_other_extrinsics[FISHEYE] = ex;
            _depth_to_other_extrinsics_publishers[FISHEYE].publish(rsExtrinsicsToMsg(ex, frame_id));
        }

        if (_enable[DEPTH] &&
            _enable[COLOR])
        {
            static const char* frame_id = "depth_to_color_extrinsics";
            auto ex = getRsExtrinsics(DEPTH, COLOR);
            _depth_to_other_extrinsics[COLOR] = ex;
            _depth_to_other_extrinsics_publishers[COLOR].publish(rsExtrinsicsToMsg(ex, frame_id));
        }

        if (_enable[DEPTH] &&
            _enable[INFRA1])
        {
            static const char* frame_id = "depth_to_infra1_extrinsics";
            auto ex = getRsExtrinsics(DEPTH, INFRA1);
            _depth_to_other_extrinsics[INFRA1] = ex;
            _depth_to_other_extrinsics_publishers[INFRA1].publish(rsExtrinsicsToMsg(ex, frame_id));
        }

        if (_enable[DEPTH] &&
            _enable[INFRA2])
        {
            static const char* frame_id = "depth_to_infra2_extrinsics";
            auto ex = getRsExtrinsics(DEPTH, INFRA2);
            _depth_to_other_extrinsics[INFRA2] = ex;
            _depth_to_other_extrinsics_publishers[INFRA2].publish(rsExtrinsicsToMsg(ex, frame_id));
        }
    }
    catch(const std::exception& ex)
    {
        ROS_ERROR_STREAM("An exception has been thrown: " << ex.what());
        throw;
    }
    catch(...)
    {
        ROS_ERROR_STREAM("Unknown exception has occured!");
        throw;
    }
}

void BaseRealSenseNode::updateStreamCalibData(const rs2::video_stream_profile& video_profile)
{
    stream_index_pair stream_index{video_profile.stream_type(), video_profile.stream_index()};
    auto intrinsic = video_profile.get_intrinsics();
    _stream_intrinsics[stream_index] = intrinsic;
    _camera_info[stream_index].width = intrinsic.width;
    _camera_info[stream_index].height = intrinsic.height;
    _camera_info[stream_index].header.frame_id = _optical_frame_id[stream_index];

    _camera_info[stream_index].K.at(0) = intrinsic.fx;
    _camera_info[stream_index].K.at(2) = intrinsic.ppx;
    _camera_info[stream_index].K.at(4) = intrinsic.fy;
    _camera_info[stream_index].K.at(5) = intrinsic.ppy;
    _camera_info[stream_index].K.at(8) = 1;

    _camera_info[stream_index].P.at(0) = _camera_info[stream_index].K.at(0);
    _camera_info[stream_index].P.at(1) = 0;
    _camera_info[stream_index].P.at(2) = _camera_info[stream_index].K.at(2);
    _camera_info[stream_index].P.at(3) = 0;
    _camera_info[stream_index].P.at(4) = 0;
    _camera_info[stream_index].P.at(5) = _camera_info[stream_index].K.at(4);
    _camera_info[stream_index].P.at(6) = _camera_info[stream_index].K.at(5);
    _camera_info[stream_index].P.at(7) = 0;
    _camera_info[stream_index].P.at(8) = 0;
    _camera_info[stream_index].P.at(9) = 0;
    _camera_info[stream_index].P.at(10) = 1;
    _camera_info[stream_index].P.at(11) = 0;

    rs2::stream_profile depth_profile;
    if (!getEnabledProfile(DEPTH, depth_profile))
    {
        ROS_ERROR_STREAM("Given depth profile is not supported by current device!");
        ros::shutdown();
        exit(1);
    }


    _camera_info[stream_index].distortion_model = "plumb_bob";

    // set R (rotation matrix) values to identity matrix
    _camera_info[stream_index].R.at(0) = 1.0;
    _camera_info[stream_index].R.at(1) = 0.0;
    _camera_info[stream_index].R.at(2) = 0.0;
    _camera_info[stream_index].R.at(3) = 0.0;
    _camera_info[stream_index].R.at(4) = 1.0;
    _camera_info[stream_index].R.at(5) = 0.0;
    _camera_info[stream_index].R.at(6) = 0.0;
    _camera_info[stream_index].R.at(7) = 0.0;
    _camera_info[stream_index].R.at(8) = 1.0;

    for (int i = 0; i < 5; i++)
    {
        _camera_info[stream_index].D.push_back(intrinsic.coeffs[i]);
    }

    if (stream_index == DEPTH && _enable[DEPTH] && _enable[COLOR])
    {
        _camera_info[stream_index].P.at(3) = 0;     // Tx
        _camera_info[stream_index].P.at(7) = 0;     // Ty
    }

    if (_align_depth)
    {
        for (auto& profiles : _enabled_profiles)
        {
            for (auto& profile : profiles.second)
            {
                auto video_profile = profile.as<rs2::video_stream_profile>();
                stream_index_pair stream_index{video_profile.stream_type(), video_profile.stream_index()};
                _depth_aligned_camera_info[stream_index] = _camera_info[stream_index];
            }
        }
    }
}

tf::Quaternion BaseRealSenseNode::rotationMatrixToQuaternion(const float rotation[9]) const
{
    Eigen::Matrix3f m;
    // We need to be careful about the order, as RS2 rotation matrix is
    // column-major, while Eigen::Matrix3f expects row-major.
    m << rotation[0], rotation[3], rotation[6],
         rotation[1], rotation[4], rotation[7],
         rotation[2], rotation[5], rotation[8];
    Eigen::Quaternionf q(m);
    return tf::Quaternion(q.x(), q.y(), q.z(), q.w());
}

void BaseRealSenseNode::publish_static_tf(const ros::Time& t,
                                          const float3& trans,
                                          const quaternion& q,
                                          const std::string& from,
                                          const std::string& to)
{
    geometry_msgs::TransformStamped msg;
    msg.header.stamp = t;
    msg.header.frame_id = from;
    msg.child_frame_id = to;
    msg.transform.translation.x = trans.z;
    msg.transform.translation.y = -trans.x;
    msg.transform.translation.z = -trans.y;
    msg.transform.rotation.x = q.x;
    msg.transform.rotation.y = q.y;
    msg.transform.rotation.z = q.z;
    msg.transform.rotation.w = q.w;
    _static_tf_broadcaster.sendTransform(msg);
}

void BaseRealSenseNode::publishStaticTransforms()
{
    ROS_INFO("publishStaticTransforms...");
    // Publish static transforms
    tf::Quaternion quaternion_optical;
    quaternion_optical.setRPY(-M_PI / 2, 0.0, -M_PI / 2);

    // Get the current timestamp for all static transforms
    ros::Time transform_ts_ = ros::Time::now();

    // The depth frame is used as the base link.
    // Hence no additional transformation is done from base link to depth frame.
    // Transform base link to depth frame
    float3 zero_trans{0, 0, 0};
    publish_static_tf(transform_ts_, zero_trans, quaternion{0, 0, 0, 1}, _base_frame_id, _frame_id[DEPTH]);

    // Transform depth frame to depth optical frame
    quaternion q{quaternion_optical.getX(), quaternion_optical.getY(), quaternion_optical.getZ(), quaternion_optical.getW()};
    publish_static_tf(transform_ts_, zero_trans, q, _frame_id[DEPTH], _optical_frame_id[DEPTH]);

    rs2::stream_profile depth_profile;
    if (!getEnabledProfile(DEPTH, depth_profile))
    {
        ROS_ERROR_STREAM("Given depth profile is not supported by current device!");
        ros::shutdown();
        exit(1);
    }


    if (_enable[COLOR])
    {
        // Transform base to color
        const auto& ex = getRsExtrinsics(COLOR, DEPTH);
        auto Q = rotationMatrixToQuaternion(ex.rotation);
        Q = quaternion_optical * Q * quaternion_optical.inverse();

        float3 trans{ex.translation[0], ex.translation[1], ex.translation[2]};
        quaternion q1{Q.getX(), Q.getY(), Q.getZ(), Q.getW()};
        publish_static_tf(transform_ts_, trans, q1, _base_frame_id, _frame_id[COLOR]);

        // Transform color frame to color optical frame
        quaternion q2{quaternion_optical.getX(), quaternion_optical.getY(), quaternion_optical.getZ(), quaternion_optical.getW()};
        publish_static_tf(transform_ts_, zero_trans, q2, _frame_id[COLOR], _optical_frame_id[COLOR]);

        if (_align_depth)
        {
            publish_static_tf(transform_ts_, trans, q1, _base_frame_id, _depth_aligned_frame_id[COLOR]);
            publish_static_tf(transform_ts_, zero_trans, q2, _depth_aligned_frame_id[COLOR], _optical_frame_id[COLOR]);
        }
    }

    if (_enable[INFRA1])
    {
        const auto& ex = getRsExtrinsics(INFRA1, DEPTH);
        auto Q = rotationMatrixToQuaternion(ex.rotation);
        Q = quaternion_optical * Q * quaternion_optical.inverse();

        // Transform base to infra1
        float3 trans{ex.translation[0], ex.translation[1], ex.translation[2]};
        quaternion q1{Q.getX(), Q.getY(), Q.getZ(), Q.getW()};
        publish_static_tf(transform_ts_, trans, q1, _base_frame_id, _frame_id[INFRA1]);

        // Transform infra1 frame to infra1 optical frame
        quaternion q2{quaternion_optical.getX(), quaternion_optical.getY(), quaternion_optical.getZ(), quaternion_optical.getW()};
        publish_static_tf(transform_ts_, zero_trans, q2, _frame_id[INFRA1], _optical_frame_id[INFRA1]);

        if (_align_depth)
        {
            publish_static_tf(transform_ts_, trans, q1, _base_frame_id, _depth_aligned_frame_id[INFRA1]);
            publish_static_tf(transform_ts_, zero_trans, q2, _depth_aligned_frame_id[INFRA1], _optical_frame_id[INFRA1]);
        }
    }

    if (_enable[INFRA2])
    {
        const auto& ex = getRsExtrinsics(INFRA2, DEPTH);
        auto Q = rotationMatrixToQuaternion(ex.rotation);
        Q = quaternion_optical * Q * quaternion_optical.inverse();

        // Transform base to infra2
        float3 trans{ex.translation[0], ex.translation[1], ex.translation[2]};
        quaternion q1{Q.getX(), Q.getY(), Q.getZ(), Q.getW()};
        publish_static_tf(transform_ts_, trans, q1, _base_frame_id, _frame_id[INFRA2]);

        // Transform infra2 frame to infra1 optical frame
        quaternion q2{quaternion_optical.getX(), quaternion_optical.getY(), quaternion_optical.getZ(), quaternion_optical.getW()};
        publish_static_tf(transform_ts_, zero_trans, q2, _frame_id[INFRA2], _optical_frame_id[INFRA2]);

        if (_align_depth)
        {
            publish_static_tf(transform_ts_, trans, q1, _base_frame_id, _depth_aligned_frame_id[INFRA2]);
            publish_static_tf(transform_ts_, zero_trans, q2, _depth_aligned_frame_id[INFRA2], _optical_frame_id[INFRA2]);
        }
    }

    if (_enable[FISHEYE])
    {
        const auto& ex = getRsExtrinsics(FISHEYE, DEPTH);
        auto Q = rotationMatrixToQuaternion(ex.rotation);
        Q = quaternion_optical * Q * quaternion_optical.inverse();

        // Transform base to infra2
        float3 trans{ex.translation[0], ex.translation[1], ex.translation[2]};
        quaternion q1{Q.getX(), Q.getY(), Q.getZ(), Q.getW()};
        publish_static_tf(transform_ts_, trans, q1, _base_frame_id, _frame_id[FISHEYE]);

        // Transform infra2 frame to infra1 optical frame
        quaternion q2{quaternion_optical.getX(), quaternion_optical.getY(), quaternion_optical.getZ(), quaternion_optical.getW()};
        publish_static_tf(transform_ts_, zero_trans, q2, _frame_id[FISHEYE], _optical_frame_id[FISHEYE]);

        if (_align_depth)
        {
            publish_static_tf(transform_ts_, trans, q1, _base_frame_id, _depth_aligned_frame_id[FISHEYE]);
            publish_static_tf(transform_ts_, zero_trans, q2, _depth_aligned_frame_id[FISHEYE], _optical_frame_id[FISHEYE]);
        }
    }
}

rs2::frame BaseRealSenseNode::get_frame(const rs2::frameset& frameset, const rs2_stream stream, const int index)
{
    rs2::frame f;
    frameset.foreach([&f, index, stream](const rs2::frame& frame) {
        if (frame.get_profile().stream_type() == stream && frame.get_profile().stream_index() == index)
            f = frame;
    });
    return f;
}


void BaseRealSenseNode::publishPointCloud(rs2::points pc, const ros::Time& t, const rs2::frameset& frameset)
{
    bool use_texture = (_pointcloud_texture.first != RS2_STREAM_ANY);
    unsigned char* color_data;
    int texture_width(0), texture_height(0);
    unsigned char no_color[3] = { 255, 255, 255 };
    if (use_texture)
    {
        rs2::frame temp_frame = get_frame(frameset, _pointcloud_texture.first, _pointcloud_texture.second).as<rs2::video_frame>();
        if (!temp_frame.is<rs2::video_frame>())
        {
            ROS_DEBUG_STREAM("texture frame not found");
            return;
        }

        rs2::video_frame texture_frame = temp_frame.as<rs2::video_frame>();
        color_data = (uint8_t*)texture_frame.get_data();
        texture_width = texture_frame.get_width();
        texture_height = texture_frame.get_height();
        assert(texture_frame.get_bytes_per_pixel() == 3); // TODO: Need to support IR image texture.
    }
    else
    {
        color_data = no_color;;
    }

    const rs2::texture_coordinate* color_point = pc.get_texture_coordinates();
    int num_valid_points(0);
    for (size_t point_idx=0; point_idx < pc.size(); point_idx++, color_point++)
    {
        float i = static_cast<float>(color_point->u);
        float j = static_cast<float>(color_point->v);

        if (i >= 0.f && i <= 1.f && j >= 0.f && j <= 1.f)
        {
            num_valid_points++;
        }
    }

    sensor_msgs::PointCloud2 msg_pointcloud;
    msg_pointcloud.header.stamp = t;
    msg_pointcloud.header.frame_id = _optical_frame_id[DEPTH];
    msg_pointcloud.width = num_valid_points;
    msg_pointcloud.height = 1;
    msg_pointcloud.is_dense = true;

    sensor_msgs::PointCloud2Modifier modifier(msg_pointcloud);

    modifier.setPointCloud2Fields(4,
                                  "x", 1, sensor_msgs::PointField::FLOAT32,
                                  "y", 1, sensor_msgs::PointField::FLOAT32,
                                  "z", 1, sensor_msgs::PointField::FLOAT32,
                                  "rgb", 1, sensor_msgs::PointField::FLOAT32);
    modifier.setPointCloud2FieldsByString(2, "xyz", "rgb");

    sensor_msgs::PointCloud2Iterator<float>iter_x(msg_pointcloud, "x");
    sensor_msgs::PointCloud2Iterator<float>iter_y(msg_pointcloud, "y");
    sensor_msgs::PointCloud2Iterator<float>iter_z(msg_pointcloud, "z");

    sensor_msgs::PointCloud2Iterator<uint8_t>iter_r(msg_pointcloud, "r");
    sensor_msgs::PointCloud2Iterator<uint8_t>iter_g(msg_pointcloud, "g");
    sensor_msgs::PointCloud2Iterator<uint8_t>iter_b(msg_pointcloud, "b");

    // Fill the PointCloud2 fields
    const rs2::vertex* vertex = pc.get_vertices();
    color_point = pc.get_texture_coordinates();

    float color_pixel[2];
    for (size_t point_idx=0; point_idx < pc.size(); vertex++, point_idx++, color_point++)
    {
        float i(0), j(0);
        if (use_texture)
        {
            i = static_cast<float>(color_point->u);
            j = static_cast<float>(color_point->v);
        }
        if (i >= 0.f && i <= 1.f && j >= 0.f && j <= 1.f)
        {
            *iter_x = vertex->x;
            *iter_y = vertex->y;
            *iter_z = vertex->z;

            color_pixel[0] = i * texture_width;
            color_pixel[1] = j * texture_height;

            int pixx = static_cast<int>(color_pixel[0]);
            int pixy = static_cast<int>(color_pixel[1]);
            int offset = (pixy * texture_width + pixx) * 3;
            *iter_r = static_cast<uint8_t>(color_data[offset]);
            *iter_g = static_cast<uint8_t>(color_data[offset + 1]);
            *iter_b = static_cast<uint8_t>(color_data[offset + 2]);

            ++iter_x; ++iter_y; ++iter_z;
            ++iter_r; ++iter_g; ++iter_b;
        }
    }
    _pointcloud_publisher.publish(msg_pointcloud);

    _send_counter = true;
}


Extrinsics BaseRealSenseNode::rsExtrinsicsToMsg(const rs2_extrinsics& extrinsics, const std::string& frame_id) const
{
    Extrinsics extrinsicsMsg;
    for (int i = 0; i < 9; ++i)
    {
        extrinsicsMsg.rotation[i] = extrinsics.rotation[i];
        if (i < 3)
            extrinsicsMsg.translation[i] = extrinsics.translation[i];
    }

    extrinsicsMsg.header.frame_id = frame_id;
    return extrinsicsMsg;
}

rs2_extrinsics BaseRealSenseNode::getRsExtrinsics(const stream_index_pair& from_stream, const stream_index_pair& to_stream)
{
    auto& from = _enabled_profiles[from_stream].front();
    auto& to = _enabled_profiles[to_stream].front();
    return from.get_extrinsics_to(to);
}

IMUInfo BaseRealSenseNode::getImuInfo(const stream_index_pair& stream_index)
{
    IMUInfo info{};
    auto sp = _enabled_profiles[stream_index].front().as<rs2::motion_stream_profile>();
    auto imuIntrinsics = sp.get_motion_intrinsics();
    if (GYRO == stream_index)
    {
        info.header.frame_id = "imu_gyro";
    }
    else if (ACCEL == stream_index)
    {
        info.header.frame_id = "imu_accel";
    }

    auto index = 0;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            info.data[index] = imuIntrinsics.data[i][j];
            ++index;
        }
        info.noise_variances[i] =  imuIntrinsics.noise_variances[i];
        info.bias_variances[i] = imuIntrinsics.bias_variances[i];
    }
    return info;
}

void BaseRealSenseNode::publishFrame(rs2::frame f, const ros::Time& t,
                                     const stream_index_pair& stream,
                                     std::map<stream_index_pair, cv::Mat>& images,
                                     const std::map<stream_index_pair, ros::Publisher>& info_publishers,
                                     const std::map<stream_index_pair, ImagePublisherWithFrequencyDiagnostics>& image_publishers,
                                     std::map<stream_index_pair, int>& seq,
                                     std::map<stream_index_pair, sensor_msgs::CameraInfo>& camera_info,
                                     const std::map<stream_index_pair, std::string>& optical_frame_id,
                                     const std::map<stream_index_pair, std::string>& encoding,
                                     bool copy_data_from_frame)
{
    ROS_DEBUG("publishFrame(...)");
    auto width = 0;
    auto height = 0;
    auto bpp = 1;
    if (f.is<rs2::video_frame>())
    {
        auto image = f.as<rs2::video_frame>();
        width = image.get_width();
        height = image.get_height();
        bpp = image.get_bytes_per_pixel();
    }
    auto& image = images[stream];

    if (copy_data_from_frame)
    {
        if (images[stream].size() != cv::Size(width, height))
        {
            image.create(height, width, _image_format[stream]);
        }
        image.data = (uint8_t*)f.get_data();
    }

    ++(seq[stream]);
    auto& info_publisher = info_publishers.at(stream);
    auto& image_publisher = image_publishers.at(stream);
    bool has_subscribers = info_publisher.getNumSubscribers() != 0 ||image_publisher.first.getNumSubscribers() != 0;

    if(has_subscribers)
    {
        sensor_msgs::ImagePtr img;
        img = cv_bridge::CvImage(std_msgs::Header(), encoding.at(stream), image).toImageMsg();
        img->width = width;
        img->height = height;
        img->is_bigendian = false;
        img->step = width * bpp;
        img->header.frame_id = optical_frame_id.at(stream);
        img->header.stamp = t;
        img->header.seq = f.get_frame_number();

        auto& cam_info = camera_info.at(stream);
        cam_info.header.stamp = img->header.stamp;
        cam_info.header.seq = img->header.seq;

        // if exposure is available, TODO
        double exposure = f.supports_frame_metadata(RS2_FRAME_METADATA_ACTUAL_EXPOSURE) ?
                            static_cast<double>(f.get_frame_metadata(RS2_FRAME_METADATA_ACTUAL_EXPOSURE)) : 0.0;

        ROS_DEBUG("Actual Exposure: %f", exposure);

        // Directly publish
        info_publisher.publish(cam_info);

        image_publisher.first.publish(img);
        image_publisher.second->update();
        ROS_DEBUG("%s stream published", rs2_stream_to_string(f.get_profile().stream_type()));

        // We published at least one frame
        _send_counter = true;
    }
}

bool BaseRealSenseNode::getEnabledProfile(const stream_index_pair& stream_index, rs2::stream_profile& profile)
    {
        // Assuming that all D400 SKUs have depth sensor
        auto profiles = _enabled_profiles[stream_index];
        auto it = std::find_if(profiles.begin(), profiles.end(),
                               [&](const rs2::stream_profile& profile)
                               { return (profile.stream_type() == stream_index.first); });
        if (it == profiles.end())
            return false;

        profile =  *it;
        return true;
    }


BaseD400Node::BaseD400Node(ros::NodeHandle& nodeHandle,
                           ros::NodeHandle& privateNodeHandle,
                           rs2::device dev, const std::string& serial_no)
    : BaseRealSenseNode(nodeHandle,
                        privateNodeHandle,
                        dev, serial_no)
{}

void BaseD400Node::callback(base_d400_paramsConfig &config, uint32_t level)
{
    ROS_DEBUG_STREAM("D400 - Level: " << level);

    if (set_default_dynamic_reconfig_values == level)
    {
        for (int i = 1 ; i < base_depth_count ; ++i)
        {
            ROS_DEBUG_STREAM("base_depth_param = " << i);
            try
            {
                setParam(config ,(base_depth_param)i);
            }
            catch(...)
            {
                ROS_ERROR_STREAM("Failed. Skip initialization of parameter " << (base_depth_param)i);
            }
        }
    }
    else
    {
        setParam(config, (base_depth_param)level);
    }
}

void BaseD400Node::setOption(stream_index_pair sip, rs2_option opt, float val)
{
    _sensors[sip].set_option(opt, val);
}

void BaseD400Node::setParam(rs435_paramsConfig &config, base_depth_param param)
{
    base_d400_paramsConfig base_config;
    base_config.base_depth_gain = config.rs435_depth_gain;
    base_config.base_depth_enable_auto_exposure = config.rs435_depth_enable_auto_exposure;
    base_config.base_depth_visual_preset = config.rs435_depth_visual_preset;
    base_config.base_depth_frames_queue_size = config.rs435_depth_frames_queue_size;
    base_config.base_depth_error_polling_enabled = config.rs435_depth_error_polling_enabled;
    base_config.base_depth_output_trigger_enabled = config.rs435_depth_output_trigger_enabled;
    base_config.base_depth_units = config.rs435_depth_units;
    base_config.base_JSON_file_path = config.rs435_JSON_file_path;
    base_config.base_sensors_enabled = config.rs435_sensors_enabled;
    setParam(base_config, param);
}

void BaseD400Node::setParam(rs415_paramsConfig &config, base_depth_param param)
{
    base_d400_paramsConfig base_config;
    base_config.base_depth_gain = config.rs415_depth_gain;
    base_config.base_depth_enable_auto_exposure = config.rs415_depth_enable_auto_exposure;
    base_config.base_depth_visual_preset = config.rs415_depth_visual_preset;
    base_config.base_depth_frames_queue_size = config.rs415_depth_frames_queue_size;
    base_config.base_depth_error_polling_enabled = config.rs415_depth_error_polling_enabled;
    base_config.base_depth_output_trigger_enabled = config.rs415_depth_output_trigger_enabled;
    base_config.base_depth_units = config.rs415_depth_units;
    base_config.base_JSON_file_path = config.rs415_JSON_file_path;
    base_config.base_sensors_enabled = config.rs415_sensors_enabled;
    setParam(base_config, param);
}

void BaseD400Node::setParam(base_d400_paramsConfig &config, base_depth_param param)
{
    // W/O for zero param
    if (0 == param)
        return;

    // Switch based on the level, defined in .py or .cfg file
    switch (param) {
    case base_depth_gain:
        ROS_DEBUG_STREAM("base_depth_gain: " << config.base_depth_gain);
        setOption(DEPTH, RS2_OPTION_GAIN, config.base_depth_gain);
        break;
    case base_depth_enable_auto_exposure:
        ROS_DEBUG_STREAM("base_depth_enable_auto_exposure: " << config.base_depth_enable_auto_exposure);
        setOption(DEPTH, RS2_OPTION_ENABLE_AUTO_EXPOSURE, config.base_depth_enable_auto_exposure);
        break;
    case base_depth_visual_preset:
        ROS_DEBUG_STREAM("base_depth_visual_preset: " << config.base_depth_visual_preset);
        setOption(DEPTH, RS2_OPTION_VISUAL_PRESET, config.base_depth_visual_preset);
        break;
    case base_depth_frames_queue_size:
        ROS_DEBUG_STREAM("base_depth_frames_queue_size: " << config.base_depth_frames_queue_size);
        setOption(DEPTH, RS2_OPTION_FRAMES_QUEUE_SIZE, config.base_depth_frames_queue_size);
        break;
    case base_depth_error_polling_enabled:
        ROS_DEBUG_STREAM("base_depth_error_polling_enabled: " << config.base_depth_error_polling_enabled);
        setOption(DEPTH, RS2_OPTION_ERROR_POLLING_ENABLED, config.base_depth_error_polling_enabled);
        break;
    case base_depth_output_trigger_enabled:
        ROS_DEBUG_STREAM("base_depth_output_trigger_enabled: " << config.base_depth_output_trigger_enabled);
        setOption(DEPTH, RS2_OPTION_OUTPUT_TRIGGER_ENABLED, config.base_depth_output_trigger_enabled);
        break;
    case base_depth_units:
        break;
    case base_sensors_enabled:
    {
        ROS_DEBUG_STREAM("base_sensors_enabled: " << config.base_sensors_enabled);
        toggleSensors(config.base_sensors_enabled);
        break;
    }
    case base_JSON_file_path:
    {
        ROS_DEBUG_STREAM("base_JSON_file_path: " << config.base_JSON_file_path);
        auto adv_dev = _dev.as<rs400::advanced_mode>();
        if (!adv_dev)
        {
            ROS_WARN_STREAM("Device doesn't support Advanced Mode!");
            return;
        }
        if (!config.base_JSON_file_path.empty())
        {
            std::ifstream in(config.base_JSON_file_path);
            if (!in.is_open())
            {
                ROS_WARN_STREAM("JSON file provided doesn't exist!");
                return;
            }

            adv_dev.load_json(config.base_JSON_file_path);
        }
        break;
    }
    case base_depth_count:
        break;
    }
}

void BaseD400Node::registerDynamicReconfigCb()
{
    _server = std::make_shared<dynamic_reconfigure::Server<base_d400_paramsConfig>>();
    _f = boost::bind(&BaseD400Node::callback, this, _1, _2);
    _server->setCallback(_f);
}
