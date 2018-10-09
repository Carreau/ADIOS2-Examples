/*
 * processConfig.h
 *
 *  Created on: Oct 2018
 *      Author: Norbert Podhorszki
 */

#ifndef PROCESS_CONFIG_H
#define PROCESS_CONFIG_H

#include <string>
#include <vector>

#include "adios2.h"

#include "settings.h"

struct OutputVariable
{
    std::string name;
    std::string type;
    adios2::ShapeID shapeID;
    size_t ndim;
    adios2::Dims shape;
    adios2::Dims decomp;
    adios2::Dims start;
    adios2::Dims count;
    size_t elemsize;
    size_t datasize;
    std::vector<char> data;
    bool availableInInput;
};

struct Config
{
    size_t nSteps = 1;
    size_t sleepBeforeIO_us = 0; // in microseconds
    size_t sleepBetweenIandO_us = 0;
    size_t sleepAfterIO_us = 0;
    std::vector<OutputVariable> variables;
    size_t currentConfigLineNumber = 0;
};

const std::vector<std::pair<std::string, size_t>> supportedTypes = {
    {"double", sizeof(double)}, {"float", sizeof(float)}, {"int", sizeof(int)}};

Config processConfig(const Settings &settings);

#endif /* PROCESS_CONFIG_H */
