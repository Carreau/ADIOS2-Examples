#include <algorithm>
#include <cstdlib>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "adios2.h"
#include "mpi.h"

std::vector<std::string> FileToLines(std::ifstream &configfile)
{
    std::vector<std::string> lines;
    for (std::string line; std::getline(configfile, line); /**/)
    {
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> LineToWords(const std::string &line)
{
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::copy(std::istream_iterator<std::string>(iss),
              std::istream_iterator<std::string>(), back_inserter(tokens));
    return tokens;
}

void printUsage()
{
    std::cout << "Usage: dataGenerator  config  output  X Y Z V W\n"
              << "  config:  data specification config file\n"
              << "  output:  generated data output file/stream\n"
              << "  X:       number of processes in 1st (slowest) dimension\n"
              << "  Y:       number of processes in 2nd dimension\n"
              << "  Z:       number of processes in 3rd dimension\n"
              << "  V:       number of processes in 4th dimension\n"
              << "  W:       number of processes in 5th dimension\n\n"
              << "  X*Y*Z*V*W must equal the number of processes\n\n";
}

size_t argToNumber(const std::string &varName, const char *arg)
{
    char *end;
    size_t retval = static_cast<size_t>(std::strtoull(arg, &end, 10));
    if (end[0] || errno == ERANGE)
    {
        throw std::invalid_argument("Invalid value given for " + varName +
                                    ": " + std::string(arg));
    }
    return retval;
}

std::string configfilename;
std::ifstream configfile;
std::string outputfilename;
size_t X = 1;
size_t Y = 1;
size_t Z = 1;
size_t V = 1;
size_t W = 1;
size_t myRank;
size_t nProc;

void processArgs(const int argc, char *argv[])
{
    if (argc < 3)
    {
        throw std::invalid_argument("Not enough arguments");
    }

    configfilename = argv[1];
    outputfilename = argv[2];
    if (argc >= 4)
    {
        X = argToNumber("X", argv[3]);
    }
    if (argc >= 5)
    {
        Y = argToNumber("Y", argv[4]);
    }
    if (argc >= 6)
    {
        Z = argToNumber("Z", argv[5]);
    }
    if (argc >= 7)
    {
        V = argToNumber("V", argv[6]);
    }
    if (argc >= 8)
    {
        W = argToNumber("W", argv[7]);
    }

    if (X * Y * Z * V * W != nProc)
    {
        throw std::invalid_argument(
            "X*Y*Z*V*W = " + std::to_string(X * Y * Z * V * W) +
            " must equal the number of processes = " + std::to_string(nProc));
    }

    configfile.open(configfilename);
    if (!configfile.is_open())
    {
        throw std::invalid_argument(configfilename + " cannot be opened ");
    }
}

bool isComment(std::string &s)
{
    bool comment = false;
    if (s[0] == '#' || s[0] == '%' || s[0] == '/')
    {
        comment = true;
    }
    return comment;
}

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
};

struct Config
{
    size_t nSteps;
    size_t sleepInSeconds;
    std::vector<OutputVariable> variables;
};

size_t getNumber(std::vector<std::string> &words, int pos, std::string lineID)
{
    size_t n;
    if (words.size() < pos + 1)
    {
        throw std::invalid_argument(
            "Line for " + lineID +
            " is invalid. Missing value at word position " +
            std::to_string(pos + 1));
    }
    n = argToNumber(lineID, words[pos].c_str());
    return n;
}

size_t processDecomp(std::string &word, std::string decompID)
{
    size_t decomp = 1;
    std::string w(word);
    std::transform(w.begin(), w.end(), w.begin(), ::toupper);
    for (size_t i = 0; i < word.size(); i++)
    {
        char c = word[i];
        if (c == 'X')
        {
            decomp *= X;
        }
        else if (c == 'Y')
        {
            decomp *= Y;
        }
        else if (c == 'Z')
        {
            decomp *= Z;
        }
        else if (c == 'V')
        {
            decomp *= V;
        }
        else if (c == 'W')
        {
            decomp *= W;
        }
        else
        {
            throw std::invalid_argument(
                "Invalid identifier '" + std::string(1, c) + "' for " +
                decompID + " in character position " + std::to_string(i + 1) +
                ". Only accepted characters are XYZVW");
        }
    }
    return decomp;
}

const std::vector<std::pair<std::string, size_t>> supportedTypes = {
    {"double", sizeof(double)}, {"float", sizeof(float)}, {"int", sizeof(int)}};

size_t getTypeSize(std::string &type)
{
    for (const auto &t : supportedTypes)
    {
        if (t.first == type)
        {
            return t.second;
        }
    }
    throw std::invalid_argument("Type '" + type + "' is invalid. ");
}

OutputVariable processArray(std::vector<std::string> &words)
{
    if (words.size() < 4)
    {
        throw std::invalid_argument("Line for array definition is invalid. "
                                    "There must be at least 4 words "
                                    "in the line (array type name ndim)");
    }
    OutputVariable ov;
    ov.shapeID = adios2::ShapeID::GlobalArray;
    ov.type = words[1];
    ov.elemsize = getTypeSize(ov.type);
    ov.name = words[2];
    ov.ndim = getNumber(words, 3, "number of dimensions of array " + ov.name);

    if (words.size() < 4 + 2 * ov.ndim)
    {
        throw std::invalid_argument(
            "Line for array definition is invalid. "
            "There must be at least 4 + 2*N words where N is the 4th word ndim "
            "in the line (array type name ndim dim1 ... dimN decomp1 ... "
            "decompN)");
    }

    for (size_t i = 0; i < ov.ndim; i++)
    {
        ov.shape.push_back(
            getNumber(words, 4 + i, "dimension " + std::to_string(i + 1)));
    }

    size_t nprocDecomp = 1;
    for (size_t i = 0; i < ov.ndim; i++)
    {
        size_t d = processDecomp(words[4 + ov.ndim + i],
                                 "decomposition " + std::to_string(i + 1));
        ov.decomp.push_back(d);
        nprocDecomp *= d;
    }
    if (nprocDecomp != nProc)
    {
        throw std::invalid_argument(
            "Invalid decomposition for array '" + ov.name +
            "'. The product of the decompositions (here " +
            std::to_string(nprocDecomp) +
            ") must equal the number of processes (here " +
            std::to_string(nProc) + ")");
    }
    return ov;
}

void defineADIOSArray(adios2::IO &io, OutputVariable &ov)
{
    std::cout << "Define Variable '" << ov.name << "' with type " << ov.type
              << std::endl;
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

size_t currentConfigLineNumber = 0;
Config processConfig(adios2::IO &io, bool verbose)
{
    verbose &= !myRank; // only rank 0 prints info
    Config cfg;
    std::vector<std::string> lines = FileToLines(configfile);
    for (auto &line : lines)
    {
        ++currentConfigLineNumber;
        if (verbose)
        {
            std::cout << line << std::endl;
        }
        std::vector<std::string> words = LineToWords(line);
        if (!words.empty() && !isComment(words[0]))
        {
            std::string key(words[0]);
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            if (key == "steps")
            {
                cfg.nSteps = getNumber(words, 1, "steps");
                if (verbose)
                {
                    std::cout << "-> Steps is set to: " << cfg.nSteps
                              << std::endl;
                }
            }
            else if (key == "sleep")
            {
                cfg.sleepInSeconds = getNumber(words, 1, "sleep");
                if (verbose)
                {
                    std::cout << "-> Sleep is set to: " << cfg.sleepInSeconds
                              << " seconds" << std::endl;
                }
            }
            else if (key == "array")
            {
                // process config line and get global array info
                OutputVariable ov = processArray(words);
                ov.datasize = ov.elemsize;
                size_t pos[5] = {0, 0, 0, 0, 0}; // Position of rank in 5D space

                // FIXME: Calculate rank's position in 5D space
                pos[0] = myRank % X;
                pos[1] = myRank / X;
                pos[2] = 0;
                pos[3] = 0;
                pos[4] = 0;

                // Calculate the local size and offsets based on the definition
                for (size_t i = 0; i < ov.ndim; ++i)
                {
                    size_t count = ov.shape[i] / ov.decomp[i];
                    size_t offs = count * pos[i];
                    if (pos[i] == ov.decomp[i] - 1)
                    {
                        // last process in dim(i) need to write all the rest of
                        // dimension
                        count = ov.decomp[i] - count * (pos[i] - 1);
                    }
                    ov.start.push_back(offs);
                    ov.count.push_back(count);
                    ov.datasize *= count;
                }

                // Allocate data array
                ov.data.resize(ov.datasize);

                // Define the ADIOS output variable
                defineADIOSArray(io, ov);
                fillArray(ov, static_cast<double>(myRank));

                cfg.variables.push_back(ov);
                if (verbose)
                {
                    std::cout << "-> Variable array name = " << ov.name
                              << " type = " << ov.type
                              << " elemsize = " << ov.elemsize << std::endl;
                }
            }
        }
    }
    configfile.close();
    return cfg;
}

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);
    int wrank, wnproc;
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    MPI_Comm_size(MPI_COMM_WORLD, &wnproc);

    const unsigned int color = 2;
    MPI_Comm mpiGeneratorComm;
    MPI_Comm_split(MPI_COMM_WORLD, color, wrank, &mpiGeneratorComm);

    int rank, size;
    MPI_Comm_rank(mpiGeneratorComm, &rank);
    MPI_Comm_size(mpiGeneratorComm, &size);
    myRank = static_cast<size_t>(rank);
    nProc = static_cast<size_t>(size);

    try
    {
        processArgs(argc, argv);
    }
    catch (std::invalid_argument &e) // command-line argument errors
    {
        if (!myRank)
        {
            std::cout << "ERROR : " << e.what() << std::endl;
            printUsage();
        }
        MPI_Finalize();
        return 0;
    }

    adios2::ADIOS adios("adios2.xml", mpiGeneratorComm, adios2::DebugON);
    adios2::IO outIO = adios.DeclareIO("DataGeneratorOutput");

    Config cfg;
    try
    {
        cfg = processConfig(outIO, true);
    }
    catch (std::invalid_argument &e) // config file processing errors
    {
        if (!myRank)
        {
            std::cout << "Config file error in line " << currentConfigLineNumber
                      << ": " << e.what() << std::endl;
        }
        MPI_Finalize();
        return 0;
    }

    try
    {
        adios2::Engine writer =
            outIO.Open(outputfilename, adios2::Mode::Write, mpiGeneratorComm);

        if (!myRank)
        {
            const auto varmap = outIO.AvailableVariables();
            std::cout << "Defined variables for writing: " << std::endl;
            for (const auto &v : varmap)
            {
                std::cout << "        " << v.first << std::endl;
            }
        }

        adios2::Variable<double> v =
            outIO.InquireVariable<double>(cfg.variables[0].name);
        if (v)
        {
            std::cout << "Variable " << cfg.variables[0].name << " shape"
                      << v.Shape()[0] << "x" << v.Shape()[1] << std::endl;
        }
        for (size_t step = 1; step < cfg.nSteps; ++step)
        {
            if (!myRank)
            {
                std::cout << "Write step " << step << std::endl;
            }
            writer.BeginStep();
            for (const auto &ov : cfg.variables)
            {
                putADIOSArray(writer, ov);
            }
            writer.EndStep();
        }
        writer.Close();
    }
    catch (std::exception &e) // config file processing errors
    {
        if (!myRank)
        {
            std::cout << "ADIOS " << e.what() << std::endl;
        }
        MPI_Finalize();
        return 0;
    }

    MPI_Finalize();
    return 0;
}
