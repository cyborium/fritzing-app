CONFIG += testcase console c++20
QT += core network testlib
QT -= gui
CONFIG -= app_bundle

TARGET = test_mcp
TEMPLATE = app

INCLUDEPATH += $$PWD/../../../src/mcp

HEADERS += \
	$$PWD/../../../src/mcp/FMcpTypes.h \
	$$PWD/../../../src/mcp/FMcpProtocol.h \
	$$PWD/../../../src/mcp/FMcpValidator.h \
	$$PWD/../../../src/mcp/IFritzingWorkspace.h \
	$$PWD/../../../src/mcp/FMcpCommandRouter.h \
	$$PWD/../../../src/mcp/FMcpServer.h

SOURCES += \
	$$PWD/../../../src/mcp/FMcpProtocol.cpp \
	$$PWD/../../../src/mcp/FMcpValidator.cpp \
	$$PWD/../../../src/mcp/FMcpCommandRouter.cpp \
	$$PWD/../../../src/mcp/FMcpServer.cpp \
	$$PWD/test_mcp.cpp
