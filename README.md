# Raven
This is the main project repo for Raven. For instructions on running experiments, refer to the README.md file under the experiments/ directory.

## Build
The build script current works for Ubuntu 20.04 or Ubuntu 22.04. Execute the following to build Raven.
```bash
cd <raven_path>/src/thirdparty
./install.sh
cd ..
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```