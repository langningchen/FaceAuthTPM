// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <string>

namespace faceauth {
// Returns true when the preloaded model service was reached and handled the
// scan. An empty sid with true means a normal scan timeout/no match. Returns
// false only when the service is unavailable/broken.
// If firstFrame is supplied, it is sent before reading another frame from the
// camera, so startup diagnostics do not discard the first useful image.
bool ScanUsingModelService(cv::VideoCapture& camera, int timeoutMs, std::wstring& sid,
                           std::wstring* error = nullptr, const cv::Mat* firstFrame = nullptr);
} // namespace faceauth
