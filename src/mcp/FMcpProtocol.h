/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#ifndef FMCPPROTOCOL_H
#define FMCPPROTOCOL_H

#include "FMcpTypes.h"

class FMcpProtocol {
public:
	static constexpr int MaxPayloadBytes = 2 * 1024 * 1024;
	static constexpr int MaxHeaderBytes = 16 * 1024;

	static FMcpHttpRequest parseHttpRequest(const QByteArray &rawBuffer);
	static bool parseRpcBody(const QByteArray &body, QString &method, QJsonObject &params, QString &errorMsg);
	static QByteArray buildHttpResponse(int statusCode, const QString &statusText, const QJsonObject &jsonBody);

private:
	static bool parseRequestLine(const QByteArray &line, FMcpHttpRequest &req);
	static bool parseContentLength(const QByteArray &headersBlock, FMcpHttpRequest &req);
	static FMcpHttpRequest makeHttpError(int status, const QString &msg);
};

#endif // FMCPPROTOCOL_H
