#include <fstream>
#include <string>

#include <glog/logging.h>
#include <opencv2/opencv.hpp>

#include <FilterFusion/Visualizer.h>
#include <FilterFusion/FilterFusionSystem.h>

bool LoadSensorData(const std::string& encoder_file_path, std::unordered_map<std::string, std::string>* time_data_map) {
    std::ifstream encoder_file(encoder_file_path);
    if (!encoder_file.is_open()) {
        LOG(ERROR) << "[LoadSensorData]: Failed to open encoder file.";
        return false;
    } 

    std::string line_str, time_str;
    while (std::getline(encoder_file, line_str)) {
        std::stringstream ss(line_str);
        if (!std::getline(ss, time_str, ',')) {
            LOG(ERROR) << "[LoadSensorData]: Find a bad line in the encoder file.: " << line_str;
            return false;
        }

        time_data_map->emplace(time_str, line_str);
    }

    return true;
}

// 1. Config file.
// 2. Dataset folder.
int main(int argc, char** argv) {
    // Eigen::Matrix3d V_R_I = Eigen::Matrix3d::Identity();
    // Eigen::Vector3d V_p_I(-0.07, 0, 1.7);
    // Eigen::Matrix3d V_R_C;
    // V_R_C << -0.00680499, -0.0153215, 0.99985,
    //          -0.999977, 0.000334627, -0.00680066,
    //          -0.000230383, -0.999883, -0.0153234;
    // Eigen::Vector3d V_p_C(1.64239, 0.247401, 1.58411);
    
    // Eigen::Matrix3d C_R_I = V_R_C.transpose() * V_R_I;
    // Eigen::Vector3d C_p_I = V_R_C.transpose() * (V_p_I - V_p_C);

    // LOG(ERROR) << std::fixed << std::setprecision(14) << "\n" << C_R_I;
    // LOG(ERROR) << std::fixed <<  std::setprecision(14) << "\n" << C_p_I;
    // return EXIT_FAILURE;
    
    if (argc != 3) {
        LOG(ERROR) << "[main]: Please input param_file, data_folder.";
        return EXIT_FAILURE;
    }

    FLAGS_minloglevel = 2;
    const std::string param_file = argv[1];
    const std::string data_folder = argv[2];

    // Create FilterFusion system.
    FilterFusion::FilterFusionSystem fusion_sys(param_file);
   
    // Load all encoder data to buffer.
    std::unordered_map<std::string, std::string> time_encoder_map;
    if (!LoadSensorData(data_folder + "/sensor_data/encoder.csv", &time_encoder_map)) {
        LOG(ERROR) << "[main]: Failed to load encoder data.";
        return EXIT_FAILURE;
    } 

    // Load all gps data to buffer.
    std::unordered_map<std::string, std::string> time_gps_map;
    if (!LoadSensorData(data_folder + "/sensor_data/gps.csv", &time_gps_map)) {
        LOG(ERROR) << "[main]: Failed to load gps data.";
        return EXIT_FAILURE;
    } 

    // Load all imu data to buffer.
    std::unordered_map<std::string, std::string> time_imu_map;
    if (!LoadSensorData(data_folder + "/sensor_data/xsens_imu.csv", &time_imu_map)) {
        LOG(ERROR) << "[main]: Failed to load imu data.";
        return EXIT_FAILURE;
    } 

    std::ifstream file_data_stamp(data_folder + "/sensor_data/data_stamp.csv");
    if (!file_data_stamp.is_open()) {
        LOG(ERROR) << "[main]: Failed to open data_stamp file.";
        return EXIT_FAILURE;
    }

    std::vector<std::string> line_data_vec;
    line_data_vec.reserve(3);
    std::string line_str, value_str;
    while (std::getline(file_data_stamp, line_str)) {
        line_data_vec.clear();
        std::stringstream ss(line_str);
        while (std::getline(ss, value_str, ',')) { line_data_vec.push_back(value_str); }

        constexpr double kToSecond = 1e-9;
        const std::string& time_str = line_data_vec[0];
        const double timestamp = std::stod(time_str) * kToSecond;
        
        const std::string& sensor_type = line_data_vec[1];
        if (sensor_type == "stereo") {
            const std::string img_file = data_folder + "/image/stereo_left/" + time_str + ".png";
            const cv::Mat raw_image = cv::imread(img_file, CV_LOAD_IMAGE_ANYDEPTH);
            if (raw_image.empty()) {
                LOG(ERROR) << "[main]: Failed to open image at time: " << time_str;
                return EXIT_FAILURE;
            }
            
            // Convert raw image to color image.
            cv::Mat color_img;
            cv::cvtColor(raw_image, color_img, cv::COLOR_BayerRG2RGB);

            // Convert raw image to gray image.
            cv::Mat gray_img;
            cv::cvtColor(color_img, gray_img, cv::COLOR_RGB2GRAY);

            // Feed image to system.
            fusion_sys.FeedImageData(timestamp, gray_img);
        }

        if (sensor_type == "encoder") {
            if (time_encoder_map.find(time_str) == time_encoder_map.end()) {
                LOG(ERROR) << "[main]: Failed to find encoder data at time: " << time_str;
                return EXIT_FAILURE;
            }
            const std::string& encoder_str = time_encoder_map.at(time_str);
            std::stringstream enc_ss(encoder_str);
            line_data_vec.clear();
            while (std::getline(enc_ss, value_str, ',')) { line_data_vec.push_back(value_str); }

            const double left_enc_cnt = std::stod(line_data_vec[1]);
            const double right_enc_cnt = std::stod(line_data_vec[2]);

            // Feed wheel data to system.
            fusion_sys.FeedWheelData(timestamp, left_enc_cnt, right_enc_cnt);
        }

        if (sensor_type == "gps") {
            if (time_gps_map.find(time_str) == time_gps_map.end()) {
                LOG(ERROR) << "[main]: Failed to find gps data at time: " << time_str;
                return EXIT_FAILURE;
            }
            const std::string& gps_str = time_gps_map.at(time_str);
            std::stringstream gps_ss(gps_str);
            line_data_vec.clear();
            while (std::getline(gps_ss, value_str, ',')) { line_data_vec.push_back(value_str); }

            const double lat = std::stod(line_data_vec[1]);
            const double lon = std::stod(line_data_vec[2]);
            const double hei = std::stod(line_data_vec[3]);

            Eigen::Matrix3d cov;
            for (size_t i = 0; i < 9; ++i) {
                cov.data()[i] = std::stod(line_data_vec[4+i]);
            }
            
            // Feed gps data to system.
            fusion_sys.FeedGpsData(timestamp, lon, lat, hei, cov);
        }

        if (sensor_type == "imu") {
            if (time_imu_map.find(time_str) == time_imu_map.end()) {
                LOG(ERROR) << "[main]: Failed to find imu data at time: " << time_str;
                return EXIT_FAILURE;
            }
            const std::string& imu_str = time_imu_map.at(time_str);
            std::stringstream imu_ss(imu_str);
            line_data_vec.clear();
            while (std::getline(imu_ss, value_str, ',')) { line_data_vec.push_back(value_str); }

            const double acc_x = std::stod(line_data_vec[11]);
            const double acc_y = std::stod(line_data_vec[12]);
            const double acc_z = std::stod(line_data_vec[13]);

            const double gyro_x = std::stod(line_data_vec[8]);
            const double gyro_y = std::stod(line_data_vec[9]);
            const double gyro_z = std::stod(line_data_vec[10]);

            // Feed gps data to system.
            fusion_sys.FeedIMUData(timestamp, acc_x, acc_y, acc_z, gyro_x, gyro_y, gyro_z);
        }
    }

    std::cin.ignore();

    return EXIT_SUCCESS;
}