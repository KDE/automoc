/*
    Copyright (C) 2007 Matthias Kretz <kretz@kde.org>

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

#define STR(x) std::string(QString(x).toLatin1())
#include <iostream>
#include <assert.h>

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QProcess>
#include <QtCore/QQueue>
#include <QtCore/QRegExp>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>
#include <QtCore/QtDebug>
#include <cstdlib>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

#ifdef Q_OS_WIN
#include <windows.h>
#include <sys/utime.h>
#else
#include <utime.h>
#endif

#if defined(Q_OS_DARWIN) || defined(Q_OS_MAC)
#include <unistd.h>
#endif

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
        bool touch(const QString &filename);
        bool generateMoc(const QString &sourceFile, const QString &mocFileName);
        void printUsage(const std::string &);
        void printVersion();
        void echoColor(const QString &msg)
        {
            QProcess cmakeEcho;
            cmakeEcho.setProcessChannelMode(QProcess::ForwardedChannels);
            QStringList args(cmakeEchoColorArgs);
            args << msg;
            cmakeEcho.start(cmakeExecutable, args, QIODevice::NotOpen);
            cmakeEcho.waitForFinished(-1);
        }

        int argc;
        char **argv;
        QString builddir;
        QString mocExe;
        QStringList mocIncludes;
        QStringList mocDefinitions;
        QStringList cmakeEchoColorArgs;
        QString cmakeExecutable;
        QFile dotFiles;
        const bool verbose;
        bool failed;
        bool automocCppChanged;
        bool generateAll;
        bool doTouch;
};

void AutoMoc::printUsage(const std::string &path)
{
    std::cout << "Usage: " << path << " <outfile> <srcdir> <builddir> <moc executable> <cmake executable> [--touch]" << endl;
}

void AutoMoc::printVersion()
{
    std::cout << "automoc4 " << AUTOMOC4_VERSION << endl;
}

void AutoMoc::dotFilesCheck(bool x)
{
    if (!x) {
        std::cerr << "Error: syntax error in " << STR(dotFiles.fileName()) << endl;
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
    : verbose(!qgetenv("VERBOSE").isEmpty()), failed(false),
    automocCppChanged(false), generateAll(false), doTouch(false)
{
    const QByteArray colorEnv = qgetenv("COLOR");
    cmakeEchoColorArgs << QLatin1String("-E") << QLatin1String("cmake_echo_color") 
        << QLatin1String("--switch=") + colorEnv << QLatin1String("--blue")
        << QLatin1String("--bold");
}

void AutoMoc::lazyInitMocDefinitions()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    QByteArray line = dotFiles.readLine();
    dotFilesCheck(line == "MOC_COMPILE_DEFINITIONS:\n");
    line = dotFiles.readLine().trimmed();
    const QStringList &cdefList = QString::fromUtf8(line).split(';', QString::SkipEmptyParts);
    line = dotFiles.readLine();
    dotFilesCheck(line == "MOC_DEFINITIONS:\n");
    line = dotFiles.readLine().trimmed();
    if (!cdefList.isEmpty()) {
        foreach (const QString &def, cdefList) {
            assert(!def.isEmpty());
            mocDefinitions << QLatin1String("-D") + def;
        }
    } else {
        const QStringList &defList = QString::fromUtf8(line).split(' ', QString::SkipEmptyParts);
        foreach (const QString &def, defList) {
            assert(!def.isEmpty());
            if (def.startsWith(QLatin1String("-D"))) {
                mocDefinitions << def;
            }
        }
    }
}

void AutoMoc::lazyInit()
{
    mocExe = argv[4];
    cmakeExecutable = argv[5];

    if (argc > 6) {
        if (argv[6] == QLatin1String("--touch")) {
            doTouch = true;
        }
    }

    lazyInitMocDefinitions();

    QByteArray line = dotFiles.readLine();
    dotFilesCheck(line == "MOC_INCLUDES:\n");
    line = dotFiles.readLine().trimmed();
    const QStringList &incPaths = QString::fromUtf8(line).split(';', QString::SkipEmptyParts);
    QSet<QString> frameworkPaths;
    foreach (const QString &path, incPaths) {
        assert(!path.isEmpty());
        mocIncludes << "-I" + path;
        if (path.endsWith(QLatin1String(".framework/Headers"))) {
            QDir framework(path);
            // Go up twice to get to the framework root
            framework.cdUp();
            framework.cdUp();
            frameworkPaths << framework.path();
        }
    }

    foreach (const QString &path, frameworkPaths) {
        mocIncludes << "-F" << path;
    }

    line = dotFiles.readLine();
    dotFilesCheck(line == "CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE:\n");
    line = dotFiles.readLine();
    if (line == "ON\n") {
        line = dotFiles.readLine();
        dotFilesCheck(line == "CMAKE_BINARY_DIR:\n");
        const QString &binDir = QLatin1String("-I") + QString::fromUtf8(dotFiles.readLine().trimmed());

        line = dotFiles.readLine();
        dotFilesCheck(line == "CMAKE_SOURCE_DIR:\n");
        const QString &srcDir = QLatin1String("-I") + QString::fromUtf8(dotFiles.readLine().trimmed());

        QStringList sortedMocIncludes;
        QMutableListIterator<QString> it(mocIncludes);
        while (it.hasNext()) {
            if (it.next().startsWith(binDir)) {
                sortedMocIncludes << it.value();
                it.remove();
            }
        }
        it.toFront();
        while (it.hasNext()) {
            if (it.next().startsWith(srcDir)) {
                sortedMocIncludes << it.value();
                it.remove();
            }
        }
        sortedMocIncludes += mocIncludes;
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
    QFile outfile(argv[1]);
    const QFileInfo outfileInfo(outfile);

    QString srcdir(argv[2]);
    if (!srcdir.endsWith('/')) {
        srcdir += '/';
    }
    builddir = argv[3];
    if (!builddir.endsWith('/')) {
        builddir += '/';
    }

    dotFiles.setFileName(argv[1] + QLatin1String(".files"));
    dotFiles.open(QIODevice::ReadOnly | QIODevice::Text);

    const QByteArray &line = dotFiles.readLine();
    dotFilesCheck(line == "SOURCES:\n");
    const QStringList &sourceFiles = QString::fromUtf8(dotFiles.readLine().trimmed()).split(';', QString::SkipEmptyParts);

    if (outfile.exists()) {
        // set generateAll = true if MOC_COMPILE_DEFINITIONS changed
        outfile.open(QIODevice::ReadOnly | QIODevice::Text);
        QByteArray buf = outfile.readLine();
        // the second line contains the joined mocDefinitions
        buf = outfile.readLine();
        buf.chop(1); // remove trailing \n
        lazyInitMocDefinitions();
        generateAll = (buf != mocDefinitions.join(QString(QLatin1Char(' '))).toUtf8());
        outfile.close();
    } else {
        generateAll = true;
    }

    // the program goes through all .cpp files to see which moc files are included. It is not really
    // interesting how the moc file is named, but what file the moc is created from. Once a moc is
    // included the same moc may not be included in the _automoc.cpp file anymore. OTOH if there's a
    // header containing Q_OBJECT where no corresponding moc file is included anywhere a
    // moc_<filename>.cpp file is created and included in the _automoc.cpp file.
    QHash<QString, QString> includedMocs;    // key = moc source filepath, value = moc output filepath
    QHash<QString, QString> notIncludedMocs; // key = moc source filepath, value = moc output filename

    QRegExp mocIncludeRegExp(QLatin1String("[\n]\\s*#\\s*include\\s+[\"<]((?:[^ \">]+/)?moc_[^ \">/]+\\.cpp|[^ \">]+\\.moc)[\">]"));
    QRegExp qObjectRegExp(QLatin1String("[\n]\\s*Q_OBJECT\\b"));
    QStringList headerExtensions;
#if defined(Q_OS_WIN)
    // not case sensitive
    headerExtensions << ".h" << ".hpp" << ".hxx";
#elif defined(Q_OS_DARWIN) || defined(Q_OS_MAC)
    headerExtensions << ".h" << ".hpp" << ".hxx";

    // detect case-sensitive filesystem
    long caseSensitive = pathconf(srcdir.toLocal8Bit(), _PC_CASE_SENSITIVE);
    if (caseSensitive == 1) {
        headerExtensions << ".H";
    }
#else
    headerExtensions << ".h" << ".hpp" << ".hxx" << ".H";
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

    foreach (const QString &absFilename, sourceFiles) {
        //qDebug() << absFilename;
        const QFileInfo sourceFileInfo(absFilename);
        if (absFilename.endsWith(QLatin1String(".cpp")) || absFilename.endsWith(QLatin1String(".cc")) || 
            absFilename.endsWith(QLatin1String(".mm")) || absFilename.endsWith(QLatin1String(".cxx")) ||
            absFilename.endsWith(QLatin1String(".C"))) {
            //qDebug() << "check .cpp file";
            QFile sourceFile(absFilename);
            sourceFile.open(QIODevice::ReadOnly);
            const QByteArray contents = sourceFile.readAll();
            if (contents.isEmpty()) {
                std::cerr << "automoc4: empty source file: " << STR(absFilename) << endl;
                continue;
            }
            const QString contentsString = QString::fromUtf8(contents);
            const QString absPath = sourceFileInfo.absolutePath() + '/';
            assert(absPath.endsWith('/'));
            int matchOffset = mocIncludeRegExp.indexIn(contentsString);
            if (matchOffset < 0) {
                // no moc #include, look whether we need to create a moc from the .h nevertheless
                //qDebug() << "no moc #include in the .cpp file";
                const QString basename = sourceFileInfo.completeBaseName();
                foreach (const QString &ext, headerExtensions) {
                    const QString headername = absPath + basename + ext;
                    if (QFile::exists(headername) && !includedMocs.contains(headername) &&
                            !notIncludedMocs.contains(headername)) {
                        const QString currentMoc = "moc_" + basename + ".cpp";
                        QFile header(headername);
                        header.open(QIODevice::ReadOnly);
                        const QByteArray contents = header.readAll();
                        if (qObjectRegExp.indexIn(QString::fromUtf8(contents)) >= 0) {
                            //qDebug() << "header contains Q_OBJECT macro";
                            notIncludedMocs.insert(headername, currentMoc);
                        }
                        break;
                    }
                }
                foreach (const QString &ext, headerExtensions) {
                    const QString privateHeaderName = absPath + basename + "_p" + ext;
                    if (QFile::exists(privateHeaderName) && !includedMocs.contains(privateHeaderName) &&
                            !notIncludedMocs.contains(privateHeaderName)) {
                        const QString currentMoc = "moc_" + basename + "_p.cpp";
                        QFile header(privateHeaderName);
                        header.open(QIODevice::ReadOnly);
                        const QByteArray contents = header.readAll();
                        if (qObjectRegExp.indexIn(QString::fromUtf8(contents)) >= 0) {
                            //qDebug() << "header contains Q_OBJECT macro";
                            notIncludedMocs.insert(privateHeaderName, currentMoc);
                        }
                        break;
                    }
                }
            } else {
                do { // call this for every moc include in the file
                    const QString currentMoc = mocIncludeRegExp.cap(1);
                    //qDebug() << "found moc include: " << currentMoc << " at offset " << matchOffset;
                    const QFileInfo currentMocInfo(currentMoc);
                    QString basename = currentMocInfo.completeBaseName();
                    const bool moc_style = basename.startsWith(QLatin1String("moc_"));

                    // If the moc include is of the moc_foo.cpp style we expect the Q_OBJECT class
                    // declaration in a header file.
                    // If the moc include is of the foo.moc style we need to look for a Q_OBJECT
                    // macro in the current source file, if it contains the macro we generate the
                    // moc file from the source file, else from the header.
                    //
                    // TODO: currently any .moc file name will be used if the source contains
                    // Q_OBJECT
                    if (moc_style || qObjectRegExp.indexIn(contentsString) < 0) {
                        if (moc_style) {
                            // basename should be the part of the moc filename used for finding the
                            // correct header, so we need to remove the moc_ part
                            basename = basename.right(basename.length() - 4);
                        }

                        bool headerFound = false;
                        foreach (const QString &ext, headerExtensions) {
                            const QString &sourceFilePath = absPath + basename + ext;
                            if (QFile::exists(sourceFilePath)) {
                                headerFound = true;
                                includedMocs.insert(sourceFilePath, currentMoc);
                                notIncludedMocs.remove(sourceFilePath);
                                break;
                            }
                        }
                        if (!headerFound) {
                            // the moc file is in a subdir => look for the header in the same subdir
                            if (currentMoc.indexOf('/') != -1) {
                                const QString &filepath = absPath + currentMocInfo.path() + QLatin1Char('/') + basename;

                                foreach (const QString &ext, headerExtensions) {
                                    const QString &sourceFilePath = filepath + ext;
                                    if (QFile::exists(sourceFilePath)) {
                                        headerFound = true;
                                        includedMocs.insert(sourceFilePath, currentMoc);
                                        notIncludedMocs.remove(sourceFilePath);
                                        break;
                                    }
                                }
                                if (!headerFound) {
                                    std::cerr << "automoc4: The file \"" << STR(absFilename) <<
                                        "\" includes the moc file \"" << STR(currentMoc) << "\", but neither \"" <<
                                        STR(absPath + basename + '{' + headerExtensions.join(",") + "}\" nor \"") <<
                                        STR(filepath + '{' + headerExtensions.join(",") + '}') <<
                                        "\" exist." << endl;
                                    ::exit(EXIT_FAILURE);
                                }
                            } else {
                                std::cerr << "automoc4: The file \"" << STR(absFilename) <<
                                    "\" includes the moc file \"" << STR(currentMoc) << "\", but \"" <<
                                    STR(absPath + basename + '{' + headerExtensions.join(",") + '}') <<
                                    "\" does not exist." << endl;
                                ::exit(EXIT_FAILURE);
                            }
                        }
                    } else {
                        includedMocs.insert(absFilename, currentMoc);
                        notIncludedMocs.remove(absFilename);
                    }

                    matchOffset = mocIncludeRegExp.indexIn(contentsString,
                            matchOffset + currentMoc.length());
                } while(matchOffset >= 0);
            }
        } else if (absFilename.endsWith(QLatin1String(".h")) || absFilename.endsWith(QLatin1String(".hpp")) ||
                absFilename.endsWith(QLatin1String(".hxx")) || absFilename.endsWith(QLatin1String(".H"))) {
            if (!includedMocs.contains(absFilename) && !notIncludedMocs.contains(absFilename)) {
                // if this header is not getting processed yet and is explicitly mentioned for the
                // automoc the moc is run unconditionally on the header and the resulting file is
                // included in the _automoc.cpp file (unless there's a .cpp file later on that
                // includes the moc from this header)
                const QString currentMoc = "moc_" + sourceFileInfo.completeBaseName() + ".cpp";
                notIncludedMocs.insert(absFilename, currentMoc);
            }
        } else {
            if (verbose) {
                std::cout << "automoc4: ignoring file '" << STR(absFilename) << "' with unknown suffix" << endl;
            }
        }
    }

    // run moc on all the moc's that are #included in source files
    QHash<QString, QString>::ConstIterator end = includedMocs.constEnd();
    QHash<QString, QString>::ConstIterator it = includedMocs.constBegin();
    for (; it != end; ++it) {
        generateMoc(it.key(), it.value());
    }

    QByteArray automocSource;
    QTextStream outStream(&automocSource, QIODevice::WriteOnly);
    outStream << "/* This file is autogenerated, do not edit\n"
        << mocDefinitions.join(QString(QLatin1Char(' '))) << "\n*/\n";

    if (notIncludedMocs.isEmpty()) {
        outStream << "enum some_compilers { need_more_than_nothing };\n";
    } else {
        // run moc on the remaining headers and include them in the _automoc.cpp file
        end = notIncludedMocs.constEnd();
        it = notIncludedMocs.constBegin();
        for (; it != end; ++it) {
            if (generateMoc(it.key(), it.value())) {
                automocCppChanged = true;
            }
            outStream << "#include \"" << it.value() << "\"\n";
        }
    }

    if (failed) {
        // if any moc process failed we don't want to touch the _automoc.cpp file so that
        // automoc4 is rerun until the issue is fixed
        std::cerr << "returning failed.."<< endl;
        return false;
    }
    outStream.flush();

    if (!automocCppChanged) {
        // compare contents of the _automoc.cpp file
        outfile.open(QIODevice::ReadOnly | QIODevice::Text);
        const QByteArray oldContents = outfile.readAll();
        outfile.close();
        if (oldContents == automocSource) {
            // nothing changed: don't touch the _automoc.cpp file
            return true;
        }
    }
    // either the contents of the _automoc.cpp file or one of the mocs included by it have changed

    // source file that includes all remaining moc files (_automoc.cpp file)
    outfile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    outfile.write(automocSource);
    outfile.close();

    // update the timestamp on the _automoc.cpp.files file to make sure we get called again
    dotFiles.close();
    if (doTouch && !touch(dotFiles.fileName())) {
        return false;
    }

    return true;
}

bool AutoMoc::touch(const QString &_filename)
{
    // sleep for 1s in order to make the modification time greater than the modification time of
    // the files written before. Equal modification time is not good enough. Just using utime with
    // time(NULL) + 1 is also not a good solution as then make will complain about clock skew.
#ifdef Q_OS_WIN
    Sleep(1000);
    _wutime(reinterpret_cast<const wchar_t *>(_filename.utf16()), 0);
#else
    const QByteArray &filename = QFile::encodeName(_filename);
    const struct timespec sleepDuration = { 1, 0 };
    nanosleep(&sleepDuration, NULL);

    int err = utime(filename.constData(), NULL);
    if (err == -1) {
        err = errno;
        std::cerr << strerror(err) << "\n";
        return false;
    }
#endif
    return true;
}

bool AutoMoc::generateMoc(const QString &sourceFile, const QString &mocFileName)
{
    qDebug() << Q_FUNC_INFO << sourceFile << mocFileName;
    const QString mocFilePath = builddir + mocFileName;
    QFileInfo mocInfo(mocFilePath);
    if (generateAll || mocInfo.lastModified() <= QFileInfo(sourceFile).lastModified()) {
        QDir mocDir = mocInfo.dir();
        // make sure the directory for the resulting moc file exists
        if (!mocDir.exists()) {
            mocDir.mkpath(mocDir.path());
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

        QProcess mocProc;
        mocProc.setProcessChannelMode(QProcess::ForwardedChannels);
        QStringList args(mocIncludes + mocDefinitions);
#ifdef Q_OS_WIN
        args << "-DWIN32";
#endif
        args << QLatin1String("-o") << mocFilePath << sourceFile;
        //qDebug() << "executing: " << mocExe << args;
        if (verbose) {
            std::cout << STR(mocExe) << " " << STR(args.join(QLatin1String(" "))) << endl;
        }
        mocProc.start(mocExe, args, QIODevice::NotOpen);
        if (mocProc.waitForStarted()) {
            const bool result = mocProc.waitForFinished(-1);
            if (!result || mocProc.exitCode()) {
                std::cerr << "automoc4: process for " << STR(mocFilePath)
                     << " failed: " << STR(mocProc.errorString()) << endl;
                std::cerr << "pid to wait for: " << mocProc.pid() << endl;
                failed = true;
                QFile::remove(mocFilePath);
            }
            return true;
        } else {
            std::cerr << "automoc4: process for " << STR(mocFilePath) << "failed to start: "
                 << STR(mocProc.errorString()) << endl;
            failed = true;
        }
    }
    return false;
}
