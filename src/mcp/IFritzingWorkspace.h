/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#ifndef IFRITZINGWORKSPACE_H
#define IFRITZINGWORKSPACE_H

#include "FMcpTypes.h"

class IFritzingWorkspace {
public:
	virtual ~IFritzingWorkspace() = default;

	virtual QJsonObject getSketchState(const QString &viewName) = 0;
	virtual QJsonObject setActiveView(const QString &viewName) = 0;
	virtual QJsonObject undo() = 0;
	virtual QJsonObject redo() = 0;
	virtual QJsonObject renderImage(const FMcpRenderSpec &spec) = 0;

	virtual QJsonObject searchParts(const QString &query, int limit) = 0;
	virtual QJsonObject getPartInfo(const QString &moduleID) = 0;

	virtual QJsonObject addPart(const FMcpPartSpec &spec) = 0;
	virtual QJsonObject movePart(const FMcpMoveSpec &spec) = 0;
	virtual QJsonObject rotatePart(const QString &partRef, double degrees) = 0;
	virtual QJsonObject deletePart(const QString &partRef) = 0;
	virtual QJsonObject setPartProperty(const QString &partRef, const QString &propName, const QString &propValue) = 0;

	virtual QJsonObject connectWire(const FMcpWireSpec &spec) = 0;
	virtual QJsonObject deleteWire(long wireId) = 0;
	virtual QJsonObject getNetlist() = 0;
};

#endif // IFRITZINGWORKSPACE_H
