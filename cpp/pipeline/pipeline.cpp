#include <chrono>
#include <cstdlib>
#include <iostream>
#include <math.h>
#include <string>
#include <thread>
#include <vector>

#include "adios2.h"
#include "mpi.h"

#include "decomp.h"
#include "processConfig.h"
#include "settings.h"

void defineADIOSArray(adios2::IO &io, const VariableInfo *ov)
{
    if (ov->type == "double")
    {
        adios2::Variable<double> v = io.DefineVariable<double>(
            ov->name, ov->shape, ov->start, ov->count, true);
        v = io.InquireVariable<double>(ov->name);
    }
    else if (ov->type == "float")
    {
        adios2::Variable<float> v = io.DefineVariable<float>(
            ov->name, ov->shape, ov->start, ov->count, true);
    }
    else if (ov->type == "int")
    {
        adios2::Variable<int> v = io.DefineVariable<int>(
            ov->name, ov->shape, ov->start, ov->count, true);
    }
}

void fillArray(VariableInfo *ov, double value)
{
    if (ov->type == "double")
    {
        double *a = reinterpret_cast<double *>(ov->data.data());
        for (size_t i = 0; i < ov->datasize / ov->elemsize; ++i)
        {
            a[i] = value;
        }
    }
    else if (ov->type == "float")
    {
        float v = static_cast<float>(value);
        float *a = reinterpret_cast<float *>(ov->data.data());
        for (size_t i = 0; i < ov->datasize / ov->elemsize; ++i)
        {
            a[i] = v;
        }
    }
    else if (ov->type == "int")
    {
        int v = static_cast<int>(value);
        int *a = reinterpret_cast<int *>(ov->data.data());
        for (size_t i = 0; i < ov->datasize / ov->elemsize; ++i)
        {
            a[i] = v;
        }
    }
}

void putADIOSArray(std::shared_ptr<adios2::Engine> writer,
                   const VariableInfo *ov)
{
    if (ov->type == "double")
    {
        const double *a = reinterpret_cast<const double *>(ov->data.data());
        writer->Put<double>(ov->name, a);
    }
    else if (ov->type == "float")
    {
        const float *a = reinterpret_cast<const float *>(ov->data.data());
        writer->Put<float>(ov->name, a);
    }
    else if (ov->type == "int")
    {
        const int *a = reinterpret_cast<const int *>(ov->data.data());
        writer->Put<int>(ov->name, a);
    }
}

void getADIOSArray(std::shared_ptr<adios2::Engine> reader,
                   std::shared_ptr<adios2::IO> io, VariableInfo *ov)
{
    if (ov->type == "double")
    {
        adios2::Variable<double> v = io->InquireVariable<double>(ov->name);
        if (!v)
        {
            ov->readFromInput = false;
            return;
        }
        v.SetSelection({ov->start, ov->count});
        double *a = reinterpret_cast<double *>(ov->data.data());
        reader->Get<double>(v, a);
        ov->readFromInput = true;
    }
    else if (ov->type == "float")
    {
        adios2::Variable<float> v = io->InquireVariable<float>(ov->name);
        if (!v)
        {
            ov->readFromInput = false;
            return;
        }
        v.SetSelection({ov->start, ov->count});
        float *a = reinterpret_cast<float *>(ov->data.data());
        reader->Get<float>(v, a);
        ov->readFromInput = true;
    }
    else if (ov->type == "int")
    {
        adios2::Variable<int> v = io->InquireVariable<int>(ov->name);
        if (!v)
        {
            ov->readFromInput = false;
            return;
        }
        v.SetSelection({ov->start, ov->count});
        int *a = reinterpret_cast<int *>(ov->data.data());
        reader->Get<int>(v, a);
        ov->readFromInput = true;
    }
}

/* return true if read-in completed */
bool readADIOS(std::shared_ptr<adios2::Engine> reader,
               std::shared_ptr<adios2::IO> io, CommandRead *cmdR, Config &cfg,
               const Settings &settings, size_t step)
{
    enum adios2::StepStatus status = reader->BeginStep();
    if (status != adios2::StepStatus::OK)
    {
        return false;
    }

    if (!settings.myRank && settings.verbose && step == 1)
    {
        const auto varmap = io->AvailableVariables();
        std::cout << "    Variables in input for reading: " << std::endl;
        for (const auto &v : varmap)
        {
            std::cout << "        " << v.first << std::endl;
        }
    }

    if (!settings.myRank && settings.verbose)
    {
        std::cout << "    Read data " << std::endl;
    }

    for (auto ov : cmdR->variables)
    {
        getADIOSArray(reader, io, ov);
    }
    reader->EndStep();
    return true;
}

void writeADIOS(std::shared_ptr<adios2::Engine> writer, CommandWrite *cmdW,
                Config &cfg, const Settings &settings, size_t step)
{
    if (!settings.myRank && settings.verbose)
    {
        std::cout << "        Write to output " << cmdW->streamName
                  << " the group " << cmdW->groupName << std::endl;
    }

    const double div =
        pow(10.0, static_cast<const double>(settings.ndigits(cfg.nSteps - 1)));
    double myValue = static_cast<double>(settings.myRank) +
                     static_cast<double>(step - 1) / div;

    for (auto ov : cmdW->variables)
    {
        if (!ov->readFromInput)
        {
            if (!settings.myRank && settings.verbose)
            {
                std::cout << "    Fill array  " << ov->name << "  for output"
                          << std::endl;
            }
            fillArray(ov, myValue);
        }
    }

    if (!settings.myRank && settings.verbose)
    {
        std::cout << "    Write data " << std::endl;
    }
    writer->BeginStep();
    for (const auto ov : cmdW->variables)
    {
        putADIOSArray(writer, ov);
    }
    writer->EndStep();
}

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    Settings settings;
    if (!settings.processArguments(argc, argv, MPI_COMM_WORLD) &&
        !settings.extraArgumentChecks())
    {
        adios2::ADIOS adios("adios2.xml", settings.appComm, adios2::DebugON);
        Config cfg;

        try
        {
            cfg = processConfig(settings);
        }
        catch (std::invalid_argument &e) // config file processing errors
        {
            if (!settings.myRank)
            {
                if (!cfg.currentConfigLineNumber)
                {
                    std::cout << "Config file error: " << e.what() << std::endl;
                }
                else
                {
                    std::cout << "Config file error in line "
                              << cfg.currentConfigLineNumber << ": " << e.what()
                              << std::endl;
                }
            }
            MPI_Finalize();
            return 0;
        }

        try
        {
            /* writing to one stream using two groups is not supported.
             * FIXME: we need to check for this condition and raise error
             */
            /* 1. Assign stream names with group names that appear in
               commands */
            // map of <streamName, groupName>
            std::map<std::string, std::string> ioUsedInWrite;
            std::map<std::string, std::string> ioUsedInRead;
            // a vector of streams in the order they appear
            std::vector<std::pair<std::string, Operation>> streamsInOrder;
            for (const auto &cmd : cfg.commands)
            {
                if (cmd->op == Operation::Write)
                {
                    auto cmdW = dynamic_cast<CommandWrite *>(cmd.get());
                    ioUsedInWrite[cmdW->streamName] = cmdW->groupName;
                    streamsInOrder.push_back(
                        std::make_pair(cmdW->streamName, Operation::Write));
                }
                else if (cmd->op == Operation::Read)
                {
                    auto cmdR = dynamic_cast<CommandRead *>(cmd.get());
                    ioUsedInRead[cmdR->streamName] = cmdR->groupName;
                    streamsInOrder.push_back(
                        std::make_pair(cmdR->streamName, Operation::Read));
                }
            }

            std::map<std::string, std::shared_ptr<adios2::IO>> ioMap;
            /*for (const auto &groupIt : cfg.groupVariablesMap)
            {
                auto io = std::make_shared<adios2::IO>(groupIt.first);
                ioMap[groupIt.first] = io;
            }*/

            /* 2. Declare/define groups and open streams in the order they
             * appear */
            std::map<std::string, std::shared_ptr<adios2::Engine>>
                readEngineMap;
            std::map<std::string, std::shared_ptr<adios2::Engine>>
                writeEngineMap;
            for (const auto &st : streamsInOrder)
            {
                const std::string &streamName = st.first;
                const bool isWrite = (st.second == Operation::Write);
                if (isWrite)
                {
                    auto &groupName = ioUsedInWrite[streamName];
                    adios2::IO io = adios.DeclareIO(groupName);
                    ioMap[groupName] = std::make_shared<adios2::IO>(io);
                    for (const auto &ov : cfg.groupVariableListMap[groupName])
                    {
                        defineADIOSArray(io, &ov);
                    }
                    adios2::Engine writer = io.Open(
                        streamName, adios2::Mode::Write, settings.appComm);
                    writeEngineMap[streamName] =
                        std::make_shared<adios2::Engine>(writer);
                    if (!settings.myRank && settings.verbose)
                    {
                        const auto varmap = io.AvailableVariables();
                        std::cout << "List of variables in group " << groupName
                                  << " for writing to: " << streamName
                                  << std::endl;
                        for (const auto &v : varmap)
                        {
                            std::cout << "        " << v.first << std::endl;
                        }
                    }
                }
                else /* Read */
                {
                    auto &groupName = ioUsedInRead[streamName];
                    adios2::IO io = adios.DeclareIO(groupName);
                    ioMap[groupName] = std::make_shared<adios2::IO>(io);
                    adios2::Engine reader = io.Open(
                        streamName, adios2::Mode::Read, settings.appComm);
                    readEngineMap[groupName] =
                        std::make_shared<adios2::Engine>(reader);
                }
            }

            /* Execute commands */
            for (size_t step = 1; step <= cfg.nSteps; ++step)
            {
                if (!settings.myRank)
                {
                    std::cout << "Step " << step << ": " << std::endl;
                }
                for (const auto cmd : cfg.commands)
                {
                    switch (cmd->op)
                    {
                    case Operation::Sleep:
                    {
                        auto cmdS =
                            dynamic_cast<const CommandSleep *>(cmd.get());
                        if (!settings.myRank && settings.verbose)
                        {
                            std::cout << "        Sleep for "
                                      << cmdS->sleepTime_us << " microseconds "
                                      << std::endl;
                        }
                        std::this_thread::sleep_for(
                            std::chrono::microseconds(cmdS->sleepTime_us));
                        break;
                    }
                    case Operation::Write:
                    {
                        auto cmdW = dynamic_cast<CommandWrite *>(cmd.get());
                        auto writer = writeEngineMap[cmdW->streamName];
                        writeADIOS(writer, cmdW, cfg, settings, step);
                        break;
                    }
                    case Operation::Read:
                    {
                        auto cmdR = dynamic_cast<CommandRead *>(cmd.get());
                        auto reader = readEngineMap[cmdR->streamName];
                        auto io = ioMap[cmdR->groupName];
                        readADIOS(reader, io, cmdR, cfg, settings, step);
                        std::cout << "        Read ";
                        if (cmdR->stepMode == adios2::StepMode::NextAvailable)
                        {
                            std::cout << "next available step from ";
                        }
                        else
                        {
                            std::cout << "latest step from ";
                        }

                        std::cout << cmdR->streamName << " using the group "
                                  << cmdR->groupName;
                        if (!cmdR->variables.empty())
                        {
                            std::cout << " with selected variables:  ";
                            for (const auto &v : cmdR->variables)
                            {
                                std::cout << v << " ";
                            }
                        }
                        std::cout << std::endl;
                        break;
                    }
                    }
                    std::cout << std::endl;
                }
            }

            /* Close all streams in order of opening */
            for (const auto &st : streamsInOrder)
            {
                const std::string &streamName = st.first;
                const bool isWrite = st.second;
                if (isWrite)
                {
                    auto writer = writeEngineMap[streamName];
                    writer->Close();
                }
                else /* Read */
                {
                    auto reader = readEngineMap[streamName];
                    reader->Close();
                }
            }
        }
        catch (std::exception &e) // config file processing errors
        {
            if (!settings.myRank)
            {
                std::cout << "ADIOS " << e.what() << std::endl;
            }
            MPI_Finalize();
            return 0;
        }
    }

    MPI_Finalize();
    return 0;
}
