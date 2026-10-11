sudo chown -R $USER:$(id -gn) /mydata
sudo chmod 755 /mydata


./run_ducklakefootprint.sh 2>&1 | tee /mydata/benchmark_run.log