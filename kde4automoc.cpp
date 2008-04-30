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

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
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

class AutoMoc
{
    public:
        AutoMoc();
        bool run();

    private:
        void generateMoc(const QString &sourceFile, const QString &mocFileName);
        void waitForProcesses();
        void printUsage(const QString &);
        void printVersion();
        void echoColor(const QString &msg)
        {
            QProcess *cmakeEcho = new QProcess;
            cmakeEcho->setProcessChannelMode(QProcess::ForwardedChannels);
            QStringList args(cmakeEchoColorArgs);
            args << msg;
            cmakeEcho->start(QLatin1String("cmake"), args, QIODevice::NotOpen);
            processes.enqueue(Process(cmakeEcho, QString()));
        }

        QString builddir;
        QString mocExe;
        QStringList mocIncludes;
        QStringList cmakeEchoColorArgs;
        const bool verbose;
        QTextStream cerr;
        QTextStream cout;
        struct Process
        {
            Process(QProcess *a, const QString &b) : qproc(a), mocFilePath(b) {}
            QProcess *qproc;
            QString mocFilePath;
        };
        QQueue<Process> processes;
        bool failed;
};

void AutoMoc::printUsage(const QString &path)
{
    cout << "usage: " << path << " <outfile> <srcdir> <builddir> <moc executable>" << endl;
}

void AutoMoc::printVersion()
{
    cout << "automoc4 1.0" << endl;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (!AutoMoc().run()) {
        return EXIT_FAILURE;
    }
    return 0;
}

AutoMoc::AutoMoc()
    : verbose(!qgetenv("VERBOSE").isEmpty()), cerr(stderr), cout(stdout), failed(false)
{
    const QByteArray colorEnv = qgetenv("COLOR");
    cmakeEchoColorArgs << QLatin1String("-E") << QLatin1String("cmake_echo_color") 
        << QLatin1String("--switch=") + colorEnv << QLatin1String("--blue")
        << QLatin1String("--bold");
}

bool AutoMoc::run()
{
    const QStringList args = QCoreApplication::arguments();
    Q_ASSERT(args.size() > 0);
    if (args.size() == 2) {
        if ((args[1]=="--help") || (args[1]=="-h")) {
        printUsage(args[0]);
       ::exit(0);
        }
        else if (args[1]=="--version") {
        printVersion();
       ::exit(0);
        }
    }
    else if (args.size() < 4) {
        printUsage(args[0]);
       ::exit(EXIT_FAILURE);
    }
    QFile outfile(args[1]);
    const QFileInfo outfileInfo(outfile);

    QString srcdir(args[2]);
    if (!srcdir.endsWith('/')) {
        srcdir += '/';
    }
    builddir = args[3];
    if (!builddir.endsWith('/')) {
        builddir += '/';
    }
    mocExe = args[4];

    QFile dotFiles(args[1] + ".files");
    dotFiles.open(QIODevice::ReadOnly | QIODevice::Text);
    QByteArray line = dotFiles.readLine();
    Q_ASSERT(line == "MOC_INCLUDES:\n");
    line = dotFiles.readLine().trimmed();
    const QStringList incPaths = QString::fromUtf8(line).split(';');
    foreach (const QString &path, incPaths) {
        if (!path.isEmpty()) {
            mocIncludes << "-I" + path;
        }
    }
    line = dotFiles.readLine();
    Q_ASSERT(line == "SOURCES:\n");
    line = dotFiles.readLine().trimmed();
    dotFiles.close();
    const QStringList sourceFiles = QString::fromUtf8(line).split(';');

    // the program goes through all .cpp files to see which moc files are included. It is not really
    // interesting how the moc file is named, but what file the moc is created from. Once a moc is
    // included the same moc may not be included in the _automoc.cpp file anymore. OTOH if there's a
    // header containing Q_OBJECT where no corresponding moc file is included anywhere a
    // moc_<filename>.cpp file is created and included in the _automoc.cpp file.
    QHash<QString, QString> includedMocs;    // key = moc source filepath, value = moc output filepath
    QHash<QString, QString> notIncludedMocs; // key = moc source filepath, value = moc output filename

    QRegExp mocIncludeRegExp(QLatin1String("[\n]\\s*#\\s*include\\s+[\"<](moc_[^ \">]+\\.cpp|[^ \">]+\\.moc)[\">]"));
    QRegExp qObjectRegExp(QLatin1String("[\n]\\s*Q_OBJECT\\b"));
    QStringList headerExtensions;
    headerExtensions << ".h" << ".hpp" << ".hxx" << ".H";
    foreach (const QString &absFilename, sourceFiles) {
        //qDebug() << absFilename;
        const QFileInfo sourceFileInfo(absFilename);
        if (absFilename.endsWith(".cpp") || absFilename.endsWith(".cc") ||
                absFilename.endsWith(".cxx") || absFilename.endsWith(".C")) {
            //qDebug() << "check .cpp file";
            QFile sourceFile(absFilename);
            sourceFile.open(QIODevice::ReadOnly);
            const QByteArray contents = sourceFile.readAll();
            if (contents.isEmpty()) {
                cerr << "automoc4: empty source file: " << absFilename << endl;
                continue;
            }
            const QString contentsString = QString::fromUtf8(contents);
            const QString absPath = sourceFileInfo.absolutePath() + '/';
            Q_ASSERT(absPath.endsWith('/'));
            int matchOffset = mocIncludeRegExp.indexIn(contentsString);
            if (matchOffset < 0) {
                // no moc #include, look whether we need to create a moc from the .h nevertheless
                //qDebug() << "no moc #include in the .cpp file";
                const QString basename = sourceFileInfo.completeBaseName();
                const QString headername = absPath + basename + ".h";
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
                }
                const QString privateHeaderName = absPath + basename + "_p.h";
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
                }
            } else {
                do { // call this for every moc include in the file
                    const QString currentMoc = mocIncludeRegExp.cap(1);
                    //qDebug() << "found moc include: " << currentMoc << " at offset " << matchOffset;
                    QString basename = QFileInfo(currentMoc).completeBaseName();
                    const bool moc_style = currentMoc.startsWith("moc_");
                    if (moc_style || qObjectRegExp.indexIn(contentsString) < 0) {
                        if (moc_style) {
                            basename = basename.right(basename.length() - 4);
                        }
                        bool headerFound = false;
                        foreach (const QString &ext, headerExtensions) {
                            QString sourceFilePath = absPath + basename + ext;
                            if (QFile::exists(sourceFilePath)) {
                                headerFound = true;
                                includedMocs.insert(sourceFilePath, currentMoc);
                                notIncludedMocs.remove(sourceFilePath);
                                break;
                            }
                        }
                        if (!headerFound) {
                                cerr << "automoc4: The file \"" << absFilename <<
                                    "\" includes the moc file \"" << currentMoc << "\", but \"" <<
                                    absPath + basename + "{" + headerExtensions.join(",") + "}" <<
                                    "\" do not exist." << endl;
                                ::exit(EXIT_FAILURE);
                        }
                    } else {
                        includedMocs.insert(absFilename, currentMoc);
                        notIncludedMocs.remove(absFilename);
                    }

                    matchOffset = mocIncludeRegExp.indexIn(contentsString,
                            matchOffset + currentMoc.length());
                } while(matchOffset >= 0);
            }
        } else if (absFilename.endsWith(".h") || absFilename.endsWith(".hpp") ||
                absFilename.endsWith(".hxx") || absFilename.endsWith(".H")) {
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
               cout << "automoc4: ignoring file '" << absFilename << "' with unknown suffix" << endl;
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
    outStream << "/* This file is autogenerated, do not edit */\n";

    if (notIncludedMocs.isEmpty()) {
        outStream << "enum some_compilers { need_more_than_nothing };\n";
    } else {
        // run moc on the remaining headers and include them in the _automoc.cpp file
        end = notIncludedMocs.constEnd();
        it = notIncludedMocs.constBegin();
        for (; it != end; ++it) {
            generateMoc(it.key(), it.value());
            outStream << "#include \"" << it.value() << "\"\n";
        }
    }

    // let all remaining moc processes finish
    waitForProcesses();

    if (failed) {
        // if any moc process failed we don't want to touch the _automoc.cpp file so that
        // automoc4 is rerun until the issue is fixed
        cerr << "returning failed.."<< endl;
        return false;
    }
    outStream.flush();

    // source file that includes all remaining moc files
    outfile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    outfile.write(automocSource);
    outfile.close();

    return true;
}

void AutoMoc::waitForProcesses()
{
    while (!processes.isEmpty()) {
        Process proc = processes.dequeue();

        bool result = proc.qproc->waitForFinished(-1);
        //ignore errors from the cmake echo process
        if (!proc.mocFilePath.isEmpty()) {
            if (!result || proc.qproc->exitCode()) {
                cerr << "automoc4: process for " << proc.mocFilePath
                     << " failed: " << proc.qproc->errorString() << endl;
                cerr << "pid to wait for: " << proc.qproc->pid() << endl;
                cerr << "processes in queue: " << processes.size() << endl;
                failed = true;
                QFile::remove(proc.mocFilePath);
            }
        }
        delete proc.qproc;
    }
}

void AutoMoc::generateMoc(const QString &sourceFile, const QString &mocFileName)
{
    //qDebug() << Q_FUNC_INFO << sourceFile << mocFileName;
    const QString mocFilePath = builddir + mocFileName;
    if (QFileInfo(mocFilePath).lastModified() < QFileInfo(sourceFile).lastModified()) {
        if (verbose) {
            echoColor("Generating " + mocFilePath + " from " + sourceFile);
        } else {
            echoColor("Generating " + mocFileName);
        }

        // we don't want too many child processes
#ifdef Q_OS_FREEBSD
    static const int max_processes = 0;
#else
    static const int max_processes = 10;
#endif

        if (processes.size() > max_processes) {
            waitForProcesses();
        }

        QProcess *mocProc = new QProcess;
        mocProc->setProcessChannelMode(QProcess::ForwardedChannels);
        QStringList args(mocIncludes);
#ifdef Q_OS_WIN
        args << "-DWIN32";
#endif
        args << QLatin1String("-o") << mocFilePath << sourceFile;
        //qDebug() << "executing: " << mocExe << args;
        mocProc->start(mocExe, args, QIODevice::NotOpen);
        if (mocProc->waitForStarted())
            processes.enqueue(Process(mocProc, mocFilePath));
        else {
            cerr << "automoc4: process for " << mocFilePath << "failed to start: " 
                 << mocProc->errorString() << endl;
            failed = true;
            delete mocProc;
        }
    }
}
