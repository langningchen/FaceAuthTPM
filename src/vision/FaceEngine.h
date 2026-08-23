// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/FaceProfile.h"
#include <opencv2/core.hpp>
#include <opencv2/objdetect/face.hpp>
#include <opencv2/videoio.hpp>
#include <string>
#include <vector>

namespace faceauth {
class FaceEngine {
public:
    bool Initialize(std::wstring* error = nullptr);
    bool ExtractEmbedding(const cv::Mat& frame, std::vector<float>& embedding, cv::Rect* faceRect = nullptr);
    bool WarmUp(std::wstring* error = nullptr);
    static double Cosine(const std::vector<float>& a, const std::vector<float>& b);
private:
    cv::Ptr<cv::FaceDetectorYN> detector_;
    cv::Ptr<cv::FaceRecognizerSF> recognizer_;
};

bool OpenDefaultCamera(cv::VideoCapture& camera);
bool OpenCamera(cv::VideoCapture& camera, int cameraIndex, int width = 640, int height = 480, std::wstring* error = nullptr);
bool CaptureEnrollmentTemplate(FaceEngine& engine, std::vector<float>& averaged, int cameraIndex, std::wstring* error = nullptr);
std::wstring ScanForKnownFace(FaceEngine& engine, const std::vector<FaceProfile>& profiles, int cameraIndex, int timeoutMs);
std::wstring ScanForKnownFace(FaceEngine& engine, const std::vector<FaceProfile>& profiles, cv::VideoCapture& camera, int timeoutMs);
}
