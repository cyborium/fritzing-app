# /*******************************************************************
# Part of the Fritzing project - http://fritzing.org
# Copyright (c) 2026 Fritzing
# ********************************************************************/

HEADERS += \
	src/mcp/FMcpTypes.h \
	src/mcp/FMcpProtocol.h \
	src/mcp/FMcpValidator.h \
	src/mcp/IFritzingWorkspace.h \
	src/mcp/FMcpCommandRouter.h \
	src/mcp/FMcpWorkspaceAdapter.h \
	src/mcp/FMcpServer.h

SOURCES += \
	src/mcp/FMcpProtocol.cpp \
	src/mcp/FMcpValidator.cpp \
	src/mcp/FMcpCommandRouter.cpp \
	src/mcp/FMcpWorkspaceAdapter.cpp \
	src/mcp/FMcpServer.cpp

INCLUDEPATH += src
