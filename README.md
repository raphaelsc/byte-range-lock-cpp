# range-lock

Why was this project created? I was looking for a mechanism to lock specific regions of a shared resource such as a file, but I didn't find anything that was fit my needs.


```
$ g++ --std=c++11 main.cc -lpthread
$ ./a.out
Range lock granularity: 32768
Trying to lock [0, 1024)
Locked [0, 1024) properly, sleeping for 1 second...
Trying to lock [0, 1024*1024)
Unlocked [0, 1024) properly
Locked [0, 1024*1024) properly
Unlocked [0, 1024*1024) properly
```
