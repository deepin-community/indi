#if 0
V4L INDI Driver
INDI Interface for V4L devices
Copyright (C) 2003 - 2013 Jasem Mutlaq (mutlaqja@ikarustech.com)

    This library is free software;
you can redistribute it and / or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation;
either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY;
without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library;
if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301  USA

#endif

#include <cmath>
#include <algorithm>
#include "v4l2driver.h"
#include "indistandardproperty.h"
#include "lx/Lx.h"

// Pixel size info for different cameras
typedef struct PixelSizeInfo
{
    const char * deviceLabel; // Device label used by INDI
    const char * deviceName; // device name reported by V4L
    const char * commonName; // if null, use device name
    float pixelSizeX;
    float pixelSizeY; // if negative, use pixelSizeX also for Y
    bool tested; //if False print please report message
} PixelSizeInfo;

static const PixelSizeInfo CameraDatabase[] =
{
    { "NexImage 5", "NexImage 5", nullptr, 2.2f, -1, true },
    { "Logitech Webcam Pro 9000", "UVC Camera (046d:0809)", "Logitech Webcam Pro 9000", 3.3f, -1, true },
    { "SVBONY SV105", "SVBONY SV105: SVBONY SV105", "SVBONY SV105", 3.0f, -1, true },
    { "SVBONY SV205", "SVBONY SV205: SVBONY SV205", "SVBONY SV205", 4.0f, -1, true },
    { "NexImage 10", "NexImage 10", nullptr, 1.67f, -1, true },
    { "NexImage Burst Color", "NexImage Burst Color", nullptr, 3.75f, -1, false },
    { "NexImage Burst Mono", "NexImage Burst Mono", nullptr, 3.75f, -1, false },
    { "Skyris 132C", "Skyris 132C", nullptr, 3.75f, -1, false },
    { "Skyris 132M", "Skyris 132M", nullptr, 3.75f, -1, false },
    { "Skyris 236C", "Skyris 236C", nullptr, 2.8f, -1, false },
    { "Skyris 236M", "Skyris 236M", nullptr, 2.8f, -1, false },
    { "iOptron iPolar", "iOptron iPolar: iOptron iPolar", nullptr, 3.75f, -1, true },
    { "iOptron iPolar", "iOptron iPolar", nullptr, 3.75f, -1, true },
    { "iOptron iGuider", "iOptron iGuider: iOptron iGuide", nullptr, 3.75f, -1, true },
    { "iOptron iGuider", "iOptron iGuider 1", nullptr, 3.75f, -1, true },
    { "Raspberry Pi High Quality Camera", "mmal service 16.1", "Raspberry Pi High Quality Camera", 1.55f, -1, true },
    { "Logitech HD C270", "UVC Camera (046d:0825)", "Logitech HD C270", 2.8f, -1, true },
    { "IMX290 Camera", "USB 2.0 Camera: USB Camera", "USB 2.0 IMX290 Board", 2.9f, -1, true },
    { "IMX290 H264 Camera", "0c45:6366 Microdia", "Spinel 2MP Full HD Low Light WDR H264 USB Camera Module IMX290", 2.9f, -1, true },
    { "Microsoft LifeCam Cinema", "Microsoft® LifeCam Cinema(TM):", "Microsoft® LifeCam Cinema(TM)", 3.0f, -1, false },
    { "OpenAstroGuider", "OpenAstroGuider IMX290", nullptr, 2.9f, -1, false },
    { nullptr, nullptr, nullptr, 5.6f, -1, false}  // sentinel and default pixel size, needs to be last
};

V4L2_Driver::V4L2_Driver(std::string label, std::string path)
{
    setDeviceName(label.c_str());
    strncpy(defaultVideoPort, path.c_str(), 256);
    strncpy(configPort, path.c_str(), 256);

    setVersion(1, 0);

    allocateBuffers();

    divider = 128.;

    is_capturing = false;
    non_capture_frames = 0;

    Options          = nullptr;
    v4loptions       = 0;
    AbsExposureN     = nullptr;
    ManualExposureSP = nullptr;
    CaptureSizesSP.sp = nullptr;
    CaptureSizesNP.np = nullptr;
    FrameRatesSP.sp = nullptr;

    frame_received.tv_sec = 0;
    frame_received.tv_usec = 0;

    v4l_capture_started = false;

    stackMode = STACK_NONE;

    lx       = new Lx();
    lxtimer  = -1;
    stdtimer = -1;
}

V4L2_Driver::V4L2_Driver()
{
    setVersion(1, 0);

    allocateBuffers();

    divider = 128.;

    is_capturing = false;
    non_capture_frames = 0;

    Options          = nullptr;
    v4loptions       = 0;
    AbsExposureN     = nullptr;
    ManualExposureSP = nullptr;
    CaptureSizesSP.sp = nullptr;
    CaptureSizesNP.np = nullptr;
    FrameRatesSP.sp = nullptr;

    frame_received.tv_sec = 0;
    frame_received.tv_usec = 0;

    v4l_capture_started = false;

    stackMode = STACK_NONE;

    lx       = new Lx();
    lxtimer  = -1;
    stdtimer = -1;
}

V4L2_Driver::~V4L2_Driver()
{
    releaseBuffers();
}

void V4L2_Driver::updateFrameSize()
{
    if (CaptureFormatSP.findOnSwitchIndex() == IMAGE_MONO)
        frameBytes = PrimaryCCD.getSubW() * PrimaryCCD.getSubH() * (PrimaryCCD.getBPP() / 8 + (PrimaryCCD.getBPP() % 8 ? 1 : 0));
    else
        frameBytes = PrimaryCCD.getSubW() * PrimaryCCD.getSubH() *
                     (PrimaryCCD.getBPP() / 8 + (PrimaryCCD.getBPP() % 8 ? 1 : 0)) * 3;

    PrimaryCCD.setFrameBufferSize(frameBytes);
    LOGF_DEBUG("%s: frame bytes %d", __FUNCTION__, PrimaryCCD.getFrameBufferSize());
}

bool V4L2_Driver::initProperties()
{
    INDI::CCD::initProperties();
    addDebugControl();

    /* Port */
    // Only load config port if it was empty. If it was already initialized, then we have an explicitly defined device
    // with defaultVideoPort and we shouldn't mess this up.
    if (configPort[0] == 0 && IUGetConfigText(getDeviceName(), PortTP.name, PortT[0].name, configPort, 256) == 0)
        IUFillText(&PortT[0], "PORT", "Port", configPort);
    else
        IUFillText(&PortT[0], "PORT", "Port", defaultVideoPort);
    IUFillTextVector(&PortTP, PortT, NARRAY(PortT), getDeviceName(), INDI::SP::DEVICE_PORT, "Ports", OPTIONS_TAB, IP_RW, 0,
                     IPS_IDLE);


    // Capture format.
    CaptureFormat mono = {"INDI_MONO", "Mono", 8};
    CaptureFormat color = {"INDI_RGB", "RGB", 8, true};
    addCaptureFormat(mono);
    addCaptureFormat(color);
    if (CaptureFormatSP[IMAGE_RGB].getState() == ISS_ON)
    {
        PrimaryCCD.setNAxis(3);
        updateFrameSize();
    }

    /* Image depth */
    IUFillSwitch(&ImageDepthS[0], "8 bits", "", ISS_ON);
    IUFillSwitch(&ImageDepthS[1], "16 bits", "", ISS_OFF);
    IUFillSwitchVector(&ImageDepthSP, ImageDepthS, NARRAY(ImageDepthS), getDeviceName(), "Image Depth", "",
                       IMAGE_SETTINGS_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    /* Camera Name */
    IUFillText(&camNameT[0], "Model", "", nullptr);
    IUFillTextVector(&camNameTP, camNameT, NARRAY(camNameT), getDeviceName(), "Camera", "", IMAGE_INFO_TAB, IP_RO, 0,
                     IPS_IDLE);

    /* Stacking Mode */
    IUFillSwitch(&StackModeS[STACK_NONE], "None", "", ISS_ON);
    IUFillSwitch(&StackModeS[STACK_MEAN], "Mean", "", ISS_OFF);
    IUFillSwitch(&StackModeS[STACK_ADDITIVE], "Additive", "", ISS_OFF);
    IUFillSwitch(&StackModeS[STACK_TAKE_DARK], "Take Dark", "", ISS_OFF);
    IUFillSwitch(&StackModeS[STACK_RESET_DARK], "Reset Dark", "", ISS_OFF);
    IUFillSwitchVector(&StackModeSP, StackModeS, NARRAY(StackModeS), getDeviceName(), "Stack", "", MAIN_CONTROL_TAB,
                       IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    stackMode = STACK_NONE;

    /* Inputs */
    IUFillSwitchVector(&InputsSP, nullptr, 0, getDeviceName(), "V4L2_INPUT", "Inputs", CAPTURE_FORMAT, IP_RW,
                       ISR_1OFMANY, 0, IPS_IDLE);
    /* Capture Formats */
    IUFillSwitchVector(&CaptureFormatsSP, nullptr, 0, getDeviceName(), "V4L2_FORMAT", "Capture Format", CAPTURE_FORMAT,
                       IP_RW, ISR_1OFMANY, 0, IPS_IDLE);
    /* Capture Sizes */
    IUFillSwitchVector(&CaptureSizesSP, nullptr, 0, getDeviceName(), "V4L2_SIZE_DISCRETE", "Capture Size",
                       CAPTURE_FORMAT, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);
    IUFillNumberVector(&CaptureSizesNP, nullptr, 0, getDeviceName(), "V4L2_SIZE_STEP", "Capture Size", CAPTURE_FORMAT,
                       IP_RW, 0, IPS_IDLE);
    /* Frame Rate */
    IUFillSwitchVector(&FrameRatesSP, nullptr, 0, getDeviceName(), "V4L2_FRAMEINT_DISCRETE", "Frame Interval",
                       CAPTURE_FORMAT, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);
    IUFillNumberVector(&FrameRateNP, nullptr, 0, getDeviceName(), "V4L2_FRAMEINT_STEP", "Frame Interval",
                       CAPTURE_FORMAT, IP_RW, 60, IPS_IDLE);
    /* Capture Colorspace */
    IUFillText(&CaptureColorSpaceT[0], "Name", "", nullptr);
    IUFillText(&CaptureColorSpaceT[1], "YCbCr Encoding", "", nullptr);
    IUFillText(&CaptureColorSpaceT[2], "Quantization", "", nullptr);
    IUFillTextVector(&CaptureColorSpaceTP, CaptureColorSpaceT, NARRAY(CaptureColorSpaceT), getDeviceName(),
                     "V4L2_COLORSPACE", "ColorSpace", IMAGE_INFO_TAB, IP_RO, 0, IPS_IDLE);

    /* Color Processing */
    IUFillSwitch(&ColorProcessingS[0], "Quantization", "", ISS_ON);
    IUFillSwitch(&ColorProcessingS[1], "Color Conversion", "", ISS_OFF);
    IUFillSwitch(&ColorProcessingS[2], "Linearization", "", ISS_OFF);
    IUFillSwitchVector(&ColorProcessingSP, ColorProcessingS, NARRAY(ColorProcessingS), getDeviceName(),
                       "V4L2_COLOR_PROCESSING", "Color Process", CAPTURE_FORMAT, IP_RW, ISR_NOFMANY, 0, IPS_IDLE);

    /* V4L2 Settings */
    IUFillNumberVector(&ImageAdjustNP, nullptr, 0, getDeviceName(), "Image Adjustments", "", IMAGE_GROUP, IP_RW, 60,
                       IPS_IDLE);

    PrimaryCCD.getCCDInfo()->p = IP_RW;

    PrimaryCCD.setMinMaxStep("CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", 0.001, 3600, 1, false);

    if (!lx->initProperties(this))
        LOG_WARN("Can not init Long Exposure");

#ifdef HAVE_WEBSOCKET
    SetCCDCapability(CCD_CAN_BIN | CCD_CAN_SUBFRAME | CCD_HAS_STREAMING | CCD_CAN_ABORT | CCD_HAS_WEB_SOCKET);
#else
    SetCCDCapability(CCD_CAN_BIN | CCD_CAN_SUBFRAME | CCD_HAS_STREAMING | CCD_CAN_ABORT);
#endif

    v4l_base->setDeviceName(getDeviceName());
    return true;
}

void V4L2_Driver::initCamBase()
{
    v4l_base = new INDI::V4L2_Base();
}

void V4L2_Driver::ISGetProperties(const char * dev)
{
    if (dev != nullptr && strcmp(getDeviceName(), dev) != 0)
        return;

    INDI::CCD::ISGetProperties(dev);

    defineProperty(&PortTP);
    loadConfig(true, INDI::SP::DEVICE_PORT);

    if (isConnected())
    {
        defineProperty(&camNameTP);

        defineProperty(&InputsSP);
        defineProperty(&CaptureFormatsSP);

        if (CaptureSizesSP.sp != nullptr)
            defineProperty(&CaptureSizesSP);
        else if (CaptureSizesNP.np != nullptr)
            defineProperty(&CaptureSizesNP);
        if (FrameRatesSP.sp != nullptr)
            defineProperty(&FrameRatesSP);
        else if (FrameRateNP.np != nullptr)
            defineProperty(&FrameRateNP);

        defineProperty(&StackModeSP);

        v4l_base->setNative(EncodeFormatSP[FORMAT_NATIVE].s == ISS_ON);

#ifdef WITH_V4L2_EXPERIMENTS
        defineProperty(&ImageDepthSP);
        defineProperty(&ColorProcessingSP);
        defineProperty(&CaptureColorSpaceTP);
#endif
    }
}

bool V4L2_Driver::updateProperties()
{
    INDI::CCD::updateProperties();

    if (isConnected())
    {
        defineProperty(&camNameTP);
        getBasicData();

        defineProperty(&InputsSP);
        defineProperty(&CaptureFormatsSP);

        if (CaptureSizesSP.sp != nullptr)
            defineProperty(&CaptureSizesSP);
        else if (CaptureSizesNP.np != nullptr)
            defineProperty(&CaptureSizesNP);
        if (FrameRatesSP.sp != nullptr)
            defineProperty(&FrameRatesSP);
        else if (FrameRateNP.np != nullptr)
            defineProperty(&FrameRateNP);

        defineProperty(&StackModeSP);

#ifdef WITH_V4L2_EXPERIMENTS
        defineProperty(&ImageDepthSP);
        defineProperty(&ColorProcessingSP);
        defineProperty(&CaptureColorSpaceTP);
#endif

        // Check if we have pixel size info
        const PixelSizeInfo * info = CameraDatabase;
        std::string deviceName = std::string(v4l_base->getDeviceName());
        // to lower case.
        std::transform(deviceName.begin(), deviceName.end(), deviceName.begin(), ::tolower);
        while (info->deviceName)
        {
            std::string infoDeviceName = std::string(info->deviceName);
            std::transform(infoDeviceName.begin(), infoDeviceName.end(), infoDeviceName.begin(), ::tolower);
            std::string infoDeviceLabel = std::string(info->deviceLabel);
            std::transform(infoDeviceLabel.begin(), infoDeviceLabel.end(), infoDeviceLabel.begin(), ::tolower);

            // Case insensitive comparision
            if (infoDeviceName == deviceName || infoDeviceLabel == deviceName)
                break;
            ++info;
        }

        const char * commonName = info->commonName;
        float pixX             = info->pixelSizeX;
        float pixY             = info->pixelSizeY;

        if (!commonName)
            commonName = info->deviceName;
        if (pixY < 0)
            pixY = pixX;

        if (info->deviceName)
        {
            LOGF_INFO("Setting pixel size correctly for %s", commonName);
            if (info->tested == false)
            {
                LOGF_INFO("Please report that the camera worked: Name: %s/%s Detected and working, to https://bit.ly/2S1Vxjq",
                          v4l_base->getDeviceName(), commonName);
            }
        }
        else
        {
            LOGF_INFO("Setting pixel size to default of %5.2f", pixX);
            LOGF_INFO("For future autodetection of pixel size, please report the following: Reported Name: %s, "
                      "Common Name (Eg: NexImage 10), Pixel Size to the following thread: https://bit.ly/2S1Vxjq",
                      v4l_base->getDeviceName());
        }
        SetCCDParams(V4LFrame->width, V4LFrame->height, V4LFrame->bpp, pixX, pixY);
        PrimaryCCD.setImageExtension("fits");

        if (v4l_base->isLXmodCapable())
            lx->updateProperties();
        return true;
    }
    else
    {
        unsigned int i;

        if (v4l_base->isLXmodCapable())
            lx->updateProperties();

        deleteProperty(camNameTP.name);

        deleteProperty(InputsSP.name);
        deleteProperty(CaptureFormatsSP.name);

        if (CaptureSizesSP.sp != nullptr)
            deleteProperty(CaptureSizesSP.name);
        else if (CaptureSizesNP.np != nullptr)
            deleteProperty(CaptureSizesNP.name);
        if (FrameRatesSP.sp != nullptr)
            deleteProperty(FrameRatesSP.name);
        else if (FrameRateNP.np != nullptr)
            deleteProperty(FrameRateNP.name);

        deleteProperty(ImageAdjustNP.name);
        for (i = 0; i < v4loptions; i++)
            deleteProperty(Options[i].name);
        if (Options)
            free(Options);
        Options    = nullptr;
        v4loptions = 0;

        deleteProperty(StackModeSP.name);

#ifdef WITH_V4L2_EXPERIMENTS
        deleteProperty(ImageDepthSP.name);
        deleteProperty(ColorProcessingSP.name);
        deleteProperty(CaptureColorSpaceTP.name);
#endif

        return true;
    }
}

bool V4L2_Driver::ISNewSwitch(const char * dev, const char * name, ISState * states, char * names[], int n)
{
    char errmsg[ERRMSGSIZ];
    unsigned int iopt;

    /* ignore if not ours */
    if (dev != nullptr && strcmp(getDeviceName(), dev) != 0)
        return true;

    /* Input */
    if (strcmp(name, InputsSP.name) == 0)
    {
        //if ((StreamSP.s == IPS_BUSY) ||  (ExposeTimeNP->s == IPS_BUSY) || (RecordStreamSP.s == IPS_BUSY)) {
        if (PrimaryCCD.isExposing() || Streamer->isBusy())
        {
            LOG_ERROR("Can not set input while capturing.");
            InputsSP.s = IPS_ALERT;
            IDSetSwitch(&InputsSP, nullptr);
            return false;
        }
        else
        {
            unsigned int inputindex, oldindex;
            oldindex = IUFindOnSwitchIndex(&InputsSP);
            IUResetSwitch(&InputsSP);
            IUUpdateSwitch(&InputsSP, states, names, n);
            inputindex = IUFindOnSwitchIndex(&InputsSP);

            if (v4l_base->setinput(inputindex, errmsg) == -1)
            {
                LOGF_INFO("ERROR (setinput): %s", errmsg);
                IUResetSwitch(&InputsSP);
                InputsSP.sp[oldindex].s = ISS_ON;
                InputsSP.s              = IPS_ALERT;
                IDSetSwitch(&InputsSP, nullptr);
                return false;
            }

            deleteProperty(CaptureFormatsSP.name);
            v4l_base->getcaptureformats(&CaptureFormatsSP);
            defineProperty(&CaptureFormatsSP);
            if (CaptureSizesSP.sp != nullptr)
                deleteProperty(CaptureSizesSP.name);
            else if (CaptureSizesNP.np != nullptr)
                deleteProperty(CaptureSizesNP.name);

            v4l_base->getcapturesizes(&CaptureSizesSP, &CaptureSizesNP);

            if (CaptureSizesSP.sp != nullptr)
                defineProperty(&CaptureSizesSP);
            else if (CaptureSizesNP.np != nullptr)
                defineProperty(&CaptureSizesNP);
            InputsSP.s = IPS_OK;
            IDSetSwitch(&InputsSP, nullptr);
            LOGF_INFO("Capture input: %d. %s", inputindex, InputsSP.sp[inputindex].name);
            return true;
        }
    }

    /* Encoder Format */
    if (EncodeFormatSP.isNameMatch(name))
    {
        auto format = IUFindOnSwitchName(states, names, n);
        v4l_base->setNative(strcmp(format, EncodeFormatSP[FORMAT_NATIVE].getName()) == 0);
        // Let parent handle the rest
        return INDI::CCD::ISNewSwitch(dev, name, states, names, n);
    }

    /* Capture Format */
    if (strcmp(name, CaptureFormatsSP.name) == 0)
    {
        //if ((StreamSP.s == IPS_BUSY) ||  (ExposeTimeNP->s == IPS_BUSY) || (RecordStreamSP.s == IPS_BUSY)) {
        if (PrimaryCCD.isExposing() || Streamer->isBusy())
        {
            LOG_ERROR("Can not set format while capturing.");
            CaptureFormatsSP.s = IPS_ALERT;
            IDSetSwitch(&CaptureFormatsSP, nullptr);
            return false;
        }
        else
        {
            int index, oldindex;
            oldindex = IUFindOnSwitchIndex(&CaptureFormatsSP);
            IUResetSwitch(&CaptureFormatsSP);
            IUUpdateSwitch(&CaptureFormatsSP, states, names, n);
            index = IUFindOnSwitchIndex(&CaptureFormatsSP);

            if (index < 0 || v4l_base->setcaptureformat(*((unsigned int *)CaptureFormatsSP.sp[index].aux), errmsg) == -1)
            {
                LOGF_INFO("ERROR (setformat): %s", errmsg);
                IUResetSwitch(&CaptureFormatsSP);
                CaptureFormatsSP.sp[oldindex].s = ISS_ON;
                CaptureFormatsSP.s              = IPS_ALERT;
                IDSetSwitch(&CaptureFormatsSP, nullptr);
                return false;
            }

            V4LFrame->bpp = v4l_base->getBpp();
            PrimaryCCD.setBPP(V4LFrame->bpp);

            if (CaptureSizesSP.sp != nullptr)
                deleteProperty(CaptureSizesSP.name);
            else if (CaptureSizesNP.np != nullptr)
                deleteProperty(CaptureSizesNP.name);
            v4l_base->getcapturesizes(&CaptureSizesSP, &CaptureSizesNP);

            if (CaptureSizesSP.sp != nullptr)
                defineProperty(&CaptureSizesSP);
            else if (CaptureSizesNP.np != nullptr)
                defineProperty(&CaptureSizesNP);
            CaptureFormatsSP.s = IPS_OK;

#ifdef WITH_V4L2_EXPERIMENTS
            IUSaveText(&CaptureColorSpaceT[0], getColorSpaceName(&v4l_base->fmt));
            IUSaveText(&CaptureColorSpaceT[1], getYCbCrEncodingName(&v4l_base->fmt));
            IUSaveText(&CaptureColorSpaceT[2], getQuantizationName(&v4l_base->fmt));
            IDSetText(&CaptureColorSpaceTP, nullptr);
#endif
            //direct_record=recorder->setpixelformat(v4l_base->fmt.fmt.pix.pixelformat);
            INDI_PIXEL_FORMAT pixelFormat;
            uint8_t pixelDepth = 8;
            if (getPixelFormat(v4l_base->fmt.fmt.pix.pixelformat, pixelFormat, pixelDepth))
                Streamer->setPixelFormat(pixelFormat, pixelDepth);

            saveConfig(true, CaptureFormatsSP.name);
            IDSetSwitch(&CaptureFormatsSP, "Capture format: %d. %s", index, CaptureFormatsSP.sp[index].name);
            return true;
        }
    }

    /* Capture Size (Discrete) */
    if (strcmp(name, CaptureSizesSP.name) == 0)
    {
        //if ((StreamSP.s == IPS_BUSY) ||  (ExposeTimeNP->s == IPS_BUSY) || (RecordStreamSP.s == IPS_BUSY)) {
        if (PrimaryCCD.isExposing() || Streamer->isBusy())
        {
            LOG_ERROR("Can not set capture size while capturing.");
            CaptureSizesSP.s = IPS_ALERT;
            IDSetSwitch(&CaptureSizesSP, nullptr);
            return false;
        }
        else
        {
            int index{0}, w {0}, h {0};
            IUUpdateSwitch(&CaptureSizesSP, states, names, n);
            index = IUFindOnSwitchIndex(&CaptureSizesSP);

            if (index >= 0)
                sscanf(CaptureSizesSP.sp[index].name, "%dx%d", &w, &h);
            if (w == 0 || h == 0 || v4l_base->setcapturesize(w, h, errmsg) == -1)
            {
                LOGF_INFO("ERROR (setsize): %s", errmsg);
                CaptureSizesSP.s = IPS_ALERT;
                IDSetSwitch(&CaptureSizesSP, nullptr);
                return false;
            }

            if (FrameRatesSP.sp != nullptr)
                deleteProperty(FrameRatesSP.name);
            else if (FrameRateNP.np != nullptr)
                deleteProperty(FrameRateNP.name);
            v4l_base->getframerates(&FrameRatesSP, &FrameRateNP);
            if (FrameRatesSP.sp != nullptr)
                defineProperty(&FrameRatesSP);
            else if (FrameRateNP.np != nullptr)
                defineProperty(&FrameRateNP);

            PrimaryCCD.setFrame(0, 0, w, h);
            V4LFrame->width  = w;
            V4LFrame->height = h;
            PrimaryCCD.setResolution(w, h);
            updateFrameSize();
            Streamer->setSize(w, h);

            CaptureSizesSP.s = IPS_OK;
            IDSetSwitch(&CaptureSizesSP, "Capture size (discrete): %d. %s", index, CaptureSizesSP.sp[index].name);

            saveConfig(true, CaptureSizesSP.name);
            return true;
        }
    }

    /* Frame Rate (Discrete) */
    if (strcmp(name, FrameRatesSP.name) == 0)
    {
        if (PrimaryCCD.isExposing() || Streamer->isBusy())
        {
            LOG_ERROR("Can not change frame rate while capturing.");
            FrameRatesSP.s = IPS_ALERT;
            IDSetSwitch(&FrameRatesSP, nullptr);
            return false;
        }
        int index {0};
        struct v4l2_fract frate;
        IUUpdateSwitch(&FrameRatesSP, states, names, n);
        index = IUFindOnSwitchIndex(&FrameRatesSP);
        if (index >= 0)
            sscanf(FrameRatesSP.sp[index].name, "%d/%d", &frate.numerator, &frate.denominator);
        if (index < 0 || (v4l_base->*(v4l_base->setframerate))(frate, errmsg) == -1)
        {
            LOGF_INFO("ERROR (setframerate): %s", errmsg);
            FrameRatesSP.s = IPS_ALERT;
            IDSetSwitch(&FrameRatesSP, nullptr);
            return false;
        }

        FrameRatesSP.s = IPS_OK;
        IDSetSwitch(&FrameRatesSP, "Frame Period (discrete): %d. %s", index, FrameRatesSP.sp[index].name);
        return true;
    }

    /* Image Depth */
    if (strcmp(name, ImageDepthSP.name) == 0)
    {
        if (Streamer->isRecording())
        {
            LOG_WARN("Can not set Image depth (8/16bits) while recording.");
            return false;
        }

        IUResetSwitch(&ImageDepthSP);
        IUUpdateSwitch(&ImageDepthSP, states, names, n);
        ImageDepthSP.s = IPS_OK;
        if (ImageDepthS[0].s == ISS_ON)
        {
            PrimaryCCD.setBPP(8);
        }
        else
        {
            PrimaryCCD.setBPP(16);
        }
        IDSetSwitch(&ImageDepthSP, nullptr);
        return true;
    }

    /* Stacking Mode */
    if (strcmp(name, StackModeSP.name) == 0)
    {
        IUResetSwitch(&StackModeSP);
        IUUpdateSwitch(&StackModeSP, states, names, n);
        StackModeSP.s = IPS_OK;
        stackMode     = IUFindOnSwitchIndex(&StackModeSP);
        if (stackMode == STACK_RESET_DARK)
        {
            if (V4LFrame->darkFrame != nullptr)
            {
                free(V4LFrame->darkFrame);
                V4LFrame->darkFrame = nullptr;
            }
        }

        IDSetSwitch(&StackModeSP, "Setting Stacking Mode: %s", StackModeS[stackMode].name);
        return true;
    }

    /* V4L2 Options/Menus */
    for (iopt = 0; iopt < v4loptions; iopt++)
        if (strcmp(Options[iopt].name, name) == 0)
            break;
    if (iopt < v4loptions)
    {
        unsigned int ctrl_id, optindex, ctrlindex;

        LOGF_DEBUG("Toggle switch %s=%s", Options[iopt].name, Options[iopt].label);

        Options[iopt].s = IPS_IDLE;
        IUResetSwitch(&Options[iopt]);
        if (IUUpdateSwitch(&Options[iopt], states, names, n) < 0)
            return false;

        optindex = IUFindOnSwitchIndex(&Options[iopt]);
        if (Options[iopt].sp[optindex].aux != nullptr)
            ctrlindex = *(unsigned int *)(Options[iopt].sp[optindex].aux);
        else
            ctrlindex = optindex;
        ctrl_id = (*((unsigned int *)Options[iopt].aux));
        LOGF_DEBUG("  On switch is (%d) %s=\"%s\", ctrl_id = 0x%X ctrl_index=%d", optindex,
                   Options[iopt].sp[optindex].name, Options[iopt].sp[optindex].label, ctrl_id, ctrlindex);
        if (v4l_base->setOPTControl(ctrl_id, ctrlindex, errmsg) < 0)
        {
            if (Options[iopt].nsp == 1) // button
            {
                Options[iopt].sp[optindex].s = ISS_OFF;
            }
            Options[iopt].s = IPS_ALERT;
            IDSetSwitch(&Options[iopt], nullptr);
            LOGF_ERROR("Unable to adjust setting. %s", errmsg);
            return false;
        }
        if (Options[iopt].nsp == 1) // button
        {
            Options[iopt].sp[optindex].s = ISS_OFF;
        }
        Options[iopt].s = IPS_OK;
        IDSetSwitch(&Options[iopt], nullptr);
        return true;
    }

    /* ColorProcessing */
    if (strcmp(name, ColorProcessingSP.name) == 0)
    {
        if (CaptureFormatSP[IMAGE_MONO].getState() == ISS_ON)
        {
            IUUpdateSwitch(&ColorProcessingSP, states, names, n);
            v4l_base->setColorProcessing(ColorProcessingS[0].s == ISS_ON, ColorProcessingS[1].s == ISS_ON,
                                         ColorProcessingS[2].s == ISS_ON);
            ColorProcessingSP.s = IPS_OK;
            IDSetSwitch(&ColorProcessingSP, nullptr);
            V4LFrame->bpp = v4l_base->getBpp();
            PrimaryCCD.setBPP(V4LFrame->bpp);
            PrimaryCCD.setBPP(V4LFrame->bpp);
            updateFrameSize();
            return true;
        }
        else
        {
            LOG_WARN("No color processing in color mode ");
            return false;
        }
    }
    lx->ISNewSwitch(dev, name, states, names, n);
    return INDI::CCD::ISNewSwitch(dev, name, states, names, n);
}

bool V4L2_Driver::ISNewText(const char * dev, const char * name, char * texts[], char * names[], int n)
{
    IText * tp;

    /* ignore if not ours */
    if (dev != nullptr && strcmp(getDeviceName(), dev) != 0)
        return true;

    if (strcmp(name, PortTP.name) == 0)
    {
        PortTP.s = IPS_OK;
        tp = IUFindText(&PortTP, names[0]);
        if (!tp)
            return false;
        IUSaveText(tp, texts[0]);
        IDSetText(&PortTP, nullptr);

        saveConfig(true, PortTP.name);
        return true;
    }

    lx->ISNewText(dev, name, texts, names, n);
    return INDI::CCD::ISNewText(dev, name, texts, names, n);
}

bool V4L2_Driver::ISNewNumber(const char * dev, const char * name, double values[], char * names[], int n)
{
    char errmsg[ERRMSGSIZ];

    /* ignore if not ours */
    if (dev != nullptr && strcmp(getDeviceName(), dev) != 0)
        return true;

    /* Capture Size (Step/Continuous) */
    if (strcmp(name, CaptureSizesNP.name) == 0)
    {
        if (PrimaryCCD.isExposing() || Streamer->isBusy())
        {
            LOG_ERROR("Can not set capture size while capturing.");
            CaptureSizesNP.s = IPS_BUSY;
            IDSetNumber(&CaptureSizesNP, nullptr);
            return false;
        }
        else
        {
            unsigned int sizes[2], w = 0, h = 0;
            double rsizes[2];

            if (strcmp(names[0], "Width") == 0)
            {
                sizes[0] = values[0];
                sizes[1] = values[1];
            }
            else
            {
                sizes[0] = values[1];
                sizes[1] = values[0];
            }
            if (v4l_base->setcapturesize(sizes[0], sizes[1], errmsg) == -1)
            {
                LOGF_INFO("ERROR (setsize): %s", errmsg);
                CaptureSizesNP.s = IPS_ALERT;
                IDSetNumber(&CaptureSizesNP, nullptr);
                return false;
            }
            if (strcmp(names[0], "Width") == 0)
            {
                w         = v4l_base->getWidth();
                rsizes[0] = (double)w;
                h         = v4l_base->getHeight();
                rsizes[1] = (double)h;
            }
            else
            {
                w         = v4l_base->getWidth();
                rsizes[1] = (double)w;
                h         = v4l_base->getHeight();
                rsizes[0] = (double)h;
            }

            PrimaryCCD.setFrame(0, 0, w, h);
            IUUpdateNumber(&CaptureSizesNP, rsizes, names, n);
            V4LFrame->width  = w;
            V4LFrame->height = h;
            PrimaryCCD.setResolution(w, h);
            CaptureSizesNP.s = IPS_OK;
            updateFrameSize();
            Streamer->setSize(w, h);

            IDSetNumber(&CaptureSizesNP, "Capture size (step/cont): %dx%d", w, h);
            return true;
        }
    }

    if (strcmp(ImageAdjustNP.name, name) == 0)
    {
        ImageAdjustNP.s = IPS_IDLE;

        if (IUUpdateNumber(&ImageAdjustNP, values, names, n) < 0)
            return false;

        for (int i = 0; i < ImageAdjustNP.nnp; i++)
        {
            unsigned int const ctrl_id = *((unsigned int *)ImageAdjustNP.np[i].aux0);
            double const value = ImageAdjustNP.np[i].value;

            LOGF_DEBUG("  Setting %s (%s) to %f, ctrl_id = 0x%X", ImageAdjustNP.np[i].name,
                       ImageAdjustNP.np[i].label, value, ctrl_id);

            if (v4l_base->setINTControl(ctrl_id, ImageAdjustNP.np[i].value, errmsg) < 0)
            {
                /* Some controls may become read-only depending on selected options */
                LOGF_WARN("Unable to adjust %s (ctrl_id =  0x%X)", ImageAdjustNP.np[i].label,
                          ctrl_id);
            }
            /* Some controls may have been ajusted by the driver */
            /* a read is mandatory as VIDIOC_S_CTRL is write only and does not return the actual new value */
            v4l_base->getControl(ctrl_id, &(ImageAdjustNP.np[i].value), errmsg);

            /* Warn the client if the control returned another value than what was set */
            if(value != ImageAdjustNP.np[i].value)
            {
                LOGF_WARN("Control %s set to %f returned %f (ctrl_id =  0x%X)",
                          ImageAdjustNP.np[i].label, value, ImageAdjustNP.np[i].value, ctrl_id);
            }
        }
        ImageAdjustNP.s = IPS_OK;
        IDSetNumber(&ImageAdjustNP, nullptr);
        return true;
    }

    return INDI::CCD::ISNewNumber(dev, name, values, names, n);
}

bool V4L2_Driver::StartExposure(float duration)
{
    /* Clicking the "Expose" set button while an exposure is running arrives here.
     * Now that V4L2 CCD has the option to abort, this will properly abort the exposure.
     * If CAN_ABORT is not set, we have to tell the caller we're busy until the end of this exposure.
     * If we don't, PrimaryCCD will stop exposing nonetheless and we won't be able to restart an exposure.
     */
    {
        if (Streamer->isBusy())
        {
            LOG_ERROR("Cannot start new exposure while streamer is busy, stop streaming first");
            return !(GetCCDCapability() & CCD_CAN_ABORT);
        }

        if (is_capturing)
        {
            LOGF_ERROR(
                "Cannot start new exposure until the current one completes (%.3f seconds left).",
                getRemainingExposure());
            return !(GetCCDCapability() & CCD_CAN_ABORT);

            return true;
        }
    }

    if (setShutter(duration))
    {
        V4LFrame->expose = duration;
        PrimaryCCD.setExposureDuration(duration);

        if (!lx->isEnabled() || lx->getLxmode() == LXSERIAL)
            start_capturing(false);

        /* Update exposure duration in client */
        /* FIXME: exposure update timer has period hardcoded 1 second */
        if (is_capturing && 1.0f < duration)
        {
            //if (-1 != stdtimer)
            //    IERmTimer(stdtimer);
            //stdtimer = IEAddTimer(1000, (IE_TCF *)stdtimerCallback, this);
            stdtimer = -1;
        }
        else
            stdtimer = -1;
    }

    return is_capturing;
}

bool V4L2_Driver::setShutter(double duration)
{
    if (lx->isEnabled())
    {
        LOGF_INFO("Using long exposure mode for %.3f sec frame.", duration);
        if (startlongexposure(duration))
        {
            LOGF_INFO("Started %.3f-second long exposure.", duration);
            return true;
        }
        else
        {
            DEBUGF(INDI::Logger::DBG_WARNING,
                   "Unable to start %.3f-second long exposure, falling back to auto exposure", duration);
            return false;
        }
    }
    else if (setManualExposure(duration))
    {
        exposure_duration.tv_sec  = (long) duration;
        exposure_duration.tv_usec = (long) ((duration - (double) exposure_duration.tv_sec) * 1000000.0f);

        elapsed_exposure.tv_sec = 0;
        elapsed_exposure.tv_usec = 0;

        gettimeofday(&capture_start, nullptr);

        frameCount    = 0;
        subframeCount = 0;

        // Do not spam log for short exposures.
        if (duration >= 3)
            LOGF_INFO("Started %.3f-second manual exposure.", duration);
        return true;
    }
    else
    {
        LOGF_WARN("Failed %.3f-second manual exposure, no adequate control is registered.",
                  duration);
        return false;
    }
}

bool V4L2_Driver::setManualExposure(double duration)
{
    /* N.B. Check how this differs from one camera to another. This is just a proof of concept for now */
    /* With DMx 21AU04.AS, exposing twice with the same duration causes an incomplete frame to pop in the buffer list
     * This can be worked around by verifying the buffer size, but it won't work for anything else than Y8/Y16, so set
     * exposure unconditionally */
    /*if (duration * 10000 != AbsExposureN->value)*/

    // INT control for manual exposure duration is an integer in 1/10000 seconds
    long ticks = lround(duration * 10000.0f);

    /* First check the presence of an absolute exposure control */
    if (nullptr == AbsExposureN)
    {
        /* We don't have an absolute exposure control but we can stack gray frames until the exposure elapses */
        if (CaptureFormatSP[IMAGE_MONO].getState() == ISS_ON && stackMode != STACK_NONE && stackMode != STACK_RESET_DARK)
        {
            //use frame interval as frame duration instead of max exposure time.
            if(FrameRatesSP.sp != nullptr)
            {
                LOGF_WARN("Absolute exposure duration control is undefined, stacking up to %.3f seconds using %.16s.",
                          duration, StackModeS[stackMode].name);
                int index = IUFindOnSwitchIndex(&FrameRatesSP);
                int fn, fd;
                sscanf(FrameRatesSP.sp[index].name, "%d/%d", &fn, &fd);
                IDLog("Interval = %d %d\n", fn, fd);
                ticks = (long)(10000.0 * (float)fn / (float)fd + 0.5);
                frame_duration.tv_sec  = ticks / 10000;
                frame_duration.tv_usec = (ticks % 10000) * 100;
                return true;
            }
            else
            {
                //ToDo: Same thing should be done for FrameRateNP
                LOG_ERROR("Absolute exposure duration control is undefined and tacking is not supported");
                return false;
            }
        }
        /* We don't have an absolute exposure control and stacking is not configured, bail out */
        else
        {
            LOGF_ERROR("Failed exposing, the absolute exposure duration control is undefined, and stacking is not ready.", "");
            LOGF_ERROR("Configure grayscale and stacking in order to stack streamed frames up to %.3f seconds.", duration);
            return false;
        }
    }
    /* Then if we have an exposure control, check the requested exposure duration */
    else if (AbsExposureN->max < ticks)
    {
        if( CaptureFormatSP[IMAGE_MONO].getState() == ISS_ON && stackMode == STACK_NONE )
        {
            LOG_WARN("Requested manual exposure is out of device bounds auto set stackMode to ADDITIVE" );
            stackMode = STACK_ADDITIVE;
            StackModeSP.sp[ STACK_NONE ].s = ISS_OFF;
            StackModeSP.sp[ STACK_ADDITIVE ].s = ISS_ON;
            if(ManualExposureSP) IDSetSwitch(ManualExposureSP, nullptr);
        }

        /* We can't expose as long as requested but we can stack gray frames until the exposure elapses */
        if (CaptureFormatSP[IMAGE_MONO].getState() == ISS_ON && stackMode != STACK_NONE && stackMode != STACK_RESET_DARK)
        {
            if( AbsExposureN->value != AbsExposureN->max )
            {
                LOGF_WARN("Requested manual exposure is out of device bounds [%.3f,%.3f], stacking up to %.3f seconds using %.16s.",
                          (double) AbsExposureN->min / 10000.0f, (double) AbsExposureN->max / 10000.0f,
                          duration, StackModeS[stackMode].name);
            }
            ticks = AbsExposureN->max;
        }
        /* We can't expose as long as requested and stacking is not configured, bail out */
        else
        {
            LOGF_ERROR("Failed %.3f-second manual exposure, out of device bounds [%.3f,%.3f], and stacking is not ready.",
                       duration, (double) AbsExposureN->min / 10000.0f, (double) AbsExposureN->max / 10000.0f);
            LOGF_ERROR("Configure grayscale and stacking in order to stack streamed frames up to %.3f seconds.", duration);
            return false;
        }
    }
    /* Lower-than-minimal exposure duration is left managed below */


    frame_duration.tv_sec  = ticks / 10000;
    frame_duration.tv_usec = (ticks % 10000) * 100;

    if( v4l_capture_started )
    {
        if( AbsExposureN->value != ticks )
        {
            stop_capturing();
        }
        else
        {
            return true;
        }
    }


    /* At this point we do have an absolute exposure control and a valid exposure duration, so start exposing. */

    /* Manual mode should be set before changing Exposure (Auto), if possible.
     * In some cases there might be no control available, so don't fail and try to continue.
     */
    if (ManualExposureSP)
    {
        if (ManualExposureSP->sp[0].s == ISS_OFF)
        {
            ManualExposureSP->sp[0].s = ISS_ON;
            ManualExposureSP->sp[1].s = ISS_OFF;
            ManualExposureSP->s       = IPS_IDLE;

            unsigned int const ctrlindex = ManualExposureSP->sp[0].aux ? *(unsigned int *)(ManualExposureSP->sp[0].aux) : 0;
            unsigned int const ctrl_id = (*((unsigned int *)ManualExposureSP->aux));

            char errmsg[MAXRBUF];
            if (v4l_base->setOPTControl(ctrl_id, ctrlindex, errmsg) < 0)
            {
                ManualExposureSP->sp[0].s = ISS_OFF;
                ManualExposureSP->sp[1].s = ISS_ON;
                ManualExposureSP->s       = IPS_ALERT;
                IDSetSwitch(ManualExposureSP, nullptr);

                LOGF_ERROR("Unable to adjust manual/auto exposure control. %s", errmsg);
                return false;
            }

            ManualExposureSP->s = IPS_OK;
            IDSetSwitch(ManualExposureSP, nullptr);
        }
    }
    else
    {
        LOGF_WARN("Failed switching to manual exposure, control is unavailable", "");
        /* Don't fail, let the driver try to set the absolute duration, we'll see what happens */
        /* return false; */
    }

    /* Configure absolute exposure */
    if (AbsExposureN->min <= ticks && ticks <= AbsExposureN->max)
    {
        double const restoredValue = AbsExposureN->value;
        AbsExposureN->value = ticks;

        LOGF_DEBUG("%.3f-second exposure translates to %ld 1/10,000th-second device ticks.",
                   duration, ticks);

        unsigned int const ctrl_id = *((unsigned int *)AbsExposureN->aux0);

        char errmsg[MAXRBUF];
        if (v4l_base->setINTControl(ctrl_id, AbsExposureN->value, errmsg) < 0)
        {
            ImageAdjustNP.s     = IPS_ALERT;
            AbsExposureN->value = restoredValue;
            IDSetNumber(&ImageAdjustNP, "Failed requesting %.3f-second exposure to the driver (%s).", duration, errmsg);
            return false;
        }

        ImageAdjustNP.s = IPS_OK;
        IDSetNumber(&ImageAdjustNP, nullptr);
    }
    else
    {
        LOGF_ERROR("Failed %.3f-second manual exposure, out of device bounds [%.3f,%.3f].",
                   duration, (double) AbsExposureN->min / 10000.0f, (double) AbsExposureN->max / 10000.0f);
        return false;
    }

    return true;
}

/** \internal Timer callback.
 *
 * This provides a very rough estimation of the remaining exposure to the client.
 */
void V4L2_Driver::stdtimerCallback(void * userpointer)
{
    V4L2_Driver * p = (V4L2_Driver *)userpointer;
    float remaining = p->getRemainingExposure();
    //DEBUGF(INDI::Logger::DBG_SESSION,"Exposure running, %f seconds left...", remaining);
    if (1.0f < remaining)
        p->stdtimer = IEAddTimer(1000, (IE_TCF *)stdtimerCallback, userpointer);
    else
        p->stdtimer = -1;
    p->PrimaryCCD.setExposureLeft(remaining);
}

bool V4L2_Driver::start_capturing(bool do_stream)
{
    // FIXME Must migrate completely to Stream
    // The class shouldn't be making calls to encoder/recorder directly
    // Stream? Yes or No
    // Direct Record?
    INDI_UNUSED(do_stream);
    if (Streamer->isBusy())
    {
        LOG_WARN("Cannot start exposure while streaming is in progress");
        return false;
    }

    if (is_capturing)
    {
        LOGF_WARN("Cannot start exposure while another is in progress (%.3f seconds left)",
                  getRemainingExposure());
        return false;
    }

    if( !v4l_capture_started )
    {
        char errmsg[ERRMSGSIZ];
        if (v4l_base->start_capturing(errmsg))
        {
            LOGF_WARN("V4L2 base failed starting capture (%s)", errmsg);
            return false;
        }
        else
        {
            gettimeofday(&frame_received, nullptr);
            v4l_capture_started = true;
        }
    }

    //if (do_stream)
    //v4l_base->doRecord(Streamer->isDirectRecording());

    is_capturing = true;
    return true;
}

bool V4L2_Driver::stop_capturing()
{
    if (!is_capturing && !v4l_capture_started)
    {
        LOG_WARN("No exposure or streaming in progress");
        return true;
    }

    if (!Streamer->isBusy() && 0.0f < getRemainingExposure())
    {
        LOGF_WARN("Stopping running exposure %.3f seconds before completion",
                  getRemainingExposure());
    }

    // FIXME what to do with doRecord?
    //if(Streamer->isDirectRecording())
    //v4l_base->doRecord(false);
    char errmsg[ERRMSGSIZ];
    if (v4l_base->stop_capturing(errmsg))
    {
        LOGF_WARN("V4L2 base failed stopping capture (%s)", errmsg);
    }

    is_capturing = false;
    v4l_capture_started = false;
    return true;
}

bool V4L2_Driver::startlongexposure(double timeinsec)
{
    lxtimer = IEAddTimer((int)(timeinsec * 1000.0), (IE_TCF *)lxtimerCallback, this);
    v4l_base->setlxstate(LX_ACCUMULATING);
    return (lx->startLx());
}

void V4L2_Driver::lxtimerCallback(void * userpointer)
{
    V4L2_Driver * p = (V4L2_Driver *)userpointer;

    p->lx->stopLx();
    if (p->lx->getLxmode() == LXSERIAL)
    {
        p->v4l_base->setlxstate(LX_TRIGGERED);
    }
    else
    {
        p->v4l_base->setlxstate(LX_ACTIVE);
    }
    IERmTimer(p->lxtimer);
    if (!p->v4l_base->isstreamactive())
        p->is_capturing = p->start_capturing(false); // jump to new/updateFrame
    //p->v4l_base->start_capturing(errmsg); // jump to new/updateFrame
}

bool V4L2_Driver::UpdateCCDBin(int hor, int ver)
{
    if (CaptureFormatSP[IMAGE_RGB].getState() == ISS_ON)
    {
        if (hor == 1 && ver == 1)
        {
            PrimaryCCD.setBin(hor, ver);
            Streamer->setSize(PrimaryCCD.getSubW(), PrimaryCCD.getSubH());
            return true;
        }

        LOG_WARN("Binning color frames is currently not supported.");
        return false;
    }

    if (hor != ver)
    {
        LOGF_WARN("Cannot accept asymmetrical binning %dx%d.", hor, ver);
        return false;
    }

    if (hor != 1 && hor != 2 && hor != 4)
    {
        LOG_WARN("Can only accept 1x1, 2x2, and 4x4 binning.");
        return false;
    }

    if (Streamer->isBusy())
    {
        LOG_WARN("Cannot change binning while streaming/recording.");
        return false;
    }

    PrimaryCCD.setBin(hor, ver);
    Streamer->setSize(PrimaryCCD.getSubW() / hor, PrimaryCCD.getSubH() / ver);

    return true;
}

bool V4L2_Driver::UpdateCCDFrame(int x, int y, int w, int h)
{
    char errmsg[ERRMSGSIZ];

    //LOGF_INFO("calling updateCCDFrame: %d %d %d %d", x, y, w, h);
    //IDLog("calling updateCCDFrame: %d %d %d %d\n", x, y, w, h);
    if (v4l_base->setcroprect(x, y, w, h, errmsg) != -1)
    {
        struct v4l2_rect crect;
        crect = v4l_base->getcroprect();

        V4LFrame->width  = crect.width;
        V4LFrame->height = crect.height;
        PrimaryCCD.setFrame(x, y, w, h);
        updateFrameSize();
        Streamer->setSize(w, h);
        return true;
    }
    else
    {
        LOGF_INFO("ERROR (setcroprect): %s", errmsg);
    }

    return false;
}

void V4L2_Driver::newFrame(void * p)
{
    ((V4L2_Driver *)(p))->newFrame();
}

/** @internal Stack normalized luminance pixels coming from the camera in an accumulator frame.
 */
void V4L2_Driver::stackFrame()
{
    /* FIXME: use unsigned floats, or double */
    size_t const size      = v4l_base->getWidth() * v4l_base->getHeight();
    float const * src       = v4l_base->getLinearY();
    float const * const end = v4l_base->getLinearY() + size;
    float * dest            = V4LFrame->stackedFrame;

    if (!V4LFrame->stackedFrame)
    {
        /* FIXME: allocate and reset the accumulator frame prior to this function, because memory owner is unclear, and we need more speed while accumulating */
        V4LFrame->stackedFrame = (float *)malloc(sizeof(float) * size);
        memcpy(V4LFrame->stackedFrame, src, sizeof(float) * size);
        subframeCount = 1;
    }
    else
    {
        /* Clamp to max float value */
        float const frameMax = std::numeric_limits<float>::max();
        while (src < end) if (frameMax - *dest < *src)
            {
                *dest++ = frameMax;
                src++;
            }
            else *dest++ += *src++;
        subframeCount += 1;
    }
}

struct timeval V4L2_Driver::getElapsedExposure() const
{
    struct timeval now = { .tv_sec = 0, .tv_usec = 0 }, duration = { .tv_sec = 0, .tv_usec = 0 };
    gettimeofday( &now, nullptr );
    timersub(&now, &capture_start, &duration);
    return duration;
}

float V4L2_Driver::getRemainingExposure() const
{
    struct timeval remaining = { .tv_sec = 0, .tv_usec = 0 };
    timersub(&exposure_duration, &elapsed_exposure, &remaining);
    return (float) remaining.tv_sec + (float) remaining.tv_usec / 1000000.0f;
}

void V4L2_Driver::newFrame()
{
    struct timeval current_frame_duration = frame_received;
    gettimeofday(&frame_received, nullptr);
    timersub(&frame_received, &current_frame_duration, &current_frame_duration);


    if (Streamer->isBusy())
    {
        non_capture_frames = 0;

        int width             = v4l_base->getWidth();
        int height            = v4l_base->getHeight();
        int bpp               = v4l_base->getBpp();
        int dbpp              = 8;
        int totalBytes        = 0;
        unsigned char * buffer = nullptr;

        std::unique_lock<std::mutex> guard(ccdBufferLock);

        if (v4l_base->getFormat() == V4L2_PIX_FMT_MJPEG)
        {
            Streamer->setPixelFormat(INDI_JPG);
            auto buffer = v4l_base->getMJPEGBuffer(totalBytes);
            if (buffer)
            {
                PrimaryCCD.setFrameBufferSize(totalBytes);
                memcpy(PrimaryCCD.getFrameBuffer(), buffer, totalBytes);
            }
            guard.unlock();

            Streamer->newFrame(buffer, totalBytes);
            return;
        }

        if (CaptureFormatSP[IMAGE_MONO].getState() == ISS_ON)
        {
            V4LFrame->Y = v4l_base->getY();
            totalBytes  = width * height * (dbpp / 8);
            buffer      = V4LFrame->Y;
        }
        else
        {
            V4LFrame->RGB24Buffer = v4l_base->getRGBBuffer();
            totalBytes            = width * height * (dbpp / 8) * 3;
            buffer                = V4LFrame->RGB24Buffer;
        }

        // downscale Y10 Y12 Y16
        if (bpp > dbpp)
        {
            unsigned short * src = (unsigned short *)buffer;
            unsigned char * dest = buffer;
            unsigned char shift = 0;

            if (bpp < 16)
            {
                switch (bpp)
                {
                    case 10:
                        shift = 2;
                        break;
                    case 12:
                        shift = 4;
                        break;
                }
                for (int i = 0; i < totalBytes; i++)
                {
                    *dest++ = *(src++) >> shift;
                }
            }
            else
            {
                unsigned char * src = (unsigned char *)buffer + 1; // Y16 is little endian

                for (int i = 0; i < totalBytes; i++)
                {
                    *dest++ = *src;
                    src += 2;
                }
            }
        }

        if (PrimaryCCD.getBinX() > 1)
        {
            memcpy(PrimaryCCD.getFrameBuffer(), buffer, totalBytes);
            PrimaryCCD.binFrame();
            guard.unlock();
            Streamer->newFrame(PrimaryCCD.getFrameBuffer(), frameBytes / PrimaryCCD.getBinX());
        }
        else
        {
            guard.unlock();
            Streamer->newFrame(buffer, frameBytes);
        }
        return;
    }

    if ( PrimaryCCD.isExposing() )
    {
        non_capture_frames = 0;
        if( !is_capturing )
        {
            LOG_DEBUG("Skip frame, setup not complete yet" );
            return; //skip this frame
        }

        struct timeval capture_frame_dif = { .tv_sec = 0, .tv_usec = 0 };
        timersub(&frame_received, &capture_start, &capture_frame_dif);

        float cfd = (float) capture_frame_dif.tv_sec + (float) capture_frame_dif.tv_usec / 1000000.0f;
        float fd = (float) frame_duration.tv_sec + (float) frame_duration.tv_usec / 1000000.0f;

        if( cfd < fd * 0.9 )
        {
            LOGF_DEBUG("Skip early frame cfd = %ld.%06ld seconds.", capture_frame_dif.tv_sec, capture_frame_dif.tv_usec);
            return;
        }

        timeradd(&elapsed_exposure, &frame_duration, &elapsed_exposure);

        LOGF_DEBUG("Frame took %ld.%06ld s, e = %ld.%06ld s, t = %ld.%06ld s., cfd = %ld.%06ld s.",
                   current_frame_duration.tv_sec, current_frame_duration.tv_usec,
                   elapsed_exposure.tv_sec, elapsed_exposure.tv_usec,
                   exposure_duration.tv_sec, exposure_duration.tv_usec,
                   capture_frame_dif.tv_sec, capture_frame_dif.tv_usec
                  );


        float remaining = getRemainingExposure();
        PrimaryCCD.setExposureLeft(remaining);

        // Stack Mono frames
        if ((stackMode) && !(lx->isEnabled()) && CaptureFormatSP[IMAGE_MONO].getState() == ISS_ON)
        {
            stackFrame();
        }

        /* FIXME: stacking does not account for transfer time, so we'll miss the last frames probably */
        if ((stackMode) && !(lx->isEnabled()) && CaptureFormatSP[IMAGE_MONO].getState() == ISS_ON &&
                (timercmp(&elapsed_exposure, &exposure_duration, < )))
            return; // go on stacking

        struct timeval const current_exposure = getElapsedExposure();

        if (EncodeFormatSP->findOnSwitchIndex() ==  FORMAT_NATIVE && v4l_base->getFormat() == V4L2_PIX_FMT_MJPEG)
        {
            std::unique_lock<std::mutex> guard(ccdBufferLock);
            int totalBytes = 0;
            auto buffer = v4l_base->getMJPEGBuffer(totalBytes);
            if (buffer)
            {
                memcpy(PrimaryCCD.getFrameBuffer(), buffer, totalBytes);
                PrimaryCCD.setFrameBufferSize(totalBytes, false);
            }
            PrimaryCCD.setImageExtension("jpg");
            guard.unlock();
        }
        else if (CaptureFormatSP[IMAGE_MONO].getState() == ISS_ON)
        {
            if (!stackMode)
            {
                unsigned char * src, *dest;
                src  = v4l_base->getY();
                dest = (unsigned char *)PrimaryCCD.getFrameBuffer();

                std::unique_lock<std::mutex> guard(ccdBufferLock);
                memcpy(dest, src, frameBytes);
                guard.unlock();
                //for (i=0; i< frameBytes; i++)
                //*(dest++) = *(src++);

                PrimaryCCD.binFrame();
            }
            else
            {
                float * src = V4LFrame->stackedFrame;

                /* If we have a dark frame configured, substract it from the stack */
                if ((stackMode != STACK_TAKE_DARK) && (V4LFrame->darkFrame != nullptr))
                {
                    float * dark = V4LFrame->darkFrame;

                    for (int i = 0; i < v4l_base->getWidth() * v4l_base->getHeight(); i++)
                    {
                        if (*src > *dark)
                            *src -= *dark;
                        else
                            *src = 0.0;
                        src++;
                        dark++;
                    }
                    src = V4LFrame->stackedFrame;
                }

                //IDLog("Copying stack frame from %p to %p.\n", src, dest);
                if (stackMode == STACK_MEAN)
                {
                    if (ImageDepthS[0].s == ISS_ON)
                    {
                        // depth 8 bits
                        unsigned char * dest = (unsigned char *)PrimaryCCD.getFrameBuffer();

                        std::unique_lock<std::mutex> guard(ccdBufferLock);
                        for (int i = 0; i < v4l_base->getWidth() * v4l_base->getHeight(); i++)
                            *dest++ = (unsigned char)((*src++ * 255.0f) / subframeCount);
                        guard.unlock();
                    }
                    else
                    {
                        // depth 16 bits
                        unsigned short * dest = (unsigned short *)PrimaryCCD.getFrameBuffer();

                        std::unique_lock<std::mutex> guard(ccdBufferLock);
                        for (int i = 0; i < v4l_base->getWidth() * v4l_base->getHeight(); i++)
                            *dest++ = (unsigned short)((*src++ * 65535.0f) / subframeCount);
                        guard.unlock();
                    }

                    free(V4LFrame->stackedFrame);
                    V4LFrame->stackedFrame = nullptr;
                }
                else if (stackMode == STACK_ADDITIVE)
                {
                    /* Clamp additive stacking to frame dynamic range - that is, do not consider normalized source greater than 1.0f */
                    if (ImageDepthS[0].s == ISS_ON)
                    {
                        // depth 8 bits
                        unsigned char * dest = (unsigned char *)PrimaryCCD.getFrameBuffer();

                        std::unique_lock<std::mutex> guard(ccdBufferLock);
                        for (int i = 0; i < v4l_base->getWidth() * v4l_base->getHeight(); i++)
                        {
                            *dest++ = *src < 1.0f ? (unsigned char)((*src * 255)) : 255;
                            src++;
                        }
                        guard.unlock();
                    }
                    else
                    {
                        // depth 16 bits
                        unsigned short * dest = (unsigned short *)PrimaryCCD.getFrameBuffer();

                        for (int i = 0; i < v4l_base->getWidth() * v4l_base->getHeight(); i++)
                        {
                            *dest++ = *src < 1.0f ? (unsigned short)((*src * 65535)) : 65535;
                            src++;
                        }
                    }

                    free(V4LFrame->stackedFrame);
                    V4LFrame->stackedFrame = nullptr;
                }
                else if (stackMode == STACK_TAKE_DARK)
                {
                    if (V4LFrame->darkFrame != nullptr)
                        free(V4LFrame->darkFrame);
                    V4LFrame->darkFrame    = V4LFrame->stackedFrame;
                    V4LFrame->stackedFrame = nullptr;
                    src                    = V4LFrame->darkFrame;
                    if (ImageDepthS[0].s == ISS_ON)
                    {
                        // depth 8 bits
                        unsigned char * dest = (unsigned char *)PrimaryCCD.getFrameBuffer();

                        std::unique_lock<std::mutex> guard(ccdBufferLock);
                        for (int i = 0; i < v4l_base->getWidth() * v4l_base->getHeight(); i++)
                            *dest++ = (unsigned char)((*src++ * 255));
                        guard.unlock();
                    }
                    else
                    {
                        // depth 16 bits
                        unsigned short * dest = (unsigned short *)PrimaryCCD.getFrameBuffer();

                        std::unique_lock<std::mutex> guard(ccdBufferLock);
                        for (int i = 0; i < v4l_base->getWidth() * v4l_base->getHeight(); i++)
                            *dest++ = (unsigned short)((*src++ * 65535));
                        guard.unlock();
                    }
                }
            }
            PrimaryCCD.setImageExtension("fits");
        }
        else
        {
            // Binning not supported in color images for now
            std::unique_lock<std::mutex> guard(ccdBufferLock);
            unsigned char * src  = v4l_base->getRGBBuffer();
            unsigned char * dest = PrimaryCCD.getFrameBuffer();
            // We have RGB RGB RGB data but for FITS file we need each color in separate plane. i.e. RRR GGG BBB ..etc
            unsigned char * red   = dest;
            unsigned char * green = dest + v4l_base->getWidth() * v4l_base->getHeight() * (v4l_base->getBpp() / 8);
            unsigned char * blue  = dest + v4l_base->getWidth() * v4l_base->getHeight() * (v4l_base->getBpp() / 8) * 2;

            for (int i = 0; i < (int)frameBytes; i += 3)
            {
                *(red++)   = *(src + i);
                *(green++) = *(src + i + 1);
                *(blue++)  = *(src + i + 2);
            }
            guard.unlock();

            PrimaryCCD.setImageExtension("fits");
        }
        frameCount += 1;

        if (lx->isEnabled())
        {
            if (Streamer->isBusy() == false)
                stop_capturing();

            if (PrimaryCCD.getExposureDuration() >= 3)
                LOGF_INFO("Capture of LX frame took %ld.%06ld seconds.", current_exposure.tv_sec, current_exposure.tv_usec);
            ExposureComplete(&PrimaryCCD);
        }
        else
        {
            //if (!is_streaming && !is_recording) stop_capturing();
            if (Streamer->isBusy() == false)
            {
                //just mark stop
                is_capturing = false;
            }
            else
                LOGF_DEBUG("%s: streamer is busy, continue capturing\n", __FUNCTION__);


            if (PrimaryCCD.getExposureDuration() >= 3)
                LOGF_INFO("Capture of one frame (%d stacked frames) took %ld.%06ld seconds.",  subframeCount, current_exposure.tv_sec,
                          current_exposure.tv_usec);
            ExposureComplete(&PrimaryCCD);
        }
    }
    else
    {
        non_capture_frames++;

        if( non_capture_frames > 10 )
        {
            /* If we arrive here, PrimaryCCD is not exposing anymore, we can't forward the frame and we can't be aborted neither, thus abort the exposure right now.
            * That issue can be reproduced when clicking the "Set" button on the "Main Control" tab while an exposure is running.
            * Note that the patch in StartExposure returning busy instead of error prevents the flow from coming here, so now it's only a safeguard. */
            IDLog("%s: frame received while not exposing, force-aborting capture\n", __FUNCTION__);
            AbortExposure();
        }
    }
}

bool V4L2_Driver::AbortExposure()
{
    if (lx->isEnabled())
    {
        lx->stopLx();
        return true;
    }
    else if (!Streamer->isBusy())
    {
        if (-1 != stdtimer)
            IERmTimer(stdtimer);
        return stop_capturing();
    }

    LOG_WARN("Cannot abort exposure while video streamer is busy, stop streaming first");
    return false;
}

bool V4L2_Driver::Connect()
{
    char errmsg[ERRMSGSIZ];
    if (!isConnected())
    {
        if (v4l_base->connectCam(PortT[0].text, errmsg) < 0)
        {
            LOGF_ERROR("Error: unable to open device %s: %s", PortT[0].text, errmsg);
            return false;
        }

        /* Sucess! */
        LOGF_INFO("%s is online.", getDeviceName());

        // If port not stored in config already then save it.
        if (strcmp(PortT[0].text, configPort))
            saveConfig(true, PortTP.name);

        v4l_base->registerCallback(newFrame, this);
        lx->setCamerafd(v4l_base->fd);

        if (!(strcmp((const char *)v4l_base->cap.driver, "pwc")))
            LOG_INFO(
                "To use LED Long exposure mode with recent kernels, see https://code.google.com/p/pwc-lxled/");
    }

    return true;
}

bool V4L2_Driver::Disconnect()
{
    if (isConnected())
    {
        v4l_base->disconnectCam(PrimaryCCD.isExposing() || Streamer->isBusy());
        if (PrimaryCCD.isExposing() || Streamer->isBusy())
            Streamer->close();
    }
    return true;
}

const char * V4L2_Driver::getDefaultName()
{
    return (const char *)"V4L2 CCD";
}

/* Retrieves basic data from the device upon connection.*/
void V4L2_Driver::getBasicData()
{
    //int xmax, ymax, xmin, ymin;
    unsigned int w, h;
    int inputindex = -1, formatindex = -1;
    struct v4l2_fract frate;

    v4l_base->getinputs(&InputsSP);
    v4l_base->getcaptureformats(&CaptureFormatsSP);
    v4l_base->getcapturesizes(&CaptureSizesSP, &CaptureSizesNP);
    v4l_base->getframerates(&FrameRatesSP, &FrameRateNP);

    w                = v4l_base->getWidth();
    h                = v4l_base->getHeight();
    V4LFrame->width  = w;
    V4LFrame->height = h;
    V4LFrame->bpp    = v4l_base->getBpp();

    inputindex  = IUFindOnSwitchIndex(&InputsSP);
    formatindex = IUFindOnSwitchIndex(&CaptureFormatsSP);
    frate       = (v4l_base->*(v4l_base->getframerate))();
    if (inputindex >= 0 && formatindex >= 0)
        LOGF_INFO("Found initial Input \"%s\", Format \"%s\", Size %dx%d, Frame interval %d/%ds",
                  InputsSP.sp[inputindex].name, CaptureFormatsSP.sp[formatindex].name, w, h, frate.numerator,
                  frate.denominator);
    else
        LOGF_INFO("Found initial size %dx%d, frame interval %d/%ds", w, h, frate.numerator,
                  frate.denominator);

    IUSaveText(&camNameT[0], v4l_base->getDeviceName());
    IDSetText(&camNameTP, nullptr);
#ifdef WITH_V4L2_EXPERIMENTS
    IUSaveText(&CaptureColorSpaceT[0], getColorSpaceName(&v4l_base->fmt));
    IUSaveText(&CaptureColorSpaceT[1], getYCbCrEncodingName(&v4l_base->fmt));
    IUSaveText(&CaptureColorSpaceT[2], getQuantizationName(&v4l_base->fmt));
    IDSetText(&CaptureColorSpaceTP, nullptr);
#endif
    if (Options)
        free(Options);
    Options    = nullptr;
    v4loptions = 0;
    updateV4L2Controls();

    PrimaryCCD.setResolution(w, h);
    PrimaryCCD.setFrame(0, 0, w, h);
    PrimaryCCD.setBPP(V4LFrame->bpp);
    updateFrameSize();
    //direct_record=recorder->setpixelformat(v4l_base->fmt.fmt.pix.pixelformat);
    //recorder->setsize(w, h);
    INDI_PIXEL_FORMAT pixelFormat;
    uint8_t pixelDepth = 8;
    if (getPixelFormat(v4l_base->fmt.fmt.pix.pixelformat, pixelFormat, pixelDepth))
        Streamer->setPixelFormat(pixelFormat, pixelDepth);

    Streamer->setSize(w, h);
}

void V4L2_Driver::updateV4L2Controls()
{
    unsigned int i;

    LOG_DEBUG("Enumerating V4L2 controls...");

    // #1 Query for INTEGER controls, and fill up the structure
    free(ImageAdjustNP.np);
    ImageAdjustNP.nnp = 0;

    //if (v4l_base->queryINTControls(&ImageAdjustNP) > 0)
    //defineProperty(&ImageAdjustNP);
    v4l_base->enumerate_ext_ctrl();
    useExtCtrl = false;

    if (v4l_base->queryExtControls(&ImageAdjustNP, &v4ladjustments, &Options, &v4loptions, getDeviceName(),
                                   IMAGE_BOOLEAN))
        useExtCtrl = true;
    else
        v4l_base->queryControls(&ImageAdjustNP, &v4ladjustments, &Options, &v4loptions, getDeviceName(), IMAGE_BOOLEAN);

    if (v4ladjustments > 0)
    {
        LOGF_DEBUG("Found %d V4L2 adjustments", v4ladjustments);
        defineProperty(&ImageAdjustNP);

        for (int i = 0; i < ImageAdjustNP.nnp; i++)
        {
            if (strcmp(ImageAdjustNP.np[i].label, "Exposure (Absolute)") == 0 ||
                    strcmp(ImageAdjustNP.np[i].label, "Exposure Time, Absolute") == 0 ||
                    strcmp(ImageAdjustNP.np[i].label, "Exposure") == 0)
            {
                AbsExposureN = ImageAdjustNP.np + i;
                LOGF_DEBUG("- %s (used for absolute exposure duration)", ImageAdjustNP.np[i].label);
            }
            else LOGF_DEBUG("- %s", ImageAdjustNP.np[i].label);
        }
    }
    LOGF_DEBUG("Found %d V4L2 options", v4loptions);
    for (i = 0; i < v4loptions; i++)
    {
        defineProperty(&Options[i]);

        if (strcmp(Options[i].label, "Exposure, Auto") == 0 || strcmp(Options[i].label, "Auto Exposure") == 0)
        {
            ManualExposureSP = Options + i;
            LOGF_DEBUG("- %s (used for manual/auto exposure control)", Options[i].label);
        }
        else LOGF_DEBUG("- %s", Options[i].label);
    }

    if(!AbsExposureN)
    {
        DEBUGF(INDI::Logger::DBG_WARNING, "Absolute exposure duration control is not possible on the device!", "");
    }

    if(!ManualExposureSP)
        DEBUGF(INDI::Logger::DBG_WARNING, "Manual/auto exposure control is not possible on the device!", "");

    //v4l_base->enumerate_ctrl();
}

void V4L2_Driver::allocateBuffers()
{
    V4LFrame = new img_t;

    if (V4LFrame == nullptr)
    {
        LOG_ERROR("Critical Error: Unable to initialize driver. Low memory.");
        exit(-1);
    }

    V4LFrame->Y            = nullptr;
    V4LFrame->U            = nullptr;
    V4LFrame->V            = nullptr;
    V4LFrame->RGB24Buffer  = nullptr;
    V4LFrame->stackedFrame = nullptr;
    V4LFrame->darkFrame    = nullptr;
}

void V4L2_Driver::releaseBuffers()
{
    delete (V4LFrame);
}

bool V4L2_Driver::StartStreaming()
{
    if (PrimaryCCD.getBinX() > 1 && PrimaryCCD.getNAxis() > 2)
    {
        LOG_WARN("Cannot stream binned color frame.");
        return false;
    }

    auto onSwitch = IUFindOnSwitch(&CaptureFormatsSP);
    if (onSwitch && strstr(onSwitch->label, "JPEG"))
        v4l_base->setNative(true);
    /* Callee will take care of checking states */
    return start_capturing(true);
}

bool V4L2_Driver::StopStreaming()
{
    if (!Streamer->isBusy() /*&& is_capturing*/)
    {
        /* Strange situation indeed, but it's theoretically possible to try to stop streaming while exposing - safeguard actually */
        LOGF_WARN("Cannot stop streaming, exposure running (%.1f seconds remaining)",
                  getRemainingExposure());
        return false;
    }

    v4l_base->setNative(EncodeFormatSP[FORMAT_NATIVE].s == ISS_ON);
    return stop_capturing();
}

bool V4L2_Driver::saveConfigItems(FILE * fp)
{
    INDI::CCD::saveConfigItems(fp);

    IUSaveConfigText(fp, &PortTP);
    IUSaveConfigSwitch(fp, &StackModeSP);

    if (ImageAdjustNP.nnp > 0)
        IUSaveConfigNumber(fp, &ImageAdjustNP);

    // Try to save important auto-generated properties, if found.

    // Format
    auto format = getProperty("V4L2_FORMAT");
    if (format.isValid())
        IUSaveConfigSwitch(fp, format.getSwitch());

    // Size
    auto size = getProperty("V4L2_SIZE_DISCRETE");
    if (size.isValid())
        IUSaveConfigSwitch(fp, size.getSwitch());

    auto fps = getProperty("V4L2_FRAMEINT_DISCRETE");
    if (fps.isValid())
        IUSaveConfigSwitch(fp, fps.getSwitch());


    return Streamer->saveConfigItems(fp);
}

bool V4L2_Driver::getPixelFormat(uint32_t v4l2format, INDI_PIXEL_FORMAT &pixelFormat, uint8_t &pixelDepth)
{
    //IDLog("recorder: setpixelformat %d\n", format);
    pixelDepth = 8;
    switch (v4l2format)
    {
        case V4L2_PIX_FMT_GREY:
#ifdef V4L2_PIX_FMT_Y10
        case V4L2_PIX_FMT_Y10:
#endif
#ifdef V4L2_PIX_FMT_Y12
        case V4L2_PIX_FMT_Y12:
#endif
#ifdef V4L2_PIX_FMT_Y16
        case V4L2_PIX_FMT_Y16:
#endif
            pixelFormat = INDI_MONO;
#ifdef V4L2_PIX_FMT_Y10
            if (v4l2format == V4L2_PIX_FMT_Y10)
                pixelDepth = 10;
#endif
#ifdef V4L2_PIX_FMT_Y12
            if (v4l2format == V4L2_PIX_FMT_Y12)
                pixelDepth = 12;
#endif
#ifdef V4L2_PIX_FMT_Y16
            if (v4l2format == V4L2_PIX_FMT_Y16)
                pixelDepth = 16;
#endif
            return true;
        case V4L2_PIX_FMT_SBGGR8:
#ifdef V4L2_PIX_FMT_SBGGR10
        case V4L2_PIX_FMT_SBGGR10:
#endif
#ifdef V4L2_PIX_FMT_SBGGR12
        case V4L2_PIX_FMT_SBGGR12:
#endif
        case V4L2_PIX_FMT_SBGGR16:
            pixelFormat = INDI_BAYER_BGGR;
#ifdef V4L2_PIX_FMT_SBGGR10
            if (v4l2format == V4L2_PIX_FMT_SBGGR10)
                pixelDepth = 10;
#endif
#ifdef V4L2_PIX_FMT_SBGGR12
            if (v4l2format == V4L2_PIX_FMT_SBGGR12)
                pixelDepth = 12;
#endif
            if (v4l2format == V4L2_PIX_FMT_SBGGR16)
                pixelDepth = 16;
            return true;
        case V4L2_PIX_FMT_SGBRG8:
#ifdef V4L2_PIX_FMT_SGBRG10
        case V4L2_PIX_FMT_SGBRG10:
#endif
#ifdef V4L2_PIX_FMT_SGBRG12
        case V4L2_PIX_FMT_SGBRG12:
#endif
            pixelFormat = INDI_BAYER_GBRG;
#ifdef V4L2_PIX_FMT_SGBRG10
            if (v4l2format == V4L2_PIX_FMT_SGBRG10)
                pixelDepth = 10;
#endif
#ifdef V4L2_PIX_FMT_SGBRG12
            if (v4l2format == V4L2_PIX_FMT_SGBRG12)
                pixelDepth = 12;
#endif
            return true;
#if defined(V4L2_PIX_FMT_SGRBG8) || defined(V4L2_PIX_FMT_SGRBG10) || defined(V4L2_PIX_FMT_SGRBG12)
#ifdef V4L2_PIX_FMT_SGRBG8
        case V4L2_PIX_FMT_SGRBG8:
#endif
#ifdef V4L2_PIX_FMT_SGRBG10
        case V4L2_PIX_FMT_SGRBG10:
#endif
#ifdef V4L2_PIX_FMT_SGRBG12
        case V4L2_PIX_FMT_SGRBG12:
#endif
            pixelFormat = INDI_BAYER_GRBG;
#ifdef V4L2_PIX_FMT_SGRBG10
            if (v4l2format == V4L2_PIX_FMT_SGRBG10)
                pixelDepth = 10;

#endif
#ifdef V4L2_PIX_FMT_SGRBG12
            if (v4l2format == V4L2_PIX_FMT_SGRBG12)
                pixelDepth = 12;
#endif
            return true;
#endif
#if defined(V4L2_PIX_FMT_SRGGB8) || defined(V4L2_PIX_FMT_SRGGB10) || defined(V4L2_PIX_FMT_SRGGB12)
#ifdef V4L2_PIX_FMT_SRGGB8
        case V4L2_PIX_FMT_SRGGB8:
#endif
#ifdef V4L2_PIX_FMT_SRGGB10
        case V4L2_PIX_FMT_SRGGB10:
#endif
#ifdef V4L2_PIX_FMT_SRGGB12
        case V4L2_PIX_FMT_SRGGB12:
#endif
            pixelFormat = INDI_BAYER_RGGB;
#ifdef V4L2_PIX_FMT_SRGGB10
            if (v4l2format == V4L2_PIX_FMT_SRGGB10)
                pixelDepth = 10;
#endif
#ifdef V4L2_PIX_FMT_SRGGB12
            if (v4l2format == V4L2_PIX_FMT_SRGGB12)
                pixelDepth = 12;
#endif
            return true;
#endif
        case V4L2_PIX_FMT_RGB24:
            pixelFormat = INDI_RGB;
            return true;
        case V4L2_PIX_FMT_BGR24:
            pixelFormat = INDI_BGR;
            return true;
        default:
            return false;
    }
}

bool V4L2_Driver::SetCaptureFormat(uint8_t index)
{
    if (Streamer->isRecording())
    {
        LOG_WARN("Can not set Image type (GRAY/COLOR) while recording.");
        return false;
    }

    PrimaryCCD.setNAxis(index == IMAGE_MONO ? 2 : 3);
    updateFrameSize();
    Streamer->setPixelFormat(index == 0 ? INDI_MONO : INDI_RGB, 8);
    return true;
}
