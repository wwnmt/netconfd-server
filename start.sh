#!/bin/bash

sudo ubusd &
disown
sudo chown `whoami` /run/ubus.sock
./bin/netconfd
