/*
* Copyright (C) 2007-2008 Benjamin C Meyer
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * The name of the contributors may not be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY <copyright holder> ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <QtCore>
#include "pp.h"
#include "parser.h"
#include "control.h"
#include "binder.h"

static bool preprocess(const QString &sourceFile, QByteArray *out)
{
    rpp::pp_environment env;
    rpp::pp preprocess(env);

    rpp::pp_null_output_iterator null_out;

    const char *ppconfig = ":/trolltech/generator/rpp/src/rpp/pp-qt-configuration";

    QFile configFile(ppconfig);
    if (!configFile.open(QFile::ReadOnly)) {
        fprintf(stderr, "Preprocessor configuration file not found '%s'\n", ppconfig);
        return false;
    }

    QByteArray ba = configFile.readAll();
    configFile.close();
    preprocess.operator() (ba.constData(), ba.constData() + ba.size(), null_out);

    QString qtdir = getenv ("QTDIR");
    if (qtdir.isEmpty()) {
        fprintf(stderr, "Generator requires QTDIR to be set\n");
        return false;
    }

    qtdir += "/include";

    QString currentDir = QDir::current().absolutePath();
    QFileInfo sourceInfo(sourceFile);
    QDir::setCurrent(sourceInfo.absolutePath());

    preprocess.push_include_path(".");
    preprocess.push_include_path(QDir::convertSeparators(qtdir).toStdString());
    preprocess.push_include_path(QDir::convertSeparators(qtdir + "/QtXml").toStdString());
    preprocess.push_include_path(QDir::convertSeparators(qtdir + "/QtNetwork").toStdString());
    preprocess.push_include_path(QDir::convertSeparators(qtdir + "/QtCore").toStdString());
    preprocess.push_include_path(QDir::convertSeparators(qtdir + "/QtGui").toStdString());
    preprocess.push_include_path(QDir::convertSeparators(qtdir + "/QtOpenGL").toStdString());

    std::string result;
    result.reserve (20 * 1024); // 20K

    result += "# 1 \"builtins\"\n";
    result += "# 1 \"";
    result += sourceFile.toStdString();
    result += "\"\n";

    preprocess.file (sourceInfo.fileName().toStdString(),
                     rpp::pp_output_iterator<std::string> (result));

    *out = QString::fromStdString(result).toUtf8();
    return true;
}

QString convertAccessPolicy(CodeModel::AccessPolicy policy) {
    switch(policy) {
        case CodeModel::Public: return "public";
        case CodeModel::Private: return "private";
        case CodeModel::Protected: return "protected";
    }
    return QString();
}

bool functionLessThan(CodeModelPointer<_FunctionModelItem> &s1, CodeModelPointer<_FunctionModelItem> &s2) {
    if (s1->accessPolicy() == s2->accessPolicy())
        return s1->name().toLower() < s2->name().toLower();
    return (s1->accessPolicy() < s2->accessPolicy());
}

QString makeFunction(FunctionModelItem function, const QString preFix = QString()) {
    QString fullName;
    fullName += function->type().toString();
    fullName += " ";
    fullName += preFix;
    fullName += function->name();
    ArgumentList arguments = function->arguments();
    QStringList args;
    for (int i = 0; i < arguments.count(); ++i) {
        QString arg = arguments[i]->name();
        if (arg.isEmpty())
            arg = QString("arg%1").arg(i);
        QString theargs = arguments[i]->type().toString() + " " + arg;
        if (arguments[i]->defaultValue())
            theargs += " = " + arguments[i]->defaultValueExpression();
        args += theargs;
    }
    fullName += "(" + args.join(", ") + ")";
    if (function->isConstant())
        fullName +=  " const";

    if (function->isStatic())
        fullName =  "static " + fullName;

    return fullName;
}


void outputFile(ClassModelItem clazz, FunctionList functions,
                QList<QPair<QString,QString> > extraFunctions)
{
    // setup
    QString className = clazz->name();
    QString shortName = className.mid(className.lastIndexOf(QRegExp("[A-Z][a-z]*"))).toLower();
    QString tstClassName = "tst_" + className;
    QString indent = "    ";
    QTextStream out(stdout);

    // License
    out << "/****************************************************************************" << endl
        << "**" << endl
        << "** Copyright (C) 2008-$THISYEAR$ $TROLLTECH$. All rights reserved." << endl
        << "**" << endl
        << "** This file is part of the $MODULE$ of the Qt Toolkit." << endl
        << "**" << endl
        << "** $TROLLTECH_DUAL_LICENSE$" << endl
        << "**" << endl
        << "** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE" << endl
        << "** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE." << endl
        << "**" << endl
        << "****************************************************************************/" << endl;

    // Includes
    out << endl;
    out << "#include <QtTest/QtTest>" << endl;
    out << "#include <" << clazz->fileName().mid(clazz->fileName().indexOf('/')) << ">" << endl;


    // Generate class definition
    out << endl;
    out << QString("class %1 : public QObject").arg(tstClassName) << endl;
    out << "{" << endl;
    {
        out << indent << "Q_OBJECT" << endl;

        out << endl;
        out << "public slots:" << endl;
        for(int i = 0; i < extraFunctions.count(); ++i) {
            out << indent << QString("void %1();").arg(extraFunctions.at(i).first) << endl;

        }

        out << endl;
        out << "private slots:" << endl;
        out << indent << QString("void %1_data();").arg(className.toLower()) << endl;
        out << indent << QString("void %1();").arg(className.toLower()) << endl;
        QStringList done;
        foreach (FunctionModelItem fun, functions) {
            if (done.contains(fun->name()))
                continue;
            out << indent << QString("void %1_data();").arg(fun->name()) << endl;
            out << indent << QString("void %1();").arg(fun->name()) << endl;
            done.append(fun->name());
        }
    }
    out << "};" << endl;


    // Generate subclass
    out << endl;
    out << "// Subclass that exposes the protected functions." << endl;
    out << "class Sub" + className + " : public " << className << endl;
    out << "{" << endl;
    {
        out << "public:";
        foreach (FunctionModelItem fun, functions) {
            // implement protected or virtual functions
            if (fun->accessPolicy() != CodeModel::Protected && !fun->isAbstract())
                continue;

            out << endl;
            if (fun->isAbstract())
                out << indent << QString("// %1::%2 is a pure virtual function.").arg(className).arg(fun->name()) << endl;
            out << indent << makeFunction(fun, fun->isAbstract() ? "" : "call_") << endl;

            out << indent << indent << "{ ";
            {
                ArgumentList arguments = fun->arguments();
                if (!fun->isAbstract()) {
                    QStringList args;
                    for (int j = 0; j < arguments.count(); ++j) {
                        QString arg = arguments[j]->name();
                        if (arg.isEmpty())
                            arg = QString("arg%1").arg(j);
                        args += arg;
                    }
                    out << QString("return Sub%1::%2(%3);").arg(className).arg(fun->name()).arg(args.join(", "));
                }
            }
            out << " }" << endl;
        }
    }
    out << "};" << endl;


    // Generate functions
    for(int i = 0; i < extraFunctions.count(); ++i) {
        out << endl;
        out << QString("// %1").arg(extraFunctions.at(i).second) << endl;
        out << QString("void %1::%2()\n{\n}").arg(tstClassName).arg(extraFunctions.at(i).first) << endl;
    }

    QStringList declared;

    out << endl;
    out << QString("void %1::%2_data()\n{\n}\n").arg(tstClassName).arg(className.toLower()) << endl;
    out << QString("void %1::%2()").arg(tstClassName).arg(className.toLower()) << endl;
    out << "{" << endl;
    {
        out << indent << QString("Sub%1 %2;").arg(className).arg(shortName) << endl;
        out << "#if 0" << endl;
        foreach (FunctionModelItem fun, functions) {
            QStringList args;
            for (int i = 0; i < fun->arguments().count(); ++i)
                args += fun->arguments()[i]->type().toString();
            QString returnType = fun->type().toString();
            out << indent;
            if (returnType != "void")
                out << "QCOMPARE(";
            out << shortName << ".";
            if (fun->accessPolicy() == CodeModel::Protected)
                out << "call_";
            out << fun->name() << "(" << args.join(", ") << ")";
            if (returnType != "void")
                out << ", " << returnType << ")";
            out << ";" << endl;
        }
        out << "#endif" << endl;
        out << indent << "QSKIP(\"Test is not implemented.\", SkipAll);" << endl;
    }
    out << "}" << endl;

    QStringList done;
    foreach (FunctionModelItem fun, functions) {
        if (done.contains(fun->name()))
            continue;

        // data function
        out << endl;

        QStringList fetchMeType;
        QStringList fetchMeName;

        ArgumentList arguments = fun->arguments();
        for (int i = 0; i < arguments.count(); ++i) {
            QString type = arguments[i]->type().toString();
            type = type.mid(0, type.indexOf(' '));
            QString name = arguments[i]->name();
            if (arguments[i]->type().indirections()) {
                type = "int";
                name = name + "Count";
            }
            fetchMeType << type;
            fetchMeName << name;
        }
        if (fun->type().toString() != "void") {
            QString returnType = fun->type().toString();
            fetchMeType << returnType;
            fetchMeName << fun->name();
        }
        if (fetchMeType.isEmpty()) {
            fetchMeType << "int";
            fetchMeName << "foo";
        }

        QStringList knownTypes;
        knownTypes << "int" << "qreal" << "bool" << "QString" << "QRectF" << "QPointF" << "QPixmap" << "QFont" << "QUrl" << "uint";
        bool knowAllTypes = true;
        for (int i = 0; i < fetchMeType.count(); ++i) {
            if (!knownTypes.contains(fetchMeType[i])) {
                if (!declared.contains(fetchMeType[i])) {
                    out << "Q_DECLARE_METATYPE(" << fetchMeType[i] << ")" << endl;
                    declared.append(fetchMeType[i]);
                }
                knowAllTypes = false;
            }
        }


        out << QString("void %1::%2_data()\n{").arg(tstClassName).arg(fun->name()) << endl;

        if (!knowAllTypes)
            out << "#if 0" << endl;
        for (int i = 0; i < fetchMeType.count(); ++i)
            out << indent << QString("QTest::addColumn<%1>(\"%2\");").arg(fetchMeType[i]).arg(fetchMeName[i]) << endl;

        out << indent << "QTest::newRow(\"null\")";
        for (int i = 0; i < fetchMeType.count(); ++i) {
            out << " << ";
            if (fetchMeType[i] == "int")
                out << "0";
            else if (fetchMeType[i] == "qreal")
                out << "0.0";
            else if (fetchMeType[i] == "bool")
                out << "false";
            else if (fetchMeType[i] == "QString")
                out << "QString(\"foo\")";
            else
                out << fetchMeType[i] << "()";
        }
        out << ";" << endl;
        if (!knowAllTypes)
            out << "#endif" << endl;
        out << "}" << endl;

        out << endl;
        out << "// " << convertAccessPolicy(fun->accessPolicy()) << " " << makeFunction(fun) << endl;
        out << QString("void %1::%2()").arg(tstClassName).arg(fun->name()) << endl;
        out << "{" << endl;
        {
            out << "#if 0" << endl;
            for (int i = 0; i < fetchMeType.count(); ++i)
                out << indent << QString("QFETCH(%1, %2);").arg(fetchMeType[i]).arg(fetchMeName[i]) << endl;
            out << endl;
            out << indent << "Sub" << className << " " << shortName << ";" << endl;


            // any possible spies that the class emits
            // ### check spy args
            // ### properties
            out << endl;
            int spies = 0;
            for(int i = 0; i < functions.count(); ++i) {
                FunctionModelItem funt = functions[i];
                if (funt->functionType() == CodeModel::Signal) {
                    ArgumentList arguments = funt->arguments();
                    QStringList args;
                    for (int i = 0; i < arguments.count(); ++i) {
                        args += arguments[i]->type().toString();
                    }

                    out << indent << QString("QSignalSpy spy%1(&%2, SIGNAL(%3(%4)));").arg(spies++).arg(shortName).arg(funt->name()).arg(args.join(", ")) << endl;
                }
            }
            if (spies > 0)
                out << endl;

            bool returnsSomething = (fun->type().toString() != "void");
            out << indent;
            if (returnsSomething)
                out << "QCOMPARE(";
            out << shortName << ".";
            if (fun->accessPolicy() == CodeModel::Protected)
                out << "call_";
            out << fun->name();
            out << "(";
            QStringList args;
            for (int i = 0; i < arguments.count(); ++i)
                args.append(arguments[i]->name());
            out << args.join(", ");
            out << ")";
            if (returnsSomething) {
                out << ", " << fun->name() << ")";
            }
            out << ";" << endl;

            if (spies > 0)
                out << endl;

            spies = 0;
            for(int i = 0; i < functions.count(); ++i) {
                FunctionModelItem funt = functions[i];
                if (funt->functionType() == CodeModel::Signal) {
                    out << indent << QString("QCOMPARE(spy\%1.count(), 0);").arg(spies++) << endl;
                }
            }

            /*
            // any classes that can be used to check
            if (!fun->isConstant()) {
                out << endl;
                out << indent << "// This function isn't const so you should be able to do comparisons on all the const functions." << endl;
                for(int i = 0; i < functions.count(); ++i) {
                    FunctionModelItem funt = functions[i];
                    if (funt->type().toString() != "void")
                        out << indent << "// QCOMPARE(foo." << funt->name() << "(), something);" << endl;
                }
            }
            */
            out << "#endif" << endl;
            out << indent << "QSKIP(\"Test is not implemented.\", SkipAll);" << endl;
        }
        out << "}" << endl;
        done.append(fun->name());
    }

    // Footer
    out << endl;
    out << "QTEST_MAIN(" << tstClassName << ")" << endl;
    out << "#include \"" << tstClassName.toLower() << ".moc\"" << endl;
    out << endl;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        QTextStream error(stderr);
        error << "usage: " << argv[0] << " foo.h" << " Class" << endl;
        return -1;
    }

    QString fileName = argv[1];
    QString className = argv[2];

    QByteArray contents;
    if (!preprocess(fileName, &contents)) {
        QTextStream error(stderr);
        error << "Error preprocessing: " << fileName << endl;
        return (-1);
    }

/*    QFile ff(".pp");
      ff.open(QFile::WriteOnly);
      ff.write(contents);
      ff.close();*/

    Control control;
    Parser p(&control);
    pool __pool;

    TranslationUnitAST *ast = p.parse(contents, contents.size(), &__pool);

    CodeModel model;
    Binder binder(&model, p.location());
    FileModelItem dom = binder.run(ast);

    int i = 0;
    for (i = 0; i < dom->classes().count(); ++i) {
        if (dom->classes().at(i)->name() == className)
            break;
    }
    if (i == dom->classes().count()) {
        qWarning() << "Class not found: " << className << "Try removing the includes at the top of the file.";
        return 1;
    }

    ClassModelItem clazz = dom->classes()[i];
    // TODO parent class?
    FunctionList functions = clazz->functions();
    // TODO ignore signals
    for (int i = functions.count() - 1; i >= 0; --i) {
        if (functions.at(i)->accessPolicy() == CodeModel::Private
            || functions.at(i)->name() == className
            || functions.at(i)->name() == "~" + className)
            functions.removeAt(i);
    }
    qSort(functions.begin(), functions.end(), functionLessThan);

    QList<QPair<QString,QString> > extraFunctions;
    extraFunctions << QPair<QString,QString>("initTestCase", "This will be called before the first test function is executed.\n// It is only called once.");
    extraFunctions << QPair<QString,QString>("cleanupTestCase", "This will be called after the last test function is executed.\n// It is only called once.");
    extraFunctions << QPair<QString,QString>("init", "This will be called before each test function is executed.");
    extraFunctions << QPair<QString,QString>("cleanup", "This will be called after every test function.");

    outputFile(clazz, functions, extraFunctions);

    //qmake -project "CONFIG += qtestlib"
    return 0;
}
