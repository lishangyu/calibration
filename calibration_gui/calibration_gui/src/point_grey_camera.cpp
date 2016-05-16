/**************************************************************************************************
   Software License Agreement (BSD License)

   Copyright (c) 2014-2015, LAR toolkit developers - University of Aveiro - http://lars.mec.ua.pt
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted
   provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of
   conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of
   conditions and the following disclaimer in the documentation and/or other materials provided
   with the distribution.
 * Neither the name of the University of Aveiro nor the names of its contributors may be used to
   endorse or promote products derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
   IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
   FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
   IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
   OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************************************/
/**
   \file  camera.cpp
   \brief Ball detection with a stereo system
   \author Marcelo Pereira
   \date   December, 2015
 */

#include "calibration_gui/point_grey_camera_config.h"
#include "calibration_gui/point_grey_camera.h"
#include <sys/time.h>


//Marker's publisher
ros::Publisher ballCentroidCam_pub;
ros::Publisher ballCentroidCamPnP_pub;
image_transport::Publisher rawImage_pub;
image_transport::Publisher ballCentroidImage_pub;

Mat CameraMatrix1, CameraMatrix2, disCoeffs1, disCoeffs2, R1, R2, P1, P2, Q, T;

typedef unsigned long long timestamp_t;

static timestamp_t get_timestamp ()
{
	struct timeval now;
	gettimeofday (&now, NULL);
	return now.tv_usec + (timestamp_t)now.tv_sec * 1000000;
}

/**
   @brief Cameras connection
   @param[out] ppCameras
   @return void
 */
void ConnectCameras(Camera &Camera)
{
	//PointGrey

	Error error;

	BusManager busManager;
	unsigned int numCameras=0, count=0;
	while(numCameras!=1 && ros::ok())
	{
		error = busManager.GetNumOfCameras(&numCameras);
		if(error != PGRERROR_OK || count==10)
		{
			cout<<"Failed to get number of cameras"<<endl;
			throw exception();
		}
		cout<<numCameras<<"cameras detected"<<endl;
		count++;
		ros::Duration(2).sleep(); // sleep for two seconds
	}

	//Camera camera1;

	PGRGuid pGuid;
	error = busManager.GetCameraFromIndex(0,&pGuid);
	if(error != PGRERROR_OK)
	{
		cout<<"Failed to get index of camera 1"<<endl;
		exit(1);
	}


	CameraInfo camInfo;

	//connect the camera
	error=Camera.Connect(&pGuid);
	if ( error != PGRERROR_OK )
	{
		cout << "Failed to connect to camera" <<endl;
		exit(1);
	}

	// Get the camera info and print it out
	error = Camera.GetCameraInfo( &camInfo );
	if ( error != PGRERROR_OK )
	{
		cout << "Failed to get camera info from camera" << endl;
		exit(1);
	}
	cout << camInfo.vendorName << " "
	     << camInfo.modelName << " "
	     << camInfo.serialNumber << endl;


	SetConfiguration(Camera);


	error = Camera.StartCapture();
	if ( error == PGRERROR_ISOCH_BANDWIDTH_EXCEEDED )
	{
		cout << "Bandwidth exceeded" << endl;
		exit(1);
	}
	else if ( error != PGRERROR_OK )
	{
		cout << "Failed to start image capture" << endl;
		exit(1);
	}
}

/**
   @brief Image capture
   @param[in] ppCameras
   @return void
 */
void ImageCapture(Camera &Camera)
{
	Mat img;
	Mat imgHSV;
	Mat unImg;
	Mat imgBinary;
	Image rawImage;
	Image rgbImage;

	// create the main window, and attach the trackbars
	namedWindow( "Camera", CV_WINDOW_NORMAL );
	namedWindow( "Binarized Image", CV_WINDOW_NORMAL );
	namedWindow( "Control", CV_WINDOW_NORMAL );
	namedWindow( "Circle", CV_WINDOW_NORMAL );
	namedWindow( "Canny", CV_WINDOW_NORMAL );

	/* Trackbars for Hue, Saturation and Value (HSV) in "Camera 1" window */
	int lowH = 0;
	int highH = 179;

	int lowS = 101;
	int highS = 255;

	int lowV = 37;
	int highV = 255;

	int valC = 200;

	int valA = 150;

	int maxR = 300;
	int minR = 150;

	// Hue 0-179
	cvCreateTrackbar("Upper Hue        ", "Control", &highH, 179);
	cvCreateTrackbar("Lower Hue        ", "Control", &lowH, 179);
	// Saturation 0-255
	cvCreateTrackbar("Upper Saturation", "Control", &highS, 255);
	cvCreateTrackbar("Lower Saturation", "Control", &lowS, 255);
	// Value 0-255
	cvCreateTrackbar("Upper Value     ", "Control", &highV, 255);
	cvCreateTrackbar("Lower Value     ", "Control", &lowV, 255);

	cvCreateTrackbar("Canny Threshold ", "Control", &valC, 200);

	// =========================================================================
	// Uncomment below if using Hough Circles
	// =========================================================================
	// cvCreateTrackbar("Accumulator     ", "Control", &valA, 255);
	//
	// cvCreateTrackbar("Max. Radius (p) ", "Control", &maxR, 720);
	// cvCreateTrackbar("Min. Radius (p) ", "Control", &minR, 720);
	// =========================================================================
	// Uncomment above if using Hough Circles
	// =========================================================================


	Property properties;
	properties.type=FRAME_RATE;
	Camera.GetProperty(&properties);
	cout<<"Frame rate "<<properties.absValue<<endl;

	sensor_msgs::ImagePtr image_msg;

	// capture loop
	ros::Rate loop_rate(15);
	timestamp_t t0, t1;
	double secs;
	int key = 0;
	while(key != 1048689 && key != 1114193 && ros::ok())
	{
		// Get the image camera 1
		t0 = get_timestamp();
		Error error = Camera.RetrieveBuffer( &rawImage );
		if ( error != PGRERROR_OK )
		{
			cout << "capture error 1" << endl;
			continue;
		}

		// convert to rgb
		rawImage.Convert(PIXEL_FORMAT_BGR, &rgbImage );

		// convert to OpenCV Mat
		unsigned int rowBytes = (double)rgbImage.GetReceivedDataSize()/(double)rgbImage.GetRows();
		img = Mat(rgbImage.GetRows(), rgbImage.GetCols(), CV_8UC3, rgbImage.GetData(),rowBytes);

		if(!img.empty()) {
			image_msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", img).toImageMsg();
			rawImage_pub.publish(image_msg);
		}

		imshow("Camera", img); // untouched image

		// =========================================================================
		// Pre-processing
		// =========================================================================

		// Convert the captured image frame BGR to HSV
		undistort(img,unImg,CameraMatrix1,disCoeffs1);
		cvtColor(unImg, imgHSV, COLOR_BGR2HSV);

		// Threshold the image
		inRange(imgHSV, Scalar(lowH, lowS, lowV), Scalar(highH, highS, highV), imgBinary);

		//morphological closing (fill small holes in the foreground)
		dilate( imgBinary, imgBinary, getStructuringElement(MORPH_RECT, Size(5, 5)) );
		erode(imgBinary, imgBinary, getStructuringElement(MORPH_RECT, Size(5, 5)) );

		//morphological opening (remove small objects from the foreground)
		erode(imgBinary, imgBinary, getStructuringElement(MORPH_RECT, Size(5, 5)) );
		dilate( imgBinary, imgBinary, getStructuringElement(MORPH_RECT, Size(5, 5)) );

		GaussianBlur( imgBinary, imgBinary, Size(5, 5), 2, 2 );



		imshow("Binarized Image", imgBinary);

		// =====================================================================
		// Circle detection
		// Hough Circles or Polygonal Curve fitting algorithm, pick one
		// =====================================================================

		//HoughCircles(unImg, imgBinary, valC, valA, minRadius, maxRadius);

		PolygonalCurveDetection(unImg, imgBinary, valC);

		// get user key*/
		key = waitKey(1);
		ros::spinOnce();
		loop_rate.sleep();

		t1 = get_timestamp();
		secs = (t1 - t0) / 1000000.0L;
		std::cout << secs << std::endl;
	}

	destroyWindow("Camera"); //destroy the window
	destroyWindow("Binarized Image");
	destroyWindow("Control");
	destroyWindow("Circle");
}


void HoughDetection(const Mat &img, const Mat& imgBinary, int valCanny, int valAccumulator, int minRadius, int maxRadius)
{
	Mat dst = img.clone();

	vector<Vec3f> circles;

	HoughCircles( imgBinary, circles, CV_HOUGH_GRADIENT, 1, imgBinary.rows/8, valCanny, valAccumulator, minRadius, maxRadius );

	for( size_t i = 0; i < circles.size(); i++ )
	{
		Point center(cvRound(circles[i][0]), cvRound(circles[i][1]));
		int radius = cvRound(circles[i][2]);
		std::cout << "HoughCircles Radius = " <<radius << std::endl;
		// circle center
		circle( dst, center, 3, Scalar(255,0,0), -1, 8, 0 );
		// circle outline
		circle( dst, center, radius, Scalar(0,255,0), 1, 8, 0 );
		double Dist = CameraMatrix1.at<double>(0,0)*(BALL_DIAMETER/(2*radius));
		std::cout << "Real world diameter = " << Dist << std::endl;
	}

	imshow("Circles", dst);
}



/**
   @brief Ball detection in the stereo system
   @param[in]
   @return void
 */
//void ballDetection(sensor_msgs::PointCloud cloud)
void PolygonalCurveDetection( Mat &img, Mat &imgBinary, int valCanny )
{
	vector<vector<Point> > contours;

	/// Detect edges using canny
	Mat imgCanny;
	Canny( imgBinary, imgCanny, valCanny, valCanny*2, 3 );

	imshow("Canny", imgCanny); // debug

	findContours(imgCanny.clone(),contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);

	vector<Point> approx;
	Mat dst = img.clone();
	vector<double> areas;

	pcl::PointXYZ centroid; // Point structure for centroid
	/* The following lines make sure that position (0,0,0) is published when
	   the ball is not detected (avoids publishing previous positions when the
	   ball is no longer detected) */
	centroid.x = 0;
	centroid.y = 0;
	centroid.z = 0;

	pcl::PointXYZ centroidRadius; // Point structure for centroid
	/* The following lines make sure that position (0,0,0) is published when
	   the ball is not detected (avoids publishing previous positions when the
	   ball is no longer detected) */
	centroidRadius.x = -999;
	centroidRadius.y = -999;
	centroidRadius.z = -999;

	for(int i=0; i<contours.size(); i++)
	{
		approxPolyDP(Mat(contours[i]), approx, arcLength(Mat(contours[i]), true)*0.02,true);

		// Skip small or non-convex objects
		if (std::fabs(cv::contourArea(contours[i])) < 1000 || !cv::isContourConvex(approx))
			continue;


		// Detect and label circles
		if(approx.size()>=6)
		{
			double area = cv::contourArea(contours[i]);
			cv::Rect r = cv::boundingRect(contours[i]);
			int radius = (r.width/2 +r.width/2)/2;

			if (abs(1 - ((double)r.width / r.height)) <= 0.2 && abs(1 - (area / (CV_PI * std::pow(radius, 2)))) <= 0.2
			    && area>1000)
			{
				//cout<< "radius - "<< radius <<endl; // DEBUGGING

				// Computing distnace from camera to center of detected circle
				double Dist;
				// + BALL_DIAMETER/4 is a correction since the sphere centroid is deeper than the center of the detected circle
				Dist = (CameraMatrix1.at<double>(0,0)*(BALL_DIAMETER/(radius*2)));
				std::stringstream s;
				s<<Dist;
				string str = s.str();
				setLabel(dst, str, contours[i]);

				centroid.x = r.x + radius;
				centroid.y = r.y + radius;
				centroid.z = radius;

				// Method based on circle radius =======================================
				cv::Mat image_vector = (cv::Mat_<double>(3,1) << centroid.x, centroid.y, 1);
				cv::Mat camera_vector = CameraMatrix1;

				camera_vector = CameraMatrix1.inv() * (Dist * image_vector);
				//cout << camera_vector << endl; // DEBUGGING
				//cout << CameraMatrix1.inv() << endl; // DEBUGGING
				centroidRadius.x = camera_vector.at<double>(0);
				centroidRadius.y = camera_vector.at<double>(1);
				centroidRadius.z = camera_vector.at<double>(2);
				//cout << centroidRadius << endl; // DEBUGGING
			}
		}
	}
	CentroidPub(centroid, centroidRadius);
	imshow("Circle", dst);
}

void CentroidPub( const pcl::PointXYZ centroid, const pcl::PointXYZ centroidRadius )
{
	// Method based on solvePnP ================================================
	geometry_msgs::PointStamped CentroidCam;

	CentroidCam.point.x = centroid.x;
	CentroidCam.point.y = centroid.y;
	CentroidCam.point.z = centroid.z;

	CentroidCam.header.stamp = ros::Time::now();
	ballCentroidCamPnP_pub.publish(CentroidCam);
	//std::cout << CentroidCam << std::endl;

	// Method based on circle radius ===========================================
	CentroidCam.point.x = centroidRadius.x;
	CentroidCam.point.y = centroidRadius.y;
	CentroidCam.point.z = centroidRadius.z;

	CentroidCam.header.stamp = ros::Time::now();
	ballCentroidCam_pub.publish(CentroidCam);
	//std::cout << CentroidCam << std::endl;
}

void setLabel(cv::Mat& im, const std::string label, std::vector<cv::Point>& contour)
{
	int fontface = cv::FONT_HERSHEY_SIMPLEX;
	double scale = 0.8;
	int thickness = 1;
	int baseline = 0;

	cv::Size text = cv::getTextSize(label, fontface, scale, thickness, &baseline);
	cv::Rect r = cv::boundingRect(contour);
	int radius = (r.width/2 + r.height/2)/2;

	cv::Point pt(r.x + ((r.width - text.width) / 2), r.y + ((r.height + text.height) / 2));
	cv::Point center(r.x + ((r.width) / 2), r.y + ((r.height) / 2));
	//cv::rectangle(im, pt + cv::Point(0, baseline), pt + cv::Point(text.width, -text.height), CV_RGB(255,255,255), CV_FILLED);
	//cv::putText(im, label, pt, fontface, scale, CV_RGB(0,0,0), thickness, 8);
	//circle( im, center, 4, Scalar(255,0,0), -1, 8, 0 );
	cv::line(im, cv::Point(center.x - 7, center.y), cv::Point(center.x + 7, center.y), cv::Scalar(255,255,255), 2);  //crosshair horizontal
	cv::line(im, cv::Point(center.x, center.y - 7), cv::Point(center.x, center.y + 7), cv::Scalar(255,255,255), 2);  //crosshair vertical
	// circle outline
	circle( im, center, radius, Scalar(255,255,255), 2, 8, 0 );

	sensor_msgs::ImagePtr image_msg;
	image_msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", im).toImageMsg();
	ballCentroidImage_pub.publish(image_msg);
}

/**
   @brief Main function of the camera node
   @param argc
   @param argv
   @return int
 */
int main(int argc, char **argv)
{
	ros::init(argc, argv, "Point_Grey");
	ros::NodeHandle n("~");
	n.getParam("ballDiameter", BALL_DIAMETER);
	cout << "Ball diameter:" << BALL_DIAMETER << endl;
	string node_namespace = ros::this_node::getNamespace();

	//read calibration paraneters
	string a="/intrinsic_calibrations/ros_calib.yaml";
	string path = ros::package::getPath("calibration_gui");
	path += a;
	FileStorage fs(path, FileStorage::READ);
	if(!fs.isOpened())
	{
		cout<<"failed to open document"<<endl;
		return -1;
	}

	fs["CM1"] >> CameraMatrix1;
	fs["D1"] >> disCoeffs1;
	std::cout << CameraMatrix1 << std::endl;
	std::cout << disCoeffs1 << std::endl;


	image_transport::ImageTransport it(n);
	rawImage_pub = it.advertise("/" + node_namespace + "/RawImage", 1);
	ballCentroidImage_pub = it.advertise("BallDetection", 1);
	ballCentroidCam_pub = n.advertise<geometry_msgs::PointStamped>( "SphereCentroid", 1);
	ballCentroidCamPnP_pub = n.advertise<geometry_msgs::PointStamped>( "SphereCentroidPnP", 1);

	Camera Camera;
	try
	{
		ConnectCameras(Camera);
	}
	catch (const exception&)
	{
		return EXIT_FAILURE;
	}

	ImageCapture(Camera);

	return 0;
}
