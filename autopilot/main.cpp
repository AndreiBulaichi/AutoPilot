#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <inference_engine.hpp>
#include <samples/ocv_common.hpp>
#include <samples/slog.hpp>
#include <errno.h>
#include <fcntl.h>
#include <opencv2/highgui/highgui.hpp>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <thread>
#include <mutex>
#include <unistd.h>
#include "include/AutoPilot.h"
#include "include/DeltaTimer.h"
#include "include/DeltaTimer.cpp"
#include "include/LaneDetector.hpp"
#include "include/LaneDetector.cpp"

#define ARDUINO_DEBUG 0

using namespace InferenceEngine;

cv::VideoCapture cap(0);
size_t width;
size_t height;
cv::Mat frame(height, width, CV_8UC3);

mutex frameMtx;
mutex imShowMtx;

int main()
{
    if (!cap.isOpened()) {
        throw std::logic_error("Cannot open input file or camera");
        return -1;
    }

    // cap.set(cv::CAP_PROP_FRAME_WIDTH, 320);
    // cap.set(cv::CAP_PROP_FRAME_HEIGHT, 240);

    width = (size_t) cap.get(cv::CAP_PROP_FRAME_WIDTH);
    height = (size_t) cap.get(cv::CAP_PROP_FRAME_HEIGHT);

    std::cout << cv::getBuildInformation() << std::endl;
    std::cout << "InferenceEngine: " << GetInferenceEngineVersion() << std::endl;

    std::thread getFrameTh(getFrame);
    std::thread showFrameTh(showFrame);
    //std::thread detectLanesTh(detectLanes);
    std::thread detectCarsTh(detectCars);
    std::thread detectTrafficTh(detectTraffic);
    // std::thread arduinoI2CTh(arduinoI2C);

    getFrameTh.join();
    showFrameTh.join();
    //detectLanesTh.join();
    detectCarsTh.join();
    detectTrafficTh.join();
    // arduinoI2CTh.join();

    atexit(exitRoutine);
    return 0;
}

void frameToBlob(const cv::Mat& frame, InferRequest::Ptr& inferRequest,
                 const std::string& inputName) {

    /* Resize and copy data from the image to the input blob */
    Blob::Ptr frameBlob = inferRequest->GetBlob(inputName);
    matU8ToBlob<uint8_t>(frame, frameBlob);
}

void getFrame()
{
    DeltaTimer timer;
    cv::Mat frameBuffer(height, width, CV_8UC3);

    while(true)
    {
        timer.resetDeltaTimer();
        cap >> frameBuffer;

        frameMtx.lock();
        frame = frameBuffer.clone();
        frameMtx.unlock();

        std::cout << "Capture FPS : " 
                  << 1 / ((float)timer.getDeltaTimeMs() / 1000) 
                  << std::endl;
    }
}

void showFrame()
{
    cv::Mat frameCpy(height, width, CV_8UC3);

    string fpsMesage = "";

    while(true)
    {
        frameMtx.lock();
        frameCpy = frame.clone();
        frameMtx.unlock();

        if(!frameCpy.empty())
        {
            imShowMtx.lock();
            cv::imshow("frame", frameCpy);
            cv::waitKey(1);
            imShowMtx.unlock();
        }
        usleep(1000);
    }
}

void detectLanes()
{
    DeltaTimer timer;
    cv::Mat frameCpy(height, width, CV_8UC3);
    LaneDetector laneDetector(1, width, height);

    string fpsMesage = "";
    
    while(true)
    {
        timer.resetDeltaTimer();

        frameMtx.lock();
        frameCpy = frame.clone();
        frameMtx.unlock();

        if(!frameCpy.empty())
        {

            cv::Mat image = *(laneDetector.runCurvePipeline(frame));
            steer = floor(laneDetector.getSteeringAngle()) + 50;

            cv::putText(image, fpsMesage, cv::Point2f(0, 75), cv::FONT_HERSHEY_PLAIN, 1.5,
                            cv::Scalar(255, 0, 0));

            imShowMtx.lock();
            cv::imshow("Lane", image);
            cv::waitKey(1);
            imShowMtx.unlock();
        }

        // Need to sincronize in order to not process the same frame
        usleep(30000);
        fpsMesage =  "Lane detection FPS : " 
          + std::to_string(1 / ((float)timer.getDeltaTimeMs() / 1000));
    }
}

void detectCars()
{
    cv::Mat frameCpy(height, width, CV_8UC3);

    std::string networkPath = "../../../models/pedestrian_and_vehicles/origin/mobilenet_iter_73000.xml";
    constexpr float confidenceThreshold = 0.7f;

    try {
    // --------------------------- 1. Load Plugin for inference engine -------------------------------------
    slog::info << "Loading plugin" << slog::endl;
    InferencePlugin plugin = PluginDispatcher().getPluginByDevice(deviceName);
    printPluginVersion(plugin, std::cout);

    // --------------------------- 2. Read IR Generated by ModelOptimizer (.xml and .bin files) ------------
    slog::info << "Loading network files" << slog::endl;
    CNNNetReader netReader;
    /** Read network model **/
    netReader.ReadNetwork(networkPath);
    /** Set batch size to 1 **/
    slog::info << "Batch size is forced to  1." << slog::endl;
    netReader.getNetwork().setBatchSize(1);
    /** Extract model name and load it's weights **/
    std::string binFileName = fileNameNoExt(networkPath) + ".bin";
    netReader.ReadWeights(binFileName);
    /** Read labels (if any)**/
    std::string labelFileName = fileNameNoExt(networkPath) + ".labels";
    std::vector<std::string> labels;
    std::ifstream inputFile(labelFileName);
    std::copy(std::istream_iterator<std::string>(inputFile),
              std::istream_iterator<std::string>(),
              std::back_inserter(labels));
    // -----------------------------------------------------------------------------------------------------

    /** SSD-based network should have one input and one output **/
    // --------------------------- 3. Configure input & output ---------------------------------------------
    // --------------------------- Prepare input blobs -----------------------------------------------------
    slog::info << "Checking that the inputs are as the app expects" << slog::endl;
    InputsDataMap inputInfo(netReader.getNetwork().getInputsInfo());
    if (inputInfo.size() != 1) {
        throw std::logic_error("This app accepts networks having only one input");
    }
    InputInfo::Ptr& input = inputInfo.begin()->second;
    auto inputName = inputInfo.begin()->first;
    input->setPrecision(Precision::U8);
    input->getInputData()->setLayout(Layout::NCHW);
    // --------------------------- Prepare output blobs -----------------------------------------------------
    slog::info << "Checking that the outputs are as the app expects" << slog::endl;
    OutputsDataMap outputInfo(netReader.getNetwork().getOutputsInfo());
    if (outputInfo.size() != 1) {
        throw std::logic_error("This app accepts networks having only one output");
    }
    DataPtr& output = outputInfo.begin()->second;
    auto outputName = outputInfo.begin()->first;
    const int num_classes = netReader.getNetwork().getLayerByName(outputName.c_str())->GetParamAsInt("num_classes");
    if (static_cast<int>(labels.size()) != num_classes) {
        if (static_cast<int>(labels.size()) == (num_classes - 1))  // if network assumes default "background" class, having no label
            labels.insert(labels.begin(), "fake");
        else
            labels.clear();
    }
    const SizeVector outputDims = output->getTensorDesc().getDims();
    const int maxProposalCount = outputDims[2];
    const int objectSize = outputDims[3];
    if (objectSize != 7) {
        throw std::logic_error("Output should have 7 as a last dimension");
    }
    if (outputDims.size() != 4) {
        throw std::logic_error("Incorrect output dimensions for SSD");
    }
    output->setPrecision(Precision::FP32);
    output->setLayout(Layout::NCHW);
    // -----------------------------------------------------------------------------------------------------

    // --------------------------- 4. Loading model to the plugin ------------------------------------------
    slog::info << "Loading model to the plugin" << slog::endl;
    ExecutableNetwork network = plugin.LoadNetwork(netReader.getNetwork(), {});
    // -----------------------------------------------------------------------------------------------------

    // --------------------------- 5. Create infer request -------------------------------------------------
    InferRequest::Ptr async_infer_request_curr = network.CreateInferRequestPtr();
    // -----------------------------------------------------------------------------------------------------

    // --------------------------- 6. Do inference ---------------------------------------------------------
    slog::info << "Start inference " << slog::endl;

    typedef std::chrono::duration<double, std::ratio<1, 1000>> ms;
    auto total_t0 = std::chrono::high_resolution_clock::now();
    auto wallclock = std::chrono::high_resolution_clock::now();
    double ocv_decode_time = 0, ocv_render_time = 0;

    std::cout << "To close the application, press 'CTRL+C' or any key with focus on the output window" << std::endl;
    while (true) 
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto t1 = std::chrono::high_resolution_clock::now();

        frameMtx.lock();
        frameCpy = frame.clone();
        frameMtx.unlock();

        if(!frameCpy.empty())
        {
            frameToBlob(frameCpy, async_infer_request_curr, inputName);
            t1 = std::chrono::high_resolution_clock::now();
            ocv_decode_time = std::chrono::duration_cast<ms>(t1 - t0).count();
            t0 = std::chrono::high_resolution_clock::now();
            async_infer_request_curr->StartAsync();

            if (OK == async_infer_request_curr->Wait(IInferRequest::WaitMode::RESULT_READY)) 
            {
                t1 = std::chrono::high_resolution_clock::now();
                ms detection = std::chrono::duration_cast<ms>(t1 - t0);

                t0 = std::chrono::high_resolution_clock::now();
                ms wall = std::chrono::duration_cast<ms>(t0 - wallclock);
                wallclock = t0;

                t0 = std::chrono::high_resolution_clock::now();
                std::ostringstream out;
                out << "OpenCV cap/render time: " << std::fixed << std::setprecision(2)
                    << (ocv_decode_time + ocv_render_time) << " ms";
                cv::putText(frameCpy, out.str(), cv::Point2f(0, 25), cv::FONT_HERSHEY_PLAIN, 1.5, cv::Scalar(0, 255, 0));
                out.str("");
                out << "Wallclock time ";
                out << std::fixed << std::setprecision(2) << wall.count() << " ms (" << 1000.f / wall.count() << " fps)";
                cv::putText(frameCpy, out.str(), cv::Point2f(0, 50), cv::FONT_HERSHEY_PLAIN, 1.5, cv::Scalar(0, 0, 255));
                out.str("");
                out << "Detection time  : " << std::fixed << std::setprecision(2) << detection.count()
                    << " ms ("
                    << 1000.f / detection.count() << " fps)";
                cv::putText(frameCpy, out.str(), cv::Point2f(0, 75), cv::FONT_HERSHEY_PLAIN, 1.5,
                            cv::Scalar(255, 0, 0));

                // ---------------------------Process output blobs--------------------------------------------------
                // Processing results of the CURRENT request
                const float *detections = async_infer_request_curr->GetBlob(outputName)->buffer().as<PrecisionTrait<Precision::FP32>::value_type*>();
                for (int i = 0; i < maxProposalCount; i++) {
                    float image_id = detections[i * objectSize + 0];
                    if (image_id < 0) {
                        break;
                    }

                    float confidence = detections[i * objectSize + 2];
                    auto label = static_cast<int>(detections[i * objectSize + 1]);
                    float xmin = detections[i * objectSize + 3] * width;
                    float ymin = detections[i * objectSize + 4] * height;
                    float xmax = detections[i * objectSize + 5] * width;
                    float ymax = detections[i * objectSize + 6] * height;

                    if (confidence > confidenceThreshold) {
                        /** Drawing only objects when > confidence_threshold probability **/
                        std::ostringstream conf;
                        conf << ":" << std::fixed << std::setprecision(3) << confidence;
                        cv::putText(frameCpy,
                                    (static_cast<size_t>(label) < labels.size() ?
                                    labels[label] : std::string("label #") + std::to_string(label)) + conf.str(),
                                    cv::Point2f(xmin, ymin - 5), cv::FONT_HERSHEY_COMPLEX_SMALL, 1,
                                    cv::Scalar(0, 0, 255));
                        cv::rectangle(frameCpy, cv::Point2f(xmin, ymin), cv::Point2f(xmax, ymax), cv::Scalar(0, 0, 255));
                    }
                }

            }

        }        

        t1 = std::chrono::high_resolution_clock::now();
        ocv_render_time = std::chrono::duration_cast<ms>(t1 - t0).count();

        imShowMtx.lock();
        cv::imshow("Cars results", frameCpy);
        const int key = cv::waitKey(1);
        if (27 == key)  // Esc
            break;
        imShowMtx.unlock();
    }
    // -----------------------------------------------------------------------------------------------------
    auto total_t1 = std::chrono::high_resolution_clock::now();
    ms total = std::chrono::duration_cast<ms>(total_t1 - total_t0);
    std::cout << "Total Inference time: " << total.count() << std::endl;
    }
    catch (const std::exception& error) {
        std::cerr << "[ ERROR ] " << error.what() << std::endl;
        return;
    }
    catch (...) {
        std::cerr << "[ ERROR ] Unknown/internal exception happened." << std::endl;
        return;
    }

    slog::info << "Execution successful" << slog::endl;

    return;
}

void detectTraffic()
{
    cv::Mat frameCpy(height, width, CV_8UC3);

    std::string networkPath = "../../../models/traffic_signs/FP16/mobilenet_iter_17000.xml";
    constexpr float confidenceThreshold = 0.8f;

    try {
    // --------------------------- 1. Load Plugin for inference engine -------------------------------------
    slog::info << "Loading plugin" << slog::endl;
    InferencePlugin plugin = PluginDispatcher().getPluginByDevice(deviceName);
    printPluginVersion(plugin, std::cout);

    // --------------------------- 2. Read IR Generated by ModelOptimizer (.xml and .bin files) ------------
    slog::info << "Loading network files" << slog::endl;
    CNNNetReader netReader;
    /** Read network model **/
    netReader.ReadNetwork(networkPath);
    /** Set batch size to 1 **/
    slog::info << "Batch size is forced to  1." << slog::endl;
    netReader.getNetwork().setBatchSize(1);
    /** Extract model name and load it's weights **/
    std::string binFileName = fileNameNoExt(networkPath) + ".bin";
    netReader.ReadWeights(binFileName);
    /** Read labels (if any)**/
    std::string labelFileName = fileNameNoExt(networkPath) + ".labels";
    std::vector<std::string> labels;
    std::ifstream inputFile(labelFileName);
    std::copy(std::istream_iterator<std::string>(inputFile),
              std::istream_iterator<std::string>(),
              std::back_inserter(labels));
    // -----------------------------------------------------------------------------------------------------

    /** SSD-based network should have one input and one output **/
    // --------------------------- 3. Configure input & output ---------------------------------------------
    // --------------------------- Prepare input blobs -----------------------------------------------------
    slog::info << "Checking that the inputs are as the app expects" << slog::endl;
    InputsDataMap inputInfo(netReader.getNetwork().getInputsInfo());
    if (inputInfo.size() != 1) {
        throw std::logic_error("This app accepts networks having only one input");
    }
    InputInfo::Ptr& input = inputInfo.begin()->second;
    auto inputName = inputInfo.begin()->first;
    input->setPrecision(Precision::U8);
    input->getInputData()->setLayout(Layout::NCHW);
    // --------------------------- Prepare output blobs -----------------------------------------------------
    slog::info << "Checking that the outputs are as the app expects" << slog::endl;
    OutputsDataMap outputInfo(netReader.getNetwork().getOutputsInfo());
    if (outputInfo.size() != 1) {
        throw std::logic_error("This app accepts networks having only one output");
    }
    DataPtr& output = outputInfo.begin()->second;
    auto outputName = outputInfo.begin()->first;
    const int num_classes = netReader.getNetwork().getLayerByName(outputName.c_str())->GetParamAsInt("num_classes");
    if (static_cast<int>(labels.size()) != num_classes) {
        if (static_cast<int>(labels.size()) == (num_classes - 1))  // if network assumes default "background" class, having no label
            labels.insert(labels.begin(), "fake");
        else
            labels.clear();
    }
    const SizeVector outputDims = output->getTensorDesc().getDims();
    const int maxProposalCount = outputDims[2];
    const int objectSize = outputDims[3];
    if (objectSize != 7) {
        throw std::logic_error("Output should have 7 as a last dimension");
    }
    if (outputDims.size() != 4) {
        throw std::logic_error("Incorrect output dimensions for SSD");
    }
    output->setPrecision(Precision::FP32);
    output->setLayout(Layout::NCHW);
    // -----------------------------------------------------------------------------------------------------

    // --------------------------- 4. Loading model to the plugin ------------------------------------------
    slog::info << "Loading model to the plugin" << slog::endl;
    ExecutableNetwork network = plugin.LoadNetwork(netReader.getNetwork(), {});
    // -----------------------------------------------------------------------------------------------------

    // --------------------------- 5. Create infer request -------------------------------------------------
    InferRequest::Ptr async_infer_request_curr = network.CreateInferRequestPtr();
    // -----------------------------------------------------------------------------------------------------

    // --------------------------- 6. Do inference ---------------------------------------------------------
    slog::info << "Start inference " << slog::endl;

    typedef std::chrono::duration<double, std::ratio<1, 1000>> ms;
    auto total_t0 = std::chrono::high_resolution_clock::now();
    auto wallclock = std::chrono::high_resolution_clock::now();
    double ocv_decode_time = 0, ocv_render_time = 0;

    std::cout << "To close the application, press 'CTRL+C' or any key with focus on the output window" << std::endl;
    while (true) 
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto t1 = std::chrono::high_resolution_clock::now();

        frameMtx.lock();
        frameCpy = frame.clone();
        frameMtx.unlock();

        if(!frameCpy.empty())
        {
            frameToBlob(frameCpy, async_infer_request_curr, inputName);
            t1 = std::chrono::high_resolution_clock::now();
            ocv_decode_time = std::chrono::duration_cast<ms>(t1 - t0).count();
            t0 = std::chrono::high_resolution_clock::now();
            async_infer_request_curr->StartAsync();

            if (OK == async_infer_request_curr->Wait(IInferRequest::WaitMode::RESULT_READY)) 
            {
                t1 = std::chrono::high_resolution_clock::now();
                ms detection = std::chrono::duration_cast<ms>(t1 - t0);

                t0 = std::chrono::high_resolution_clock::now();
                ms wall = std::chrono::duration_cast<ms>(t0 - wallclock);
                wallclock = t0;

                t0 = std::chrono::high_resolution_clock::now();
                std::ostringstream out;
                out << "OpenCV cap/render time: " << std::fixed << std::setprecision(2)
                    << (ocv_decode_time + ocv_render_time) << " ms";
                cv::putText(frameCpy, out.str(), cv::Point2f(0, 25), cv::FONT_HERSHEY_PLAIN, 1.5, cv::Scalar(0, 255, 0));
                out.str("");
                out << "Wallclock time ";
                out << std::fixed << std::setprecision(2) << wall.count() << " ms (" << 1000.f / wall.count() << " fps)";
                cv::putText(frameCpy, out.str(), cv::Point2f(0, 50), cv::FONT_HERSHEY_PLAIN, 1.5, cv::Scalar(0, 0, 255));
                out.str("");
                out << "Detection time  : " << std::fixed << std::setprecision(2) << detection.count()
                    << " ms ("
                    << 1000.f / detection.count() << " fps)";
                cv::putText(frameCpy, out.str(), cv::Point2f(0, 75), cv::FONT_HERSHEY_PLAIN, 1.5,
                            cv::Scalar(255, 0, 0));

                // ---------------------------Process output blobs--------------------------------------------------
                // Processing results of the CURRENT request
                const float *detections = async_infer_request_curr->GetBlob(outputName)->buffer().as<PrecisionTrait<Precision::FP32>::value_type*>();
                for (int i = 0; i < maxProposalCount; i++) {
                    float image_id = detections[i * objectSize + 0];
                    if (image_id < 0) {
                        break;
                    }

                    float confidence = detections[i * objectSize + 2];
                    auto label = static_cast<int>(detections[i * objectSize + 1]);
                    float xmin = detections[i * objectSize + 3] * width;
                    float ymin = detections[i * objectSize + 4] * height;
                    float xmax = detections[i * objectSize + 5] * width;
                    float ymax = detections[i * objectSize + 6] * height;

                    if (confidence > confidenceThreshold) {
                        /** Drawing only objects when > confidence_threshold probability **/
                        std::ostringstream conf;
                        conf << ":" << std::fixed << std::setprecision(3) << confidence;
                        cv::putText(frameCpy,
                                    (static_cast<size_t>(label) < labels.size() ?
                                    labels[label] : std::string("label #") + std::to_string(label)) + conf.str(),
                                    cv::Point2f(xmin, ymin - 5), cv::FONT_HERSHEY_COMPLEX_SMALL, 1,
                                    cv::Scalar(0, 0, 255));
                        cv::rectangle(frameCpy, cv::Point2f(xmin, ymin), cv::Point2f(xmax, ymax), cv::Scalar(0, 0, 255));
                    }
                }

            }

        }        

        t1 = std::chrono::high_resolution_clock::now();
        ocv_render_time = std::chrono::duration_cast<ms>(t1 - t0).count();

        imShowMtx.lock();
        cv::imshow("Traffic results", frameCpy);
        const int key = cv::waitKey(1);
        if (27 == key)  // Esc
            break;
        imShowMtx.unlock();
    }
    // -----------------------------------------------------------------------------------------------------
    auto total_t1 = std::chrono::high_resolution_clock::now();
    ms total = std::chrono::duration_cast<ms>(total_t1 - total_t0);
    std::cout << "Total Inference time: " << total.count() << std::endl;
    }
    catch (const std::exception& error) {
        std::cerr << "[ ERROR ] " << error.what() << std::endl;
        return;
    }
    catch (...) {
        std::cerr << "[ ERROR ] Unknown/internal exception happened." << std::endl;
        return;
    }

    slog::info << "Execution successful" << slog::endl;

    return;
}

void arduinoI2C()
{
    DeltaTimer timer;
    timer.resetDeltaTimer();

    std::cout << __func__ << " I2C: Connecting\n"  << std::endl;

    if ((I2CFileStream = open(devName, O_RDWR)) < 0)
    {
        std::cerr << __func__ << " I2C: Failed to access " << devName << std::endl;
        exit(1);
    }

    std::cout << " I2C: acquiring buss to " << std::hex << ADDRESS << std::endl;

    if (ioctl(I2CFileStream, I2C_SLAVE, ADDRESS) < 0)
    {
        std::cerr << __func__ << " I2C: Failed to acquire bus access/talk to slave "
                  << std::hex << ADDRESS << std::endl;
        exit(1);
    }

    uint8_t cmd[6] = {0, 0, 0, 0, 0, 0};

    while(1)
    {
        if(timer.getDeltaTimeMs() >= 50)
        {
            timer.resetDeltaTimer();

            cmd[0] = SPEED_VALUE_FLAG;
            cmd[1] = speed >> 8;
            cmd[2] = speed;
            cmd[3] = DIR_VALUE_FLAG;
            cmd[4] = steer;

            if(lightsOn){
                setBit(cmd[5], 0);
            } else {
                clearBit(cmd[5], 0);
            }

            if(stopOn){
                setBit(cmd[5], 1);
            } else {
                clearBit(cmd[5], 1);
            }

            #if ARDUINO_DEBUG
            std::cout << __func__ << " Sending " <<
                    std::hex << (short)cmd[0] << " | " <<
                    std::dec << (short)((cmd[1] << 8)  + cmd[2]) << " | " <<
                    std::hex << (short)cmd[3] << " | " <<
                    std::dec << (int)cmd[4] << " | " <<
                    std::hex << (short)cmd[5] << std::endl;
            #endif

            uint16_t numBytes = write(I2CFileStream, cmd, 6);

            #if ARDUINO_DEBUG
            if (numBytes == 6)
                cout << __func__ << " succeeded" << endl;
            #else
            unused(numBytes);
            #endif
        }
    }
    return;
}

int8_t constrainValue(int8_t val, int8_t a, int8_t b)
{
        if(val < a)
            return a;
        else if(val >= b)
            return b;
        return val;
}

void exitRoutine (void)
{
    close(I2CFileStream);
}