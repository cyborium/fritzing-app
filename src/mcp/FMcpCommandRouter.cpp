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
	registerStage2Handlers();
	registerStage3Handlers();
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

void FMcpCommandRouter::registerStage2Handlers() {
	m_handlers.insert(QStringLiteral("sketch.open"), [this](const QJsonObject &p) { return handleOpenSketch(p); });
	m_handlers.insert(QStringLiteral("sketch.save"), [this](const QJsonObject &p) { return handleSaveSketch(p); });

	m_handlers.insert(QStringLiteral("pcb.get_board_info"), [this](const QJsonObject &p) { return handleGetPcbBoardInfo(p); });
	m_handlers.insert(QStringLiteral("pcb.resize_board"), [this](const QJsonObject &p) { return handleResizePcbBoard(p); });
	m_handlers.insert(QStringLiteral("pcb.set_layers"), [this](const QJsonObject &p) { return handleSetPcbLayers(p); });
	m_handlers.insert(QStringLiteral("pcb.run_drc"), [this](const QJsonObject &p) { return handleRunDrc(p); });
	m_handlers.insert(QStringLiteral("pcb.copper_fill"), [this](const QJsonObject &p) { return handleSetPcbCopperFill(p); });

	m_handlers.insert(QStringLiteral("sim.get_spice_netlist"), [this](const QJsonObject &p) { return handleGetSpiceNetlist(p); });
	m_handlers.insert(QStringLiteral("sim.control"), [this](const QJsonObject &p) { return handleControlSimulation(p); });

	m_handlers.insert(QStringLiteral("export.bom"), [this](const QJsonObject &p) { return handleExportBomCsv(p); });
	m_handlers.insert(QStringLiteral("export.gerber"), [this](const QJsonObject &p) { return handleExportGerber(p); });
}

void FMcpCommandRouter::registerStage3Handlers() {
	m_handlers.insert(QStringLiteral("pcb.autoroute"), [this](const QJsonObject &p) { return handleAutoroutePcb(p); });
	m_handlers.insert(QStringLiteral("pcb.clear_traces"), [this](const QJsonObject &p) { return handleClearPcbTraces(p); });
	m_handlers.insert(QStringLiteral("pcb.set_trace_style"), [this](const QJsonObject &p) { return handleSetPcbTraceStyle(p); });

	m_handlers.insert(QStringLiteral("parts.import_fzpz"), [this](const QJsonObject &p) { return handleImportFzpz(p); });

	m_handlers.insert(QStringLiteral("sketch.clear"), [this](const QJsonObject &p) { return handleClearSketch(p); });
	m_handlers.insert(QStringLiteral("sketch.add_note"), [this](const QJsonObject &p) { return handleAddSketchNote(p); });
	m_handlers.insert(QStringLiteral("sketch.add_power_symbol"), [this](const QJsonObject &p) { return handleAddPowerSymbol(p); });

	m_handlers.insert(QStringLiteral("export.svg"), [this](const QJsonObject &p) { return handleExportViewSvg(p); });
	m_handlers.insert(QStringLiteral("export.pdf"), [this](const QJsonObject &p) { return handleExportViewPdf(p); });
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

QJsonObject FMcpCommandRouter::handleOpenSketch(const QJsonObject &params) const {
	QString filePath;
	QString err;
	if (!FMcpValidator::validateSketchPath(params, filePath, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->openSketch(filePath);
}

QJsonObject FMcpCommandRouter::handleSaveSketch(const QJsonObject &params) const {
	QString filePath;
	QString err;
	if (!FMcpValidator::validateSketchPath(params, filePath, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->saveSketch(filePath);
}

QJsonObject FMcpCommandRouter::handleGetPcbBoardInfo(const QJsonObject &params) const {
	Q_UNUSED(params);
	return m_workspace->getPcbBoardInfo();
}

QJsonObject FMcpCommandRouter::handleResizePcbBoard(const QJsonObject &params) const {
	FMcpBoardResizeSpec spec;
	QString err;
	if (!FMcpValidator::validateBoardResizeParams(params, spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->resizePcbBoard(spec);
}

QJsonObject FMcpCommandRouter::handleSetPcbLayers(const QJsonObject &params) const {
	int layers = 0;
	QString err;
	if (!FMcpValidator::validateBoardLayers(params, layers, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->setPcbLayers(layers);
}

QJsonObject FMcpCommandRouter::handleRunDrc(const QJsonObject &params) const {
	Q_UNUSED(params);
	return m_workspace->runDesignRulesCheck();
}

QJsonObject FMcpCommandRouter::handleSetPcbCopperFill(const QJsonObject &params) const {
	QString mode;
	QString err;
	if (!FMcpValidator::validateCopperFillMode(params, mode, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->setPcbCopperFill(mode);
}

QJsonObject FMcpCommandRouter::handleGetSpiceNetlist(const QJsonObject &params) const {
	Q_UNUSED(params);
	return m_workspace->getSpiceNetlist();
}

QJsonObject FMcpCommandRouter::handleControlSimulation(const QJsonObject &params) const {
	QString action;
	QString err;
	if (!FMcpValidator::validateSimControlAction(params, action, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->controlSimulation(action);
}

QJsonObject FMcpCommandRouter::handleExportBomCsv(const QJsonObject &params) const {
	QString outputPath;
	QString err;
	if (!FMcpValidator::optionalString(params, QStringLiteral("outputPath"), QString(), outputPath, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->exportBomCsv(outputPath);
}

QJsonObject FMcpCommandRouter::handleExportGerber(const QJsonObject &params) const {
	FMcpGerberSpec spec;
	QString err;
	if (!FMcpValidator::validateGerberParams(params, spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->exportGerber(spec);
}

QJsonObject FMcpCommandRouter::handleAutoroutePcb(const QJsonObject &params) const {
	int maxCycles = 16;
	QString err;
	if (!FMcpValidator::validateAutorouteParams(params, maxCycles, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->autoroutePcb(maxCycles);
}

QJsonObject FMcpCommandRouter::handleClearPcbTraces(const QJsonObject &params) const {
	Q_UNUSED(params);
	return m_workspace->clearPcbTraces();
}

QJsonObject FMcpCommandRouter::handleSetPcbTraceStyle(const QJsonObject &params) const {
	FMcpTraceStyleSpec spec;
	QString err;
	if (!FMcpValidator::validateTraceStyleParams(params, spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->setPcbTraceStyle(spec);
}

QJsonObject FMcpCommandRouter::handleImportFzpz(const QJsonObject &params) const {
	QString filePath;
	bool addToBin = true;
	QString err;
	if (!FMcpValidator::validateFzpzImportParams(params, filePath, addToBin, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->importFzpz(filePath, addToBin);
}

QJsonObject FMcpCommandRouter::handleClearSketch(const QJsonObject &params) const {
	Q_UNUSED(params);
	return m_workspace->clearSketch();
}

QJsonObject FMcpCommandRouter::handleAddSketchNote(const QJsonObject &params) const {
	FMcpNoteSpec spec;
	QString err;
	if (!FMcpValidator::validateNoteParams(params, spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->addSketchNote(spec);
}

QJsonObject FMcpCommandRouter::handleAddPowerSymbol(const QJsonObject &params) const {
	FMcpPowerSymbolSpec spec;
	QString err;
	if (!FMcpValidator::validatePowerSymbolParams(params, spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->addPowerSymbol(spec);
}

QJsonObject FMcpCommandRouter::handleExportViewSvg(const QJsonObject &params) const {
	FMcpVectorExportSpec spec;
	QString err;
	if (!FMcpValidator::validateVectorExportParams(params, QStringLiteral(".svg"), spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->exportViewSvg(spec);
}

QJsonObject FMcpCommandRouter::handleExportViewPdf(const QJsonObject &params) const {
	FMcpVectorExportSpec spec;
	QString err;
	if (!FMcpValidator::validateVectorExportParams(params, QStringLiteral(".pdf"), spec, err)) {
		return FMcpResult::fail(err);
	}
	return m_workspace->exportViewPdf(spec);
}
