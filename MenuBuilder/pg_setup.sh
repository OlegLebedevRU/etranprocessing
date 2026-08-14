#!/bin/bash
sudo docker exec iot-rpc-rest-app-pg-1 psql -U postgres -c "ALTER USER etran WITH PASSWORD 'etran';"
sudo docker exec iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing -c "SELECT 1 as test;"
