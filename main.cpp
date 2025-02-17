#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/core/utility.hpp"

#include <stdio.h>

using namespace cv;

void disp2Depth(cv::Mat dispMap, cv::Mat &depthMap)
{
	int type = dispMap.type();

	float fx = 682.421509;
	float fy = 682.421509;
	float cx = 633.947449;
	float cy = 404.559906;
	float baseline = 150; //基线距离150mm

	if (type == CV_8U)
	{
		const float PI = 3.14159265358;
		int height = dispMap.rows;
		int width = dispMap.cols;

		uchar* dispData = (uchar*)dispMap.data;
		ushort* depthData = (ushort*)depthMap.data;
		for (int i = 0; i < height; i++)
		{
			for (int j = 0; j < width; j++)
			{
				int id = i * width + j;
				if (!dispData[id])  continue;  //防止0除
				depthData[id] = ushort((float)fx *baseline / ((float)dispData[id]));
			}
		}
	}
	else
	{
		cv::waitKey(0);
	}
}


static void print_help()
{
	printf("\nDemo stereo matching converting L and R images into disparity and point clouds\n");
	printf("\nUsage: stereo_match <left_image> <right_image> [--algorithm=bm|sgbm|hh|sgbm3way] [--blocksize=<block_size>]\n"
		"[--max-disparity=<max_disparity>] [--scale=scale_factor>] [-i=<intrinsic_filename>] [-e=<extrinsic_filename>]\n"
		"[--no-display] [-o=<disparity_image>] [-p=<point_cloud_file>]\n");
}

static void saveXYZ(const char* filename, const Mat& mat)
{
	const double max_z = 1.0e4;
	FILE* fp = fopen(filename, "wt");
	for (int y = 0; y < mat.rows; y++)
	{
		for (int x = 0; x < mat.cols; x++)
		{
			Vec3f point = mat.at<Vec3f>(y, x);
			if (fabs(point[2] - max_z) < FLT_EPSILON || fabs(point[2]) > max_z) continue;
			fprintf(fp, "%f %f %f\n", point[0], point[1], point[2]);
		}
	}
	fclose(fp);
}

int main(int argc, char** argv)
{
		std::string img1_filename = "L0.jpg";
		std::string img2_filename = "R0.jpg";
		std::string intrinsic_filename = "intrinsics.yml";
		std::string extrinsic_filename = "extrinsics.yml";
		std::string disparity_filename = "test.bmp";
		std::string point_cloud_filename = "dscv.pcd";

	enum { STEREO_BM = 0, STEREO_SGBM = 1, STEREO_HH = 2, STEREO_VAR = 3, STEREO_3WAY = 4 };
	int alg = STEREO_SGBM;
	int SADWindowSize, numberOfDisparities=0;

	bool no_display = 0;
	float scale = 1.0;

	Ptr<StereoBM> bm = StereoBM::create(16, 9);
	Ptr<StereoSGBM> sgbm = StereoSGBM::create(0, 16, 3);
	Mat K = (Mat_<double>(3, 3) << 682.421509, 0, 633.947449, 0, 682.421509, 404.559906, 0, 0, 1);//fx,cx,fy,cy
	
	if (alg < 0)
	{
		printf("Command-line parameter error: Unknown stereo algorithm\n");
		print_help();
		return -1;
	}
	if (scale < 0)
	{
		printf("Command-line parameter error: The scale factor (--scale=<...>) must be a positive floating-point number\n");
		return -3;
	}
	if (SADWindowSize < 1 || SADWindowSize % 2 != 1)
	{
		printf("Command-line parameter error: The block size (--blocksize=<...>) must be a positive odd number\n");
		return -4;
	}
	if (img1_filename.empty() || img2_filename.empty())
	{
		printf("Command-line parameter error: both left and right images must be specified\n");
		return -5;
	}
	if ((!intrinsic_filename.empty()) ^ (!extrinsic_filename.empty()))
	{
		printf("Command-line parameter error: either both intrinsic and extrinsic parameters must be specified, or none of them (when the stereo pair is already rectified)\n");
		return -6;
	}

	if (extrinsic_filename.empty() && !point_cloud_filename.empty())
	{
		printf("Command-line parameter error: extrinsic and intrinsic parameters must be specified to compute the point cloud\n");
		return -7;
	}

	int color_mode = alg == STEREO_BM ? 0 : -1;
	Mat img1 = imread(img1_filename, color_mode);
	Mat img2 = imread(img2_filename, color_mode);

	if (img1.empty())
	{
		printf("Command-line parameter error: could not load the first input image file\n");
		return -8;
	}
	if (img2.empty())
	{
		printf("Command-line parameter error: could not load the second input image file\n");
		return -9;
	}

	if (scale != 1.f)
	{
		Mat temp1, temp2;
		int method = scale < 1 ? INTER_AREA : INTER_CUBIC;
		resize(img1, temp1, Size(), scale, scale, method);
		img1 = temp1;
		resize(img2, temp2, Size(), scale, scale, method);
		img2 = temp2;
	}

	Size img_size = img1.size();
	numberOfDisparities = numberOfDisparities > 0 ? numberOfDisparities : ((img_size.width / 8) + 15) & -16;
	Rect roi1, roi2;
	Mat Q;

	if (!intrinsic_filename.empty())
	{
		// reading intrinsic parameters
		FileStorage fs(intrinsic_filename, FileStorage::READ);
		if (!fs.isOpened())
		{
			printf("Failed to open file %s\n", intrinsic_filename.c_str());
			return -1;
		}

		Mat M1, D1, M2, D2;
		fs["M1"] >> M1;
		fs["D1"] >> D1;
		fs["M2"] >> M2;
		fs["D2"] >> D2;

		M1 *= scale;
		M2 *= scale;

		fs.open(extrinsic_filename, FileStorage::READ);
		if (!fs.isOpened())
		{
			printf("Failed to open file %s\n", extrinsic_filename.c_str());
			return -1;
		}

		Mat R, T, R1, P1, R2, P2;
		fs["R"] >> R;
		fs["T"] >> T;

		stereoRectify(M1, D1, M2, D2, img_size, R, T, R1, R2, P1, P2, Q, CALIB_ZERO_DISPARITY, -1, img_size, &roi1, &roi2);

		Mat map11, map12, map21, map22;
		initUndistortRectifyMap(M1, D1, R1, P1, img_size, CV_16SC2, map11, map12);
		initUndistortRectifyMap(M2, D2, R2, P2, img_size, CV_16SC2, map21, map22);

		Mat img1r, img2r;
		remap(img1, img1r, map11, map12, INTER_LINEAR);
		remap(img2, img2r, map21, map22, INTER_LINEAR);

		img1 = img1r;
		img2 = img2r;
	}

	//numberOfDisparities = numberOfDisparities > 0 ? numberOfDisparities : ((img_size.width / 8) + 15) & -16;

	bm->setROI1(roi1);
	bm->setROI2(roi2);
	bm->setPreFilterCap(31);
	bm->setBlockSize(SADWindowSize > 0 ? SADWindowSize : 9);
	bm->setMinDisparity(0);
	bm->setNumDisparities(numberOfDisparities);
	bm->setTextureThreshold(10);
	bm->setUniquenessRatio(15);
	bm->setSpeckleWindowSize(100);
	bm->setSpeckleRange(32);
	bm->setDisp12MaxDiff(1);

	sgbm->setPreFilterCap(63);
	int sgbmWinSize = SADWindowSize > 0 ? SADWindowSize : 3;
	sgbm->setBlockSize(sgbmWinSize);

	int cn = img1.channels();

	sgbm->setP1(8 * cn*sgbmWinSize*sgbmWinSize);
	sgbm->setP2(32 * cn*sgbmWinSize*sgbmWinSize);
	sgbm->setMinDisparity(0);
	sgbm->setNumDisparities(numberOfDisparities);
	sgbm->setUniquenessRatio(10);
	sgbm->setSpeckleWindowSize(100);
	sgbm->setSpeckleRange(32);
	sgbm->setDisp12MaxDiff(1);
	if (alg == STEREO_HH)
		sgbm->setMode(StereoSGBM::MODE_HH);
	else if (alg == STEREO_SGBM)
		sgbm->setMode(StereoSGBM::MODE_SGBM);
	else if (alg == STEREO_3WAY)
		sgbm->setMode(StereoSGBM::MODE_SGBM_3WAY);

	Mat disp, disp8;
	Mat depthImg(720, 1280, CV_16UC1);
	//Mat img1p, img2p, dispp;
	//copyMakeBorder(img1, img1p, 0, 0, numberOfDisparities, 0, IPL_BORDER_REPLICATE);
	//copyMakeBorder(img2, img2p, 0, 0, numberOfDisparities, 0, IPL_BORDER_REPLICATE);

	if (alg == STEREO_BM)
		bm->compute(img1, img2, disp);
	else if (alg == STEREO_SGBM || alg == STEREO_HH || alg == STEREO_3WAY)
		sgbm->compute(img1, img2, disp);

	//disp = dispp.colRange(numberOfDisparities, img1p.cols);
	if (alg != STEREO_VAR)
		disp.convertTo(disp8, CV_8U, 255 / (numberOfDisparities*16.));
	else
		disp.convertTo(disp8, CV_8U);
	if (!no_display)
	{
		namedWindow("left", 1);
		imshow("left", img1);
		namedWindow("right", 1);
		imshow("right", img2);
		namedWindow("disparity", 0);
		imshow("disparity", disp8);
		
		disp2Depth(disp8, depthImg);
		imshow("depth",depthImg);
		printf("press any key to continue...");
		fflush(stdout);
		waitKey();
		printf("\n");
	}

	if (!disparity_filename.empty())
		imwrite(disparity_filename, disp8);

	if (!point_cloud_filename.empty())
	{
		printf("storing the point cloud...");
		fflush(stdout);
		Mat xyz;
		reprojectImageTo3D(disp, xyz, Q, true);
		saveXYZ(point_cloud_filename.c_str(), xyz);
		printf("\n");
	}

	return 0;
}






//#include "opencv2/calib3d/calib3d.hpp"
//#include "opencv2/imgproc.hpp"
//#include "opencv2/imgcodecs.hpp"
//#include "opencv2/highgui.hpp"
//#include "opencv2/core/utility.hpp"
//#include <stdio.h>
//
//using namespace cv;
//
//void insertDepth32f(cv::Mat& depth)
//{
//	const int width = depth.cols;
//	const int height = depth.rows;
//	float* data = (float*)depth.data;
//	cv::Mat integralMap = cv::Mat::zeros(height, width, CV_64F);
//	cv::Mat ptsMap = cv::Mat::zeros(height, width, CV_32S);
//	double* integral = (double*)integralMap.data;
//	int* ptsIntegral = (int*)ptsMap.data;
//	memset(integral, 0, sizeof(double) * width * height);
//	memset(ptsIntegral, 0, sizeof(int) * width * height);
//	for (int i = 0; i < height; ++i)
//	{
//		int id1 = i * width;
//		for (int j = 0; j < width; ++j)
//		{
//			int id2 = id1 + j;
//			if (data[id2] > 1e-3)
//			{
//				integral[id2] = data[id2];
//				ptsIntegral[id2] = 1;
//			}
//		}
//	}
//	// 积分区间
//	for (int i = 0; i < height; ++i)
//	{
//		int id1 = i * width;
//		for (int j = 1; j < width; ++j)
//		{
//			int id2 = id1 + j;
//			integral[id2] += integral[id2 - 1];
//			ptsIntegral[id2] += ptsIntegral[id2 - 1];
//		}
//	}
//	for (int i = 1; i < height; ++i)
//	{
//		int id1 = i * width;
//		for (int j = 0; j < width; ++j)
//		{
//			int id2 = id1 + j;
//			integral[id2] += integral[id2 - width];
//			ptsIntegral[id2] += ptsIntegral[id2 - width];
//		}
//	}
//	int wnd;
//	double dWnd = 2;
//	while (dWnd > 1)
//	{
//		wnd = int(dWnd);
//		dWnd /= 2;
//		for (int i = 0; i < height; ++i)
//		{
//			int id1 = i * width;
//			for (int j = 0; j < width; ++j)
//			{
//				int id2 = id1 + j;
//				int left = j - wnd - 1;
//				int right = j + wnd;
//				int top = i - wnd - 1;
//				int bot = i + wnd;
//				left = max(0, left);
//				right = min(right, width - 1);
//				top = max(0, top);
//				bot = min(bot, height - 1);
//				int dx = right - left;
//				int dy = (bot - top) * width;
//				int idLeftTop = top * width + left;
//				int idRightTop = idLeftTop + dx;
//				int idLeftBot = idLeftTop + dy;
//				int idRightBot = idLeftBot + dx;
//				int ptsCnt = ptsIntegral[idRightBot] + ptsIntegral[idLeftTop] - (ptsIntegral[idLeftBot] + ptsIntegral[idRightTop]);
//				double sumGray = integral[idRightBot] + integral[idLeftTop] - (integral[idLeftBot] + integral[idRightTop]);
//				if (ptsCnt <= 0)
//				{
//					continue;
//				}
//				data[id2] = float(sumGray / ptsCnt);
//			}
//		}
//		int s = wnd / 2 * 2 + 1;
//		if (s > 201)
//		{
//			s = 201;
//		}
//		cv::GaussianBlur(depth, depth, cv::Size(s, s), s, s);
//	}
//}
///*函数作用：视差图转深度图输入：　　dispMap ----视差图，8位单通道，CV_8UC1　　K       ----内参矩阵，float类型输出：　　depthMap ----深度图，16位无符号单通道，CV_16UC1*/
//void disp2Depth(cv::Mat dispMap, cv::Mat &depthMap)
//{
//	int type = dispMap.type();
//
//	float fx = 682.421509;
//	float fy = 682.421509;
//	float cx = 633.947449;
//	float cy = 404.559906;
//	float baseline = 150; //基线距离150mm
//
//	if (type == CV_8U)
//	{
//		const float PI = 3.14159265358;
//		int height = dispMap.rows;
//		int width = dispMap.cols;
//
//		uchar* dispData = (uchar*)dispMap.data;
//		ushort* depthData = (ushort*)depthMap.data;
//		for (int i = 0; i < height; i++)
//		{
//			for (int j = 0; j < width; j++)
//			{
//				int id = i * width + j;
//				if (!dispData[id])  continue;  //防止0除
//				depthData[id] = ushort((float)fx *baseline / ((float)dispData[id]));
//			}
//		}
//	}
//	else
//	{
//		cv::waitKey(0);
//	}
//}
//
//int main(int argc, char** argv)
//{
//	std::string img1_filename = "L0.jpg";
//	std::string img2_filename = "R0.jpg";
//	std::string intrinsic_filename = "intrinsics.yml";
//	std::string extrinsic_filename = "extrinsics.yml";
//	std::string disparity_filename = "test.bmp";
//	std::string point_cloud_filename = "dscv.pcd";
//
//	enum { STEREO_BM = 0, STEREO_SGBM = 1, STEREO_HH = 2, STEREO_VAR = 3, STEREO_3WAY = 4 };
//	int alg = STEREO_BM;
//	int SADWindowSize, numberOfDisparities =0;
//	bool no_display = 0;
//	float scale = 1.0;
//
//	Ptr<StereoBM> bm = StereoBM::create(16, 9);
//	Ptr<StereoSGBM> sgbm = StereoSGBM::create(0, 16, 3);
//	Mat K = (Mat_<double>(3, 3) << 682.421509, 0, 633.947449, 0, 682.421509, 404.559906, 0, 0, 1);//fx,cx,fy,cy
//
//	int color_mode = -1;
//	Mat img1 = imread(img1_filename, color_mode);
//	Mat img2 = imread(img2_filename, color_mode);
//	Mat depthImg(720, 1280, CV_16UC1);
//
//	if (scale != 1.f)
//	{
//		Mat temp1, temp2;
//		int method = scale < 1 ? INTER_AREA : INTER_CUBIC;
//		resize(img1, temp1, Size(), scale, scale, method);
//		img1 = temp1;
//		resize(img2, temp2, Size(), scale, scale, method);
//		img2 = temp2;
//	}
//
//	Size img_size = img1.size();
//
//	Rect roi1, roi2;
//	Mat Q;
//
//	if (!intrinsic_filename.empty())
//	{
//		// reading intrinsic parameters
//		FileStorage fs(intrinsic_filename, FileStorage::READ);
//		if (!fs.isOpened())
//		{
//			printf("Failed to open file %s\n", intrinsic_filename.c_str());
//			return -1;
//		}
//
//		Mat M1, D1, M2, D2;
//		fs["M1"] >> M1;
//		fs["D1"] >> D1;
//		fs["M2"] >> M2;
//		fs["D2"] >> D2;
//
//		M1 *= scale;
//		M2 *= scale;
//
//		fs.open(extrinsic_filename, FileStorage::READ);
//		if (!fs.isOpened())
//		{
//			printf("Failed to open file %s\n", extrinsic_filename.c_str());
//			return -1;
//		}
//
//		Mat R, T, R1, P1, R2, P2;
//		fs["R"] >> R;
//		fs["T"] >> T;
//
//		stereoRectify(M1, D1, M2, D2, img_size, R, T, R1, R2, P1, P2, Q, CALIB_ZERO_DISPARITY, -1, img_size, &roi1, &roi2);
//
//		Mat map11, map12, map21, map22;
//		initUndistortRectifyMap(M1, D1, R1, P1, img_size, CV_16SC2, map11, map12);
//		initUndistortRectifyMap(M2, D2, R2, P2, img_size, CV_16SC2, map21, map22);
//
//		Mat img1r, img2r;
//		remap(img1, img1r, map11, map12, INTER_LINEAR);
//		remap(img2, img2r, map21, map22, INTER_LINEAR);
//
//		img1 = img1r;
//		img2 = img2r;
//	}
//
//	numberOfDisparities = ((img_size.width / 8) + 15) & -16;
//
//	bm->setROI1(roi1);
//	bm->setROI2(roi2);
//	bm->setPreFilterCap(31);
//	bm->setBlockSize(9);
//	bm->setMinDisparity(0);
//	bm->setNumDisparities(numberOfDisparities);
//	bm->setTextureThreshold(10);
//	bm->setUniquenessRatio(15);
//	bm->setSpeckleWindowSize(100);
//	bm->setSpeckleRange(32);
//	bm->setDisp12MaxDiff(1);
//
//	sgbm->setPreFilterCap(63);
//	int sgbmWinSize = SADWindowSize > 0 ? SADWindowSize : 3;
//	sgbm->setBlockSize(sgbmWinSize);
//
//	int cn = img1.channels();
//
//	sgbm->setP1(8 * cn*sgbmWinSize*sgbmWinSize);
//	sgbm->setP2(32 * cn*sgbmWinSize*sgbmWinSize);
//	sgbm->setMinDisparity(0);
//	sgbm->setNumDisparities(numberOfDisparities);
//	sgbm->setUniquenessRatio(10);
//	sgbm->setSpeckleWindowSize(100);
//	sgbm->setSpeckleRange(32);
//	sgbm->setDisp12MaxDiff(1);
//	if (alg == STEREO_HH)
//		sgbm->setMode(StereoSGBM::MODE_HH);
//	else if (alg == STEREO_SGBM)
//		sgbm->setMode(StereoSGBM::MODE_SGBM);
//	else if (alg == STEREO_3WAY)
//		sgbm->setMode(StereoSGBM::MODE_SGBM_3WAY);
//
//	Mat disp, disp8;
//	int64 t = getTickCount();
//	if (alg == STEREO_BM)
//		bm->compute(img1, img2, disp);
//	else if (alg == STEREO_SGBM || alg == STEREO_HH || alg == STEREO_3WAY)
//		sgbm->compute(img1, img2, disp);
//	t = getTickCount() - t;
//	printf("Time elapsed: %fms\n", t * 1000 / getTickFrequency());
//	disp.convertTo(disp8, CV_8U);
//	imshow("left", img1);
//	imshow("right", img2);
//	imshow("disparity", disp8);
//	disp2Depth(disp8,depthImg);
//	//insertDepth32f(depthImg);
//	imshow("test", depthImg);
//	printf("press any key to continue...");
//	fflush(stdout);
//	waitKey();
//	imwrite("ddd.png", depthImg);
//	return 0;
//}
