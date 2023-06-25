**Disclaimer**: the system is under construction - API and data formats are subject to change without preserve any backward compatibility.

## Project's goal

Create a version control system which can effectively work both with small and extra large repositories.

## Build

To build the project cmake 3.20+ and C++ with C++23 support are required (g++-12, clang-16).

Create a build directory:
```
mkdir out && cd out
```

Generate a project with explicit version of compiler:
```
export CXX=g++-12 && export CC=gcc-12 && cmake -DCMAKE_BUILD_TYPE=Debug ..
```

Make a build:
```
make -j
```

Run the command line utility to see available subcommands:
```
./cmd/vcs
```

## Converting existing repository

An existing git repository can be converted into the vcs format. Run:

```
vcs git convert --git <repo-path>/.git <vcs-path>
```
