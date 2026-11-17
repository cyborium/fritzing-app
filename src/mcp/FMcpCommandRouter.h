/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#ifndef FMCPCOMMANDROUTER_H
#define FMCPCOMMANDROUTER_H

#include "IFritzingWorkspace.h"

#include <QHash>
#include <QStringList>
#include <functional>

class FMcpCommandRouter {
public:
	using HandlerFunc = std::function<QJsonObject(const QJsonObject &)>;

	explicit FMcpCommandRouter(IFritzingWorkspace *workspace);

	QJsonObject dispatch(const QString &method, const QJsonObject &params) const;
	QJsonObject dispatchRawJson(const QByteArray &rawBody) const;
	QStringList registeredMethods() const;

private:
	void registerHandlers();
	void registerStage2Handlers();
	void registerStage3Handlers();

	QJsonObject handleGetState(const QJsonObject &params) const;
	QJsonObject handleSetView(const QJsonObject &params) const;
	QJsonObject handleUndo(const QJsonObject &params) const;
	QJsonObject handleRedo(const QJsonObject &params) const;
	QJsonObject handleRenderImage(const QJsonObject &params) const;
	QJsonObject handleOpenSketch(const QJsonObject &params) const;
	QJsonObject handleSaveSketch(const QJsonObject &params) const;

	QJsonObject handleSearchParts(const QJsonObject &params) const;
	QJsonObject handleGetPartInfo(const QJsonObject &params) const;

	QJsonObject handleAddPart(const QJsonObject &params) const;
	QJsonObject handleMovePart(const QJsonObject &params) const;
	QJsonObject handleRotatePart(const QJsonObject &params) const;
	QJsonObject handleDeletePart(const QJsonObject &params) const;
	QJsonObject handleSetProperty(const QJsonObject &params) const;

	QJsonObject handleConnectWire(const QJsonObject &params) const;
	QJsonObject handleDeleteWire(const QJsonObject &params) const;
	QJsonObject handleGetNetlist(const QJsonObject &params) const;

	QJsonObject handleGetPcbBoardInfo(const QJsonObject &params) const;
	QJsonObject handleResizePcbBoard(const QJsonObject &params) const;
	QJsonObject handleSetPcbLayers(const QJsonObject &params) const;
	QJsonObject handleRunDrc(const QJsonObject &params) const;
	QJsonObject handleSetPcbCopperFill(const QJsonObject &params) const;

	QJsonObject handleGetSpiceNetlist(const QJsonObject &params) const;
	QJsonObject handleControlSimulation(const QJsonObject &params) const;

	QJsonObject handleExportBomCsv(const QJsonObject &params) const;
	QJsonObject handleExportGerber(const QJsonObject &params) const;

	QJsonObject handleAutoroutePcb(const QJsonObject &params) const;
	QJsonObject handleClearPcbTraces(const QJsonObject &params) const;
	QJsonObject handleSetPcbTraceStyle(const QJsonObject &params) const;
	QJsonObject handleImportFzpz(const QJsonObject &params) const;
	QJsonObject handleClearSketch(const QJsonObject &params) const;
	QJsonObject handleAddSketchNote(const QJsonObject &params) const;
	QJsonObject handleAddPowerSymbol(const QJsonObject &params) const;
	QJsonObject handleExportViewSvg(const QJsonObject &params) const;
	QJsonObject handleExportViewPdf(const QJsonObject &params) const;

	IFritzingWorkspace *m_workspace = nullptr;
	QHash<QString, HandlerFunc> m_handlers;
};

#endif // FMCPCOMMANDROUTER_H
