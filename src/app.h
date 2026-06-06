#pragma once

#include <string>

struct RpiCamTsOptions {
    std::string config_path = "RpiCamTs.yml";
    bool config_path_explicit = false;

    std::string h264_output_path = "video.264";
    bool h264_output_path_explicit = false;

    bool generate_default_config = false;
    std::string generate_default_config_path;
};

class RpiCamTsApp {
  public:
    explicit RpiCamTsApp(RpiCamTsOptions options);

    int run();

  private:
    RpiCamTsOptions options_;
};
