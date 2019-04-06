# RifDock
RifDock Library for Conformational Search

Ideally RifDock should be merged into Rosetta, but no one has stepped up to this herculean task yet.

***Building***

RifDock links against Rosetta. While RifDock may be compatible with the bleeding edge version of Rosetta, the last known-good build can be found on the following branch:

<b>Last known-good Rosetta branch</b>: bcov/stable1

To build RifDock:

Optain a copy of gcc with version >= 4.9 (I have no idea if this is the minimum version)

Build a Rosetta cxx11_omp build with:  
```bash
cd rosetta/main/source  
CXX=/my/g++/version CC=/my/gcc/version ./ninja_build cxx11_omp -remake  
```

Clone this repository and perform:  
```bash
cd rifdock  
mkdir build  
cd build  
CXX=/my/g++/version CC=/my/gcc/version CMAKE_ROSETTA_PATH=/Path/to/a/rosetta/main cmake .. -DCMAKE_BUILD_TYPE=Release  
make -j3 rif_dock_test rifgen  
```

There is an optional CMAKE flag if you do did not link against the standard cxx11_omp build to specify which build you did use. That flag is as follows:  
```bash
CMAKE_FINAL_ROSETTA_PATH=/Path/to/a/rosetta/main/source/cmake/build_my_custom_build_type  
```

Use this flag in addition to the CMAKE_ROSETTA_PATH flag.

Unit tests may be built with:  
```bash
make test_libscheme  
```

***Running***

The executables for RifDock are built at:  
```bash
scheme/build/apps/rosetta/rifgen  
scheme/build/apps/rosetta/rif_dock_test  
```

The unit test executable is at:  
```bash
scheme/build/schemelib/test/test_libscheme  
```


***Modifying***

RifDock is licenced under the Apache 2 License which can be found at rifdock/LICENSE.

