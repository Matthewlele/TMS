/*******************************************************************************
*                                                                              *
*                       Brno University of Technology                          *
*                       CPhoto@FIT                                             *
*                                                                              *
*                       Tone Mapping Studio	                                   *
*                                                                              *
*                       Diploma thesis                                         *
*                       Author: Matus Bicanovsky                               *
*                       Brno 2024                                              *
*                                                                              *
*                       Implementation of the TMOTao18 class                   *
*       Video Decolorization Using Visual Proximity Coherence Optimization     *
*******************************************************************************/
/* --------------------------------------------------------------------------- *
 * TMOTao18.cpp: implementation of the TMOTao18 class.   *
 * --------------------------------------------------------------------------- */

#include "TMOTao18.h"

/* --------------------------------------------------------------------------- *
 * Constructor serves for describing a technique and input parameters          *
 * --------------------------------------------------------------------------- */
TMOTao18::TMOTao18()
{
	SetName(L"Tao18");					  // TODO - Insert operator name
	SetDescription(L"Add your TMO description here"); // TODO - Insert description

	dParameter.SetName(L"ParameterName");				// TODO - Insert parameters names
	dParameter.SetDescription(L"ParameterDescription"); // TODO - Insert parameter descriptions
	dParameter.SetDefault(1);							// TODO - Add default values
	dParameter = 1.;
	dParameter.SetRange(-1000.0, 1000.0); // TODO - Add acceptable range if needed
	this->Register(dParameter);
}

TMOTao18::~TMOTao18()
{
}

/* --------------------------------------------------------------------------- *
 * This overloaded function is an implementation of your tone mapping operator *
 * --------------------------------------------------------------------------- */
int TMOTao18::Transform()
{
	// Source image is stored in local parameter pSrc
	// Destination image is in pDst

	// Initialy images are in RGB format, but you can
	// convert it into other format
	pSrc->Convert(TMO_Yxy); // This is format of Y as luminance
	pDst->Convert(TMO_Yxy); // x, y as color information

	double *pSourceData = pSrc->GetData();		// You can work at low level data
	double *pDestinationData = pDst->GetData(); // Data are stored in form of array
												// of three doubles representing
												// three colour components
	double pY, px, py;

	int j = 0;
	for (j = 0; j < pSrc->GetHeight(); j++)
	{
		pSrc->ProgressBar(j, pSrc->GetHeight()); // You can provide progress bar
		for (int i = 0; i < pSrc->GetWidth(); i++)
		{
			pY = *pSourceData++;
			px = *pSourceData++;
			py = *pSourceData++;

			// Here you can use your transform
			// expressions and techniques...
			pY *= dParameter; // Parameters can be used like
							  // simple variables

			// and store results to the destination image
			*pDestinationData++ = pY;
			*pDestinationData++ = px;
			*pDestinationData++ = py;
		}
	}
	pSrc->ProgressBar(j, pSrc->GetHeight());
	pDst->Convert(TMO_RGB);
	return 0;
}

float TMOTao18::computeEntropy(const cv::Mat& hist)
{
	float entropy = 0.0;
	float total = cv::sum(hist)[0];
	for (int i = 0; i < hist.rows; i++)
	{
		float p = hist.at<float>(i) / total;
		if (p != 0)
		{
			entropy -= p * log(p);
		}
	}
	return entropy;
}

void TMOTao18::computeProximity(const cv::Mat& currentFrame, const cv::Mat& previousFrame, float& deltaL, float& deltaC)
{
	cv::Mat currLab, prevLab;
	//convert to Lab color space
	cv::cvtColor(currentFrame, currLab, cv::COLOR_BGR2Lab);
	cv::cvtColor(previousFrame, prevLab, cv::COLOR_BGR2Lab);

	//calculate difference between current and previous frame
	cv::Mat diffLab = cv::abs(currLab - prevLab);

	//calculate the histogram of the difference
	std::vector<cv::Mat> diffLabChannels(3);
	cv::normalize(diffLab, diffLab, 0.0, 255.0, cv::NORM_MINMAX, CV_32F);
	cv::split(diffLab, diffLabChannels);
	cv::Mat histL, histA, histB;
	int histSize = 256;
	float range[] = {0,256};
	const float* histRange = {range};
	cv::calcHist(&diffLabChannels[0], 1, 0, cv::Mat(), histL, 1, &histSize, &histRange);
	cv::calcHist(&diffLabChannels[1], 1, 0, cv::Mat(), histA, 1, &histSize, &histRange);
	cv::calcHist(&diffLabChannels[2], 1, 0, cv::Mat(), histB, 1, &histSize, &histRange);

	//calculate the entropy of the histograms to get decolorization proximity values
	deltaL = std::abs(computeEntropy(histL));
	float deltaA = computeEntropy(histA);
	float deltaB = computeEntropy(histB);

	deltaC = sqrt((deltaA * deltaA + deltaB * deltaB) / 2.0);
}

cv::Mat TMOTao18::applyLPD(const cv::Mat& currentFrame, const cv::Mat& previousFrame, const cv::Mat& previousGray, double beta)
{
	//convert rgb frames to lab
	cv::Mat currLab, prevLab, prevGrayD;
	cv::cvtColor(currentFrame, currLab, cv::COLOR_BGR2Lab);
	cv::cvtColor(previousFrame, prevLab, cv::COLOR_BGR2Lab);
	std::vector<cv::Mat> prevLabChannels(3);
	std::vector<cv::Mat> currLabChannels(3);
	cv::split(prevLab, prevLabChannels);
	cv::split(currLab, currLabChannels);
	prevLabChannels[0] = prevLabChannels[0] / 100.0;
	currLabChannels[0] = currLabChannels[0] / 100.0;

	//initialize grayscale frame 
	cv::Mat currGray(currentFrame.size(), CV_32F);
	std::vector<cv::Mat> bgrChannels(3);
	cv::split(currentFrame, bgrChannels);
	currGray = 0.299 * bgrChannels[2] + 0.587 * bgrChannels[1] + 0.114 * bgrChannels[0];
	
	//iterative optimization to minimize energy function
	cv::Mat G;   //start with initial grayscale frame
	G = currGray.clone();
	const double learningRate = 1e-3; //step length for gradient descent
	const int maxIterations = 10;     //maximum number of iterations
	const double epsilon = 1e-3;      //convergence threshold
	cv::Mat dir = cv::Mat::zeros(G.size(), CV_32F);
	cv::Mat prevGrad = cv::Mat::zeros(G.size(), CV_32F);
	for(int i = 0; i < maxIterations; i++){
		cv::Mat gradient = cv::Mat::zeros(G.size(), CV_32F);
		for(int y = 0; y < G.rows; y++){
			for(int x = 0; x < G.cols; x++){
				//spatial grad
				float eta_diff = currLabChannels[0].at<float>(y,x) - prevLabChannels[0].at<float>(std::max(0, y-1), std::max(0, x-1));
				float diff = G.at<float>(y, x) - G.at<float>(std::max(0, y-1), std::max(0, x-1)) - eta_diff;
				float spatialGrad = 2 * diff;
				//temporal grad
				float lum_diff = currLabChannels[0].at<float>(y,x) - prevLabChannels[0].at<float>(y,x);
				float diff2 = G.at<float>(y, x) - previousGray.at<float>(y, x) - lum_diff;
				float temporalGrad = 2 * diff2;
				
				gradient.at<float>(y, x) = (1-beta) * spatialGrad + (beta * temporalGrad);
			}
		}
		//G -= learningRate * gradient;
		if(i == 0){
			dir = -gradient;
		}
		else{
			double betCGM = cv::sum(gradient.mul(gradient))[0] / cv::sum(prevGrad.mul(prevGrad))[0];
			dir = -gradient + betCGM * dir;
		}
		G += learningRate * dir;
		prevGrad = gradient.clone();
		//check for convergence
		if(cv::norm(gradient, cv::NORM_L2) < epsilon){
			break;
		}
	}
	cv::Mat result = G.clone();
	return result;
}

cv::Mat TMOTao18::applyMPD(const cv::Mat& currentFrame, const cv::Mat& previousFrame, const cv::Mat& previousGray)
{
	cv::Mat currLab, prevLab, prevLabGray;
    cv::cvtColor(currentFrame, currLab, cv::COLOR_BGR2Lab);
    cv::cvtColor(previousFrame, prevLab, cv::COLOR_BGR2Lab);
	

	std::vector<cv::Mat> currLabChannels(3), prevLabChannels(3);
	cv::split(currLab, currLabChannels);
	cv::split(prevLab, prevLabChannels);
	//fprintf(stderr, "Frames converted to Lab\n");
	currLabChannels[0] = currLabChannels[0] / 100.0;
	prevLabChannels[0] = prevLabChannels[0] / 100.0;
	cv::Mat flow;
    cv::calcOpticalFlowFarneback(prevLabChannels[0], currLabChannels[0], flow, 0.5, 3, 15, 3, 5, 1.2, 0);
	
	cv::Mat curr_L = currLabChannels[0];
	cv::Mat prev_L = prevLabChannels[0];
	//initialize Ci with the current frame
    cv::Mat Ci = previousGray.clone();
	Ci.convertTo(Ci, CV_32F);
	const float sigma_p = 0.05;            //noise variance in frame transition
    const float sigma_T = 0.1;            //noise variance in frame transition
    const float alpha = 0.01;              //step length for gradient ascent 
    const int maxIterations = 10;          //maximum number of iterations
	for (int iter = 0; iter < maxIterations; iter++) {
        cv::Mat Ci_new = Ci.clone();

        for (int y = 0; y < currentFrame.rows; y++) {
            for (int x = 0; x < currentFrame.cols; x++) {
                cv::Point2f flowAt = flow.at<cv::Point2f>(y, x);
				//fprintf(stderr, "Flow at: %f %f\n", flowAt.x, flowAt.y);
                int newX = cv::borderInterpolate(x + flowAt.x, currentFrame.cols, cv::BORDER_REFLECT_101);
                int newY = cv::borderInterpolate(y + flowAt.y, currentFrame.rows, cv::BORDER_REFLECT_101);

                // Calculate Mi using the L2-norm
				float currLVal = curr_L.at<float>(y, x);
				float prevLVal = prev_L.at<float>(newY, newX);
				float Mi = std::sqrt(
					std::pow(currLabChannels[0].at<float>(y, x) - prevLabChannels[0].at<float>(newY, newX), 2) +
					std::pow(currLabChannels[1].at<float>(y, x) - prevLabChannels[1].at<float>(newY, newX), 2) +
					std::pow(currLabChannels[2].at<float>(y, x) - prevLabChannels[2].at<float>(newY, newX), 2)
				);
				if(Mi == 0.0){
					Mi += 1e-6;
				}
                //calculate the gradient of P(Ci)
				
				float term1 = std::exp(-std::pow(Ci.at<float>(y,x) - previousGray.at<float>(y, x), 2) / (sigma_p * sigma_p));
				float term2 = std::exp(-std::pow(Ci.at<float>(y,x) - previousGray.at<float>(newY, newX), 2) / (sigma_T * sigma_T * Mi));
                float gradP_Ci = term1 * term2;

                //update Ci using gradient ascent
                Ci_new.at<float>(y, x) = Ci.at<float>(y, x) + alpha * gradP_Ci;
            }
        }

        // Check for convergence (optional)
        if (cv::norm(Ci_new - Ci) < 1e-3) {
            break;
        }

        Ci = Ci_new;
    }
	//fprintf(stderr, "MPD done\n");
	cv::Mat result;
	double weight = 0.5;
	cv::addWeighted(previousGray, weight, Ci, 1 - weight, 0, result);
	//previousGray.convertTo(result, CV_32F);
	//result = 0.5 * result + 0.5 * Ci;
	//fprintf(stderr, "Result calculated\n");
	//result.convertTo(result, CV_8U);
    return result;

}

cv::Mat TMOTao18::applyHPD(const cv::Mat& currentFrame, const cv::Mat& previousFrame, const cv::Mat& previousGray, double phi)
{
	//convert rgb frames to Lab color space
	cv::Mat currLab, prevLab;
	cv::cvtColor(currentFrame, currLab, cv::COLOR_BGR2Lab);
	cv::cvtColor(previousFrame, prevLab, cv::COLOR_BGR2Lab);

	//split lab channels
	std::vector<cv::Mat> currLabChannels(3), prevLabChannels(3);
	cv::split(currLab, currLabChannels);
	cv::split(prevLab, prevLabChannels);
	prevLabChannels[0] = prevLabChannels[0] / 100.0;
	currLabChannels[0] = currLabChannels[0] / 100.0;
	currLabChannels[1] = (currLabChannels[1] + 128.0f) / 255.0f;  // a channel (range [-128, 127])
    currLabChannels[2] = (currLabChannels[2] + 128.0f) / 255.0f;  // b channel (range [-128, 127])
	prevLabChannels[1] = (prevLabChannels[1] + 128.0f) / 255.0f;  // a channel
    prevLabChannels[2] = (prevLabChannels[2] + 128.0f) / 255.0f;  // b channel

	//calculate optical flow between current and previous frame
	cv::Mat flow;
	cv::calcOpticalFlowFarneback(prevLabChannels[0], currLabChannels[0], flow, 0.5, 3, 15, 3, 5, 1.2, 0);

	//calculate differential refinement frame D_i
	cv::Mat differentialRefinement = cv::Mat::zeros(currentFrame.size(), CV_32F);
	for(int y = 0; y < currentFrame.rows; y++)
	{
		for(int x = 0; x < currentFrame.cols; x++)
		{
			cv::Point2f flowAt = flow.at<cv::Point2f>(y, x);
			int newX = cv::borderInterpolate(x + flowAt.x, currentFrame.cols, cv::BORDER_REFLECT_101);
            int newY = cv::borderInterpolate(y + flowAt.y, currentFrame.rows, cv::BORDER_REFLECT_101); 
			//calculate chrominance diff
			float chromaDiff = 0.0;
			for(int k = 1; k < 3; k++){  //only a and b channels
				chromaDiff += currLabChannels[k].at<float>(newY, newX) - prevLabChannels[k].at<float>(y, x);
			}
			//chromaDiff /= 2.0;
			//calculate the luminance difference
            float luminanceDiff = currLabChannels[0].at<float>(y, x) - prevLabChannels[0].at<float>(y, x);

            //calculate the differential refinement value
            differentialRefinement.at<float>(y, x) = phi * chromaDiff + (1 - phi) * luminanceDiff;
		}
	}
	cv::Mat result;
	cv::add(previousGray, differentialRefinement, result);
	return result;
}

std::vector<int> TMOTao18::classify(const std::vector<cv::Vec2f>& proximityValues)
{
	//set number of clusters
	const int k = 3;
	//convert the data points to a cv::Mat of type CV_32F
    cv::Mat dataMat(proximityValues.size(), 2, CV_32F);
    for (size_t i = 0; i < proximityValues.size(); i++) {
        dataMat.at<float>(static_cast<int>(i), 0) = proximityValues[i][0];
		dataMat.at<float>(static_cast<int>(i), 1) = proximityValues[i][1];
    }
	
	//initialize cluster assignemnts with k-means++
	std::vector<int> clusters(proximityValues.size());
	cv::TermCriteria criteria(cv::TermCriteria::Type(1+2), 10, 1.0);
    cv::kmeans(dataMat, k, clusters, criteria, 3, cv::KMEANS_PP_CENTERS);

	//calculate mean, covariance and prior probability for each cluster
	std::vector<cv::Vec2f> means(k);
	std::vector<cv::Mat> covariances(k);
	std::vector<double> priors(k);

	for (int j = 0; j < k; j++) {
        cv::Mat clusterData(0, 2, CV_32F);
        for (size_t i = 0; i < proximityValues.size(); i++) {
            if (clusters[i] == j) {
                cv::Mat row = (cv::Mat_<float>(1, 2) << proximityValues[i][0], proximityValues[i][1]);
                clusterData.push_back(row);
            }
        }
        if (!clusterData.empty()) {
            cv::Mat meanMat, covarMat;
            cv::calcCovarMatrix(clusterData, covarMat, meanMat, cv::COVAR_NORMAL | cv::COVAR_ROWS);
            covarMat = covarMat / (clusterData.rows - 1); // Normalize the covariance matrix
            means[j] = cv::Vec2f(meanMat.at<double>(0, 0), meanMat.at<double>(0, 1));
            covariances[j] = covarMat;
            priors[j] = static_cast<double>(clusterData.rows) / proximityValues.size();
        } else {
            covariances[j] = cv::Mat::eye(2, 2, CV_64F); // Identity matrix if no data points
            means[j] = cv::Vec2f(0, 0);
            priors[j] = 0;
        }
    }
	
	//set penalty parameters
	const double lambda = 1.0;
	const double xi = 1.0;

	bool convergance = false;
	//iteratively update the cluster assignments
	int iter = 0;
	double L = 0.0;
	double prevL = -std::numeric_limits<double>::infinity();
	std::vector<int> newClusters = clusters;
    while (true) {
		int consecutiveCount = 0;
		int lastCluster = newClusters[0];
        // Reassign each data point based on GMM probability
        
        for (size_t i = 0; i < proximityValues.size(); i++) {
            cv::Vec2d ui(proximityValues[i][0], proximityValues[i][1]);
            std::vector<double> clusterProbabilities(k);

            for (int j = 0; j < k; j++) {
                if (newClusters[i] == j) {
        			if (lastCluster == j) {
            			consecutiveCount++;
        			} else {
            			consecutiveCount = 1;
        			}
        			lastCluster = j;
				}
    			double penalty = (consecutiveCount > 5) ? lambda / (1 + std::exp(-xi * (consecutiveCount - 5))) : 0.0;
                clusterProbabilities[j] = gaussianLikelihood(ui, means[j], covariances[j], priors[j]) - penalty;
            }

            newClusters[i] = std::distance(clusterProbabilities.begin(), std::max_element(clusterProbabilities.begin(), clusterProbabilities.end()));
        }

        // Update cluster statistics (mean, covariance, and prior)
        for (int j = 0; j < k; j++) {
            cv::Mat clusterData(0, 2, CV_32F);
            for (size_t i = 0; i < proximityValues.size(); i++) {
                if (newClusters[i] == j) {
                    cv::Mat row = (cv::Mat_<float>(1, 2) << proximityValues[i][0], proximityValues[i][1]);
                    clusterData.push_back(row);
                }
            }
            if (!clusterData.empty()) {
                cv::Mat meanMat;
                cv::calcCovarMatrix(clusterData, covariances[j], meanMat, cv::COVAR_NORMAL | cv::COVAR_ROWS);
                means[j] = cv::Vec2f(meanMat.at<float>(0, 0), meanMat.at<float>(0, 1));
                priors[j] = static_cast<double>(clusterData.rows) / proximityValues.size();
            } else {
                covariances[j] = cv::Mat::eye(2, 2, CV_64F); // Identity matrix if no data points
                means[j] = cv::Vec2f(0, 0);
                priors[j] = 0;
            }
        }

        // Calculate the log-likelihood to check for convergence
        L = logLikelihood(proximityValues, newClusters, means, covariances, priors);
        // Check for convergence
        if (hasConverged(L, prevL, 1e-3)) {
            break;
        }
		
        prevL = L;
        clusters = newClusters;
		iter++;
    }

    // Return the cluster assignments for all frames
    return clusters;

}

double TMOTao18::gaussianLikelihood(const cv::Vec2d& ui, const cv::Vec2d& mean, const cv::Mat& cov, double prior)
{
    cv::Vec2d diff = ui - mean;
    cv::Mat diffMat = cv::Mat(diff).reshape(1);               //convert diff to a 2x1 matrix
    cv::Mat covInv = cov.inv();
	
    cv::Mat tmpMat = diffMat.t() * covInv * diffMat;
    double tmp = tmpMat.at<double>(0, 0);
    double likelihood = std::exp(-0.5 * tmp) / std::sqrt(std::pow(2 * CV_PI, diffMat.rows) * cv::determinant(cov));
    return likelihood;
}

double TMOTao18::logLikelihood(const std::vector<cv::Vec2f>& dataPoints, const std::vector<int>& clusters, const std::vector<cv::Vec2f>& means, const std::vector<cv::Mat>& covariances, const std::vector<double>& priors)
{
    double L = 0.0;
    for (size_t i = 0; i < dataPoints.size(); i++) {
        int j = clusters[i];
        cv::Vec2d dataPoint(dataPoints[i][0], dataPoints[i][1]);
        L += std::log(priors[j]) * gaussianLikelihood(dataPoint, means[j], covariances[j], priors[j]);
    }
    return L;
}

bool TMOTao18::hasConverged(double L, double prevL, double threshold)
{
    return std::abs(L - prevL) < threshold;
}

int TMOTao18::TransformVideo()
{
	int width = vSrc->GetWidth();
	int height = vSrc->GetHeight();
	cv::VideoCapture vid = vSrc->getVideoCaptureObject();
	std::vector<cv::Mat> channels;
	cv::Mat currentFrame, previousFrame, previousGray;
	std::vector<cv::Vec2f> proximityValues;
	int tmp = 0;
	float minDeltaL, minDeltaC, maxDeltaL, maxDeltaC;
	minDeltaC = 100.0;
	minDeltaL = 100.0;
	maxDeltaC = 0.0;
	maxDeltaL = 0.0;
	for(int cnt = 0; cnt < vSrc->GetTotalNumberOfFrames(); cnt++)
	{
		vSrc->GetMatVideoFrame(vid, cnt, currentFrame);
		if(cnt == 0)
		{

		}
		else
		{
			float deltaL, deltaC;
			computeProximity(currentFrame, previousFrame, deltaL, deltaC);
			proximityValues.push_back(cv::Vec2f(static_cast<float>(deltaL), static_cast<float>(deltaC)));
			//fprintf(stderr, "Frame %d processed deltaL %f deltaC %f\n", cnt, proximityValues[tmp][0], proximityValues[tmp][1]);
			if(proximityValues[tmp][0] < minDeltaL)
			{
				minDeltaL = proximityValues[tmp][0];
			}
			if(proximityValues[tmp][0] > maxDeltaL)
			{
				maxDeltaL = proximityValues[tmp][0];
			}
			if(proximityValues[tmp][1] < minDeltaC)
			{
				minDeltaC = proximityValues[tmp][1];
			}
			if(proximityValues[tmp][1] > maxDeltaC)
			{
				maxDeltaC = proximityValues[tmp][1];
			}
			tmp++;
		}
		fprintf(stderr, "\rFrame %d/%d processed", cnt, vSrc->GetTotalNumberOfFrames());
		fflush(stdout);
		previousFrame = currentFrame.clone();
	}
	fprintf(stderr, "\nMin deltaL %f Max deltaL %f Min deltaC %f Max deltaC %f\n", minDeltaL, maxDeltaL, minDeltaC, maxDeltaC);
	std::vector<int> classifications = classify(proximityValues);
	previousFrame = cv::Mat::zeros(height, width, CV_32FC3);
	previousGray = cv::Mat::zeros(height, width, CV_32F);
	cv::Mat result, normResult;
	for(int i = 0; i < vSrc->GetTotalNumberOfFrames(); i++)
	{
		vSrc->GetMatVideoFrame(vid, i, currentFrame);
		if(i == 0)
		{
			fprintf(stderr, "Frame %d processed by LPD ", i);
			result = applyLPD(currentFrame, previousFrame, previousGray, 0.5);
		}
		else
		{
			if(classifications[i-1] == 0)
			{
				fprintf(stderr, "Frame %d processed by LPD ", i);
				result = applyLPD(currentFrame, previousFrame, previousGray, 0.5);
			}
			/*else if(classifications[i-1] == 1)
			{
				fprintf(stderr, "Frame %d processed by MPD ", i);
				result = applyMPD(currentFrame, previousFrame, previousGray);
			}*/
			else
			{
				fprintf(stderr, "Frame %d processed by HPD ", i);
				result = applyHPD(currentFrame, previousFrame, previousGray, 0.5);
			}
		}
		//cv::cvtColor(currentFrame, result, cv::COLOR_BGR2GRAY);
		//currentFrame = currentFrame * 255.0;
		//result = applyLPD(currentFrame, previousFrame, previousGray, 0.5);
		//currentFrame = currentFrame / 255.0;
		normResult = result.clone();
		//cv::normalize(result, normResult, 0.0, 1.0, cv::NORM_MINMAX, CV_32F);
		channels.clear();
		channels.push_back(normResult);
		channels.push_back(normResult);
		channels.push_back(normResult);
		cv::Mat finalResult;
		cv::merge(channels, finalResult);
		// i want to print the tmpResult matrix
		// i want to print the type of data in tmpResult
		//fprintf(stderr, "Type of data in currentFrame: %d\n", currentFrame.type());
		//fprintf(stderr, "Type of data in tmpResult: %d\n", result.type());
		
		vDst->setMatFrame(vDst->getVideoWriterObject(), finalResult);
		double minVal, maxVal;
		cv::Point minLoc, maxLoc;
		cv::minMaxLoc(normResult, &minVal, &maxVal, &minLoc, &maxLoc);
		fprintf(stderr, "min %f max %f, dataType: %d size %dx%d\n",minVal, maxVal, normResult.type(), normResult.cols, normResult.rows);
		//cv::Mat store;
		//cv::normalize(result, store, 0.0, 1.0, cv::NORM_MINMAX, CV_32F);
		previousFrame = currentFrame.clone();
		previousGray = normResult.clone();
	}
}
