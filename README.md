# plugIN TinyML
## Introduction

This project aims to demonstrate the development of a basic plugin powered by Allxon Octo. The PlugIN TinyML is an Allxon Octo plugin that runs on Nvidia Orin Nano and works in conjunction with Arduino Nano BLE 33 to perform TinyML models for making inferences. In this sample project, a computer vision model is deployed on the Arduino board, which detects whether a person is sitting or if an empty chair is shown in front of the camera module. PlugIN TinyML streams the output data via the serial port from Arduino and establishes a channel between Allxon Portal and Orin Nano to interact with the data.

## Features

After the plugin is installed, you can access various feature cards on the Allxon Portal.

![](https://raw.githubusercontent.com/yo80106/plugIN-TinyML/main/screenshots/portal.png)

### States

The States card consists of two tabs - "Keyword" and "Data Stream." Under the "Keyword" tab, you will find the latest keyword received by the plugin. You can use this card to double-check whether the plugin has received the correct and fresh keyword from the Commands card.

As for the "Data Stream" tab, it indicates whether the serial port and output file have been successfully opened. Please note that if you decide not to save the data to a file (which you can specify in the Configs card), no output file will be generated, resulting in a blank space being shown.

### Configs

The Configs card features a checkbox that allows you to specify whether you want to save the output data from the Arduino to a log file. Additionally, there is an input box where you can enter the directory where the data will be saved. If you choose to leave the directory empty, the plugin will automatically save your log file to the current working directory (which you can check in the Properties card). Please note that the saving directory will be set by the plugin only if you have checked the "Save Log to File" checkbox.

### Commands

The Commands card features an input box that allows you to enter keywords for capturing specific data in the output. For instance, in this project, the meaningful lines in the output data are "Detecting result: chair" and "Detecting result: person." By specifying the keyword "chair," the plugin will search for instances of it within the output stream. This way, you can easily extract relevant information from the data.

### Alert Settings

The Alerts Settings card includes a checkbox that allows you to specify whether you wish to receive notifications when a keyword appears three times in a row. Please note that the plugin will continue counting the keyword in the data stream, regardless of whether you decide to save the data to a file or not. ***However, it is essential to note that this feature is currently undergoing testing and, as a result, has been temporarily disabled.***
