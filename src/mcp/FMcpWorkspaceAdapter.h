/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#ifndef FMCPWORKSPACEADAPTER_H
#define FMCPWORKSPACEADAPTER_H

#include "IFritzingWorkspace.h"
#include "../viewlayer.h"

#include <QObject>
#include <QSet>

class MainWindow;
class SketchWidget;
class ItemBase;
class ConnectorItem;
class Wire;
class ModelPart;

class FMcpWorkspaceAdapter : public QObject, public IFritzingWorkspace {
	Q_OBJECT
public:
	explicit FMcpWorkspaceAdapter(QObject *parent = nullptr);
	~FMcpWorkspaceAdapter() override = default;

	QJsonObject getSketchState(const QString &viewName) override;
	QJsonObject setActiveView(const QString &viewName) override;
	QJsonObject undo() override;
	QJsonObject redo() override;
	QJsonObject renderImage(const FMcpRenderSpec &spec) override;

	QJsonObject searchParts(const QString &query, int limit) override;
	QJsonObject getPartInfo(const QString &moduleID) override;

	QJsonObject addPart(const FMcpPartSpec &spec) override;
	QJsonObject movePart(const FMcpMoveSpec &spec) override;
	QJsonObject rotatePart(const QString &partRef, double degrees) override;
	QJsonObject deletePart(const QString &partRef) override;
	QJsonObject setPartProperty(const QString &partRef, const QString &propName, const QString &propValue) override;

	QJsonObject connectWire(const FMcpWireSpec &spec) override;
	QJsonObject deleteWire(long wireId) override;
	QJsonObject getNetlist() override;

private:
	MainWindow *currentMainWindow() const;
	SketchWidget *resolveView(MainWindow *mw, const QString &viewName) const;
	ViewLayer::ViewID toViewId(const QString &viewName) const;
	QString fromViewId(ViewLayer::ViewID viewId) const;

	ItemBase *resolvePartInView(SketchWidget *view, const QString &partRef, QString &err) const;
	ConnectorItem *resolvePinInPart(ItemBase *part, const QString &pinRef, QString &err) const;
	bool resolveWireEndpoints(SketchWidget *view, const FMcpWireSpec &spec, ConnectorItem *&fromConn, ConnectorItem *&toConn, QJsonObject &errObj) const;

	QJsonObject serializePart(ItemBase *part) const;
	QJsonArray serializePartConnectors(ItemBase *part) const;
	QJsonObject serializeWire(Wire *wire) const;
	QJsonObject serializeModelPartSummary(ModelPart *mp) const;

	bool areConnectorsDirectlyWired(ConnectorItem *fromConn, ConnectorItem *toConn) const;
};

#endif // FMCPWORKSPACEADAPTER_H
