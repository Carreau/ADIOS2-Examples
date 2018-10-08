/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * settings.h
 *
 *  Created on: Oct 2018
 *      Author: Norbert Podhorszki
 */

#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <fstream>
#include <string>
#include <vector>

#include <mpi.h>

class Settings
{

public:
    /* user arguments */
    std::string configFileName;
    bool doRead = false;
    std::string inputName;
    bool doWrite = false;
    std::string outputName;
    unsigned int verbose = 0;
    size_t appId = 0;
    //   process decomposition
    std::vector<size_t> processDecomp = {1, 1, 1, 1, 1, 1, 1, 1,
                                         1, 1, 1, 1, 1, 1, 1, 1};

    /* public variables */
    MPI_Comm appComm = MPI_COMM_WORLD; // will change to split communicator
    size_t myRank = 0;
    size_t nProc = 1;
    std::ifstream configFile;
    size_t nDecomp = 0;

    Settings();
    int processArguments(int argc, char *argv[], MPI_Comm worldComm);
    int extraArgumentChecks();
    size_t stringToNumber(const std::string &varName, const char *arg);

private:
    void displayHelp();
    int processArgs(int argc, char *argv[]);
};

#endif /* SETTINGS_H_ */
