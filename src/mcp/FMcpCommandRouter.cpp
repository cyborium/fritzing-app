/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#include "FMcpCommandRouter.h"
#include "FMcpProtocol.h"
#include "FMcpValidator.h"

#include <algorithm>

FMcpCommandRouter::FMcpCommandRouter(IFritzingWorkspace *workspace)
	: m_workspace(workspace) {
	registerHandlers();
}

void FMcpCommandRouter::registerHandlers() {
	m_handlers.insert(QStringLiteral("sketch.get_state"), [this](const QJsonObject &p) { return handleGetState(p); });
	m_handlers.insert(QStringLiteral("sketch.set_view"), [this](const QJsonObject &p) { return handleSetView(p); });
	m_handlers.insert(QStringLiteral("sketch.undo"), [this](const QJsonObject &p) { return handleUndo(p); });
	m_handlers.insert(QStringLiteral("sketch.redo"), [this](const QJsonObject &p) { return handleRedo(p); });
	m_handlers.insert(QStringLiteral("sketch.render_image"), [this](const QJsonObject &p) { return handleRenderImage(p); });

	m_handlers.insert(QStringLiteral("parts.search"), [this](const QJsonObject &p) { return handleSearchParts(p); });
	m_handlers.insert(QStringLiteral("parts.get_info"), [this](const QJsonObject &p) { return handleGetPartInfo(p); });

	m_handlers.insert(QStringLiteral("part.add"), [this](const QJsonObject &p) { return handleAddPart(p); });
	m_handlers.insert(QStringLiteral("part.move"), [this](const QJsonObject &p) { return handleMovePart(p); });
	m_handlers.insert(QStringLiteral("part.rotate"), [this](const QJsonObject &p) { return handleRotatePart(p); });
	m_handlers.insert(QStringLiteral("part.delete"), [this](const QJsonObject &p) { return handleDeletePart(p); });
	m_handlers.insert(QStringLiteral("part.set_property"), [this](const QJsonObject &p) { return handleSetProperty(p); });

	m_handlers.insert(QStringLiteral("wire.connect"), [this](const QJsonObject &p) { return handleConnectWire(p); });
	m_handlers.insert(QStringLiteral("wire.delete"), [this](const QJsonObject &p) { return handleDeleteWire(p); });
	m_handlers.insert(QStringLiteral("wire.get_netlist"), [this](const QJsonObject &p) { return handleGetNetlist(p); });
}

QStringList FMcpCommandRouter::registeredMethods() const {
	QStringList keys = m_handlers.keys();
	std::sort(keys.begin(), keys.end());
	return keys;
}

QJsonObject FMcpCommandRouter::dispatch(const QString &method, const QJsonObject &params) const {
	if (!m_workspace) {
		return FMcpResult::fail(QStringLiteral("Workspace is not available"), QStringLiteral("WORKSPACE_UNAVAILABLE"));
	}
	auto it = m_handlers.constFind(method.trimmed());
	if (it == m_handlers.constEnd()) {
		return FMcpResult::fail(QStringLiteral("Unknown method '%1'").arg(method), QStringLiteral("UNKNOWN_METHOD"));
	}
	return it.value()(params);
}

QJsonObject FMcpCommandRouter::dispatchRawJson(const QByteArray &rawBody) const {
	QString method;
	QJsonObject params;
	QString err;
	if (!FMcpProtocol::parseRpcBody(rawBody, method, params, err)) {
		return FMcpResult::fail(err, QStringLiteral("PARSE_ERROR"));
	}
	return dispatch(method, params);
}

QJsonObject FMcpCommandRouter::handleGetState(const QJsonObject &params) const {
	QString rawView;
	QString normalizedView;
	QString err;
	if (!FMcpValidator::optionalString(params, QStringLiteral("view"), QString(), rawView, err) ||
		!FMcpValidator::validateViewName(rawView, true, normalizedView, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->getSketchState(normalizedView);
}

QJsonObject FMcpCommandRouter::handleSetView(const QJsonObject &params) const {
	QString rawView;
	QString normalizedView;
	QString err;
	if (!FMcpValidator::requireNonEmptyString(params, QStringLiteral("view"), rawView, err) ||
		!FMcpValidator::validateViewName(rawView, false, normalizedView, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->setActiveView(normalizedView);
}

QJsonObject FMcpCommandRouter::handleUndo(const QJsonObject &params) const {
	Q_UNUSED(params);
	return m_workspace->undo();
}

QJsonObject FMcpCommandRouter::handleRedo(const QJsonObject &params) const {
	Q_UNUSED(params);
	return m_workspace->redo();
}

QJsonObject FMcpCommandRouter::handleRenderImage(const QJsonObject &params) const {
	FMcpRenderSpec spec;
	QString err;
	if (!FMcpValidator::validateRenderParams(params, spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->renderImage(spec);
}

QJsonObject FMcpCommandRouter::handleSearchParts(const QJsonObject &params) const {
	QString query;
	int limit = 25;
	QString err;
	if (!FMcpValidator::validateSearchParams(params, query, limit, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->searchParts(query, limit);
}

QJsonObject FMcpCommandRouter::handleGetPartInfo(const QJsonObject &params) const {
	QString moduleID;
	QString err;
	if (!FMcpValidator::requireNonEmptyString(params, QStringLiteral("moduleID"), moduleID, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->getPartInfo(moduleID);
}

QJsonObject FMcpCommandRouter::handleAddPart(const QJsonObject &params) const {
	FMcpPartSpec spec;
	QString err;
	if (!FMcpValidator::validatePartAddParams(params, spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->addPart(spec);
}

QJsonObject FMcpCommandRouter::handleMovePart(const QJsonObject &params) const {
	FMcpMoveSpec spec;
	QString err;
	if (!FMcpValidator::validatePartMoveParams(params, spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->movePart(spec);
}

QJsonObject FMcpCommandRouter::handleRotatePart(const QJsonObject &params) const {
	QString partRef;
	double degrees = 0.0;
	QString err;
	if (!FMcpValidator::requireNonEmptyString(params, QStringLiteral("part"), partRef, err) ||
		!FMcpValidator::validateRotation(params, degrees, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->rotatePart(partRef, degrees);
}

QJsonObject FMcpCommandRouter::handleDeletePart(const QJsonObject &params) const {
	QString partRef;
	QString err;
	if (!FMcpValidator::requireNonEmptyString(params, QStringLiteral("part"), partRef, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->deletePart(partRef);
}

QJsonObject FMcpCommandRouter::handleSetProperty(const QJsonObject &params) const {
	QString partRef;
	QString propName;
	QString propValue;
	QString err;
	if (!FMcpValidator::validateSetPropertyParams(params, partRef, propName, propValue, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->setPartProperty(partRef, propName, propValue);
}

QJsonObject FMcpCommandRouter::handleConnectWire(const QJsonObject &params) const {
	FMcpWireSpec spec;
	QString err;
	if (!FMcpValidator::validateWireConnectParams(params, spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->connectWire(spec);
}

QJsonObject FMcpCommandRouter::handleDeleteWire(const QJsonObject &params) const {
	long wireId = 0;
	QString err;
	if (!FMcpValidator::validateWireDeleteParams(params, wireId, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->deleteWire(wireId);
}

QJsonObject FMcpCommandRouter::handleGetNetlist(const QJsonObject &params) const {
	Q_UNUSED(params);
	return m_workspace->getNetlist();
}
