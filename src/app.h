#pragma once

#include <string>

struct RpiCamTsOptions {
    std::string mtxrpicam_path;
};

class RpiCamTsApp {
  public:
    explicit RpiCamTsApp(RpiCamTsOptions options);

    int run();

  private:
    RpiCamTsOptions options_;
};
