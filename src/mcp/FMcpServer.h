/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#ifndef FMCPSERVER_H
#define FMCPSERVER_H

#include "FMcpCommandRouter.h"
#include "FMcpProtocol.h"

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <memory>

class FMcpServer : public QObject {
	Q_OBJECT
public:
	static constexpr quint16 DefaultPort = 8765;

	explicit FMcpServer(IFritzingWorkspace *workspace, QObject *parent = nullptr);
	~FMcpServer() override;

	bool start(quint16 port = DefaultPort);
	void stop();
	bool isRunning() const;
	quint16 serverPort() const;

	QByteArray processRawHttp(const QByteArray &rawRequest) const;
	QByteArray routeHttpRequest(const FMcpHttpRequest &req) const;

private Q_SLOTS:
	void onNewConnection();
	void onSocketReadyRead();
	void onSocketDisconnected();

private:
	std::unique_ptr<FMcpCommandRouter> m_router;
	QTcpServer *m_tcpServer = nullptr;
	QHash<QTcpSocket *, QByteArray> m_buffers;
};

#endif // FMCPSERVER_H
