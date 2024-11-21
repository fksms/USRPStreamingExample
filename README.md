# USRPStreamingTest
 
## How to build

### MacOS

Install uhd, cmake, pkg-config, and fftw
```
brew install uhd cmake pkg-config fftw
```

The FFTW installed via Homebrew does not include the `FFTW3LibraryDepends.cmake` file, causing `cmake` to fail. To resolve this, modify the following file (refer to [this PR](https://github.com/FFTW/fftw3/pull/338/files) as well).

- `/opt/homebrew/Cellar/fftw/X.X.X/lib/cmake/fftw3/FFTW3Config.cmake`
- `/opt/homebrew/Cellar/fftw/X.X.X/lib/cmake/fftw3/FFTW3fConfig.cmake`
- `/opt/homebrew/Cellar/fftw/X.X.X/lib/cmake/fftw3/FFTW3lConfig.cmake`
```diff
- include ("${CMAKE_CURRENT_LIST_DIR}/FFTW3LibraryDepends.cmake")
+ include ("${CMAKE_CURRENT_LIST_DIR}/FFTW3LibraryDepends.cmake" OPTIONAL)
```

Build (Release)
```
./build.sh
```

Build (Debug)
```
./build.sh debug
```