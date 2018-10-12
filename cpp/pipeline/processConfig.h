/*
 * processConfig.h
 *
 *  Created on: Oct 2018
 *      Author: Norbert Podhorszki
 */

#ifndef PROCESS_CONFIG_H
#define PROCESS_CONFIG_H

#include <map>
#include <string>
#include <vector>

#include "adios2.h"

#include "settings.h"

struct VariableInfo
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
    bool readFromInput;
};

enum Operation
{
    Sleep,
    Write,
    Read
};

class Command
{
public:
    Operation op;
    std::string conditionalGroup;
    Command(Operation operation);
    virtual ~Command() = 0;
};

class CommandSleep : public Command
{
public:
    const size_t sleepTime_us = 0; // in microseconds
    CommandSleep(size_t time);
    ~CommandSleep();
};

class CommandWrite : public Command
{
public:
    const std::string streamName;
    const std::string groupName;
    std::vector<VariableInfo *> variables;
    CommandWrite(std::string stream, std::string group);
    ~CommandWrite();
};

class CommandRead : public Command
{
public:
    const adios2::StepMode stepMode;
    const std::string streamName;
    const std::string groupName;
    const float timeout_sec;
    std::vector<VariableInfo *> variables;
    CommandRead(std::string stream, std::string group);
    ~CommandRead();
};

struct Config
{
    size_t nSteps = 1;
    // groupName, list of variables to preserve user defined order
    std::map<std::string, std::vector<VariableInfo>> groupVariableListMap;
    // same group/variables but in an ordered map for finding
    // a particular variable
    std::map<std::string, std::map<std::string, VariableInfo *>>
        groupVariablesMap;
    // appID, list of commands
    std::vector<std::shared_ptr<Command>> commands;
    size_t currentConfigLineNumber = 0;
};

const std::vector<std::pair<std::string, size_t>> supportedTypes = {
    {"double", sizeof(double)}, {"float", sizeof(float)}, {"int", sizeof(int)}};

Config processConfig(const Settings &settings);

#endif /* PROCESS_CONFIG_H */
