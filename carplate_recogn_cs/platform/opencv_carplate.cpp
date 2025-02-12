#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <pthread.h>
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include <opencv2/ml.hpp>
#include "opencv2/ml/ml.hpp"
#include "opencv_carplate.h"
#include "mainwindow.h"
#include <opencv2/imgproc.hpp>
#include "socket_client.h"
#include "public.h"


using namespace std;
using namespace cv;
using namespace cv::ml;

extern MainWindow *mainwindow;

char g_carplate_str[16] = {0};
int g_carplate_update = 0;

class car_license_recogn    car_recogn_unit;

bool verifySizes_closeImg(const RotatedRect & candidate)
{
	float error = 0.4;
	const float aspect = 44/14; //长宽比
	int min = 20*aspect*20; //最小区域
	int max = 180*aspect*180;  //最大区域
	float rmin = aspect - aspect*error; //考虑误差后的最小长宽比
	float rmax = aspect + aspect*error; //考虑误差后的最大长宽比

	int area = candidate.size.height * candidate.size.width;
	float r = (float)candidate.size.width/(float)candidate.size.height;
	if(r <1)
		r = 1/r;

	if( (area < min || area > max) || (r< rmin || r > rmax)  )
		return false;
	else
		return true;
}

bool isFileExists_ifstream(string& name) {
    ifstream f(name.c_str());
    return f.good();
}

void RgbConvToGray(const Mat& inputImage,Mat & outpuImage)  //g = 0.3R+0.59G+0.11B
{
	outpuImage = Mat(inputImage.rows ,inputImage.cols ,CV_8UC1);  

	for (int i = 0 ;i<inputImage.rows ;++ i)
	{
		uchar *ptrGray = outpuImage.ptr<uchar>(i); 
		const Vec3b * ptrRgb = inputImage.ptr<Vec3b>(i);
		for (int j = 0 ;j<inputImage.cols ;++ j)
		{
			ptrGray[j] = 0.3*ptrRgb[j][2]+0.59*ptrRgb[j][1]+0.11*ptrRgb[j][0];	
		}
	}
}

void normalPosArea(Mat &intputImg, vector<RotatedRect> &rects_optimal, vector <Mat>& output_area )
{
	float r,angle;
	
	for (int i = 0 ;i< rects_optimal.size() ; ++i)
	{
		//旋转区域
		angle = rects_optimal[i].angle;
		r = (float)rects_optimal[i].size.width / (float) (float)rects_optimal[i].size.height;
		if(r<1)
			angle = 90 + angle;//旋转图像使其得到长大于高度图像。

		Mat rotmat = getRotationMatrix2D(rects_optimal[i].center , angle,1);//获得变形矩阵对象

		Mat img_rotated;
		warpAffine(intputImg ,img_rotated, rotmat, intputImg.size(),INTER_CUBIC);
		if(img_rotated.empty())
		{
			printf("%s: img_rotated is empty\n", __FUNCTION__);
			return ;
		}

		//imshow("img_rotated",img_rotated);
		//waitKey();
		//裁剪图像
		Size rect_size = rects_optimal[i].size;
		if(r<1)
			swap(rect_size.width, rect_size.height); //交换高和宽

		Mat rotated_rgb;
		Mat  img_crop;
		if(img_rotated.channels() == 4)
		{
            cvtColor(img_rotated, rotated_rgb, COLOR_RGBA2RGB);
			getRectSubPix(rotated_rgb, rect_size, rects_optimal[i].center , img_crop );
		}
		else
		{
			getRectSubPix(img_rotated, rect_size, rects_optimal[i].center , img_crop );
		}

		//用光照直方图调整所有裁剪得到的图像，使具有相同宽度和高度，适用于训练和分类
		Mat resultResized;
		resultResized.create(33,144,CV_8UC3);
		resize(img_crop , resultResized,resultResized.size() , 0,0,INTER_CUBIC);

		Mat grayResult;
		RgbConvToGray(resultResized ,grayResult);
		//blur(grayResult ,grayResult,Size(3,3));
		equalizeHist(grayResult,grayResult);

		output_area.push_back(grayResult);
	}
}

// 找出波峰
void find_waves(float threshold, const vector<float>& histogram, vector<vector<int>>& wave_peaks)
{
	int up_point = -1;
	bool is_peak = false;
	if(histogram[0] > threshold)
	{
		up_point = 0;
		is_peak = true;
	}

    int i = 0;
	for(i=0;i<histogram.size();i++)
	{
		if(is_peak && (histogram[i] < threshold))
		{
			if((i-up_point) > 2)
			{
				is_peak = false;
				vector<int> tmp;
				tmp.push_back(up_point);
				tmp.push_back(i);
				wave_peaks.push_back(tmp);
			}
		}
		else if(!is_peak && (histogram[i] >= threshold))
		{
			is_peak = true;
			up_point = i;
		}

	}
	if(is_peak && up_point != -1 &&((i - up_point) > 4))
	{
		vector<int> tmp;
		tmp.push_back(up_point);
		tmp.push_back(i);
		wave_peaks.push_back(tmp);
	}
}

//根据找出的波峰，分隔图片，从而得到逐个字符图片
void seperate_card(Mat &src, vector<Mat> &dst, const vector<vector<int>> &waves)
{
	Mat tmp;
	for(int i = 0;i<waves.size();i++)
	{
		tmp = src.colRange(waves[i][0], waves[i][1]).clone();
		dst.push_back(tmp);
	}
}


void bincount(Mat& a, Mat& b, Mat& result, int n)
{
	a.reshape(0,1);
	b.reshape(0,1);
  
    float max_v = *max_element(a.begin<float>(),a.end<float>());


    int maxValue = max_v;
    maxValue = (maxValue + 1)> n ? (maxValue + 1) : n;

   
    Mat res = Mat(1, maxValue, CV_32F, cv::Scalar::all(0));


    float *pa = a.ptr<float>(0);
    float *pb = b.ptr<float>(0);
    float *pr = res.ptr<float>(0);

	for(int i=0;i<a.total();i++)
	{
	// printf("%d\n",(int)pa[i]);
	pr[(int)pa[i]] += (float)pb[i];
	}

    res.copyTo(result);

}

// 获取特征
void preprocess_hog(vector<Mat>digits, vector<Mat> &samples)
{
	for(int i = 0;i < digits.size();i++)
	{
		Mat gx, gy;
		Sobel(digits[i], gx, CV_32F, 1, 0);
		Sobel(digits[i], gy, CV_32F, 0, 1);

		Mat mag, ang;
		cartToPolar(gx, gy, mag, ang);


		int bin_n = 16;
		Mat bin = ang.mul(bin_n);
		bin = bin / (2*CV_PI);

		Mat bin1, bin2, bin3, bin4;
		Mat mag1, mag2, mag3, mag4;
		Mat res, tmp;

 		bin(Rect(0, 0, 10, 10)).copyTo(bin1);
		mag(Rect(0, 0, 10, 10)).copyTo(mag1);

		bin(Rect(10, 0, bin.cols-10, 10)).copyTo(bin2);

		mag(Rect(10, 0, mag.cols-10, 10)).copyTo(mag2);

		bin(Rect(0, 10, 10, bin.rows-10)).copyTo(bin3);
        bin3.reshape(0,1);
		mag(Rect(0, 10, 10, mag.rows-10)).copyTo(mag3);

		bin(Rect(10, 10, bin.cols-10, bin.rows-10)).copyTo(bin4);
 
		mag(Rect(10, 10, mag.cols-10, mag.rows-10)).copyTo(mag4);

		bincount(bin1, mag1, res, 16);

		bincount(bin2, mag2, tmp, 16);


		vconcat(res, tmp, res);
		bincount(bin3, mag3, tmp, 16);
		vconcat(res, tmp, res);
		bincount(bin4, mag4, tmp, 16);
		vconcat(res, tmp, res);

		float eps = 1e-7;
		res /= sum(res)[0] + eps;

		sqrt(res, res);
		res /= norm(res) + eps;

		samples.push_back(res);
	}
}


Mat projectHistogram(const Mat& img ,int t)  //ˮƽ��ֱֱ��ͼ,0Ϊ����ͳ��
{                                            //1Ϊ����ͳ��
	int sz = (t)? img.rows: img.cols;
	Mat mhist = Mat::zeros(1, sz ,CV_32F);

	for(int j = 0 ;j < sz; j++ )
	{
		Mat data = (t)?img.row(j):img.col(j);
		mhist.at<float>(j) = countNonZero(data);
	}

	double min,max;
	minMaxLoc(mhist , &min ,&max);

	if(max > 0)
		mhist.convertTo(mhist ,-1,1.0f/max , 0);

	return mhist;
}

void features(const Mat & in , Mat & out ,int sizeData)
{
	Mat vhist = projectHistogram(in , 1); //ˮƽֱ��ͼ
	Mat hhist = projectHistogram(in , 0);  //��ֱֱ��ͼ

	Mat lowData;
	resize(in , lowData ,Size(sizeData ,sizeData ));
	int numCols = vhist.cols + hhist.cols + lowData.cols * lowData.cols;
	out = Mat::zeros(1, numCols , CV_32F);

	int j = 0;
	for (int i =0 ;i<vhist.cols ; ++i)
	{
		out.at<float>(j) = vhist.at<float>(i);
		j++;
	}
	for (int i=0 ; i < hhist.cols ;++i)
	{
		out.at<float>(j) = hhist.at<float>(i);
	}
	for(int x =0 ;x<lowData.rows ;++x)
	{
		for (int y =0 ;y < lowData.cols ;++ y)
		{
			out.at<float>(j) = (float)lowData.at<unsigned char>(x,y);
			j++;
		}
	}

}


void char_sort(vector <RotatedRect > & in_char ) //���ַ������������
{
	vector <RotatedRect >  out_char;
	const int length = 7;           //7���ַ�
	int index[length] = {0,1,2,3,4,5,6};
	float centerX[length];

	for (int i=0;i < length ; ++ i)
	{
		centerX[i] = in_char[i].center.x;
	}

	for (int j=0;j <length;j++) {
		for (int i=length-2;i >= j;i--)
			if (centerX[i] > centerX[i+1])
			{
				float t=centerX[i];
				centerX[i]=centerX[i+1];
				centerX[i+1]=t;

				int tt = index[i];
				index[i] = index[i+1];
				index[i+1] = tt;
			}
	}

	for(int i=0;i<length ;i++)
		out_char.push_back(in_char[(index[i])]);

	in_char.clear();     //���in_char
	in_char = out_char; //������õ��ַ������������¸�ֵ��in_char
}

bool char_verifySizes(const RotatedRect & candidate)
{
	float aspect = 45.0f/90.0f;
	float width,height;
	if (candidate.size.width >=candidate.size.height)
	{
		width = (float) candidate.size.height;
		height = (float) candidate.size.width;
	}

	else 
	{
		width = (float) candidate.size.width;
		height  = (float)candidate.size.height;
	}
	//����ȷ�����˸߱ȿ�Ҫ��

	float charAspect = (float) width/ (float)height;//���߱�

	float error = 0.5;
	float minHeight = 15;  //��С�߶�11
	float maxHeight = 33;  //���߶�33

	float minAspect = 0.05;  //���ǵ�����1����С������Ϊ0.15
	float maxAspect = 1.0;

	if( charAspect > minAspect && charAspect <= 1.0
		&&  height>= minHeight && height< maxHeight) //��0����maxAspect�����ȡ��߶�����������
		return true;
	else
		return false;
}
	

bool clearLiuDing(Mat& img)
{
	vector<float> fJump;
	int whiteCount = 0;
	const int x = 7;
	const int x1 =2;
	const int y = 2;
	Mat jump = Mat::zeros(1, img.rows, CV_32F);
	Mat jump_col = Mat::zeros(1,img.cols,CV_32F);
	for (int i = 0; i < img.rows; i++)
	{
		int jumpCount = 0;
		for (int j = 0; j < img.cols - 1; j++)
		{
			if (img.at<char>(i, j) != img.at<char>(i, j + 1)) 
			{
				jumpCount++;
			}   

			if (img.at<uchar>(i, j) == 255) 
			{
				whiteCount++;
			}
		}

		jump.at<float>(i) = (float)jumpCount;
	}
	int iCount = 0;

	for (int i = 0; i < img.rows; i++) 
	{
		fJump.push_back(jump.at<float>(i));
		if (jump.at<float>(i) >= 16 && jump.at<float>(i) <= 45) 
		{
			iCount++;//�����ַ�����һ����������
		}
	}

	////�����Ĳ��ǳ���
	if (iCount * 1.0 / img.rows <= 0.40)
	{
		return false; //�������������������ҲҪ��һ������ֵ��
	}
	//�����㳵�Ƶ�����
	if (whiteCount * 1.0 / (img.rows * img.cols) < 0.15 ||
		whiteCount * 1.0 / (img.rows * img.cols) > 0.50)
	{
		return false;
	}

	for (int i = 0; i < img.rows; i++) 
	{
		if (jump.at<float>(i) <= x||i<2||i>(img.rows-2)) 
		{
			for (int j = 0; j < img.cols; j++)
			{
				img.at<char>(i, j) = 0;
			}
		}

	}

	for (int z = 0;z <img.cols; z++)

	{
		//int x = img.cols
		if ( z<2 ||z>(img.cols-2))
		{
			for (int w=0; w < img.rows;w++)
			{
				img.at<char>(w,z)=0;
			}
			
		}
	}
	//medianBlur(img,img,3);
	return true;
}

int char_segment(const Mat & inputImg,vector <Mat>& dst_mat)//�õ�20*20�ı�׼�ַ��ָ�ͼ��
{
	Mat img_threshold;
	//blur(inputImg,inputImg,Size(7,7));
	threshold(inputImg ,img_threshold , 180,255 ,THRESH_BINARY );
	imwrite("m.jpg", inputImg);
	//Mat element = getStructuringElement(MORPH_RECT ,Size(3 ,3));  //����̬ѧ�ĽṹԪ��
	//morphologyEx(img_threshold ,img_threshold,CV_MOP_CLOSE,element);  //��̬ѧ����

	//imshow ("img_thresho00ld",img_threshold);
	//waitKey();
	Mat img_contours;
	img_threshold.copyTo(img_contours);

	//imshow ("img_threshold",img_threshold);
	if (!clearLiuDing(img_contours))
    {
	   std::cout << "不是车牌" << endl;
	   return -1;
	   //waitKey();
	 }
	else
    {

	//imshow("img_cda",img_contours);
	//waitKey();
	Mat result2;
	inputImg.copyTo(result2);

	 vector < vector <Point> > contours;
	 findContours(img_contours ,contours,RETR_EXTERNAL,CHAIN_APPROX_NONE);

	 vector< vector <Point> > ::iterator itc = contours.begin();
	 vector<RotatedRect> char_rects;
	  //vector<Mat> char_rects;

	//Mat result2;
   // img_contours.copyTo(result2);

	 drawContours(result2,contours,-1, Scalar(0,255,255), 1); 
	 //imshow("result22",result2);
	 //waitKey();

	 while( itc != contours.end())
   {
		RotatedRect minArea = minAreaRect(Mat( *itc )); //����ÿ����������С�н��������
		Point2f vertices[4];
		minArea.points(vertices);

		if(!char_verifySizes(minArea))  //�жϾ��������Ƿ����Ҫ��
		{
			itc = contours.erase(itc);}
		//contours.

		else     
		{
			++itc; 
			char_rects.push_back(minArea);  	
		}  

	}

	/* while (itc!=contours.end())
	 {  

		   
		 Rect mr= boundingRect(Mat(*itc));  
		 Mat auxRoi(img_threshold, mr);

		 if(char_verifySizes(auxRoi))
		 {  
			 
		 }  

*/
	 //imshow("char1",char_rects[1]);
	 //imshow("char2",char_rects[2]);
	 //imshow("char3",char_rects[3]);
	//imshow("char4",char_rects[4]);
	 ////imshow("char5",char_rects[5]);
	/// imshow("char6",char_rects[6]);
	// imshow("char7",char_rects[0]);



	// waitKey();
    char_sort(char_rects); //���ַ�����

	vector <Mat> char_mat;

	for (int i = 0; i<char_rects.size() ;i++ )
	{
		Rect roi = char_rects[i].boundingRect();
    	if(0 >roi.x || 0 >roi.width || 0 >roi.y || 0 >roi.height || roi.x + roi.width >img_threshold.cols || roi.y + roi.height >img_threshold.rows)
		{
			printf("Error: x=%d, y=%d, w=%d, h=%d\n", roi.x, roi.y, roi.width, roi.height);
			return -1;
		}
		//char_mat.push_back(Mat(inputImg,char_rects[i].boundingRect())) ;
		char_mat.push_back(Mat(img_threshold,char_rects[i].boundingRect())) ;
	}

	//imshow("char_mat1",char_mat[0]);
	//imshow("char_mat2",char_mat[1]);
	//imshow("char_mat3",char_mat[2]);
	//imshow("char_mat4",char_mat[3]);
	//imshow("char_mat5",char_mat[4]);
	//imshow("char_mat6",char_mat[5]);
	//imshow("char_mat7",char_mat[6]);
	//waitKey();
//
	Mat train_mat(2,3,CV_32FC1);
	int length ;
	dst_mat.resize(7);
	Point2f srcTri[3];  
	Point2f dstTri[3];

	for (int i = 0; i==0;i++)
	{
		srcTri[0] = Point2f( 0,0 );  
		srcTri[1] = Point2f( char_mat[i].cols - 1, 0 );  
		srcTri[2] = Point2f( 0, char_mat[i].rows - 1 );
		length = char_mat[i].rows > char_mat[i].cols?char_mat[i].rows:char_mat[i].cols;
		dstTri[0] = Point2f( 0.0, 0.0 );  
		dstTri[1] = Point2f( length, 0.0 );  
		dstTri[2] = Point2f( 0.0, length ); 
		train_mat = getAffineTransform( srcTri, dstTri );
		dst_mat[i]=Mat::zeros(length,length,char_mat[i].type());		
		warpAffine(char_mat[i],dst_mat[i],train_mat,dst_mat[i].size(),INTER_LINEAR,BORDER_CONSTANT,Scalar(0));
		//resize(dst_mat[i],dst_mat[i],Size(20,20),0,0,CV_INTER_CUBIC);  //�ߴ����Ϊ20*20
		resize(dst_mat[i],dst_mat[i],Size(20,20));

	}

	for (int i = 1; i< char_mat.size();++i)
	{
		srcTri[0] = Point2f( 0,0 );  
		srcTri[1] = Point2f( char_mat[i].cols - 1, 0 );  
		srcTri[2] = Point2f( 0, char_mat[i].rows - 1 );
		length = char_mat[i].rows > char_mat[i].cols?char_mat[i].rows:char_mat[i].cols;
		dstTri[0] = Point2f( 0.0, 0.0 );  
		dstTri[1] = Point2f( length, 0.0 );  
		dstTri[2] = Point2f( 0.0, length ); 
		train_mat = getAffineTransform( srcTri, dstTri );
		dst_mat[i]=Mat::zeros(length,length,char_mat[i].type());		
		warpAffine(char_mat[i],dst_mat[i],train_mat,dst_mat[i].size(),INTER_LINEAR,BORDER_CONSTANT,Scalar(0));
		//resize(dst_mat[i],dst_mat[i],Size(20,20),0,0,CV_INTER_CUBIC);  //�ߴ����Ϊ20*20
		resize(dst_mat[i],dst_mat[i],Size(20,20));

	}


	/*for ( int i =0;i < char_mat.size();i++ )
	{
		int h=char_mat[i].rows;  
		int w=char_mat[i].cols;  
		int charSize=20;    //ͳһÿ���ַ��Ĵ�С  
		Mat transformMat=Mat::eye(2,3,CV_32F);  
		int m=max(w,h);  
		transformMat.at<float>(0,2)=m/2 - w/2;  
		transformMat.at<float>(1,2)=m/2 - h/2;  

		Mat warpImage(m,m, char_mat[i].type());  
		warpAffine(char_mat[i], warpImage, transformMat, warpImage.size(), INTER_LINEAR, BORDER_CONSTANT, Scalar(0) );  

		vector <Mat> dst_mat;  
		resize(warpImage, dst_mat[i], Size(charSize, charSize) );   
	}
  */
 	return 0;
 }

}

// 训练数字及字母识别模型
void ann_train(Ptr<ANN_MLP> &ann ,int numCharacters, int nlayers)
{
	Mat trainData ,classes;
	FileStorage fs;

	// 获取训练数据
    fs.open(MODEL_XML_ANN , FileStorage::READ);

	fs["TrainingData"] >>trainData;
	fs["classes"] >>classes;

	// 设定神经网络参数
	Mat layerSizes(1,4,CV_32SC1);
	layerSizes.at<int>( 0 ) = trainData.cols;
	layerSizes.at<int>( 1 ) = 200;
	layerSizes.at<int>( 2 ) = 100;
	layerSizes.at<int>( 3 ) = numCharacters;

	ann->setLayerSizes(layerSizes);
	ann->setActivationFunction(ANN_MLP::SIGMOID_SYM);

	ann->setTrainMethod(ANN_MLP::BACKPROP, 0.001, 0.1);
	ann->setTermCriteria(TermCriteria(TermCriteria::MAX_ITER | TermCriteria::EPS, 100, 0.001));
	Mat trainClasses;
	trainClasses.create(trainData.rows , numCharacters ,CV_32FC1);
	for (int i =0;i< trainData.rows; i++)
	{
		for (int k=0 ; k< trainClasses.cols ; k++ )
		{
			if ( k == (int)classes.at<uchar> (i))
			{
				trainClasses.at<float>(i,k)  = 1 ;
			}
			else
				trainClasses.at<float>(i,k)  = 0;			
		}		
	}

	// 训练模型
	ann->train( trainData ,ROW_SAMPLE , trainClasses);
	// 保存训练好的模型
    ann->save(MODEL_XML_MLP);
}

// 训练中文识别模型
void ann_train_ch(Ptr<ANN_MLP> &ann ,int numCharacters, int nlayers)
{
	Mat trainData ,classes;
	FileStorage fs;

	// 获取训练数据
    fs.open(MODEL_XML_ANN_CH , FileStorage::READ);

	fs["TrainingData"] >>trainData;
	fs["classes"] >>classes;

	// 设定神经网络参数
	Mat layerSizes(1,4,CV_32SC1);
	layerSizes.at<int>( 0 ) = trainData.cols;
	layerSizes.at<int>( 1 ) = 200;
	layerSizes.at<int>( 2 ) = 100;
	layerSizes.at<int>( 3 ) = numCharacters;

	ann->setLayerSizes(layerSizes);
	ann->setActivationFunction(ANN_MLP::SIGMOID_SYM);
	ann->setTrainMethod(ANN_MLP::BACKPROP, 0.001, 0.1);
	ann->setTermCriteria(TermCriteria(TermCriteria::MAX_ITER | TermCriteria::EPS,200, 0.001));
	Mat trainClasses;
	trainClasses.create(trainData.rows , numCharacters ,CV_32FC1);
	for (int i =0;i< trainData.rows; i++)
	{
		for (int k=0 ; k< trainClasses.cols ; k++ )
		{
			if ( k == (int)classes.at<uchar> (i))
			{
				trainClasses.at<float>(i,k)  = 1 ;
			}
			else
				trainClasses.at<float>(i,k)  = 0;			
		}		
	}

	// 训练模型
	ann->train( trainData ,ROW_SAMPLE , trainClasses);
	// 保存训练好的模型
    ann->save(MODEL_XML_MLP_CH);
}


car_license_recogn::car_license_recogn(void)
{
	printf("%s: enter ++\n", __FUNCTION__);
}

int car_license_recogn::car_recogn_init(void)
{
	printf("%s: enter ++\n", __FUNCTION__);

    this->frame_size = FRAME_BUF_SIZE;
    this->frame_buf = (unsigned char *)malloc(this->frame_size);
    if(this->frame_buf == NULL)
        return -1;

	return 0;
}

int car_license_recogn::car_plate_detect(Mat& image)
{
	Mat hsvImg;
    vector <Mat> hsvSplit;

	if(image.empty())
	{
		printf("%s: image is empty!\n", __FUNCTION__);
		return -1;
	}

	cvtColor(image, hsvImg, COLOR_BGR2HSV);
    split(hsvImg,hsvSplit);
	equalizeHist(hsvSplit[2],hsvSplit[2]);
	merge(hsvSplit,hsvImg);

	const int min_blue =100;
	const int max_blue =140;
	int avg_h = (min_blue+max_blue)/2;
	int channels = hsvImg.channels();
	int nRows = hsvImg.rows;
	//图像数据列需要考虑通道数的影响
	int nCols = hsvImg.cols * channels;

	if (hsvImg.isContinuous())//连续存储的数据，按一行处理
	{
		nCols *= nRows;
		nRows = 1;
	}

	int i, j;
	uchar* p;

	const float  minref_sv = 64; //参考的S V的值
	const float max_sv = 255; // S V 的最大值

	for (i = 0; i < nRows; ++i)
	{
		p = hsvImg.ptr<uchar>(i);
		for (j = 0; j < nCols; j += 3)
		{
			int H = int(p[j]); //0-180
			int S = int(p[j + 1]);  //0-255
			int V = int(p[j + 2]);  //0-255
			bool colorMatched = false;

			if (H > min_blue && H < max_blue)
			{
				int Hdiff = 0;
				float Hdiff_p = float(Hdiff) / 40;
				float min_sv = 0;

				if (H > avg_h)
				{
					Hdiff = H - avg_h;
				}	
				else
				{
					Hdiff = avg_h - H;
				}
				min_sv = minref_sv - minref_sv / 2 * (1 - Hdiff_p);
				if ((S > 70&& S < 255) &&(V > 70 && V < 255))
				colorMatched = true;
			}

			if (colorMatched == true) 
			{
				p[j] = 0; p[j + 1] = 0; p[j + 2] = 255;
			}
			else 
			{
				p[j] = 0; p[j + 1] = 0; p[j + 2] = 0;
			}
		}
	}

	Mat src_grey;
	Mat img_threshold;
	vector<Mat> hsvSplit_done;
	split(hsvImg, hsvSplit_done);
	src_grey = hsvSplit_done[2];
	vector <RotatedRect>  rects;
	Mat element = getStructuringElement(MORPH_RECT ,Size(17 ,3));  //闭形态学的结构元素
	morphologyEx(src_grey ,img_threshold,MORPH_CLOSE,element); 
	morphologyEx(img_threshold,img_threshold,MORPH_OPEN,element);//形态学处理

	vector< vector <Point> > contours;//寻找车牌区域的轮廓
	findContours(img_threshold ,contours,RETR_EXTERNAL, CHAIN_APPROX_NONE);//只检测外轮廓
	//对候选的轮廓进行进一步筛选
	vector< vector <Point> > ::iterator itc = contours.begin();
	while( itc != contours.end())
	{
		RotatedRect mr = minAreaRect(Mat( *itc )); //返回每个轮廓的最小有界矩形区域
		if(!verifySizes_closeImg(mr))  //判断矩形轮廓是否符合要求
		{
			itc = contours.erase(itc);
		}
		else     
		{

			rects.push_back(mr);
			++itc;
		}      
	}

	if(rects.empty())
	{
        //printf("%s: rects is empty!\n", __FUNCTION__);
		return -1;
	}

	vector <Mat> output_area;
	normalPosArea(image ,rects, output_area);  //获得144*33的候选车牌区域output_area

	if(output_area.empty())
	{
		printf("%s: output_area is empty!\n", __FUNCTION__);
		return -1;
	}

	plate_mat = output_area[0];

	return 0;
}

int num = 0;
int car_license_recogn::car_plate_recogn(void)
{
	vector<Mat> plates_svm;
	int ret;

	plates_svm.push_back(plate_mat);

	vector <Mat> char_seg;
	ret = char_segment(plates_svm[0],char_seg); 
	if(ret != 0)
	{
        printf("char_segment failed\n");
		return -1;
	}

	// imwrite("/home/zengzr/Desktop/chinese/chuan/chuan"+to_string(num)+".jpg",char_seg[0]);
	// imshow("char1",char_seg[10]);
	// num++;
	// imwrite("char10.jpg",char_seg[0]);
	// imwrite("char11.jpg",char_seg[1]);
	// imwrite("char12.jpg",char_seg[2]);
	// imwrite("char13.jpg",char_seg[3]);
	// imwrite("char14.jpg",char_seg[4]);
	// imwrite("char15.jpg",char_seg[5]);
	// imwrite("char16.jpg",char_seg[6]);


	// 判断当前路径是否已经有训练好的模型，如有则加载，没有则训练
	Ptr< ANN_MLP > ann_classify = ANN_MLP::create();
	Ptr< ANN_MLP > ann_classify_ch = ANN_MLP::create();

    string ch_model_name = MODEL_XML_MLP_CH;
    string model_name = MODEL_XML_MLP;
	if(isFileExists_ifstream(model_name))
	{
		printf("Loading model ...\n");
		ann_classify = cv::Algorithm::load<cv::ml::ANN_MLP>(model_name);
	}
	else
	{
		printf("Training model...\n");
		ann_train(ann_classify ,34, 100);
	}

	if(isFileExists_ifstream(ch_model_name))
	{
		printf("Loading ch model ...\n");
		ann_classify_ch = cv::Algorithm::load<cv::ml::ANN_MLP>(ch_model_name);
	}
	else
	{
		printf("Training ch model ...\n");
		ann_train_ch(ann_classify_ch ,31, 100);
	}

    string result = "";
    char letter[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'K', 'L', 'M', 'N', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
    string chinese[] = {"zh_cuan", "川",
	"zh_e", "鄂",
	"zh_gan", "赣",
	"zh_gan1", "甘",
	"zh_gui", "贵",
	"zh_gui1", "桂",
	"zh_hei", "黑",
	"zh_hu", "沪",
	"zh_ji", "冀",
	"zh_jin", "津",
	"zh_jing", "京",
	"zh_jl", "吉",
	"zh_liao", "辽",
	"zh_lu", "鲁",
	"zh_meng", "蒙",
	"zh_min", "闽",
	"zh_ning", "宁",
	"zh_qing", "靑",
	"zh_qiong", "琼",
	"zh_shan", "陕",
	"zh_su", "苏",
	"zh_sx", "晋",
	"zh_wan", "皖",
	"zh_xiang", "湘",
	"zh_xin", "新",
	"zh_yu", "豫",
	"zh_yu1", "渝",
	"zh_yue", "粤",
	"zh_yun", "云",
	"zh_zang", "藏",
	"zh_zhe", "浙"};

	for (int i=0;i< char_seg.size(); ++i)
	{
		if (i==0)
	  	{	// 识别汉字（第一个字符）
			Mat sample;
			copyMakeBorder(char_seg[i], sample, 1, 1, 5, 5, BORDER_CONSTANT, Scalar(0, 0, 0));
			resize(sample, sample, Size(20, 20), INTER_AREA);
			sample = sample.reshape(0,1);
			sample.convertTo(sample, CV_32F, 1, 0);
			Mat output;
			ann_classify_ch->predict(sample, output);
			Point maxLoc;
			double maxVal;
			minMaxLoc(output , 0 ,&maxVal , 0 ,&maxLoc);

			result += chinese[2 * maxLoc.x + 1];
		}
		else
		{	// 识别字母和数字
			Mat sample;
			copyMakeBorder(char_seg[i], sample, 1, 1, 5, 5, BORDER_CONSTANT, Scalar(0, 0, 0));
			resize(sample, sample, Size(20, 20), INTER_AREA);
			sample = sample.reshape(0,1);
			sample.convertTo(sample, CV_32F, 1, 0);

			Mat output;
			ann_classify->predict(sample, output);

			Point maxLoc;
			double maxVal;
			minMaxLoc(output , 0 ,&maxVal , 0 ,&maxLoc);

			if(maxLoc.x < 10)
			{
				result += '0' + maxLoc.x;
			}
			else
			{
				result += letter[(maxLoc.x - 10)];
			}
		}
	}

   if(plates_svm.size() != 0)  
	{
		//imshow("Test", plates_svm[0]);     //��ȷԤ��Ļ�����ֻ��һ�����plates_svm[0]
		
	}
	else
	{
		std::cout<<"定位失败";
		return -1;
		
	}

    memset(g_carplate_str, 0, sizeof(g_carplate_str));
    memcpy(g_carplate_str, result.c_str(), 9);
    g_carplate_update = 1;
    proto_0x10_carplate(g_carplate_str);

    printf("carplate: %s\n", g_carplate_str);
    //mainwindow->mainwin_set_cartext(car_num);

	cout<<endl;

	return 0;
}

void *opencv_car_recogn_thread(void *arg)
{
	class car_license_recogn *car_recogn = &car_recogn_unit;
	QImage tmpQImage;
	Mat captureMat;
	int frame_len = 0;
	int ret = 0;

    printf("### %s enter ++\n", __FUNCTION__);

	ret = car_recogn->car_recogn_init();
	if(ret != 0)
	{
		printf("%s: car_recogn_init failed !\n", __FUNCTION__);
		return NULL;
	}

    while(1)
    {

        ret = capture_get_newframe(car_recogn->frame_buf, car_recogn->frame_size, &frame_len);
        if(ret <= 0)
		{
			usleep(100 *1000);
			continue;
		}
		//printf("frame_buf: %p, frame_len: %d\n", frame_buf, frame_len);

		/* 将v4l2获取到的MJPEG图像转换为QImage格式 */
        tmpQImage = jpeg_to_QImage(car_recogn->frame_buf, frame_len);
		if(tmpQImage.isNull())
		{
			printf("%s ERROR: qImage is null !\n", __FUNCTION__);
			continue;
		}

		//tmpQImage.load("1.jpg"); 

		/* convert qimage to cvMat */
		captureMat = QImage_to_cvMat(tmpQImage).clone();
		if(captureMat.empty())
		{
			printf("%s ERROR: mat is empty\n", __FUNCTION__);
			continue;
		}

        //imwrite("error.jpg", captureMat);
        //captureMat = imread("error.jpg");
		//imshow("img_input2", captureMat);
		//imwrite("error.jpg", captureMat);
		//waitKey();
#if 1
		ret = car_recogn->car_plate_detect(captureMat);
		if(ret != 0)
		{
			usleep(100 *1000);
			continue;
		}

		tmpQImage = cvMat_to_QImage(car_recogn->plate_mat);
		if(tmpQImage.isNull())
		{
			printf("%s ERROR: plate QImage is null !\n", __FUNCTION__);
			continue;
		}

        //mainwin_set_plateImg(tmpQImage);

		ret = car_recogn->car_plate_recogn();
#endif
        sleep(1);
		//waitKey();
    }

	return NULL;
}

int start_car_recogn_task(void)
{
	pthread_t tid;
    int ret;

	ret = pthread_create(&tid, NULL, opencv_car_recogn_thread, NULL);
	if(ret != 0)
	{
		return -1;
	}

    return 0;
}

//using namespace cv;


