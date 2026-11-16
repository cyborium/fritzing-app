/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#include "FMcpServer.h"

#include <QHostAddress>

FMcpServer::FMcpServer(IFritzingWorkspace *workspace, QObject *parent)
	: QObject(parent)
	, m_router(std::make_unique<FMcpCommandRouter>(workspace))
	, m_tcpServer(new QTcpServer(this)) {
	connect(m_tcpServer, &QTcpServer::newConnection, this, &FMcpServer::onNewConnection);
}

FMcpServer::~FMcpServer() {
	stop();
}

bool FMcpServer::start(quint16 port) {
	if (m_tcpServer->isListening()) {
		return true;
	}
	return m_tcpServer->listen(QHostAddress::LocalHost, port);
}

void FMcpServer::stop() {
	if (m_tcpServer->isListening()) {
		m_tcpServer->close();
	}
	const auto sockets = m_buffers.keys();
	for (QTcpSocket *sock : sockets) {
		sock->disconnectFromHost();
	}
	m_buffers.clear();
}

bool FMcpServer::isRunning() const {
	return m_tcpServer->isListening();
}

quint16 FMcpServer::serverPort() const {
	return m_tcpServer->serverPort();
}

QByteArray FMcpServer::routeHttpRequest(const FMcpHttpRequest &req) const {
	if (req.statusCode != 200) {
		QString statusText = (req.statusCode == 413) ? QStringLiteral("Payload Too Large") : QStringLiteral("Bad Request");
		return FMcpProtocol::buildHttpResponse(req.statusCode, statusText, FMcpResult::fail(req.errorMessage, QStringLiteral("HTTP_ERROR")));
	}
	if (req.path == QStringLiteral("/health")) {
		if (req.method != QStringLiteral("GET")) {
			return FMcpProtocol::buildHttpResponse(405, QStringLiteral("Method Not Allowed"), FMcpResult::fail(QStringLiteral("Use GET for /health"), QStringLiteral("METHOD_NOT_ALLOWED")));
		}
		QJsonObject status;
		status[QStringLiteral("service")] = QStringLiteral("fritzing-mcp-bridge");
		status[QStringLiteral("methods")] = QJsonArray::fromStringList(m_router->registeredMethods());
		return FMcpProtocol::buildHttpResponse(200, QStringLiteral("OK"), FMcpResult::ok(status));
	}
	if (req.path == QStringLiteral("/rpc")) {
		if (req.method != QStringLiteral("POST")) {
			return FMcpProtocol::buildHttpResponse(405, QStringLiteral("Method Not Allowed"), FMcpResult::fail(QStringLiteral("Use POST for /rpc"), QStringLiteral("METHOD_NOT_ALLOWED")));
		}
		QJsonObject result = m_router->dispatchRawJson(req.body);
		return FMcpProtocol::buildHttpResponse(200, QStringLiteral("OK"), result);
	}
	return FMcpProtocol::buildHttpResponse(404, QStringLiteral("Not Found"), FMcpResult::fail(QStringLiteral("Unknown endpoint '%1'").arg(req.path), QStringLiteral("NOT_FOUND")));
}

QByteArray FMcpServer::processRawHttp(const QByteArray &rawRequest) const {
	FMcpHttpRequest req = FMcpProtocol::parseHttpRequest(rawRequest);
	if (!req.complete) {
		return QByteArray();
	}
	return routeHttpRequest(req);
}

void FMcpServer::onNewConnection() {
	while (m_tcpServer->hasPendingConnections()) {
		QTcpSocket *socket = m_tcpServer->nextPendingConnection();
		if (!socket) {
			continue;
		}
		m_buffers.insert(socket, QByteArray());
		connect(socket, &QTcpSocket::readyRead, this, &FMcpServer::onSocketReadyRead);
		connect(socket, &QTcpSocket::disconnected, this, &FMcpServer::onSocketDisconnected);
	}
}

void FMcpServer::onSocketReadyRead() {
	auto *socket = qobject_cast<QTcpSocket *>(sender());
	if (!socket || !m_buffers.contains(socket)) {
		return;
	}
	m_buffers[socket].append(socket->readAll());
	QByteArray response = processRawHttp(m_buffers.value(socket));
	if (response.isEmpty()) {
		return;
	}
	m_buffers.remove(socket);
	socket->write(response);
	socket->flush();
	socket->disconnectFromHost();
}

void FMcpServer::onSocketDisconnected() {
	auto *socket = qobject_cast<QTcpSocket *>(sender());
	if (!socket) {
		return;
	}
	m_buffers.remove(socket);
	socket->deleteLater();
}
