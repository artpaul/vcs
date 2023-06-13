**Disclaimer**: the system is under construction - API and data formats are subject to change without preserve any backward compatibility.

## Цель проекта

Создать систему контроля версий, которая бы эффективно работала как с небольшими репозиториями, так и с репозиториями объёмом в сотни гигабайт и более.


## Собрать проект

Для сборки проекта необходимы cmake версии 3.20+ и компилятор C++ с поддержкой C++23 (g++-12, clang-16).

Создать директорию для сборочных файлов и перейти в неё:
```
mkdir out && cd out
```

Выполнить генерацию проекта с указанием версии компилятора. Например, для g++-12:
```
export CXX=g++-12 && export CC=gcc-12 && cmake -DCMAKE_BUILD_TYPE=Debug ..
```

Выполнить сборку:
```
make -j
```

Запустить консольную утилиту:
```
./cmd/vcs
```
