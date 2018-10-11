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

void defineADIOSArray(adios2::IO &io, const VariableInfo &ov)
{
    if (ov.type == "double")
    {
        adios2::Variable<double> v = io.DefineVariable<double>(
            ov.name, ov.shape, ov.start, ov.count, true);
        v = io.InquireVariable<double>(ov.name);
    }
    else if (ov.type == "float")
    {
        adios2::Variable<float> v = io.DefineVariable<float>(
            ov.name, ov.shape, ov.start, ov.count, true);
    }
    else if (ov.type == "int")
    {
        adios2::Variable<int> v =
            io.DefineVariable<int>(ov.name, ov.shape, ov.start, ov.count, true);
    }
}

void fillArray(VariableInfo &ov, double value)
{
    if (ov.type == "double")
    {
        double *a = reinterpret_cast<double *>(ov.data.data());
        for (size_t i = 0; i < ov.datasize / ov.elemsize; ++i)
        {
            a[i] = value;
        }
    }
    else if (ov.type == "float")
    {
        float v = static_cast<float>(value);
        float *a = reinterpret_cast<float *>(ov.data.data());
        for (size_t i = 0; i < ov.datasize / ov.elemsize; ++i)
        {
            a[i] = v;
        }
    }
    else if (ov.type == "int")
    {
        int v = static_cast<int>(value);
        int *a = reinterpret_cast<int *>(ov.data.data());
        for (size_t i = 0; i < ov.datasize / ov.elemsize; ++i)
        {
            a[i] = v;
        }
    }
}

void putADIOSArray(adios2::Engine &writer, const VariableInfo &ov)
{
    if (ov.type == "double")
    {
        const double *a = reinterpret_cast<const double *>(ov.data.data());
        writer.Put<double>(ov.name, a);
    }
    else if (ov.type == "float")
    {
        const float *a = reinterpret_cast<const float *>(ov.data.data());
        writer.Put<float>(ov.name, a);
    }
    else if (ov.type == "int")
    {
        const int *a = reinterpret_cast<const int *>(ov.data.data());
        writer.Put<int>(ov.name, a);
    }
}

void getADIOSArray(adios2::Engine &reader, adios2::IO &io, VariableInfo &ov)
{
    if (ov.type == "double")
    {
        adios2::Variable<double> v = io.InquireVariable<double>(ov.name);
        if (!v)
        {
            ov.readFromInput = false;
            return;
        }
        v.SetSelection({ov.start, ov.count});
        double *a = reinterpret_cast<double *>(ov.data.data());
        reader.Get<double>(v, a);
        ov.readFromInput = true;
    }
    else if (ov.type == "float")
    {
        adios2::Variable<float> v = io.InquireVariable<float>(ov.name);
        if (!v)
        {
            ov.readFromInput = false;
            return;
        }
        v.SetSelection({ov.start, ov.count});
        float *a = reinterpret_cast<float *>(ov.data.data());
        reader.Get<float>(v, a);
        ov.readFromInput = true;
    }
    else if (ov.type == "int")
    {
        adios2::Variable<int> v = io.InquireVariable<int>(ov.name);
        if (!v)
        {
            ov.readFromInput = false;
            return;
        }
        v.SetSelection({ov.start, ov.count});
        int *a = reinterpret_cast<int *>(ov.data.data());
        reader.Get<int>(v, a);
        ov.readFromInput = true;
    }
}

/* return true if read-in completed */
bool readADIOS(adios2::Engine &reader, adios2::IO &io,
               std::vector<VariableInfo> &variables, const Settings &settings,
               size_t step)
{
    enum adios2::StepStatus status = reader.BeginStep();
    if (status != adios2::StepStatus::OK)
    {
        return false;
    }

    if (!settings.myRank && settings.verbose && step == 1)
    {
        const auto varmap = io.AvailableVariables();
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

    for (VariableInfo &ov : variables)
    {
        getADIOSArray(reader, io, ov);
    }
    reader.EndStep();
    return true;
}

void writeADIOS(adios2::Engine &writer, Config &cfg,
                std::vector<VariableInfo> &variables, const Settings &settings,
                size_t step)
{
    const double div =
        pow(10.0, static_cast<const double>(settings.ndigits(cfg.nSteps - 1)));
    double myValue = static_cast<double>(settings.myRank) +
                     static_cast<double>(step - 1) / div;

    for (VariableInfo &ov : variables)
    {
        if (!ov.readFromInput)
        {
            if (!settings.myRank && settings.verbose)
            {
                std::cout << "    Fill array  " << ov.name << "  for output"
                          << std::endl;
            }
            fillArray(ov, myValue);
        }
    }

    if (!settings.myRank && settings.verbose)
    {
        std::cout << "    Write data " << std::endl;
    }
    writer.BeginStep();
    for (const VariableInfo &ov : variables)
    {
        putADIOSArray(writer, ov);
    }
    writer.EndStep();
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
            std::map<std::string, std::shared_ptr<adios2::IO>> ioMap;
            for (const auto &groupIt : cfg.groupVariablesMap)
            {
                auto io = std::make_shared<adios2::IO>(groupIt.first);
                ioMap[groupIt.first] = io;
            }

            std::map<std::string, std::shared_ptr<adios2::Engine>>
                readEngineMap;
            std::map<std::string, std::shared_ptr<adios2::Engine>>
                writeEngineMap;
            for (const auto cmd : cfg.commands)
            {
                if (cmd->op == Operation::Write)
                {
                    auto cmdW = dynamic_cast<CommandWrite *>(cmd.get());
                    auto engine = std::make_shared<adios2::Engine>();
                    writeEngineMap[cmdW->groupName] = engine;
                }
                if (cmd->op == Operation::Read)
                {
                    auto cmdR = dynamic_cast<CommandRead *>(cmd.get());
                    auto engine = std::make_shared<adios2::Engine>();
                    readEngineMap[cmdR->groupName] = engine;
                }
            }

            for (const auto cmd : cfg.commands)
            {
                switch (cmd->op)
                {
                case Operation::Sleep:
                {
                    auto cmdS = dynamic_cast<const CommandSleep *>(cmd.get());
                    std::cout << "        Sleep for " << cmdS->sleepTime_us
                              << " microseconds " << std::endl;
                    break;
                }
                case Operation::Write:
                {
                    auto cmdW = dynamic_cast<CommandWrite *>(cmd.get());
                    std::cout << "        Write to output " << cmdW->streamName
                              << " the group " << cmdW->groupName;
                    if (!cmdW->variables.empty())
                    {
                        std::cout << " with selected variables:  ";
                        for (const auto &v : cmdW->variables)
                        {
                            std::cout << v << " ";
                        }
                    }
                    std::cout << std::endl;
                    break;
                }
                case Operation::Read:
                {
                    auto cmdR = dynamic_cast<CommandRead *>(cmd.get());
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

            if (settings.doRead)
            {
                reader = inIO.Open(settings.inputName, adios2::Mode::Read,
                                   settings.appComm);
            }

            if (settings.doWrite)
            {
                // Define the ADIOS output variables
                for (const VariableInfo &ov : cfg.groupVariablesMap)
                {
                    defineADIOSArray(outIO, ov);
                }

                writer = outIO.Open(settings.outputName, adios2::Mode::Write,
                                    settings.appComm);

                if (!settings.myRank && settings.verbose)
                {
                    const auto varmap = outIO.AvailableVariables();
                    std::cout << "List of variables for writing: " << std::endl;
                    for (const auto &v : varmap)
                    {
                        std::cout << "        " << v.first << std::endl;
                    }
                }
            }

            if (settings.doRead)
            {
                /* Read as many steps as available and
                   then write as many as well */
                size_t step = 1;
                bool cont = true;
                while (cont)
                {
                    if (!settings.myRank)
                    {
                        std::cout << "Step " << step << ": " << std::endl;
                    }
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(cfg.sleepBeforeIO_us));

                    cont = readADIOS(reader, inIO, cfg, settings, step);

                    if (cont)
                    {
                        std::this_thread::sleep_for(std::chrono::microseconds(
                            cfg.sleepBetweenIandO_us));
                        if (settings.doWrite)
                        {
                            writeADIOS(writer, cfg, settings, step);
                        }
                        std::this_thread::sleep_for(
                            std::chrono::microseconds(cfg.sleepAfterIO_us));
                        ++step;
                    }
                    else if (!settings.myRank)
                    {
                        std::cout << "    No more steps from input "
                                  << std::endl;
                    }
                }
            }
            else if (settings.doWrite)
            {
                /* Write as many steps as specified in config file */
                for (size_t step = 1; step <= cfg.nSteps; ++step)
                {
                    if (!settings.myRank)
                    {
                        std::cout << "Step " << step << ": " << std::endl;
                    }

                    std::this_thread::sleep_for(std::chrono::microseconds(
                        cfg.sleepBeforeIO_us + cfg.sleepBetweenIandO_us));

                    writeADIOS(writer, cfg, settings, step);

                    std::this_thread::sleep_for(
                        std::chrono::microseconds(cfg.sleepAfterIO_us));
                }
            }

            if (settings.doRead)
            {
                reader.Close();
            }

            if (settings.doWrite)
            {
                writer.Close();
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
