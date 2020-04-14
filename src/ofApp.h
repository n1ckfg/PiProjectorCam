#pragma once

#include "ofMain.h"
#include "ofxCv.h"
#include "ofxOpenCv.h"
#include "ofxCvPiCam.h"
#include "ofxXmlSettings.h"
#include "ofxHTTP.h"
#include "ofxJSONElement.h"

#define NUM_MESSAGES 30 // how many past ws messages we want to keep

class ofApp : public ofBaseApp {

    public:

        void setup();
        void update();
        void draw();

        ofxXmlSettings settings;
        bool debug; // draw to local screen, default true
        int framerate;

        ofVideoGrabber vidGrabber;
        int camWidth;
        int camHeight;

        ofxCvPiCam cam;
        cv::Mat frame;

        // for more camera settings, see:
        // https://github.com/orgicus/ofxCvPiCam/blob/master/example-ofxCvPiCam-allSettings/src/testApp.cpp

        int camShutterSpeed; // 0 to 330000 in microseconds, default 0
        int camSharpness; // -100 to 100, default 0
        int camContrast; // -100 to 100, default 0
        int camBrightness; // 0 to 100, default 50
        int camIso; // 100 to 800, default 300
        int camExposureCompensation; // -10 to 10, default 0;

        // 0 off, 1 auto, 2 night, 3 night preview, 4 backlight, 5 spotlight, 6 sports, 7, snow, 8 beach, 9 very long, 10 fixed fps, 11 antishake, 12 fireworks, 13 max
        int camExposureMode; // 0 to 13, default 0

        ofFbo fbo;
        int fboScaleW, fboScaleH, fboPosX, fboPosY;
        ofPixels pixels;

        int streamPort;
        ofxHTTP::SimpleIPVideoServer streamServer;
        ofxHTTP::SimpleIPVideoServerSettings streamSettings;
        void updateStreamingVideo();

        bool cam1Ready, cam2Ready;

        //~ ~ ~ homography

        void setupHomography();
        void updateHomography();
        void drawHomography();

        bool movePoint(vector<ofVec2f>& points, ofVec2f point);
        void drawPoints(vector<ofVec2f>& points);
        void mousePressed(int x, int y, int button);
        void mouseDragged(int x, int y, int button);
        void mouseReleased(int x, int y, int button);
        void keyPressed(int key);
    
        ofImage left, right, warpedColor;
        vector<ofVec2f> leftPoints, rightPoints;
        bool movingPoint;
        ofVec2f* curPoint;
        bool saveMatrix;
        bool homographyReady;
    
        cv::Mat homography;
        ofxCv::Calibration calibration;
    
        int counter = 0;
        string inputFileType = "jpg";
        string outputFileType = "png";
        bool finished = false;

};
