// SPDX-License-Identifier: GPL-3.0-only
#include "vision/FaceEngine.h"
#include "common/Constants.h"
#include "common/Paths.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <sstream>
#include <thread>
#include <windows.h>

namespace faceauth {
namespace {
std::string NarrowPath(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
std::wstring WidenUtf8(const char* text) {
    if (!text || !*text)
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (n <= 0)
        return L"<unavailable>";
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), n);
    out.resize(static_cast<size_t>(n - 1));
    return out;
}
std::wstring FileInfo(const std::filesystem::path& p) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(p, ec);
    if (ec)
        return p.wstring() + L" (size unavailable: " + WidenUtf8(ec.message().c_str()) + L")";
    return p.wstring() + L" (" + std::to_wstring(size) + L" bytes)";
}
void Normalize(std::vector<float>& v) {
    double sum = 0;
    for (float x : v)
        sum += double(x) * x;
    double n = std::sqrt(sum);
    if (n > 1e-12)
        for (float& x : v)
            x = static_cast<float>(x / n);
}
} // namespace

bool FaceEngine::Initialize(std::wstring* error) {
    auto det = ModelsDir() / L"face_detection_yunet_2023mar.onnx";
    auto rec = ModelsDir() / L"face_recognition_sface_2021dec.onnx";
    std::error_code detEc, recEc;
    const bool detExists = std::filesystem::exists(det, detEc);
    const bool recExists = std::filesystem::exists(rec, recEc);
    if (!detExists || !recExists) {
        if (error) {
            *error = L"Face models are missing or inaccessible. YuNet: " + det.wstring() +
                     L"; SFace: " + rec.wstring();
        }
        return false;
    }
    try {
        detector_ =
            cv::FaceDetectorYN::create(NarrowPath(det), "", cv::Size(320, 320), 0.90f, 0.3f, 5000);
        if (detector_.empty()) {
            if (error)
                *error = L"OpenCV returned an empty YuNet detector for " + FileInfo(det);
            return false;
        }
    } catch (const cv::Exception& e) {
        if (error)
            *error =
                L"YuNet load failed. Model: " + FileInfo(det) + L". OpenCV: " + WidenUtf8(e.what());
        return false;
    } catch (const std::exception& e) {
        if (error)
            *error = L"YuNet load failed. Model: " + FileInfo(det) + L". Exception: " +
                     WidenUtf8(e.what());
        return false;
    }
    try {
        recognizer_ = cv::FaceRecognizerSF::create(NarrowPath(rec), "");
        if (recognizer_.empty()) {
            if (error)
                *error = L"OpenCV returned an empty SFace recognizer for " + FileInfo(rec);
            return false;
        }
    } catch (const cv::Exception& e) {
        if (error)
            *error =
                L"SFace load failed. Model: " + FileInfo(rec) + L". OpenCV: " + WidenUtf8(e.what());
        return false;
    } catch (const std::exception& e) {
        if (error)
            *error = L"SFace load failed. Model: " + FileInfo(rec) + L". Exception: " +
                     WidenUtf8(e.what());
        return false;
    }
    return true;
}

bool FaceEngine::WarmUp(std::wstring* error) {
    if (detector_.empty() || recognizer_.empty()) {
        if (error)
            *error = L"Face engine is not initialized";
        return false;
    }
    try {
        // Force the DNN backend to build/allocate its execution graphs before
        // LogonUI ever asks for a scan. YuNet and SFace are both exercised.
        cv::Mat detectorInput(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
        detector_->setInputSize(detectorInput.size());
        cv::Mat faces;
        detector_->detect(detectorInput, faces);

        cv::Mat recognizerInput(112, 112, CV_8UC3, cv::Scalar(127, 127, 127));
        cv::Mat feature;
        recognizer_->feature(recognizerInput, feature);
        return true;
    } catch (const cv::Exception& e) {
        if (error)
            *error = L"Face model warm-up failed: " + WidenUtf8(e.what());
        return false;
    }
}

bool FaceEngine::ExtractEmbedding(const cv::Mat& frame, std::vector<float>& embedding,
                                  cv::Rect* faceRect) {
    if (frame.empty() || detector_.empty() || recognizer_.empty())
        return false;
    try {
        detector_->setInputSize(frame.size());
        cv::Mat faces;
        detector_->detect(frame, faces);
        if (faces.rows != 1 || faces.cols < 15)
            return false;
        cv::Mat row = faces.row(0);
        cv::Mat aligned;
        recognizer_->alignCrop(frame, row, aligned);
        cv::Mat feature;
        recognizer_->feature(aligned, feature);
        cv::Mat f32;
        feature.reshape(1, 1).convertTo(f32, CV_32F);
        const float* begin = f32.ptr<float>(0);
        embedding.assign(begin, begin + f32.total());
        Normalize(embedding);
        if (faceRect) {
            int x = std::max(0, int(row.at<float>(0))), y = std::max(0, int(row.at<float>(1)));
            int w = std::max(1, int(row.at<float>(2))), h = std::max(1, int(row.at<float>(3)));
            *faceRect = cv::Rect(x, y, w, h) & cv::Rect(0, 0, frame.cols, frame.rows);
        }
        return !embedding.empty();
    } catch (const cv::Exception&) {
        return false;
    }
}

double FaceEngine::Cosine(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty())
        return -1.0;
    double s = 0;
    for (size_t i = 0; i < a.size(); ++i)
        s += double(a[i]) * b[i];
    return s;
}

bool OpenDefaultCamera(cv::VideoCapture& camera) {
    if (camera.open(0, cv::CAP_MSMF))
        return true;
    if (camera.open(0, cv::CAP_DSHOW))
        return true;
    return camera.open(0, cv::CAP_ANY);
}

bool OpenCamera(cv::VideoCapture& camera, int cameraIndex, int width, int height,
                std::wstring* error) {
    if (!camera.open(cameraIndex, cv::CAP_MSMF) && !camera.open(cameraIndex, cv::CAP_DSHOW) &&
        !camera.open(cameraIndex, cv::CAP_ANY)) {
        if (error)
            *error = L"Could not open USB camera index " + std::to_wstring(cameraIndex);
        return false;
    }
    if (width > 0)
        camera.set(cv::CAP_PROP_FRAME_WIDTH, width);
    if (height > 0)
        camera.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    return true;
}

bool CaptureEnrollmentTemplate(FaceEngine& engine, std::vector<float>& averaged, int cameraIndex,
                               std::wstring* error) {
    cv::VideoCapture camera;
    if (!camera.open(cameraIndex, cv::CAP_MSMF) && !camera.open(cameraIndex, cv::CAP_DSHOW) &&
        !camera.open(cameraIndex, cv::CAP_ANY)) {
        if (error)
            *error = L"Could not open USB camera";
        return false;
    }
    camera.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    camera.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    constexpr int kSamples = 30;
    std::vector<std::vector<float>> samples;
    auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    while (samples.size() < kSamples) {
        cv::Mat frame;
        if (!camera.read(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }
        std::vector<float> emb;
        cv::Rect r;
        bool ok = engine.ExtractEmbedding(frame, emb, &r);
        auto now = std::chrono::steady_clock::now();
        if (ok && now - last > std::chrono::milliseconds(140)) {
            samples.push_back(std::move(emb));
            last = now;
        }
        if (ok)
            cv::rectangle(frame, r, cv::Scalar(255, 255, 255), 2);
        std::string text = "FaceAuth enrollment: " + std::to_string(samples.size()) + "/" +
                           std::to_string(kSamples) + "  (ESC cancels)";
        cv::putText(frame, text, {20, 40}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255),
                    2);
        cv::imshow("FaceAuth enrollment", frame);
        if ((cv::waitKey(1) & 0xff) == 27) {
            cv::destroyWindow("FaceAuth enrollment");
            if (error)
                *error = L"Enrollment cancelled";
            return false;
        }
    }
    cv::destroyWindow("FaceAuth enrollment");
    averaged.assign(samples[0].size(), 0.0f);
    for (const auto& s : samples)
        for (size_t i = 0; i < s.size(); ++i)
            averaged[i] += s[i];
    for (float& x : averaged)
        x /= float(samples.size());
    Normalize(averaged);
    return true;
}

std::wstring ScanForKnownFace(FaceEngine& engine, const std::vector<FaceProfile>& profiles,
                              int cameraIndex, int timeoutMs) {
    if (profiles.empty())
        return {};
    cv::VideoCapture camera;
    if (!OpenCamera(camera, cameraIndex, 640, 480, nullptr))
        return {};
    return ScanForKnownFace(engine, profiles, camera, timeoutMs);
}

std::wstring ScanForKnownFace(FaceEngine& engine, const std::vector<FaceProfile>& profiles,
                              cv::VideoCapture& camera, int timeoutMs) {
    if (profiles.empty() || !camera.isOpened())
        return {};
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::wstring lastSid;
    int consecutive = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        cv::Mat frame;
        if (!camera.read(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }
        std::vector<float> emb;
        if (!engine.ExtractEmbedding(frame, emb, nullptr))
            continue;
        double best = -2, second = -2;
        const FaceProfile* winner = nullptr;
        for (const auto& p : profiles) {
            double s = FaceEngine::Cosine(emb, p.embedding);
            if (s > best) {
                second = best;
                best = s;
                winner = &p;
            } else if (s > second)
                second = s;
        }
        bool accept = winner && best >= kMatchThreshold &&
                      (profiles.size() == 1 || best - second >= kMatchMargin);
        if (accept) {
            if (lastSid == winner->sid)
                ++consecutive;
            else {
                lastSid = winner->sid;
                consecutive = 1;
            }
            if (consecutive >= kRequiredConsecutiveMatches)
                return lastSid;
        } else {
            lastSid.clear();
            consecutive = 0;
        }
    }
    return {};
}
} // namespace faceauth
