#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <sensor_msgs/Image.h>
#include <datalink_msgs/RateControl.h>
#include <datalink_msgs/QualityControl.h>
#include <datalink_msgs/RequestImage.h>
#include <opportunistic_link/image_compression.h>

class RequestImageLinkEndpoint
{
protected:

    ros::NodeHandle nh_;
    image_transport::ImageTransport it_;
    bool forward_;
    double forward_rate_;
    ros::Rate repub_rate_;
    bool override_timestamps_;
    ros::ServiceClient data_client_;
    ros::ServiceServer quality_server_;
    ros::ServiceServer rate_server_;
    image_transport::Publisher image_pub_;
    uint32_t image_subs_;
    uint8_t image_quality_;
    image_compression::ImageHandler decompressor_;

public:

    RequestImageLinkEndpoint(ros::NodeHandle &n, std::string relay_topic, std::string data_service, std::string quality_ctrl_service, std::string rate_ctrl_service, int default_quality, double default_rate, bool override_timestamps, bool latched) : nh_(n), repub_rate_(20.0), it_(n)
    {
        image_quality_ = (uint8_t)default_quality;
        forward_ = false;
        forward_rate_ = default_rate;
        if ((forward_rate_ != INFINITY) && (forward_rate_ > 0.0) && (isnan(forward_rate_) == false))
        {
            repub_rate_ = ros::Rate(forward_rate_);
        }
        override_timestamps_ = override_timestamps;
        data_client_ = nh_.serviceClient<datalink_msgs::RequestImage>(data_service);
        rate_server_ = nh_.advertiseService(rate_ctrl_service, &RequestImageLinkEndpoint::rate_cb, this);
        quality_server_ = nh_.advertiseService(quality_ctrl_service, &RequestImageLinkEndpoint::quality_cb, this);
        image_transport::SubscriberStatusCallback camera_image_cb = boost::bind(&RequestImageLinkEndpoint::subscriber_cb, this);
        image_pub_ = it_.advertise(relay_topic, 1, camera_image_cb, camera_image_cb, ros::VoidPtr(), latched);
    }

    ~RequestImageLinkEndpoint()
    {
    }

    void loop()
    {
        while (ros::ok())
        {
            if (forward_ && (forward_rate_ == INFINITY))
            {
                ROS_INFO("Requesting an image");
                try
                {
                    get_image();
                }
                catch (...)
                {
                    ROS_WARN("Failure requesting an image, unable to republish");
                }
            }
            if (forward_ && (forward_rate_ != INFINITY) && (forward_rate_ != 0.0))
            {
                ROS_INFO("Requesting an image");
                try
                {
                    get_image();
                }
                catch (...)
                {
                    ROS_WARN("Failure requesting an image, unable to republish");
                }
                repub_rate_.sleep();
            }
            ros::spinOnce();
        }
    }

    void get_image()
    {
        datalink_msgs::RequestImage::Request req;
        datalink_msgs::RequestImage::Response res;
        // Set the image quality for compression
        req.quality = image_quality_;
        if (data_client_.call(req, res))
        {
            if (res.available)
            {
                // Decompress image
                struct timespec st, et;
                clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &st);
                sensor_msgs::Image image = decompressor_.decompress_image(res.image, res.encoding);
                clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &et);
                float secs = (float)(et.tv_sec - st.tv_sec);
                secs = secs + (float)(et.tv_nsec - st.tv_nsec) / 1000000000.0;
                float ratio = ((float)image.data.size() / (float)res.image.data.size()) * 100.0;
                ROS_DEBUG("Decompression of %f %% took %f seconds", ratio, secs);
                if (override_timestamps_)
                {
                    image.header.stamp = ros::Time::now();
                }
                image_pub_.publish(image);
            }
            else
            {
                ROS_WARN("Contacted startpoint, but no images available");
            }
        }
        else
        {
            ROS_ERROR("Unable to contact startpoint");
            throw std::invalid_argument("Unable to contact startpoint");
        }
    }

    bool quality_cb(datalink_msgs::QualityControl::Request& req, datalink_msgs::QualityControl::Response& res)
    {
        if (req.Quality >= 0 && req.Quality <= 100)
        {
            image_quality_ = (uint8_t)req.Quality;
            ROS_INFO("Set image quality to %ud", image_quality_);
        }
        else if (req.Quality > 100)
        {
            image_quality_ = 100;
            ROS_WARN("Attempted to set image quality above maximum limit - set to maximum instead");
        }
        res.State = image_quality_;
        return true;
    }

    bool rate_cb(datalink_msgs::RateControl::Request& req, datalink_msgs::RateControl::Response& res)
    {
        if (req.Rate > 0.0 && (req.Rate != INFINITY) && (isnan(req.Rate) == false))
        {
            repub_rate_ = ros::Rate(req.Rate);
            forward_rate_ = req.Rate;
            res.State = forward_rate_;
            ROS_INFO("Set republish rate to %f", forward_rate_);
        }
        else if (req.Rate == INFINITY)
        {
            forward_rate_ = INFINITY;
            ROS_INFO("Set republish rate to native");
        }
        else if ((req.Rate == -1.0) && (forward_rate_ != INFINITY))
        {
            try
            {
                get_image();
            }
            catch (...)
            {
                ROS_WARN("Single message requested, unable to republish");
            }
        }
        else if (req.Rate == 0.0 || req.Rate == -0.0)
        {
            forward_rate_ = 0.0;
            res.State = forward_rate_;
            ROS_INFO("Image republishing paused");
        }
        else
        {
            ROS_ERROR("Invalid publish rate requested");
        }
        res.State = forward_rate_;
        return true;
    }

    void subscriber_cb()
    {
        image_subs_ = image_pub_.getNumSubscribers();
        if (image_subs_ == 1)
        {
            forward_ = true;
            ROS_INFO("Turned forwarding on");
        }
        else if (image_subs_ < 1)
        {
            forward_ = false;
            ROS_INFO("Turned forwarding off");
        }
    }
};

int main(int argc, char** argv)
{
    ros::init(argc, argv, "request_image_link_endpoint");
    ROS_INFO("Starting request image link endpoint...");
    ros::NodeHandle nh;
    ros::NodeHandle nhp("~");
    std::string relay_topic;
    std::string data_service;
    std::string quality_ctrl_service;
    std::string rate_ctrl_service;
    bool latched;
    double default_rate;
    int default_quality;
    bool override_timestamps;
    nhp.param(std::string("relay_topic"), relay_topic, std::string("camera/relay/rgb/image"));
    nhp.param(std::string("data_service"), data_service, std::string("camera/rgb/data"));
    nhp.param(std::string("quality_ctrl"), quality_ctrl_service, std::string("camera/rgb/quality"));
    nhp.param(std::string("rate_ctrl"), rate_ctrl_service, std::string("camera/rgb/rate"));
    nhp.param(std::string("latched"), latched, false);
    nhp.param(std::string("default_rate"), default_rate, (double)INFINITY);
    nhp.param(std::string("default_quality"), default_quality, 50);
    nhp.param(std::string("override_timestamps"), override_timestamps, true);
    RequestImageLinkEndpoint endpoint(nh, relay_topic, data_service, quality_ctrl_service, rate_ctrl_service, default_quality, default_rate, override_timestamps, latched);
    ROS_INFO("...startup complete");
    endpoint.loop();
    return 0;
}
