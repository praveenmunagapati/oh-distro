#include <memory>
#include <string>
#include <unordered_map>

#include <lcm/lcm-cpp.hpp>
#include <drc_utils/LcmWrapper.hpp>
#include <bot_param/param_client.h>
#include <bot_param/param_util.h>
#include <bot_core/camtrans.h>

#include <lcmtypes/bot_core/image_t.hpp>

#include <opencv2/opencv.hpp>

struct ChannelData {
  typedef std::shared_ptr<ChannelData> Ptr;
  std::shared_ptr<lcm::LCM> mLcm;
  std::string mChannelBase;
  std::string mChannelReceive;
  std::string mChannelTransmit;
  int mWidth;
  int mHeight;

  void onImage(const lcm::ReceiveBuffer* iBuf, const std::string& iChannel,
               const bot_core::image_t* iMessage) {
    // uncompress
    cv::Mat raw = cv::imdecode(cv::Mat(iMessage->data), -1);
    if (raw.channels() == 3) cv::cvtColor(raw, raw, CV_RGB2BGR);

    // resize to normal
    cv::Mat img;
    cv::resize(raw, img, cv::Size(mWidth, mHeight));

    int pixelFormat;
    switch (img.channels()) {
    case 1:
      pixelFormat = bot_core::image_t::PIXEL_FORMAT_GRAY;
      break;
    case 3:
      pixelFormat = bot_core::image_t::PIXEL_FORMAT_RGB;
      break;
    default:
      std::cout << "ImageReceive: invalid image type on channel " <<
        iChannel << std::endl;
      return;
    }

    // copy final data
    std::vector<uint8_t> buf(img.data, img.data + img.step*img.rows);

    // re-transmit
    bot_core::image_t msg;
    msg.utime = iMessage->utime;
    msg.width = img.cols;
    msg.height = img.rows;
    msg.row_stride = img.step;
    msg.pixelformat = pixelFormat;
    msg.size = buf.size();
    msg.data = buf;
    msg.nmetadata = 0;
    mLcm->publish(mChannelTransmit, &msg);
    std::cout << "re-transmitted image on " << mChannelTransmit << std::endl;
  }
};

struct ImageReceive {
  std::shared_ptr<drc::LcmWrapper> mLcmWrapper;
  std::shared_ptr<lcm::LCM> mLcm;
  typedef std::unordered_map<std::string, ChannelData::Ptr> ChannelGroup;
  ChannelGroup mChannels;
  BotParam* mBotParam;

  ImageReceive() {
    mLcmWrapper.reset(new drc::LcmWrapper());
    mLcm = mLcmWrapper->get();
    mBotParam = bot_param_get_global(mLcmWrapper->get()->getUnderlyingLCM(),0);
  }

  void addChannel(const std::string& iChannel) {
    ChannelData::Ptr data(new ChannelData());
    data->mChannelBase = iChannel;
    data->mChannelReceive = iChannel + "_TX";
    data->mChannelTransmit = iChannel;
    BotCamTrans* camTrans =
      bot_param_get_new_camtrans(mBotParam, iChannel.c_str());
    data->mWidth = bot_camtrans_get_width(camTrans);
    data->mHeight = bot_camtrans_get_height(camTrans);
    data->mLcm = mLcm;
    mChannels[data->mChannelReceive] = data;
    mLcm->subscribe(data->mChannelReceive, &ChannelData::onImage, data.get());
  }

  void start() {
    mLcmWrapper->startHandleThread(true);
  }
};

int main() {
  ImageReceive obj;

  obj.addChannel("CAMERA_LEFT");
  obj.addChannel("CAMERA_RIGHT");
  obj.addChannel("CAMERACHEST_LEFT");
  obj.addChannel("CAMERACHEST_RIGHT");
  obj.start();
}
