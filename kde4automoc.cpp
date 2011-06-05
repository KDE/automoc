/*
    Copyright (C) 2007 Matthias Kretz <kretz@kde.org>
                  2011 Gregory Schlomoff <gregory.schlomoff@gmail.com>

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <iostream>
#include <fstream>
#include <sstream>
#include <assert.h>
#include <sys/stat.h>
#include <set>
#include <map>
#include <cstdlib>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <sys/utime.h>
#else
#include <utime.h>
#endif

#if defined(__APPLE__)
#include <unistd.h>
#endif

#include "cmSystemTools.h"
#include "cmsys/RegularExpression.hxx"
#include "cmsys/SystemTools.hxx"

// currently this is only used for the version number, Alex
#include "automoc4_config.h"

class AutoMoc
{
    public:
        AutoMoc();
        bool run(int argc, char **argv);

    private:
        void dotFilesCheck(bool);
        void lazyInitMocDefinitions();
        void lazyInit();
        bool touch(const std::string &filename);
        bool generateMoc(const std::string &sourceFile, const std::string &mocFileName);
        void printUsage(const std::string &);
        void printVersion();
        void echoColor(const std::string &msg)
        {
            std::vector<cmStdString> command;
            command.push_back(cmakeExecutable);
            for (std::list<std::string>::const_iterator it = cmakeEchoColorArgs.begin();
                 it != cmakeEchoColorArgs.end(); ++it) {
                command.push_back(*it);
            }
            command.push_back(msg);
            cmSystemTools::RunSingleCommand(command);
        }

        // Helper functions to make code clearer
        std::string readAll(const std::string &filename);
        std::list<std::string> split(const std::string &input, char delimiter);
        std::string join(const std::list<std::string> lst, char separator);
        bool endsWith(const std::string &str, const std::string &with);
        bool startsWith(const std::string &str, const std::string &with);
        void trim(std::string &s);

        int argc;
        char **argv;
        std::string builddir;
        std::string mocExe;
        std::list<std::string> mocIncludes;
        std::list<std::string> mocDefinitions;
        std::list<std::string> cmakeEchoColorArgs;
        std::string cmakeExecutable;
        std::string dotFilesName;
        std::ifstream dotFiles;
        const bool verbose;
        bool failed;
        bool automocCppChanged;
        bool generateAll;
        bool doTouch;
};

void AutoMoc::printUsage(const std::string &path)
{
    std::cout << "Usage: " << path << " <outfile> <srcdir> <builddir> <moc executable> <cmake executable> [--touch]" << std::endl;
}

void AutoMoc::printVersion()
{
    std::cout << "automoc4 " << AUTOMOC4_VERSION << std::endl;
}

void AutoMoc::dotFilesCheck(bool x)
{
    if (!x) {
        std::cerr << "Error: syntax error in " << dotFilesName << std::endl;
        ::exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    if (!AutoMoc().run(argc, argv)) {
        return EXIT_FAILURE;
    }
    return 0;
}

AutoMoc::AutoMoc()
    : verbose(cmsys::SystemTools::GetEnv("VERBOSE") != 0), failed(false),
    automocCppChanged(false), generateAll(false), doTouch(false)
{
    std::string colorEnv = "";
    cmsys::SystemTools::GetEnv("COLOR", colorEnv);
    cmakeEchoColorArgs.push_back("-E");
    cmakeEchoColorArgs.push_back("cmake_echo_color");
    cmakeEchoColorArgs.push_back("--switch=" + colorEnv);
    cmakeEchoColorArgs.push_back("--blue");
    cmakeEchoColorArgs.push_back("--bold");
}

void AutoMoc::lazyInitMocDefinitions()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    std::string line;
    std::getline(dotFiles, line);
    dotFilesCheck(line == "MOC_COMPILE_DEFINITIONS:");
    std::getline(dotFiles, line);
    trim(line);
    const std::list<std::string> &cdefList = split(line, ';');
    std::getline(dotFiles, line);
    dotFilesCheck(line == "MOC_DEFINITIONS:");
    std::getline(dotFiles, line);
    trim(line);
    if (!cdefList.empty()) {
        for(std::list<std::string>::const_iterator it = cdefList.begin(); it != cdefList.end(); ++it)
        {
            assert(!it->empty());
            mocDefinitions.push_back("-D" + (*it));
        }
    } else {
        const std::list<std::string> &defList = split(line, ' ');
        for(std::list<std::string>::const_iterator it = defList.begin(); it != defList.end(); ++it) {
            assert(!it->empty());
            if (startsWith(*it, "-D")) {
                mocDefinitions.push_back(*it);
            }
        }
    }
}

void AutoMoc::lazyInit()
{
    mocExe = argv[4];
    cmakeExecutable = argv[5];

    if (argc > 6) {
        if (argv[6] == "--touch") {
            doTouch = true;
        }
    }

    lazyInitMocDefinitions();

    std::string line;
    std::getline(dotFiles, line);
    dotFilesCheck(line == "MOC_INCLUDES:");
    std::getline(dotFiles, line);
    trim(line);
    const std::list<std::string> &incPaths = split(line, ';');
    std::set<std::string> frameworkPaths;
    for(std::list<std::string>::const_iterator it = incPaths.begin(); it != incPaths.end(); ++it) {
        const std::string &path = *it;
        assert(!path.empty());
        mocIncludes.push_back("-I" + path);
        if (endsWith(path, ".framework/Headers")) {
            // Go up twice to get to the framework root
            std::vector<std::string> pathComponents;
            cmsys::SystemTools::SplitPath(path.c_str(), pathComponents);
            std::string frameworkPath = cmsys::SystemTools::JoinPath(pathComponents.begin(), pathComponents.end() - 2);
            frameworkPaths.insert(frameworkPath);
        }
    }

    for (std::set<std::string>::const_iterator it = frameworkPaths.begin();
         it != frameworkPaths.end(); ++it) {
        mocIncludes.push_back("-F");
        mocIncludes.push_back(*it);
    }

    std::getline(dotFiles, line);
    dotFilesCheck(line == "CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE:");
    std::getline(dotFiles, line);
    if (line == "ON") {
        std::getline(dotFiles, line);
        dotFilesCheck(line == "CMAKE_BINARY_DIR:");
        std::getline(dotFiles, line);
        trim(line);
        const std::string &binDir = "-I" + line;

        std::getline(dotFiles, line);
        dotFilesCheck(line == "CMAKE_SOURCE_DIR:");
        std::getline(dotFiles, line);
        trim(line);
        const std::string &srcDir = "-I" + line;

        std::list<std::string> sortedMocIncludes;
        std::list<std::string>::iterator it = mocIncludes.begin();
        while (it != mocIncludes.end()) {
            if (startsWith(*it, binDir)) {
                sortedMocIncludes.push_back(*it);
                it = mocIncludes.erase(it);
            } else {
                ++it;
            }
        }
        it = mocIncludes.begin();
        while (it != mocIncludes.end()) {
            if (startsWith(*it, srcDir)) {
                sortedMocIncludes.push_back(*it);
                it = mocIncludes.erase(it);
            } else {
                ++it;
            }
        }
        sortedMocIncludes.insert(sortedMocIncludes.end(), mocIncludes.begin(), mocIncludes.end());
        mocIncludes = sortedMocIncludes;
    }
}

bool AutoMoc::run(int _argc, char **_argv)
{
    assert(_argc > 0);

    argc = _argc;
    argv = _argv;

    if (argc == 2) {
        if ((argv[1]=="--help") || (argv[1]=="-h")) {
        printUsage(argv[0]);
       ::exit(0);
        }
        else if (argv[1]=="--version") {
        printVersion();
       ::exit(0);
        }
        else {
        printUsage(argv[0]);
       ::exit(EXIT_FAILURE);
        }
    }
    else if (argc < 6) {
        printUsage(argv[0]);
       ::exit(EXIT_FAILURE);
    }
    std::string outfileName = argv[1];
    std::fstream outfile;

    std::string srcdir(argv[2]);
    if (srcdir.at(srcdir.length() - 1) != '/') {
        srcdir += '/';
    }
    builddir = argv[3];
    if (builddir.at(builddir.length() - 1) != '/') {
        builddir += '/';
    }

    dotFilesName = std::string(argv[1]) + ".files";
    dotFiles.open(dotFilesName);
    std::string line;
    std::getline(dotFiles, line);
    dotFilesCheck(line == "SOURCES:");
    std::getline(dotFiles, line);
    trim(line);
    const std::list<std::string> &sourceFiles = split(line, ';');

    if (cmsys::SystemTools::FileExists(outfileName.c_str())) {
        // set generateAll = true if MOC_COMPILE_DEFINITIONS changed
        outfile.open(outfileName, std::ios_base::in);
        std::string buf;
        std::getline(outfile, buf);
        // the second line contains the joined mocDefinitions
        std::getline(outfile, buf);
        lazyInitMocDefinitions();
        generateAll = (buf != join(mocDefinitions, ' '));
        outfile.close();
    } else {
        generateAll = true;
    }

    // the program goes through all .cpp files to see which moc files are included. It is not really
    // interesting how the moc file is named, but what file the moc is created from. Once a moc is
    // included the same moc may not be included in the _automoc.cpp file anymore. OTOH if there's a
    // header containing Q_OBJECT where no corresponding moc file is included anywhere a
    // moc_<filename>.cpp file is created and included in the _automoc.cpp file.
    std::map<std::string, std::string> includedMocs;    // key = moc source filepath, value = moc output filepath
    std::map<std::string, std::string> notIncludedMocs; // key = moc source filepath, value = moc output filename

    cmsys::RegularExpression mocIncludeRegExp("[\n][ \t]*#[ \t]*include[ \t]+[\"<](([^ \">]+/)?moc_[^ \">/]+\\.cpp|[^ \">]+\\.moc)[\">]");
    cmsys::RegularExpression qObjectRegExp("[\n][ \t]*Q_OBJECT[^a-zA-Z0-9_]");
    std::list<std::string> headerExtensions;
#if defined(_WIN32)
    // not case sensitive
    headerExtensions.push_back(".h");
    headerExtensions.push_back(".hpp");
    headerExtensions.push_back(".hxx");
#elif defined(__APPLE__)
    headerExtensions.push_back(".h");
    headerExtensions.push_back(".hpp");
    headerExtensions.push_back(".hxx");

    // detect case-sensitive filesystem
    long caseSensitive = pathconf(srcdir, _PC_CASE_SENSITIVE);
    if (caseSensitive == 1) {
        headerExtensions.push_back(".H");
    }
#else
    headerExtensions.push_back(".h");
    headerExtensions.push_back(".hpp");
    headerExtensions.push_back(".hxx");
    headerExtensions.push_back(".H");
#endif
    /* not safe: if a moc file is missing it's hard to get it generated if this check is "active"
    const QDateTime &lastRun = QFileInfo(dotFiles).lastModified();
    if (!generateAll) {
        bool dirty = false;
        foreach (const QString &absFilename, sourceFiles) {
            const QFileInfo sourceFileInfo(absFilename);
            if (sourceFileInfo.lastModified() >= lastRun) {
                dirty = true;
                break;
            }
            const QString &absPathBaseName = sourceFileInfo.absolutePath() + QLatin1Char('/') + sourceFileInfo.completeBaseName();
            foreach (const QString &ext, headerExtensions) {
                const QFileInfo header(absPathBaseName + ext);
                if (header.exists() && header.lastModified() >= lastRun) {
                    dirty = true;
                    break;
                }
                const QFileInfo pheader(absPathBaseName + QLatin1String("_p") + ext);
                if (pheader.exists() && pheader.lastModified() >= lastRun) {
                    dirty = true;
                    break;
                }
            }
            if (dirty) {
                break;
            }
        }
        if (!dirty) {
            return true;
        }
    }
    */

    for (std::list<std::string>::const_iterator it = sourceFiles.begin();
          it != sourceFiles.end(); ++it) {
        const std::string &absFilename = *it;
        std::string extension = absFilename.substr(absFilename.find_last_of('.'));

        if (extension == ".cpp" || extension == ".cc" || extension == ".mm" || extension == ".cxx" ||
            extension == ".C") {
            const std::string contentsString = readAll(absFilename);
            if (contentsString.empty()) {
                std::cerr << "automoc4: empty source file: " << absFilename << std::endl;
                continue;
            }
            const std::string absPath = cmsys::SystemTools::GetFilenamePath(
                        cmsys::SystemTools::GetRealPath(absFilename.c_str())) + '/';

            int matchOffset = 0;
            if (!mocIncludeRegExp.find(contentsString.c_str())) {
                // no moc #include, look whether we need to create a moc from the .h nevertheless
                //std::cout << "no moc #include in the .cpp file";
                const std::string basename = cmsys::SystemTools::GetFilenameWithoutLastExtension(absFilename);
                for (std::list<std::string>::const_iterator ext = headerExtensions.begin();
                     ext != headerExtensions.end(); ++ext) {
                    const std::string headername = absPath + basename + (*ext);
                    if (cmsys::SystemTools::FileExists(headername.c_str())
                            && includedMocs.find(headername) == includedMocs.end()
                            && notIncludedMocs.find(headername) == notIncludedMocs.end()) {
                        const std::string currentMoc = "moc_" + basename + ".cpp";
                        const std::string contents = readAll(headername);
                        if (qObjectRegExp.find(contents)) {
                            //std::cout << "header contains Q_OBJECT macro";
                            notIncludedMocs[headername] = currentMoc;
                        }
                        break;
                    }
                }
                for (std::list<std::string>::const_iterator ext = headerExtensions.begin();
                     ext != headerExtensions.end(); ++ext) {
                    const std::string privateHeaderName = absPath + basename + "_p" + (*ext);
                    if (cmsys::SystemTools::FileExists(privateHeaderName.c_str())
                            && includedMocs.find(privateHeaderName) == includedMocs.end()
                            && notIncludedMocs.find(privateHeaderName) == notIncludedMocs.end()) {
                        const std::string currentMoc = "moc_" + basename + "_p.cpp";
                        const std::string contents = readAll(privateHeaderName);
                        if (qObjectRegExp.find(contents)) {
                            //std::cout << "header contains Q_OBJECT macro";
                            notIncludedMocs[privateHeaderName] = currentMoc;
                        }
                        break;
                    }
                }
            } else {
                // for every moc include in the file
                do
                {
                    const std::string currentMoc = mocIncludeRegExp.match(1);
                    //std::cout << "found moc include: " << currentMoc << std::endl;

                    std::string basename = cmsys::SystemTools::GetFilenameWithoutLastExtension(currentMoc);
                    const bool moc_style = startsWith(basename, "moc_");

                    // If the moc include is of the moc_foo.cpp style we expect the Q_OBJECT class
                    // declaration in a header file.
                    // If the moc include is of the foo.moc style we need to look for a Q_OBJECT
                    // macro in the current source file, if it contains the macro we generate the
                    // moc file from the source file, else from the header.
                    //
                    // TODO: currently any .moc file name will be used if the source contains
                    // Q_OBJECT
                    if (moc_style || !qObjectRegExp.find(contentsString)) {
                        if (moc_style) {
                            // basename should be the part of the moc filename used for finding the
                            // correct header, so we need to remove the moc_ part
                            basename = basename.substr(4);
                        }

                        bool headerFound = false;
                        for (std::list<std::string>::const_iterator ext = headerExtensions.begin();
                             ext != headerExtensions.end(); ++ext) {
                            const std::string &sourceFilePath = absPath + basename + (*ext);
                            if (cmsys::SystemTools::FileExists(sourceFilePath.c_str())) {
                                headerFound = true;
                                includedMocs[sourceFilePath] = currentMoc;
                                notIncludedMocs.erase(sourceFilePath);
                                break;
                            }
                        }
                        if (!headerFound) {
                            // the moc file is in a subdir => look for the header in the same subdir
                            if (currentMoc.find_first_of('/') != std::string::npos) {
                                const std::string &filepath = absPath +
                                        cmsys::SystemTools::GetFilenamePath(currentMoc) + '/' + basename;

                                for (std::list<std::string>::const_iterator ext = headerExtensions.begin();
                                     ext != headerExtensions.end(); ++ext) {
                                    const std::string &sourceFilePath = filepath + (*ext);
                                    if (cmsys::SystemTools::FileExists(sourceFilePath.c_str())) {
                                        headerFound = true;
                                        includedMocs[sourceFilePath] = currentMoc;
                                        notIncludedMocs.erase(sourceFilePath);
                                        break;
                                    }
                                }
                                if (!headerFound) {
                                    std::cerr << "automoc4: The file \"" << absFilename <<
                                        "\" includes the moc file \"" << currentMoc << "\", but neither \"" <<
                                        absPath + basename + '{' + join(headerExtensions, ',') + "}\" nor \"" <<
                                        filepath + '{' + join(headerExtensions, ',') + '}' <<
                                        "\" exist." << std::endl;
                                    ::exit(EXIT_FAILURE);
                                }
                            } else {
                                std::cerr << "automoc4: The file \"" << absFilename <<
                                    "\" includes the moc file \"" << currentMoc << "\", but \"" <<
                                    absPath + basename + '{' + join(headerExtensions, ',') + '}' <<
                                    "\" does not exist." << std::endl;
                                ::exit(EXIT_FAILURE);
                            }
                        }
                    } else {
                        includedMocs[absFilename] = currentMoc;
                        notIncludedMocs.erase(absFilename);
                    }
                    matchOffset += mocIncludeRegExp.end();
                } while(mocIncludeRegExp.find(contentsString.c_str() + matchOffset));
            }
        } else if (extension == ".h" || extension == ".hpp" ||
                extension == ".hxx" || extension == ".H") {
            if (includedMocs.find(absFilename) == includedMocs.end()
                    && notIncludedMocs.find(absFilename) == notIncludedMocs.end()) {
                // if this header is not getting processed yet and is explicitly mentioned for the
                // automoc the moc is run unconditionally on the header and the resulting file is
                // included in the _automoc.cpp file (unless there's a .cpp file later on that
                // includes the moc from this header)
                const std::string currentMoc = "moc_" + cmsys::SystemTools::GetFilenameWithoutLastExtension(absFilename) + ".cpp";
                notIncludedMocs[absFilename] = currentMoc;
            }
        } else {
            if (verbose) {
                std::cout << "automoc4: ignoring file '" << absFilename << "' with unknown suffix" << std::endl;
            }
        }
    }

    // run moc on all the moc's that are #included in source files
    for (std::map<std::string, std::string>::const_iterator it = includedMocs.begin();
         it != includedMocs.end(); ++it) {
        generateMoc(it->first, it->second);
    }

    std::stringstream outStream(std::stringstream::out);
    outStream << "/* This file is autogenerated, do not edit\n"
        << join(mocDefinitions, ' ') << "\n*/\n";

    if (notIncludedMocs.empty()) {
        outStream << "enum some_compilers { need_more_than_nothing };\n";
    } else {
        // run moc on the remaining headers and include them in the _automoc.cpp file
        for (std::map<std::string, std::string>::const_iterator it = notIncludedMocs.begin();
             it != notIncludedMocs.end(); ++it) {
            if (generateMoc(it->first, it->second)) {
                automocCppChanged = true;
            }
            outStream << "#include \"" << it->second << "\"\n";
        }
    }

    if (failed) {
        // if any moc process failed we don't want to touch the _automoc.cpp file so that
        // automoc4 is rerun until the issue is fixed
        std::cerr << "returning failed.."<< std::endl;
        return false;
    }
    outStream.flush();
    std::string automocSource = outStream.str();
    if (!automocCppChanged) {
        // compare contents of the _automoc.cpp file
        const std::string oldContents = readAll(outfileName);
        if (oldContents == automocSource) {
            // nothing changed: don't touch the _automoc.cpp file
            return true;
        }
    }
    // either the contents of the _automoc.cpp file or one of the mocs included by it have changed

    // source file that includes all remaining moc files (_automoc.cpp file)
    outfile.open(outfileName, std::ios_base::out | std::ios_base::trunc);
    outfile << automocSource;
    outfile.close();

    // update the timestamp on the _automoc.cpp.files file to make sure we get called again
    dotFiles.close();
    if (doTouch && !touch(dotFilesName)) {
        return false;
    }

    return true;
}

bool AutoMoc::touch(const std::string &filename)
{
    // sleep for 1s in order to make the modification time greater than the modification time of
    // the files written before. Equal modification time is not good enough. Just using utime with
    // time(NULL) + 1 is also not a good solution as then make will complain about clock skew.
#ifdef _WIN32
    Sleep(1000);
    _utime(filename.c_str(), 0);
#else
    const struct timespec sleepDuration = { 1, 0 };
    nanosleep(&sleepDuration, NULL);

    int err = utime(filename.c_str(), NULL);
    if (err == -1) {
        err = errno;
        std::cerr << strerror(err) << std::endl;
        return false;
    }
#endif
    return true;
}

bool AutoMoc::generateMoc(const std::string &sourceFile, const std::string &mocFileName)
{
    //std::cout << "AutoMoc::generateMoc" << sourceFile << mocFileName << std::endl;
    const std::string mocFilePath = builddir + mocFileName;
    int sourceNewerThanMoc = 0;
    bool success = cmsys::SystemTools::FileTimeCompare(sourceFile.c_str(), mocFilePath.c_str(), &sourceNewerThanMoc);
    if (generateAll || !success || sourceNewerThanMoc >= 0) {
        // make sure the directory for the resulting moc file exists
        if (!cmsys::SystemTools::FileExists(builddir.c_str(), false)) {
            cmsys::SystemTools::MakeDirectory(builddir.c_str());
        }

        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            lazyInit();
        }
        if (verbose) {
            echoColor("Generating " + mocFilePath + " from " + sourceFile);
        } else {
            echoColor("Generating " + mocFileName);
        }

        std::vector<cmStdString> command;
        command.push_back(mocExe);
        for (std::list<std::string>::const_iterator it = mocIncludes.begin();
             it != mocIncludes.end(); ++it) {
            command.push_back(*it);
        }
        for (std::list<std::string>::const_iterator it = mocDefinitions.begin();
             it != mocDefinitions.end(); ++it) {
            command.push_back(*it);
        }
#ifdef _WIN32
        command.push_back("-DWIN32");
#endif
        command.push_back("-o");
        command.push_back(mocFilePath);
        command.push_back(sourceFile);

        if (verbose) {
            std::cout << mocExe << " " << /*join(command, " ") <<*/ std::endl;
        }

        std::string output;
        int retVal = 0;
        const bool result = cmSystemTools::RunSingleCommand(command, &output, &retVal);
        if (!result || retVal) {
            std::cerr << "automoc4: process for " << mocFilePath << " failed:\n" << output << std::endl;
            failed = true;
            cmSystemTools::RemoveFile(mocFilePath.c_str());
        }
        return true;
    }
    return false;
}

std::string AutoMoc::readAll(const std::string &filename)
{
    std::ifstream file(filename);
    std::stringstream stream;
    stream << file.rdbuf();
    file.close();
    return stream.str();
}

// Splits a string according to a delimiter, and skips empty parts
std::list<std::string> AutoMoc::split(const std::string &input, char delimiter)
{
    std::list<std::string> result;
    std::stringstream stream(input);
    std::string item;
    while(std::getline(stream, item, delimiter)) {
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

std::string AutoMoc::join(const std::list<std::string> lst, char separator)
{
    if (lst.empty()) {
        return "";
    }

    std::string result;
    std::list<std::string>::const_iterator it = lst.begin();
    std::list<std::string>::const_iterator end = lst.end();
    while (it != end) {
        result += (*it) + separator;
        ++it;
    }
    result.erase(result.end() - 1);
    return result;
}

bool AutoMoc::startsWith(const std::string &str, const std::string &with)
{
    return (str.substr(0, with.length()) == with);
}

bool AutoMoc::endsWith(const std::string &str, const std::string &with)
{
    if (with.length() > (str.length())) {
        return false;
    }
    return (str.substr(str.length() - with.length(), with.length()) == with);
}

// trim whitespace from both ends of a string
void AutoMoc::trim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(::isspace))).base(), s.end());
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(::isspace))));
}
