/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#include "FMcpProtocol.h"

#include <QJsonParseError>

FMcpHttpRequest FMcpProtocol::makeHttpError(int status, const QString &msg) {
	FMcpHttpRequest req;
	req.statusCode = status;
	req.complete = true;
	req.errorMessage = msg;
	return req;
}

bool FMcpProtocol::parseRequestLine(const QByteArray &line, FMcpHttpRequest &req) {
	QList<QByteArray> parts = line.trimmed().split(' ');
	if (parts.size() < 2) {
		return false;
	}
	req.method = QString::fromLatin1(parts.at(0)).toUpper();
	QString rawPath = QString::fromUtf8(parts.at(1));
	int queryIdx = rawPath.indexOf(QLatin1Char('?'));
	req.path = (queryIdx >= 0) ? rawPath.left(queryIdx) : rawPath;
	return !req.method.isEmpty() && req.path.startsWith(QLatin1Char('/'));
}

bool FMcpProtocol::parseContentLength(const QByteArray &headersBlock, FMcpHttpRequest &req) {
	req.contentLength = 0;
	const QList<QByteArray> lines = headersBlock.split('\n');
	for (int i = 1; i < lines.size(); ++i) {
		QByteArray line = lines.at(i).trimmed();
		if (!line.toLower().startsWith("content-length:")) {
			continue;
		}
		QByteArray valStr = line.mid(15).trimmed();
		bool ok = false;
		int len = valStr.toInt(&ok);
		if (!ok || len < 0) {
			req = makeHttpError(400, QStringLiteral("Invalid Content-Length header"));
			return false;
		}
		if (len > MaxPayloadBytes) {
			req = makeHttpError(413, QStringLiteral("Payload exceeds maximum allowed size"));
			return false;
		}
		req.contentLength = len;
	}
	return true;
}

FMcpHttpRequest FMcpProtocol::parseHttpRequest(const QByteArray &rawBuffer) {
	int headerEnd = rawBuffer.indexOf("\r\n\r\n");
	int sepLen = 4;
	if (headerEnd < 0) {
		headerEnd = rawBuffer.indexOf("\n\n");
		sepLen = 2;
	}
	if (headerEnd < 0) {
		if (rawBuffer.size() > MaxHeaderBytes) {
			return makeHttpError(413, QStringLiteral("HTTP header exceeds maximum allowed size"));
		}
		return FMcpHttpRequest();
	}
	if (headerEnd > MaxHeaderBytes) {
		return makeHttpError(413, QStringLiteral("HTTP header exceeds maximum allowed size"));
	}

	QByteArray headersBlock = rawBuffer.left(headerEnd);
	int firstLineEnd = headersBlock.indexOf('\n');
	QByteArray firstLine = (firstLineEnd >= 0) ? headersBlock.left(firstLineEnd) : headersBlock;

	FMcpHttpRequest req;
	if (!parseRequestLine(firstLine, req)) {
		return makeHttpError(400, QStringLiteral("Malformed HTTP request line"));
	}
	if (!parseContentLength(headersBlock, req)) {
		return req;
	}

	int bodyOffset = headerEnd + sepLen;
	int availableBody = rawBuffer.size() - bodyOffset;
	if (availableBody < req.contentLength) {
		return req;
	}

	req.body = rawBuffer.mid(bodyOffset, req.contentLength);
	req.complete = true;
	return req;
}

bool FMcpProtocol::parseRpcBody(const QByteArray &body, QString &method, QJsonObject &params, QString &errorMsg) {
	if (body.trimmed().isEmpty()) {
		errorMsg = QStringLiteral("Request body must not be empty");
		return false;
	}

	QJsonParseError parseErr;
	QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
	if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
		errorMsg = QStringLiteral("Invalid JSON object in request body");
		return false;
	}

	QJsonObject root = doc.object();
	QJsonValue methodVal = root.value(QStringLiteral("method"));
	if (!methodVal.isString() || methodVal.toString().trimmed().isEmpty()) {
		errorMsg = QStringLiteral("Missing or non-string 'method' field");
		return false;
	}
	method = methodVal.toString().trimmed();

	if (!root.contains(QStringLiteral("params")) || root.value(QStringLiteral("params")).isNull()) {
		params = QJsonObject();
		return true;
	}
	QJsonValue paramsVal = root.value(QStringLiteral("params"));
	if (!paramsVal.isObject()) {
		errorMsg = QStringLiteral("Field 'params' must be a JSON object");
		return false;
	}
	params = paramsVal.toObject();
	return true;
}

QByteArray FMcpProtocol::buildHttpResponse(int statusCode, const QString &statusText, const QJsonObject &jsonBody) {
	QByteArray payload = QJsonDocument(jsonBody).toJson(QJsonDocument::Compact);
	QString header = QStringLiteral(
		"HTTP/1.1 %1 %2\r\n"
		"Content-Type: application/json; charset=utf-8\r\n"
		"Content-Length: %3\r\n"
		"Connection: close\r\n"
		"\r\n")
		.arg(statusCode)
		.arg(statusText)
		.arg(payload.size());
	return header.toUtf8() + payload;
}
