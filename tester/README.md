# OS HW 5 Tester
## Setup
1. Make sure you have python3 and pip3 installed:
```bash
sudo apt update && sudo apt install python3 python3-pip
```
2. Make sure you have pytest installed:
```bash
pip3 install pytest
```
3. If you wish to run the memory usage test, install `psutil` as well using:
```bash
pip3 install psutil
```
4. Download the test script to your project's directory.
## Runnning the Test
```bash
python3 -m pytest -v
```