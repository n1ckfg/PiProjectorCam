#include "ofApp.h"

using namespace cv;
using namespace ofxCv;

//--------------------------------------------------------------
void ofApp::imageChangeSize(int newSize) {
    imageRatio = (float) target.getHeight() / (float) target.getWidth();
    imageW = newSize;
    imageH = (int) ((float) imageW * imageRatio);
 
    target.allocate(targetOrig.getWidth(), targetOrig.getHeight(), OF_IMAGE_COLOR);
    imitate(target, targetOrig);
    target.resize(imageW, imageH);
    target.update();
}


void ofApp::setup() {
    settings.loadFile("settings.xml");

    debug = (bool) settings.getValue("settings:debug", 1);
    useRpiCam = (bool) settings.getValue("settings:use_rpi_cam", 1);
    camWidth = settings.getValue("settings:width", 320);
    camHeight = settings.getValue("settings:height", 240);
    framerate = settings.getValue("settings:framerate", 60);

    targetOrig.load("calibration/target/target.png");
    mouseX = 0;
    mouseY = 0;
    imageStartSize = camWidth/2;
    imageSizeIncrement = 10;
    screenMarginW = ofGetWidth() - ((float) camWidth * ((float) ofGetHeight() / (float) camHeight));
    imageChangeSize(imageStartSize);

    ofSetFrameRate(framerate);
    ofSetVerticalSync(true);
    ofHideCursor();

    if (!useRpiCam) {
        //get back a list of devices.
        vector<ofVideoDevice> devices = vidGrabber.listDevices();

        for (size_t i = 0; i < devices.size(); i++) {
            if (devices[i].bAvailable) {
                //log the device
                ofLogNotice() << devices[i].id << ": " << devices[i].deviceName;
            } else {
                //log the device and note it as unavailable
                ofLogNotice() << devices[i].id << ": " << devices[i].deviceName << " - unavailable ";
            }
        }

        vidGrabber.setDeviceID(0);
        vidGrabber.setDesiredFrameRate(framerate);
        vidGrabber.initGrabber(camWidth, camHeight);
    } else {
        // ~ ~ ~   rpi cam settings   ~ ~ ~
        camSharpness = settings.getValue("settings:sharpness", 0);
        camContrast = settings.getValue("settings:contrast", 0);
        camBrightness = settings.getValue("settings:brightness", 50);
        camIso = settings.getValue("settings:iso", 300);
        camExposureMode = settings.getValue("settings:exposure_mode", 0);
        camExposureCompensation = settings.getValue("settings:exposure_compensation", 0);
        camShutterSpeed = settings.getValue("settings:shutter_speed", 0);

        cam.setSharpness(camSharpness);
        cam.setContrast(camContrast);
        cam.setBrightness(camBrightness);
        cam.setISO(camIso);
        cam.setExposureMode((MMAL_PARAM_EXPOSUREMODE_T)camExposureMode);
        cam.setExposureCompensation(camExposureCompensation);
        cam.setShutterSpeed(camShutterSpeed);

        cam.setup(camWidth, camHeight, false); // color/gray;
    }

    // * stream video *
    // https://github.com/bakercp/ofxHTTP/blob/master/libs/ofxHTTP/include/ofx/HTTP/IPVideoRoute.h
    // https://github.com/bakercp/ofxHTTP/blob/master/libs/ofxHTTP/src/IPVideoRoute.cpp
    sendFbo.allocate(camWidth*2, camHeight, GL_RGBA);
    pixels.allocate(camWidth*2, camHeight, OF_IMAGE_COLOR);
    projectorFbo.allocate(camWidth, camHeight, GL_RGBA);
   
    streamPort = settings.getValue("settings:stream_port", 7111);
    streamSettings.setPort(streamPort);
    streamSettings.ipVideoRouteSettings.setMaxClientConnections(settings.getValue("settings:max_stream_connections", 5)); // default 5
    streamSettings.ipVideoRouteSettings.setMaxClientBitRate(settings.getValue("settings:max_stream_bitrate", 512)); // default 1024
    streamSettings.ipVideoRouteSettings.setMaxClientFrameRate(settings.getValue("settings:max_stream_framerate", 30)); // default 30
    streamSettings.ipVideoRouteSettings.setMaxClientQueueSize(settings.getValue("settings:max_stream_queue", 10)); // default 10
    streamSettings.ipVideoRouteSettings.setMaxStreamWidth(camWidth*2); // default 1920
    streamSettings.ipVideoRouteSettings.setMaxStreamHeight(camHeight); // default 1080
    streamSettings.fileSystemRouteSettings.setDefaultIndex("index.html");
    streamServer.setup(streamSettings);
    streamServer.start();

    camReady = false;

    setupHomography();
}


//--------------------------------------------------------------
void ofApp::update() {
	updateHomography();

    if (!useRpiCam) {
        vidGrabber.update();
        frame = toCv(vidGrabber);
        if (vidGrabber.isFrameNew()) camReady = true;
    } else {
        frame = cam.grab();
        if (!frame.empty()) camReady = true;
    }

    if (camReady) {
        if (debug) updateStreamingVideo();
        camReady = false;
    }
}

//--------------------------------------------------------------
void ofApp::draw() {
    ofBackground(0,0,255);
    if (debug) {
        projectorFbo.begin();
        ofBackground(0);
        ofTranslate(mouseX, mouseY);  
        target.draw(-target.getWidth()/2,-target.getHeight()/2);  
        projectorFbo.end();

        projectorFbo.draw(screenMarginW/2, 0, ofGetWidth()-screenMarginW, ofGetHeight());
    } else {
        warpedColor.draw(screenMarginW/2, 0, ofGetWidth()-screenMarginW, ofGetHeight());
    }
}

void ofApp::updateStreamingVideo() {
    sendFbo.begin();
    projectorFbo.draw(0,0);
    if(homographyReady) {
        imitate(warpedColor, frame);
        // this is how you warp one ofImage into another ofImage given the homography matrix
        // CV INTER NN is 113 fps, CV_INTER_LINEAR is 93 fps
        warpPerspective(frame, warpedColor, homography, CV_INTER_LINEAR);
        warpedColor.update();
        warpedColor.draw(camWidth, 0);
    } else {
        drawMat(frame, camWidth, 0);
    }
    sendFbo.end();

    sendFbo.readToPixels(pixels);
    streamServer.send(pixels);
}

//~ ~ ~ homography

void ofApp::setupHomography() {
    FileStorage settings(ofToDataPath("calibration/target/target_settings.yml"), FileStorage::READ);
    if (settings.isOpened()) {
        int xCount = settings["xCount"], yCount = settings["yCount"];
        calibration.setPatternSize(xCount, yCount);
        float squareSize = settings["squareSize"];
        calibration.setSquareSize(squareSize);
        CalibrationPattern patternType;
        switch (settings["patternType"]) {
        case 0: patternType = CHESSBOARD; break;
        case 1: patternType = CIRCLES_GRID; break;
        case 2: patternType = ASYMMETRIC_CIRCLES_GRID; break;
        }
        calibration.setPatternType(CHESSBOARD); // patternType);
    }

    ofDirectory leftCalibrationDir("calibration/left/");
    ofDirectory rightCalibrationDir("calibration/right/");
    leftCalibrationDir.allowExt(inputFileType);
    leftCalibrationDir.listDir();
    leftCalibrationDir.sort();
    rightCalibrationDir.allowExt(inputFileType);
    rightCalibrationDir.listDir();
    rightCalibrationDir.sort();
    int leftCalibrationDirCount = leftCalibrationDir.size();
    int rightCalibrationDirCount = rightCalibrationDir.size();
    cout << "calib L: " << leftCalibrationDirCount << ", calib R: " << rightCalibrationDirCount << endl;

    // load the previous homography if it's available
    string calibrationUrl = "calibration/homography.yml";
    ofFile previous(calibrationUrl);
    if (previous.exists()) {
        cout << "Found existing calibration file." << endl;
        FileStorage fs(ofToDataPath(calibrationUrl), FileStorage::READ);
        fs["homography"] >> homography;
        homographyReady = true;
    } else {
        for (int i=0; i<leftCalibrationDir.size(); i++) {
            string leftCalibrationUrl = leftCalibrationDir.getPath(i);
            string rightCalibrationUrl = rightCalibrationDir.getPath(i);
            cout << "calib L " << (i+1) << "/" << leftCalibrationDirCount << ": " << leftCalibrationUrl << endl;
            cout << "calib R " << (i+1) << "/" << rightCalibrationDirCount << ": " << rightCalibrationUrl << endl;

            left.load(leftCalibrationUrl);
            right.load(rightCalibrationUrl);

            vector<Point2f> leftBoardPoints;
            vector<Point2f> rightBoardPoints;
            calibration.findBoard(toCv(left), leftBoardPoints);
            calibration.findBoard(toCv(right), rightBoardPoints);
            for (int i = 0; i < leftBoardPoints.size(); i++) {
                Point2f pt = leftBoardPoints[i];
                leftPoints.push_back(ofVec2f(pt.x, pt.y));
            }
            for (int i = 0; i < rightBoardPoints.size(); i++) {
                Point2f pt = rightBoardPoints[i];
                rightPoints.push_back(ofVec2f(pt.x + right.getWidth(), pt.y));
            }
        }
        
        movingPoint = false;
        saveMatrix = false;
        homographyReady = false;
    }
}

void ofApp::updateHomography() {
    if (finished) {
        return; //warpedColor.update();
    } else {
        if (!homographyReady) {
            if(leftPoints.size() >= 4) {
                vector<Point2f> srcPoints, dstPoints;
                for(int i = 0; i < leftPoints.size(); i++) {
                    srcPoints.push_back(Point2f(rightPoints[i].x - left.getWidth(), rightPoints[i].y));
                    dstPoints.push_back(Point2f(leftPoints[i].x, leftPoints[i].y));
                }
                
                // generate a homography from the two sets of points
                homography = findHomography(Mat(srcPoints), Mat(dstPoints));
                
                //if(saveMatrix) {
                FileStorage fs(ofToDataPath("calibration/homography.yml"), FileStorage::WRITE);
                fs << "homography" << homography;
                //saveMatrix = false;
                //}
                cout << "Wrote new calibration data to calibration/homography.yml" << endl;
                homographyReady = true;
            }
        }       
    }
}

void ofApp::drawPoints(vector<ofVec2f>& points) {
    ofNoFill();
    for(int i = 0; i < points.size(); i++) {
        ofDrawCircle(points[i], 10);
        ofDrawCircle(points[i], 1);
    }
}

void ofApp::drawHomography() {   
    ofSetColor(255);
    left.draw(0, 0);
    right.draw(left.getWidth(), 0);
    if(homographyReady) {
        ofEnableBlendMode(OF_BLENDMODE_ADD);
        ofSetColor(255, 128);
        warpedColor.draw(0, 0);
        ofDisableBlendMode();
    }
    
    ofSetColor(255, 0, 0);
    drawPoints(leftPoints);
    ofSetColor(0, 255, 255);
    drawPoints(rightPoints);
    ofSetColor(128);
    for(int i = 0; i < leftPoints.size(); i++) {
        ofDrawLine(leftPoints[i], rightPoints[i]);
    }
    
    ofSetColor(255);
    ofDrawBitmapString(ofToString((int) ofGetFrameRate()), 10, 20);
}

bool ofApp::movePoint(vector<ofVec2f>& points, ofVec2f point) {
    for(int i = 0; i < points.size(); i++) {
        if(points[i].distance(point) < 20) {
            movingPoint = true;
            curPoint = &points[i];
            return true;
        }
    }
    return false;
}

void ofApp::mousePressed(int x, int y, int button) {
    moveTarget = true;
}

void ofApp::mouseDragged(int x, int y, int button) {
    if (moveTarget) {
        mouseX = x;
        mouseY = y;
    }
}

void ofApp::mouseReleased(int x, int y, int button) {
    moveTarget = false;
}

void ofApp::keyPressed(int key) {
    if(key == OF_KEY_TAB) {
        debug = !debug;
    }
}

