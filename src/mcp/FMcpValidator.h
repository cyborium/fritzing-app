/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#ifndef FMCPVALIDATOR_H
#define FMCPVALIDATOR_H

#include "FMcpTypes.h"

class FMcpValidator {
public:
	static constexpr double MaxCoordinateAbs = 100000.0;
	static constexpr int MaxSearchLimit = 500;
	static constexpr int MaxRenderDimension = 8192;
	static constexpr int MinRenderDimension = 64;

	static bool requireNonEmptyString(const QJsonObject &params, const QString &key, QString &outVal, QString &err);
	static bool optionalString(const QJsonObject &params, const QString &key, const QString &defaultVal, QString &outVal, QString &err);
	static bool requireFiniteCoord(const QJsonObject &params, const QString &key, double &outVal, QString &err);
	static bool optionalFiniteCoord(const QJsonObject &params, const QString &key, double defaultVal, double &outVal, QString &err);
	static bool validateViewName(const QString &rawView, bool allowEmpty, QString &normalized, QString &err);
	static bool validateRotation(const QJsonObject &params, double &normalizedDegrees, QString &err);

	static bool validateSearchParams(const QJsonObject &params, QString &query, int &limit, QString &err);
	static bool validatePartAddParams(const QJsonObject &params, FMcpPartSpec &spec, QString &err);
	static bool validatePartMoveParams(const QJsonObject &params, FMcpMoveSpec &spec, QString &err);
	static bool validateSetPropertyParams(const QJsonObject &params, QString &partRef, QString &propName, QString &propValue, QString &err);
	static bool validateWireConnectParams(const QJsonObject &params, FMcpWireSpec &spec, QString &err);
	static bool validateWireDeleteParams(const QJsonObject &params, long &wireId, QString &err);
	static bool validateRenderParams(const QJsonObject &params, FMcpRenderSpec &spec, QString &err);
};

#endif // FMCPVALIDATOR_H
