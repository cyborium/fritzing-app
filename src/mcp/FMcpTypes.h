/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#ifndef FMCPTYPES_H
#define FMCPTYPES_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

struct FMcpHttpRequest {
	QString method;
	QString path;
	QByteArray body;
	int contentLength = 0;
	int statusCode = 200;
	bool complete = false;
	QString errorMessage;
};

struct FMcpPartSpec {
	QString moduleID;
	QString viewName;
	double x = 0.0;
	double y = 0.0;
};

struct FMcpMoveSpec {
	QString partRef;
	double x = 0.0;
	double y = 0.0;
	bool relative = false;
};

struct FMcpWireSpec {
	QString fromPart;
	QString fromPin;
	QString toPart;
	QString toPin;
	QString viewName;
	QString color;
};

struct FMcpRenderSpec {
	QString viewName;
	QString format = QStringLiteral("png");
	QString outputPath;
	int width = 1200;
	int height = 900;
	bool cropToItems = true;
};

struct FMcpBoardResizeSpec {
	QString boardRef;
	double widthMm = 0.0;
	double heightMm = 0.0;
};

struct FMcpGerberSpec {
	QString outputDir;
	QString prefix;
};

namespace FMcpResult {

inline QJsonObject ok(const QJsonObject &data = QJsonObject()) {
	QJsonObject res = data;
	res[QStringLiteral("ok")] = true;
	return res;
}

inline QJsonObject fail(const QString &errorMsg, const QString &code = QStringLiteral("INVALID_ARGUMENT")) {
	QJsonObject res;
	res[QStringLiteral("ok")] = false;
	res[QStringLiteral("error")] = errorMsg;
	res[QStringLiteral("code")] = code;
	return res;
}

inline QString toJsonString(const QJsonObject &obj) {
	return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace FMcpResult

#endif // FMCPTYPES_H
