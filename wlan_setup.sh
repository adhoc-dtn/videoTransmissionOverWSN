#!/bin/bash

iwconfig wlan0 mode Ad-Hoc;
iwconfig wlan0 channel 03;
iwconfig wlan0 rate 54M;
iwconfig wlan0 essid scope;
iwconfig wlan0 ap 00:80:92:4e:37:4a;
ifconfig wlan0 up;
ifconfig wlan0 192.168.50.211;

