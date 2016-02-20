# byte-range-lock-cpp

Why was this project created? I was looking for a mechanism to lock specific regions of a shared resource such as a file, but I didn't find anything that was fit my needs. Please, take a look at the file **range_lock.hh** to understand how this mechanism works.

###Build requirements
* C++ >= 11
* C++ >= 14 is required for shared lock

###Compiling test
```
$ g++ --std=c++11 range_lock_test.cc -lpthread
```
or 
```
$ g++ --std=c++14 range_lock_test.cc -lpthread
```
