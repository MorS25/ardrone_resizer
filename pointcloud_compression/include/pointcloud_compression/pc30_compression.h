#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <datalink_msgs/CompressedPointCloud2.h>
#include <zlib.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/conversions.h>
#include <pcl_conversions/pcl_conversions.h>

#ifndef PC30_COMPRESSION_H
#define PC30_COMPRESSION_H

namespace pc30_compression
{

    class PC30Compressor
    {
    protected:

        std::vector<uint32_t> state_packed_;
        std::map<uint32_t, int8_t> stored_state_;
        uint32_t iframe_rate_;
        uint32_t pframe_counter_;
        pcl::PointCloud<pcl::PointXYZ> get_current_pointcloud();
        pcl::PointCloud<pcl::PointXYZ> generate_empty_pointcloud();

    public:

        enum FRAME_TYPES {IFRAME, PFRAME, UNKNOWN};
        static const uint32_t IFRAME_RATE = 10;
        static const uint32_t IFRAME_ID = 0x00ffffff;
        static const uint32_t PFRAME_ID = 0x00dfdfdf;
        static const uint32_t UNKNOWN_ID = 0x00000000;

        PC30Compressor()
        {
            iframe_rate_ = IFRAME_RATE;
            pframe_counter_ = 0;
        }

        ~PC30Compressor()
        {
        }

        void reset_encoder();

        void reset_decoder();

        FRAME_TYPES header_to_frame_type(uint32_t header_block);

        uint32_t frame_type_to_header(FRAME_TYPES frame_type);

        std::vector<uint8_t> decompress_bytes(std::vector<uint8_t>& compressed);

        std::vector<uint8_t> compress_bytes(std::vector<uint8_t>& uncompressed);

        sensor_msgs::PointCloud2 decode_pointcloud2(datalink_msgs::CompressedPointCloud2& compressed);

        datalink_msgs::CompressedPointCloud2 encode_pointcloud2(sensor_msgs::PointCloud2& cloud);

    };
}

#endif // PC30_COMPRESSION_H
