/*
 * processConfig.h
 *
 *  Created on: Oct 2018
 *      Author: Norbert Podhorszki
 */

#include <algorithm>
#include <errno.h>
#include <iostream>
#include <iterator>
#include <sstream>

#include "decomp.h"
#include "processConfig.h"

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

bool isComment(std::string &s)
{
    bool comment = false;
    if (s[0] == '#' || s[0] == '%' || s[0] == '/')
    {
        comment = true;
    }
    return comment;
}

size_t getNumber(std::vector<std::string> &words, int pos, std::string lineID)
{
    if (words.size() < pos + 1)
    {
        throw std::invalid_argument(
            "Line for " + lineID +
            " is invalid. Missing value at word position " +
            std::to_string(pos + 1));
    }

    char *end;
    size_t n = static_cast<size_t>(std::strtoull(words[pos].c_str(), &end, 10));
    if (end[0] || errno == ERANGE)
    {
        throw std::invalid_argument("Invalid value given for " + lineID + ": " +
                                    words[pos]);
    }
    return n;
}

size_t processDecomp(std::string &word, const Settings &settings,
                     std::string decompID)
{
    size_t decomp = 1;
    std::string w(word);
    std::transform(w.begin(), w.end(), w.begin(), ::toupper);
    for (size_t i = 0; i < word.size(); i++)
    {
        char c = w[i];
        if (c == 'X')
        {
            decomp *= settings.processDecomp[0];
        }
        else if (c == 'Y')
        {
            decomp *= settings.processDecomp[1];
        }
        else if (c == 'Z')
        {
            decomp *= settings.processDecomp[2];
        }
        else if (c == 'V')
        {
            decomp *= settings.processDecomp[3];
        }
        else if (c == 'W')
        {
            decomp *= settings.processDecomp[4];
        }
        else if (c == '1')
        {
            decomp *= 1;
        }
        else
        {
            throw std::invalid_argument(
                "Invalid identifier '" + std::string(1, c) + "' for " +
                decompID + " in character position " + std::to_string(i + 1) +
                ". Only accepted characters are XYZVW and 1");
        }
    }
    return decomp;
}

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

OutputVariable processArray(std::vector<std::string> &words,
                            const Settings &settings)
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
        size_t d = processDecomp(words[4 + ov.ndim + i], settings,
                                 "decomposition " + std::to_string(i + 1));
        ov.decomp.push_back(d);
        nprocDecomp *= d;
    }
    if (nprocDecomp != settings.nProc)
    {
        throw std::invalid_argument(
            "Invalid decomposition for array '" + ov.name +
            "'. The product of the decompositions (here " +
            std::to_string(nprocDecomp) +
            ") must equal the number of processes (here " +
            std::to_string(settings.nProc) + ")");
    }
    return ov;
}

Config processConfig(const Settings &settings)
{
    unsigned int verbose0 =
        (settings.myRank ? 0 : settings.verbose); // only rank 0 prints info
    std::ifstream configFile(settings.configFileName);
    if (!configFile.is_open())
    {
        throw std::invalid_argument(settings.configFileName +
                                    " cannot be opened ");
    }
    Config cfg;
    std::vector<std::string> lines = FileToLines(configFile);
    for (auto &line : lines)
    {
        ++cfg.currentConfigLineNumber;
        if (verbose0 > 1)
        {
            std::cout << "        " << line << std::endl;
        }
        std::vector<std::string> words = LineToWords(line);
        if (!words.empty() && !isComment(words[0]))
        {
            std::string key(words[0]);
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            if (key == "steps")
            {
                cfg.nSteps = getNumber(words, 1, "steps");
                if (verbose0)
                {
                    std::cout << "-> Steps is set to: " << cfg.nSteps
                              << std::endl;
                }
            }
            else if (key == "sleep")
            {
                cfg.sleepInSeconds = getNumber(words, 1, "sleep");
                if (verbose0)
                {
                    std::cout << "-> Sleep is set to: " << cfg.sleepInSeconds
                              << " seconds" << std::endl;
                }
            }
            else if (key == "array")
            {
                // process config line and get global array info
                OutputVariable ov = processArray(words, settings);
                ov.datasize = ov.elemsize;
                size_t pos[ov.ndim]; // Position of rank in 5D space

                // Calculate rank's position in ndim-space
                decompRowMajor(ov.ndim, settings.myRank, ov.decomp.data(), pos);

                // Calculate the local size and offsets based on the definition
                for (size_t i = 0; i < ov.ndim; ++i)
                {
                    size_t count = ov.shape[i] / ov.decomp[i];
                    size_t offs = count * pos[i];
                    if (pos[i] == ov.decomp[i] - 1 && pos[i] != 0)
                    {
                        // last process in dim(i) need to write all the rest of
                        // dimension
                        count = ov.shape[i] - count * (pos[i] - 1);
                    }
                    ov.start.push_back(offs);
                    ov.count.push_back(count);
                    ov.datasize *= count;
                }

                // Allocate data array
                ov.data.resize(ov.datasize);

                cfg.variables.push_back(ov);
                if (verbose0)
                {
                    std::cout << "-> Variable array name = " << ov.name
                              << " type = " << ov.type
                              << " elemsize = " << ov.elemsize << std::endl;
                }
            }
            else
            {
                throw std::invalid_argument("Unrecognized keyword '" + key +
                                            "'.");
            }
        }
    }
    configFile.close();
    return cfg;
}
