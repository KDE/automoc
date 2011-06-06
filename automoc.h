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

#ifndef AUTOMOC_H
#define AUTOMOC_H

#include <string>
#include <list>
#include <vector>
#include <fstream>

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
	void echoColor(const std::string &msg);

	// Helper functions to make the code easier to read
	std::string readAll(const std::string &filename);
	std::list<std::string> split(const std::string &input, char delimiter);
	std::string join(const std::list<std::string> lst, char separator);
	bool endsWith(const std::string &str, const std::string &with);
	bool startsWith(const std::string &str, const std::string &with);
	void trim(std::string &s);

	std::vector<std::string> args;
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

#endif AUTOMOC_H
