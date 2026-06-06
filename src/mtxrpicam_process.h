#pragma once

#include <string>

#include <sys/types.h>

struct MtxRpiCamProcessOptions {
    std::string mtxrpicam_path;
    int conf_read_fd = -1;
    int video_write_fd = -1;
    int conf_write_fd = -1;
    int video_read_fd = -1;
};

class MtxRpiCamProcess {
  public:
    explicit MtxRpiCamProcess(MtxRpiCamProcessOptions options);

    bool start();
    void request_stop();
    int wait();

    pid_t pid() const;

  private:
    MtxRpiCamProcessOptions options_;
    pid_t pid_ = -1;
};
