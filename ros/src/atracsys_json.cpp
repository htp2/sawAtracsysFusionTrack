/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-    */
/* ex: set filetype=cpp softtabstop=4 shiftwidth=4 tabstop=4 cindent expandtab: */

/*
  Author(s):  Anton Deguet
  Created on: 2014-07-21

  (C) Copyright 2014-2016 Johns Hopkins University (JHU), All Rights Reserved.

--- begin cisst license - do not edit ---

This software is provided "as is" under an open source license, with
no warranty.  The complete license can be found in license.txt and
http://www.cisst.org/cisst/license.txt.

--- end cisst license ---
*/

#include <cisstCommon/cmnPath.h>
#include <cisstCommon/cmnUnits.h>
#include <cisstCommon/cmnCommandLineOptions.h>
#include <cisstCommon/cmnQt.h>
#include <cisstMultiTask/mtsTaskManager.h>
#include <sawAtracsysFusionTrack/mtsAtracsysFusionTrack.h>
#include <sawAtracsysFusionTrack/mtsAtracsysFusionTrackToolQtWidget.h>
#include <sawAtracsysFusionTrack/mtsAtracsysFusionTrackStrayMarkersQtWidget.h>

#include <cisst_ros_crtk/mts_ros_crtk_bridge.h>

#include <QApplication>
#include <QMainWindow>


int main(int argc, char * argv[])
{
    // log configuration
    cmnLogger::SetMask(CMN_LOG_ALLOW_ALL);
    cmnLogger::SetMaskFunction(CMN_LOG_ALLOW_ALL);
    cmnLogger::SetMaskDefaultLog(CMN_LOG_ALLOW_ALL);
    cmnLogger::SetMaskClassMatching("mtsAtracsysFusionTrack", CMN_LOG_ALLOW_ALL);
    cmnLogger::AddChannel(std::cerr, CMN_LOG_ALLOW_ERRORS_AND_WARNINGS);

    // create ROS node handle
    ros::init(argc, argv, "atracsys", ros::init_options::AnonymousName);
    ros::NodeHandle rosNodeHandle;

    // parse options
    cmnCommandLineOptions options;
    std::string jsonConfigFile = "";
    double rosPeriod = 10.0 * cmn_ms;
    double tfPeriod = 20.0 * cmn_ms;
    std::list<std::string> managerConfig;

    options.AddOptionOneValue("j", "json-config",
                              "json configuration file",
                              cmnCommandLineOptions::OPTIONAL_OPTION, &jsonConfigFile);
    options.AddOptionOneValue("p", "ros-period",
                              "period in seconds to read all tool positions (default 0.01, 10 ms, 100Hz).  There is no point to have a period higher than the tracker component",
                              cmnCommandLineOptions::OPTIONAL_OPTION, &rosPeriod);
    options.AddOptionOneValue("P", "tf-ros-period",
                              "period in seconds to read all components and broadcast tf2 (default 0.02, 20 ms, 50Hz).  There is no point to have a period higher than the arm component's period",
                              cmnCommandLineOptions::OPTIONAL_OPTION, &tfPeriod);
    options.AddOptionMultipleValues("m", "component-manager",
                                    "JSON files to configure component manager",
                                    cmnCommandLineOptions::OPTIONAL_OPTION, &managerConfig);
    options.AddOptionNoValue("D", "dark-mode",
                             "replaces the default Qt palette with darker colors");

    // check that all required options have been provided
    std::string errorMessage;
    if (!options.Parse(argc, argv, errorMessage)) {
        std::cerr << "Error: " << errorMessage << std::endl;
        options.PrintUsage(std::cerr);
        return -1;
    }
    std::string arguments;
    options.PrintParsedArguments(arguments);
    std::cout << "Options provided:" << std::endl << arguments << std::endl;

    // create the components
    mtsAtracsysFusionTrack * tracker = new mtsAtracsysFusionTrack("atracsys");
    tracker->Configure(jsonConfigFile);

    // add the components to the component manager
    mtsManagerLocal * componentManager = mtsComponentManager::GetInstance();
    componentManager->AddComponent(tracker);

    // ROS CRTK bridge
    mts_ros_crtk_bridge * crtk_bridge
        = new mts_ros_crtk_bridge("sensable_phantom_crtk_bridge", &rosNodeHandle);
    crtk_bridge->add_factory_source("atracsys", "Controller", rosPeriod, tfPeriod);
    componentManager->AddComponent(crtk_bridge);
    crtk_bridge->Connect();
    
    // create a Qt user interface
    QApplication application(argc, argv);
    cmnQt::QApplicationExitsOnCtrlC();
    if (options.IsSet("dark-mode")) {
        cmnQt::SetDarkMode();
    }

    // organize all widgets in a tab widget
    QTabWidget * tabWidget = new QTabWidget;

    // stray markers
    mtsAtracsysFusionTrackStrayMarkersQtWidget * strayMarkersWidget;
    strayMarkersWidget = new mtsAtracsysFusionTrackStrayMarkersQtWidget("StrayMarkers-GUI");
    strayMarkersWidget->Configure();
    componentManager->AddComponent(strayMarkersWidget);
    componentManager->Connect(strayMarkersWidget->GetName(), "Controller",
                              tracker->GetName(), "Controller");
    tabWidget->addTab(strayMarkersWidget, "Stray Markers");

    // tools
    std::string toolName;
    mtsAtracsysFusionTrackToolQtWidget * toolWidget;

    // configure all components
    for (size_t tool = 0; tool < tracker->GetNumberOfTools(); tool++) {
        toolName = tracker->GetToolName(tool);
        // Qt Widget
        toolWidget = new mtsAtracsysFusionTrackToolQtWidget(toolName + "-GUI");
        toolWidget->Configure();
        componentManager->AddComponent(toolWidget);
        componentManager->Connect(toolWidget->GetName(), "Tool",
                                  tracker->GetName(), toolName);
        tabWidget->addTab(toolWidget, toolName.c_str());
    }

    // custom user components
    if (!componentManager->ConfigureJSON(managerConfig)) {
        CMN_LOG_INIT_ERROR << "Configure: failed to configure component-manager, check cisstLog for error messages" << std::endl;
        return -1;
    }
    
    // create and start all components
    componentManager->CreateAllAndWait(5.0 * cmn_s);
    componentManager->StartAllAndWait(5.0 * cmn_s);

    // run Qt user interface
    tabWidget->show();
    application.exec();

    // kill all components and perform cleanup
    componentManager->KillAllAndWait(5.0 * cmn_s);
    componentManager->Cleanup();

    cmnLogger::Kill();

    return 0;
}
