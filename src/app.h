#pragma once

#include <string>

enum class OutputMode {
    RawH264,
    MpegTs,
};

struct RpiCamTsOptions {
    std::string config_path = "RpiCamTs.yml";
    bool config_path_explicit = false;

    std::string output_path = "video.264";
    bool output_path_explicit = false;
    OutputMode output_mode = OutputMode::RawH264;

    bool verbose = false;

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
