/**
 * Copyright - Benjamin Laugraud - 2016
 */
#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>
#include <boost/shared_ptr.hpp>

#include <opencv2/opencv.hpp>

#include "FrameDifferenceC1L1.h"
#include "History.h"
#include "MotionProba.h"
#include "Utils.h"

using namespace std;
using namespace boost;
using namespace boost::program_options;

/******************************************************************************
 * Main program                                                               *
 ******************************************************************************/

int main(int argc, char** argv) {
  /***************************************************************************
   * Argument(s) handling.                                                   *
   ***************************************************************************/

  options_description optDesc(
    string("Copyright - Benjamin Laugraud - 2016\n")                          +
    "Usage: background_modeler_ng [options]"
  );

  optDesc.add_options()
    (
      "help,h",
      "print this help message"
    )
    (
      "input,i",
      value<string>(),
      "path to the input sequence"
    )
    (
      "output,o",
      value<string>(),
      "path to the output folder"
    )
    (
      "s-parameter,s",
      value<int32_t>()->multitoken(),
      "value of the S parameter"
    )
    (
      "n-parameter,n",
      value<int32_t>(),
      "value of the N parameter"
    )
    (
      "default,d",
      "use the default set of parameters"
    )
    (
      "visualization,v",
      "enable visualization"
    )
  ;

  variables_map varsMap;
  store(parse_command_line(argc, argv, optDesc), varsMap);
  notify(varsMap);

  /* Help message. */
  if (varsMap.count("help")) {
    cout << optDesc << endl;
    return EXIT_SUCCESS;
  }

  /*
   * Welcome message.
   */

  cout << "===========================================================" << endl;
  cout << "= LaBGen-P                                                =" << endl;
  cout << "===========================================================" << endl;
  cout << "= Copyright - Benjamin Laugraud - 2016                    =" << endl;
  cout << "= http://www.montefiore.ulg.ac.be/~blaugraud              =" << endl;
  cout << "= http://www.telecom.ulg.ac.be/research/sbg               =" << endl;
  cout << "===========================================================" << endl;
  cout << endl;

  /*
   * Extract parameters and sanity check.
   */

  int32_t sParam = 0;
  int32_t nParam = 0;

  /* "input" */
  if (!varsMap.count("input"))
    throw runtime_error("You must provide the path of the input sequence!");

  string sequence(varsMap["input"].as<string>());

  /* "output" */
  if (!varsMap.count("output"))
    throw runtime_error("You must provide the path of the output folder!");

  string output(varsMap["output"].as<string>());

  /* "default" */
  bool defaultSet = varsMap.count("default");

  if (defaultSet) {
    sParam = 19;
    nParam = 3;
  }

  /* Other parameters. */
  if (!defaultSet) {
    /* "s-parameter" */
    if (!varsMap.count("s-parameter"))
      throw runtime_error("You must provide the S parameter!");

    sParam = varsMap["s-parameter"].as<int32_t>();

    if (sParam < 1)
      throw runtime_error("The S parameter must be positive!");

    /* "n-parameter" */
    if (!varsMap.count("n-parameter"))
      throw runtime_error("You must provide the N parameter!");

    nParam = varsMap["n-parameter"].as<int32_t>();

    if (nParam < 1)
      throw runtime_error("The N parameter must be positive!");
  }

  /* "visualization" */
  bool visualization = varsMap.count("visualization");

  /* Display parameters to the user. */
  cout << "Input sequence: "      << sequence      << endl;
  cout << "   Output path: "      << output        << endl;
  cout << "             S: "      << sParam        << endl;
  cout << "             N: "      << nParam      << endl;
  cout << " Visualization: "      << visualization << endl;
  cout << endl;

  /***************************************************************************
   * Reading sequence.                                                       *
   ***************************************************************************/

  cv::VideoCapture decoder(sequence);

  if (!decoder.isOpened())
    throw runtime_error("Cannot open the '" + sequence + "' sequence.");

  int32_t height     = decoder.get(CV_CAP_PROP_FRAME_HEIGHT);
  int32_t width      = decoder.get(CV_CAP_PROP_FRAME_WIDTH);

  cout << "Reading sequence " << sequence << "..." << endl;

  cout << "          height: " << height     << endl;
  cout << "           width: " << width      << endl;

  typedef vector<cv::Mat>                                            FramesVec;
  vector<cv::Mat> frames;
  frames.reserve(decoder.get(CV_CAP_PROP_FRAME_COUNT));

  cv::Mat frame;

  while (decoder.read(frame))
    frames.push_back(frame.clone());

  decoder.release();
  cout << frames.size() << " frames read." << endl << endl;

  /***************************************************************************
   * Processing.                                                             *
   ***************************************************************************/

  cout << "Start processing..." << endl;

  /* Initialization of the background matrix. */
  cv::Mat background = cv::Mat(height, width, CV_8UC3);

  /* Initialization of the ROIs. */
  Utils::ROIs rois = Utils::getROIs(height, width); // Pixel-level.

  /* Initialization of the filter. */
  CounterMotionProba filter((min(height, width) / nParam) | 1);
  cout << "Size of the kernel: " << ((min(height, width) / nParam) | 1) << endl;

  /* Initialization of the maps matrices. */
  cv::Mat probabilityMap;
  cv::Mat filteredProbabilityMap;

  probabilityMap = cv::Mat(height, width, CV_32SC1);
  filteredProbabilityMap = cv::Mat(height, width, filter.getOpenCVEncoding());

  /* Initialization of the history structure. */
  boost::shared_ptr<PatchesHistory> history = boost::make_shared<PatchesHistory>(rois, sParam);

  /* Misc initializations. */
  boost::shared_ptr<FrameDifferenceC1L1> fdiff;// = NULL;
  bool firstFrame = true;
  int numFrame = -1;

  /* Processing loop. */
  cout << endl << "Processing...";

  for (FramesVec::const_iterator it = frames.begin(), end = frames.end(); it != end; ++it) {
    ++numFrame;

    /* Algorithm instantiation. */
    if (firstFrame)
      fdiff = make_shared<FrameDifferenceC1L1>();

    /* Background subtraction. */
    fdiff->process((*it).clone(), probabilityMap);

    /* Visualization of the input frame and its probability map. */
    if (visualization) {
      imshow("Input video", (*it));

      if (!probabilityMap.empty())
        imshow("Probability map", probabilityMap);
    }

    /* Skipping first frame. */
    if (firstFrame) {
      cout << "Skipping first frame..." << endl;

      ++it;
      firstFrame = false;

      continue;
    }

    /* Filtering probability map. */
    if (!probabilityMap.empty()) {
      filter.compute(probabilityMap, filteredProbabilityMap);

      if (visualization) {
        imshow("Filtered probability map", filteredProbabilityMap);
        cvWaitKey(1);
      }
    }

    /* Insert the current frame and its probability map into the history. */
    history->insert(filteredProbabilityMap, (*it));

    if (visualization) {
      history->median(background, sParam);

      imshow("Estimated background", background);
      cvWaitKey(1);
    }
  }

  /* Compute background and write it. */
  string outputFile =
    output + "/" + "output"      + "_"   +
    lexical_cast<string>(sParam) + "_"   +
    lexical_cast<string>(nParam) + ".png";

  /* Compute background and write it. */
  history->median(background, sParam);

  cout << "Writing " << outputFile << "..." << endl;
  cv::imwrite(outputFile, background);

  /* Cleaning. */
  if (visualization) {
    cout << endl << "Press any key to quit..." << endl;
    cvWaitKey(0);
    cvDestroyAllWindows();
  }

  /* Bye. */
  return EXIT_SUCCESS;
}