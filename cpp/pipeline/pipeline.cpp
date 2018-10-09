
#include <cstdlib>
#include <iostream>
#include <math.h>
#include <string>
#include <vector>

#include "adios2.h"
#include "mpi.h"

#include "decomp.h"
#include "processConfig.h"
#include "settings.h"

void defineADIOSArray(adios2::IO &io, const OutputVariable &ov)
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

void fillArray(OutputVariable &ov, double value)
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

void putADIOSArray(adios2::Engine &writer, const OutputVariable &ov)
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

void getADIOSArray(adios2::Engine &reader, adios2::IO &io, OutputVariable &ov)
{
    if (ov.type == "double")
    {
        adios2::Variable<double> v = io.InquireVariable<double>(ov.name);
        if (!v)
        {
            ov.availableInInput = false;
            return;
        }
        v.SetSelection({ov.start, ov.count});
        double *a = reinterpret_cast<double *>(ov.data.data());
        reader.Get<double>(v, a);
        ov.availableInInput = true;
    }
    else if (ov.type == "float")
    {
        adios2::Variable<float> v = io.InquireVariable<float>(ov.name);
        if (!v)
        {
            ov.availableInInput = false;
            return;
        }
        v.SetSelection({ov.start, ov.count});
        float *a = reinterpret_cast<float *>(ov.data.data());
        reader.Get<float>(v, a);
        ov.availableInInput = true;
    }
    else if (ov.type == "int")
    {
        adios2::Variable<int> v = io.InquireVariable<int>(ov.name);
        if (!v)
        {
            ov.availableInInput = false;
            return;
        }
        v.SetSelection({ov.start, ov.count});
        int *a = reinterpret_cast<int *>(ov.data.data());
        reader.Get<int>(v, a);
        ov.availableInInput = true;
    }
}

void readADIOS(adios2::Engine &reader, adios2::IO &io, Config &cfg,
               const Settings &settings, size_t step)
{
    reader.BeginStep();

    if (!settings.myRank && settings.verbose && step == 1)
    {
        const auto varmap = io.AvailableVariables();
        std::cout << "    Variables in input for reading: " << std::endl;
        for (const auto &v : varmap)
        {
            std::cout << "        " << v.first << std::endl;
        }
    }

    for (OutputVariable &ov : cfg.variables)
    {
        getADIOSArray(reader, io, ov);
    }
    reader.EndStep();
}

void writeADIOS(adios2::Engine &writer, Config &cfg, const Settings &settings,
                size_t step)
{
    const double div =
        pow(10.0, static_cast<const double>(settings.ndigits(cfg.nSteps - 1)));
    double myValue = static_cast<double>(settings.myRank) +
                     static_cast<double>(step - 1) / div;

    for (OutputVariable &ov : cfg.variables)
    {
        if (!ov.availableInInput)
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
    for (const OutputVariable &ov : cfg.variables)
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
            adios2::IO inIO = adios.DeclareIO("PipelineInput");
            adios2::IO outIO = adios.DeclareIO("PipelineOutput");
            adios2::Engine reader;
            adios2::Engine writer;

            if (settings.doRead)
            {
                reader = inIO.Open(settings.inputName, adios2::Mode::Read,
                                   settings.appComm);
            }

            if (settings.doWrite)
            {
                // Define the ADIOS output variables
                for (const OutputVariable &ov : cfg.variables)
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

            for (size_t step = 1; step <= cfg.nSteps; ++step)
            {
                if (!settings.myRank)
                {
                    std::cout << "Step " << step << ": " << std::endl;
                }
                if (settings.doRead)
                {
                    readADIOS(reader, inIO, cfg, settings, step);
                }
                if (settings.doWrite)
                {
                    writeADIOS(writer, cfg, settings, step);
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
