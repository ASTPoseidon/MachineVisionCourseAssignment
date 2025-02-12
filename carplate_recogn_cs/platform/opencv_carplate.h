#ifndef _OPENCV_CARPLATE_H_
#define _OPENCV_CARPLATE_H_

#include <opencv2/opencv.hpp>
#include "opencv2/core.hpp"
#include "opencv2/objdetect.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include "public.h"

using namespace std;
using namespace cv;

#define MODEL_XML_MLP      WORK_BASE_DIR"../platform/resource/MLPModel.xml"
#define MODEL_XML_MLP_CH   WORK_BASE_DIR"../platform/resource/MLPModel_ch.xml"
#define MODEL_XML_ANN      WORK_BASE_DIR"../platform/resource/ann.xml"
#define MODEL_XML_ANN_CH   WORK_BASE_DIR"../platform/resource/ann_ch.xml"

class car_license_recogn
{
public:
	car_license_recogn(void);
	//~car_license_recogn(void);
	int car_recogn_init(void);
	int car_plate_detect(Mat& image);
	int car_plate_recogn(void);

public:
	Mat plate_mat;
    unsigned char *frame_buf;
    int frame_size;
};


int start_car_recogn_task(void);


#endif	// _OPENCV_CARPLATE_H_
