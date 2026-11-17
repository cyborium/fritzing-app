/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#include "FMcpWorkspaceAdapter.h"

#include "../commands.h"
#include "../connectors/connector.h"
#include "../connectors/connectoritem.h"
#include "../fapplication.h"
#include "../items/itembase.h"
#include "../items/resizableboard.h"
#include "../items/wire.h"
#include "../mainwindow/mainwindow.h"
#include "../model/modelpart.h"
#include "../referencemodel/referencemodel.h"
#include "../sketch/pcbsketchwidget.h"
#include "../sketch/sketchwidget.h"
#include "../svg/gerbergenerator.h"
#include "../waitpushundostack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <cmath>

FMcpWorkspaceAdapter::FMcpWorkspaceAdapter(QObject *parent)
	: QObject(parent) {
}

MainWindow *FMcpWorkspaceAdapter::currentMainWindow() const {
	auto *app = qobject_cast<FApplication *>(qApp);
	return app ? app->currentMainWindow() : nullptr;
}

ViewLayer::ViewID FMcpWorkspaceAdapter::toViewId(const QString &viewName) const {
	QString v = viewName.trimmed().toLower();
	if (v == QStringLiteral("schematic")) {
		return ViewLayer::SchematicView;
	}
	if (v == QStringLiteral("pcb")) {
		return ViewLayer::PCBView;
	}
	return ViewLayer::BreadboardView;
}

QString FMcpWorkspaceAdapter::fromViewId(ViewLayer::ViewID viewId) const {
	if (viewId == ViewLayer::SchematicView) {
		return QStringLiteral("schematic");
	}
	if (viewId == ViewLayer::PCBView) {
		return QStringLiteral("pcb");
	}
	return QStringLiteral("breadboard");
}

SketchWidget *FMcpWorkspaceAdapter::resolveView(MainWindow *mw, const QString &viewName) const {
	if (!mw) {
		return nullptr;
	}
	if (!viewName.isEmpty()) {
		ViewLayer::ViewID targetId = toViewId(viewName);
		mw->setCurrentView(targetId);
		return mw->sketchWidgetForView(targetId);
	}
	for (SketchWidget *sw : mw->sketchWidgets()) {
		if (sw && sw->isVisible()) {
			return sw;
		}
	}
	mw->setCurrentView(ViewLayer::BreadboardView);
	return mw->sketchWidgetForView(ViewLayer::BreadboardView);
}

QJsonArray FMcpWorkspaceAdapter::serializePartConnectors(ItemBase *part) const {
	QJsonArray pins;
	for (ConnectorItem *ci : part->cachedConnectorItemsConst()) {
		if (!ci) {
			continue;
		}
		QPointF scenePt = ci->sceneAdjustedTerminalPoint(nullptr);
		QJsonObject pinObj;
		pinObj[QStringLiteral("id")] = ci->connectorSharedID();
		pinObj[QStringLiteral("name")] = ci->connectorSharedName();
		pinObj[QStringLiteral("x")] = scenePt.x();
		pinObj[QStringLiteral("y")] = scenePt.y();
		pinObj[QStringLiteral("connectionsCount")] = ci->connectionsCount();
		pins.append(pinObj);
	}
	return pins;
}

QJsonObject FMcpWorkspaceAdapter::serializePart(ItemBase *part) const {
	QRectF sbr = part->sceneBoundingRect();
	QJsonObject propsObj;
	if (part->modelPart()) {
		const auto &props = part->modelPart()->properties();
		for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
			propsObj[it.key()] = it.value();
		}
	}
	QJsonObject obj;
	obj[QStringLiteral("id")] = static_cast<qint64>(part->id());
	obj[QStringLiteral("title")] = part->instanceTitle();
	obj[QStringLiteral("name")] = part->constTitle();
	obj[QStringLiteral("moduleID")] = part->moduleID();
	obj[QStringLiteral("family")] = part->family();
	obj[QStringLiteral("x")] = part->pos().x();
	obj[QStringLiteral("y")] = part->pos().y();
	obj[QStringLiteral("width")] = sbr.width();
	obj[QStringLiteral("height")] = sbr.height();
	obj[QStringLiteral("locked")] = part->moveLock();
	obj[QStringLiteral("properties")] = propsObj;
	obj[QStringLiteral("connectors")] = serializePartConnectors(part);
	return obj;
}

QJsonObject FMcpWorkspaceAdapter::serializeWire(Wire *wire) const {
	QJsonObject obj;
	obj[QStringLiteral("id")] = static_cast<qint64>(wire->id());
	obj[QStringLiteral("color")] = wire->colorString();
	obj[QStringLiteral("ratsnest")] = wire->getRatsnest();
	QPointF p1 = wire->pos() + wire->line().p1();
	QPointF p2 = wire->pos() + wire->line().p2();
	obj[QStringLiteral("x1")] = p1.x();
	obj[QStringLiteral("y1")] = p1.y();
	obj[QStringLiteral("x2")] = p2.x();
	obj[QStringLiteral("y2")] = p2.y();

	QJsonArray ends;
	for (ConnectorItem *endConn : {wire->connector0(), wire->connector1()}) {
		if (!endConn) {
			continue;
		}
		for (ConnectorItem *target : endConn->connectedToItems()) {
			if (target && target->attachedTo() && target->attachedTo()->itemType() != ModelPart::Wire) {
				QJsonObject connObj;
				connObj[QStringLiteral("partId")] = static_cast<qint64>(target->attachedToID());
				connObj[QStringLiteral("partTitle")] = target->attachedToTitle();
				connObj[QStringLiteral("pin")] = target->connectorSharedID();
				ends.append(connObj);
			}
		}
	}
	obj[QStringLiteral("endpoints")] = ends;
	return obj;
}

QJsonObject FMcpWorkspaceAdapter::getSketchState(const QString &viewName) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, viewName);
	if (!view || !view->scene()) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	QJsonArray partsArr;
	QJsonArray wiresArr;
	QSet<long> seenIds;

	for (QGraphicsItem *gItem : view->scene()->items()) {
		auto *base = dynamic_cast<ItemBase *>(gItem);
		if (!base || base->viewID() != view->viewID() || !base->isVisible()) {
			continue;
		}
		ItemBase *chief = base->layerKinChief();
		if (seenIds.contains(chief->id())) {
			continue;
		}
		seenIds.insert(chief->id());

		if (auto *wire = qobject_cast<Wire *>(chief)) {
			wiresArr.append(serializeWire(wire));
		} else if (chief->itemType() != ModelPart::Space) {
			partsArr.append(serializePart(chief));
		}
	}

	QJsonObject data;
	data[QStringLiteral("view")] = fromViewId(view->viewID());
	data[QStringLiteral("parts")] = partsArr;
	data[QStringLiteral("wires")] = wiresArr;
	data[QStringLiteral("partCount")] = partsArr.size();
	data[QStringLiteral("wireCount")] = wiresArr.size();
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::setActiveView(const QString &viewName) {
	MainWindow *mw = currentMainWindow();
	if (!mw) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	ViewLayer::ViewID vid = toViewId(viewName);
	mw->setCurrentView(vid);
	QJsonObject data;
	data[QStringLiteral("view")] = fromViewId(vid);
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::undo() {
	MainWindow *mw = currentMainWindow();
	if (!mw || !mw->undoAction()) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	if (!mw->undoAction()->isEnabled()) {
		return FMcpResult::fail(QStringLiteral("Nothing to undo"), QStringLiteral("EMPTY_UNDO_STACK"));
	}
	mw->undoAction()->trigger();
	return FMcpResult::ok();
}

QJsonObject FMcpWorkspaceAdapter::redo() {
	MainWindow *mw = currentMainWindow();
	if (!mw || !mw->redoAction()) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	if (!mw->redoAction()->isEnabled()) {
		return FMcpResult::fail(QStringLiteral("Nothing to redo"), QStringLiteral("EMPTY_REDO_STACK"));
	}
	mw->redoAction()->trigger();
	return FMcpResult::ok();
}

static QRectF computeRenderRect(SketchWidget *view, bool cropToItems) {
	QRectF rect = cropToItems ? view->calculateVisibleItemsBoundingRect() : view->scene()->sceneRect();
	if (rect.isEmpty() || rect.width() < 10.0 || rect.height() < 10.0) {
		rect = QRectF(-200.0, -200.0, 600.0, 450.0);
	} else {
		rect.adjust(-30.0, -30.0, 30.0, 30.0);
	}
	return rect;
}

static QByteArray renderSceneToPng(SketchWidget *view, const FMcpRenderSpec &spec) {
	QRectF sourceRect = computeRenderRect(view, spec.cropToItems);
	QImage image(spec.width, spec.height, QImage::Format_ARGB32_Premultiplied);
	image.fill(view->background());

	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
	view->scene()->render(&painter, QRectF(0, 0, spec.width, spec.height), sourceRect, Qt::KeepAspectRatio);
	painter.end();

	QByteArray pngBytes;
	QBuffer buffer(&pngBytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return pngBytes;
}

QJsonObject FMcpWorkspaceAdapter::renderImage(const FMcpRenderSpec &spec) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, spec.viewName);
	if (!view || !view->scene()) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	QByteArray pngBytes = renderSceneToPng(view, spec);
	if (!spec.outputPath.isEmpty()) {
		QFile outFile(spec.outputPath);
		if (!outFile.open(QIODevice::WriteOnly)) {
			return FMcpResult::fail(QStringLiteral("Failed to write output file '%1'").arg(spec.outputPath), QStringLiteral("IO_ERROR"));
		}
		outFile.write(pngBytes);
		outFile.close();
	}

	QJsonObject data;
	data[QStringLiteral("view")] = fromViewId(view->viewID());
	data[QStringLiteral("width")] = spec.width;
	data[QStringLiteral("height")] = spec.height;
	data[QStringLiteral("format")] = QStringLiteral("png");
	data[QStringLiteral("imageBase64")] = QString::fromLatin1(pngBytes.toBase64());
	if (!spec.outputPath.isEmpty()) {
		data[QStringLiteral("outputPath")] = spec.outputPath;
	}
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::serializeModelPartSummary(ModelPart *mp) const {
	QJsonObject obj;
	obj[QStringLiteral("moduleID")] = mp->moduleID();
	obj[QStringLiteral("title")] = mp->title();
	obj[QStringLiteral("family")] = mp->family();
	obj[QStringLiteral("description")] = mp->description().left(200);
	QJsonObject propsObj;
	const auto &props = mp->properties();
	for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
		propsObj[it.key()] = it.value();
	}
	obj[QStringLiteral("properties")] = propsObj;
	return obj;
}

QJsonObject FMcpWorkspaceAdapter::searchParts(const QString &query, int limit) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, QString());
	ReferenceModel *refModel = view ? view->referenceModel() : nullptr;
	if (!refModel) {
		return FMcpResult::fail(QStringLiteral("ReferenceModel is not initialized"), QStringLiteral("NO_REFERENCE_MODEL"));
	}

	QList<ModelPart *> matches = refModel->search(query, false);
	QJsonArray resultsArr;
	int count = 0;
	for (ModelPart *mp : matches) {
		if (!mp || mp->moduleID().isEmpty()) {
			continue;
		}
		resultsArr.append(serializeModelPartSummary(mp));
		if (++count >= limit) {
			break;
		}
	}

	QJsonObject data;
	data[QStringLiteral("query")] = query;
	data[QStringLiteral("totalFound")] = matches.size();
	data[QStringLiteral("results")] = resultsArr;
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::getPartInfo(const QString &moduleID) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, QString());
	ReferenceModel *refModel = view ? view->referenceModel() : nullptr;
	if (!refModel) {
		return FMcpResult::fail(QStringLiteral("ReferenceModel is not initialized"), QStringLiteral("NO_REFERENCE_MODEL"));
	}

	ModelPart *mp = refModel->retrieveModelPart(moduleID);
	if (!mp) {
		return FMcpResult::fail(QStringLiteral("Part with moduleID '%1' not found").arg(moduleID), QStringLiteral("NOT_FOUND"));
	}

	mp->initConnectors(false);
	QJsonObject info = serializeModelPartSummary(mp);
	QJsonArray connsArr;
	const auto &conns = mp->connectors();
	for (auto it = conns.constBegin(); it != conns.constEnd(); ++it) {
		Connector *c = it.value();
		if (!c) {
			continue;
		}
		QJsonObject cObj;
		cObj[QStringLiteral("id")] = c->connectorSharedID();
		cObj[QStringLiteral("name")] = c->connectorSharedName();
		cObj[QStringLiteral("description")] = c->connectorSharedDescription();
		cObj[QStringLiteral("type")] = Connector::connectorNameFromType(c->connectorType());
		connsArr.append(cObj);
	}
	info[QStringLiteral("connectors")] = connsArr;
	return FMcpResult::ok(info);
}

QJsonObject FMcpWorkspaceAdapter::addPart(const FMcpPartSpec &spec) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, spec.viewName);
	if (!view || !view->referenceModel()) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	ModelPart *mp = view->referenceModel()->retrieveModelPart(spec.moduleID);
	if (!mp) {
		return FMcpResult::fail(QStringLiteral("Unknown moduleID '%1'").arg(spec.moduleID), QStringLiteral("NOT_FOUND"));
	}

	long newId = ItemBase::getNextID();
	ViewGeometry vg;
	vg.setLoc(QPointF(spec.x, spec.y));
	ViewLayer::ViewLayerPlacement vlp = view->defaultViewLayerPlacement(mp);

	auto *parentCmd = new QUndoCommand(QObject::tr("Add %1").arg(mp->title()));
	new CleanUpWiresCommand(view, CleanUpWiresCommand::UndoOnly, parentCmd);
	new CleanUpRatsnestsCommand(view, CleanUpWiresCommand::UndoOnly, parentCmd);
	new AddItemCommand(view, BaseCommand::CrossView, spec.moduleID, vlp, vg, newId, true, -1, parentCmd);
	new ShowLabelFirstTimeCommand(view, BaseCommand::CrossView, newId, true, true, parentCmd);
	new CleanUpRatsnestsCommand(view, CleanUpWiresCommand::RedoOnly, parentCmd);
	new CleanUpWiresCommand(view, CleanUpWiresCommand::RedoOnly, parentCmd);
	view->undoStack()->push(parentCmd);

	ItemBase *newPart = view->findItem(newId);
	if (!newPart) {
		return FMcpResult::fail(QStringLiteral("Failed to place part '%1' in view").arg(spec.moduleID), QStringLiteral("PLACEMENT_FAILED"));
	}
	return FMcpResult::ok(serializePart(newPart->layerKinChief()));
}

ItemBase *FMcpWorkspaceAdapter::resolvePartInView(SketchWidget *view, const QString &partRef, QString &err) const {
	bool isId = false;
	long numericId = partRef.toLong(&isId);
	if (isId) {
		ItemBase *byId = view->findItem(numericId);
		if (byId) {
			return byId->layerKinChief();
		}
	}

	ItemBase *matched = nullptr;
	for (QGraphicsItem *gItem : view->scene()->items()) {
		auto *base = dynamic_cast<ItemBase *>(gItem);
		if (!base || base->viewID() != view->viewID()) {
			continue;
		}
		ItemBase *chief = base->layerKinChief();
		if (chief->instanceTitle().compare(partRef, Qt::CaseInsensitive) == 0) {
			if (matched && matched->id() != chief->id()) {
				err = QStringLiteral("Ambiguous part title '%1'; use numeric part id").arg(partRef);
				return nullptr;
			}
			matched = chief;
		}
	}
	if (!matched) {
		err = QStringLiteral("Part '%1' not found in active view").arg(partRef);
	}
	return matched;
}

QJsonObject FMcpWorkspaceAdapter::movePart(const FMcpMoveSpec &spec) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, QString());
	if (!view) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	QString err;
	ItemBase *part = resolvePartInView(view, spec.partRef, err);
	if (!part) {
		return FMcpResult::fail(err, QStringLiteral("NOT_FOUND"));
	}
	if (part->moveLock()) {
		return FMcpResult::fail(QStringLiteral("Part '%1' is move-locked").arg(part->instanceTitle()), QStringLiteral("PART_LOCKED"));
	}

	QPointF oldPos = part->pos();
	QPointF newPos = spec.relative ? (oldPos + QPointF(spec.x, spec.y)) : QPointF(spec.x, spec.y);

	auto *parentCmd = new QUndoCommand(QObject::tr("Move %1").arg(part->instanceTitle()));
	new SimpleMoveItemCommand(view, part->id(), oldPos, newPos, parentCmd);
	view->undoStack()->push(parentCmd);

	return FMcpResult::ok(serializePart(part));
}

QJsonObject FMcpWorkspaceAdapter::rotatePart(const QString &partRef, double degrees) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, QString());
	if (!view) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	QString err;
	ItemBase *part = resolvePartInView(view, partRef, err);
	if (!part) {
		return FMcpResult::fail(err, QStringLiteral("NOT_FOUND"));
	}
	if (part->moveLock() || !part->rotationAllowed()) {
		return FMcpResult::fail(QStringLiteral("Part '%1' cannot be rotated").arg(part->instanceTitle()), QStringLiteral("ROTATION_NOT_ALLOWED"));
	}
	bool is45Step = std::abs(std::fmod(std::abs(degrees), 90.0)) > 1e-4;
	if (is45Step && !part->rotation45Allowed()) {
		return FMcpResult::fail(QStringLiteral("Part '%1' does not allow 45-degree rotation").arg(part->instanceTitle()), QStringLiteral("ROTATION_NOT_ALLOWED"));
	}

	view->triggerRotate(part, degrees);
	return FMcpResult::ok(serializePart(part));
}

QJsonObject FMcpWorkspaceAdapter::deletePart(const QString &partRef) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, QString());
	if (!view) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	QString err;
	ItemBase *part = resolvePartInView(view, partRef, err);
	if (!part) {
		return FMcpResult::fail(err, QStringLiteral("NOT_FOUND"));
	}
	if (!view->canDeleteItem(part, 1)) {
		return FMcpResult::fail(QStringLiteral("Part '%1' cannot be deleted").arg(part->instanceTitle()), QStringLiteral("DELETE_NOT_ALLOWED"));
	}

	long deletedId = part->id();
	QString deletedTitle = part->instanceTitle();
	QSet<ItemBase *> deletedItems;
	deletedItems.insert(part);

	auto *parentCmd = new QUndoCommand(QObject::tr("Delete %1").arg(deletedTitle));
	view->stackSelectionState(false, parentCmd);
	new CleanUpWiresCommand(view, CleanUpWiresCommand::UndoOnly, parentCmd);
	new CleanUpRatsnestsCommand(view, CleanUpWiresCommand::UndoOnly, parentCmd);
	view->deleteMiddle(deletedItems, parentCmd);
	for (ItemBase *itemBase : deletedItems) {
		view->makeDeleteItemCommand(itemBase, BaseCommand::CrossView, parentCmd);
	}
	new CleanUpRatsnestsCommand(view, CleanUpWiresCommand::RedoOnly, parentCmd);
	new CleanUpWiresCommand(view, CleanUpWiresCommand::RedoOnly, parentCmd);
	view->undoStack()->push(parentCmd);

	QJsonObject data;
	data[QStringLiteral("deletedId")] = static_cast<qint64>(deletedId);
	data[QStringLiteral("deletedTitle")] = deletedTitle;
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::setPartProperty(const QString &partRef, const QString &propName, const QString &propValue) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, QString());
	if (!view) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	QString err;
	ItemBase *part = resolvePartInView(view, partRef, err);
	if (!part) {
		return FMcpResult::fail(err, QStringLiteral("NOT_FOUND"));
	}

	if (propName.compare(QStringLiteral("title"), Qt::CaseInsensitive) == 0 ||
		propName.compare(QStringLiteral("label"), Qt::CaseInsensitive) == 0) {
		QString oldTitle = part->instanceTitle();
		auto *parentCmd = new QUndoCommand(QObject::tr("Rename %1 to %2").arg(oldTitle, propValue));
		new ChangeLabelTextCommand(view, part->id(), oldTitle, propValue, parentCmd);
		view->undoStack()->push(parentCmd);
		return FMcpResult::ok(serializePart(part));
	}

	QString oldVal = part->prop(propName);
	auto *parentCmd = new QUndoCommand(QObject::tr("Set %1 on %2").arg(propName, part->instanceTitle()));
	new SetPropCommand(view, part->id(), propName, oldVal, propValue, true, parentCmd);
	view->undoStack()->push(parentCmd);
	return FMcpResult::ok(serializePart(part));
}

ConnectorItem *FMcpWorkspaceAdapter::resolvePinInPart(ItemBase *part, const QString &pinRef, QString &err) const {
	ConnectorItem *bySharedId = part->findConnectorItemWithSharedID(pinRef);
	if (bySharedId) {
		return bySharedId;
	}
	for (ConnectorItem *ci : part->cachedConnectorItemsConst()) {
		if (!ci) {
			continue;
		}
		if (ci->connectorSharedID().compare(pinRef, Qt::CaseInsensitive) == 0 ||
			ci->connectorSharedName().compare(pinRef, Qt::CaseInsensitive) == 0) {
			return ci;
		}
	}
	err = QStringLiteral("Pin '%1' not found on part '%2'").arg(pinRef, part->instanceTitle());
	return nullptr;
}

bool FMcpWorkspaceAdapter::areConnectorsDirectlyWired(ConnectorItem *fromConn, ConnectorItem *toConn) const {
	for (ConnectorItem *connected : fromConn->connectedToItems()) {
		if (connected == toConn) {
			return true;
		}
		auto *wire = qobject_cast<Wire *>(connected ? connected->attachedTo() : nullptr);
		if (!wire || wire->getRatsnest()) {
			continue;
		}
		ConnectorItem *otherEnd = wire->otherConnector(connected);
		if (otherEnd && otherEnd->connectedToItems().contains(toConn)) {
			return true;
		}
	}
	return false;
}

static long executeWireCreation(SketchWidget *view, ConnectorItem *fromConn, ConnectorItem *toConn, const QString &color) {
	auto *parentCmd = new QUndoCommand(QObject::tr("Connect %1 to %2")
		.arg(fromConn->attachedToTitle(), toConn->attachedToTitle()));
	new CleanUpWiresCommand(view, CleanUpWiresCommand::UndoOnly, parentCmd);
	new CleanUpRatsnestsCommand(view, CleanUpWiresCommand::UndoOnly, parentCmd);

	long wireId = view->createWire(fromConn, toConn, view->getTraceFlag(), false, BaseCommand::CrossView, parentCmd);
	if (wireId < 0) {
		delete parentCmd;
		return -1;
	}
	if (!color.isEmpty()) {
		new WireColorChangeCommand(view, wireId, color, color, 1.0, 1.0, parentCmd);
	}
	new CleanUpRatsnestsCommand(view, CleanUpWiresCommand::RedoOnly, parentCmd);
	new CleanUpWiresCommand(view, CleanUpWiresCommand::RedoOnly, parentCmd);
	view->undoStack()->push(parentCmd);
	return wireId;
}

bool FMcpWorkspaceAdapter::resolveWireEndpoints(SketchWidget *view, const FMcpWireSpec &spec, ConnectorItem *&fromConn, ConnectorItem *&toConn, QJsonObject &errObj) const {
	QString err;
	ItemBase *fromPart = resolvePartInView(view, spec.fromPart, err);
	if (!fromPart) {
		errObj = FMcpResult::fail(err, QStringLiteral("NOT_FOUND"));
		return false;
	}
	ItemBase *toPart = resolvePartInView(view, spec.toPart, err);
	if (!toPart) {
		errObj = FMcpResult::fail(err, QStringLiteral("NOT_FOUND"));
		return false;
	}
	fromConn = resolvePinInPart(fromPart, spec.fromPin, err);
	if (!fromConn) {
		errObj = FMcpResult::fail(err, QStringLiteral("PIN_NOT_FOUND"));
		return false;
	}
	toConn = resolvePinInPart(toPart, spec.toPin, err);
	if (!toConn) {
		errObj = FMcpResult::fail(err, QStringLiteral("PIN_NOT_FOUND"));
		return false;
	}
	return true;
}

QJsonObject FMcpWorkspaceAdapter::connectWire(const FMcpWireSpec &spec) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, spec.viewName);
	if (!view) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	ConnectorItem *fromConn = nullptr;
	ConnectorItem *toConn = nullptr;
	QJsonObject errObj;
	if (!resolveWireEndpoints(view, spec, fromConn, toConn, errObj)) {
		return errObj;
	}
	if (fromConn == toConn) {
		return FMcpResult::fail(QStringLiteral("Cannot connect a pin to itself"), QStringLiteral("SELF_CONNECTION"));
	}
	if (areConnectorsDirectlyWired(fromConn, toConn)) {
		return FMcpResult::fail(QStringLiteral("Pins are already directly connected"), QStringLiteral("ALREADY_CONNECTED"));
	}

	long wireId = executeWireCreation(view, fromConn, toConn, spec.color);
	if (wireId < 0) {
		return FMcpResult::fail(QStringLiteral("Failed to create wire"), QStringLiteral("WIRE_CREATION_FAILED"));
	}

	QJsonObject data;
	data[QStringLiteral("wireId")] = static_cast<qint64>(wireId);
	data[QStringLiteral("fromPart")] = fromConn->attachedToTitle();
	data[QStringLiteral("fromPin")] = fromConn->connectorSharedID();
	data[QStringLiteral("toPart")] = toConn->attachedToTitle();
	data[QStringLiteral("toPin")] = toConn->connectorSharedID();
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::deleteWire(long wireId) {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, QString());
	if (!view) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	auto *wire = qobject_cast<Wire *>(view->findItem(wireId));
	if (!wire) {
		return FMcpResult::fail(QStringLiteral("Wire %1 not found").arg(wireId), QStringLiteral("NOT_FOUND"));
	}

	QSet<ItemBase *> deletedItems;
	QList<Wire *> chainedWires;
	wire->collectDirectWires(chainedWires);
	for (Wire *w : chainedWires) {
		deletedItems.insert(w);
	}

	auto *parentCmd = new QUndoCommand(QObject::tr("Delete wire %1").arg(wireId));
	view->stackSelectionState(false, parentCmd);
	new CleanUpWiresCommand(view, CleanUpWiresCommand::UndoOnly, parentCmd);
	new CleanUpRatsnestsCommand(view, CleanUpWiresCommand::UndoOnly, parentCmd);
	view->deleteMiddle(deletedItems, parentCmd);
	for (ItemBase *itemBase : deletedItems) {
		view->makeDeleteItemCommand(itemBase, BaseCommand::CrossView, parentCmd);
	}
	new CleanUpRatsnestsCommand(view, CleanUpWiresCommand::RedoOnly, parentCmd);
	new CleanUpWiresCommand(view, CleanUpWiresCommand::RedoOnly, parentCmd);
	view->undoStack()->push(parentCmd);

	QJsonObject data;
	data[QStringLiteral("deletedWireId")] = static_cast<qint64>(wireId);
	return FMcpResult::ok(data);
}

static QJsonArray serializeNetPins(const QList<ConnectorItem *> &net) {
	QJsonArray pinsArr;
	for (ConnectorItem *ci : net) {
		if (!ci || !ci->attachedTo()) {
			continue;
		}
		QJsonObject pinObj;
		pinObj[QStringLiteral("partId")] = static_cast<qint64>(ci->attachedToID());
		pinObj[QStringLiteral("partTitle")] = ci->attachedToTitle();
		pinObj[QStringLiteral("pin")] = ci->connectorSharedID();
		pinObj[QStringLiteral("pinName")] = ci->connectorSharedName();
		pinsArr.append(pinObj);
	}
	return pinsArr;
}

QJsonObject FMcpWorkspaceAdapter::getNetlist() {
	MainWindow *mw = currentMainWindow();
	SketchWidget *view = resolveView(mw, QString());
	if (!view) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	QHash<ConnectorItem *, int> indexer;
	QList<QList<ConnectorItem *> *> allPartConnectorItems;
	view->collectAllNets(indexer, allPartConnectorItems, false, true, false);

	QJsonArray netsArr;
	int netIdx = 0;
	for (QList<ConnectorItem *> *net : allPartConnectorItems) {
		if (net && net->size() >= 2) {
			QJsonObject netObj;
			netObj[QStringLiteral("netIndex")] = netIdx++;
			netObj[QStringLiteral("pins")] = serializeNetPins(*net);
			netsArr.append(netObj);
		}
		delete net;
	}

	QJsonObject data;
	data[QStringLiteral("netCount")] = netsArr.size();
	data[QStringLiteral("nets")] = netsArr;
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::openSketch(const QString &filePath) {
	MainWindow *mw = currentMainWindow();
	if (!mw) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	QFileInfo fi(filePath);
	if (!fi.exists() || !fi.isFile()) {
		return FMcpResult::fail(QStringLiteral("Sketch file '%1' does not exist").arg(filePath), QStringLiteral("FILE_NOT_FOUND"));
	}
	QString absPath = fi.absoluteFilePath();
	if (!mw->loadWhich(absPath, false, false, false, QString())) {
		return FMcpResult::fail(QStringLiteral("Failed to load sketch '%1'").arg(absPath), QStringLiteral("LOAD_FAILED"));
	}
	QJsonObject data;
	data[QStringLiteral("filePath")] = absPath;
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::saveSketch(const QString &filePath) {
	MainWindow *mw = currentMainWindow();
	if (!mw) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	QFileInfo fi(filePath);
	QDir parentDir = fi.dir();
	if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral("."))) {
		return FMcpResult::fail(QStringLiteral("Cannot create directory '%1'").arg(parentDir.absolutePath()), QStringLiteral("IO_ERROR"));
	}
	QString absPath = fi.absoluteFilePath();
	if (!mw->saveAsAux(absPath)) {
		return FMcpResult::fail(QStringLiteral("Failed to save sketch to '%1'").arg(absPath), QStringLiteral("SAVE_FAILED"));
	}
	QJsonObject data;
	data[QStringLiteral("filePath")] = absPath;
	return FMcpResult::ok(data);
}

static ItemBase *resolveSinglePcbBoard(PCBSketchWidget *pcbView, const QString &boardRef, QString &err) {
	QList<ItemBase *> boards = pcbView->findBoard();
	if (boards.isEmpty()) {
		err = QStringLiteral("No PCB board found in sketch");
		return nullptr;
	}
	if (boardRef.isEmpty()) {
		return boards.first();
	}
	for (ItemBase *b : boards) {
		if (QString::number(b->id()) == boardRef || b->instanceTitle().compare(boardRef, Qt::CaseInsensitive) == 0) {
			return b;
		}
	}
	err = QStringLiteral("Board '%1' not found in PCB view").arg(boardRef);
	return nullptr;
}

QJsonObject FMcpWorkspaceAdapter::getPcbBoardInfo() {
	MainWindow *mw = currentMainWindow();
	PCBSketchWidget *pcbView = mw ? mw->pcbView() : nullptr;
	if (!pcbView) {
		return FMcpResult::fail(QStringLiteral("No active PCB view"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	int boardCount = 0;
	double widthMm = 0.0;
	double heightMm = 0.0;
	QString boardTitle;
	pcbView->calcBoardDimensions(boardCount, widthMm, heightMm, boardTitle);

	QJsonArray boardsArr;
	for (ItemBase *b : pcbView->findBoard()) {
		QJsonObject bObj = serializePart(b);
		bObj[QStringLiteral("resizable")] = (qobject_cast<ResizableBoard *>(b) != nullptr);
		boardsArr.append(bObj);
	}

	QJsonObject data;
	data[QStringLiteral("boardCount")] = boardCount;
	data[QStringLiteral("layers")] = pcbView->boardLayers();
	data[QStringLiteral("widthMm")] = widthMm;
	data[QStringLiteral("heightMm")] = heightMm;
	data[QStringLiteral("boardTitle")] = boardTitle;
	data[QStringLiteral("boards")] = boardsArr;
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::resizePcbBoard(const FMcpBoardResizeSpec &spec) {
	MainWindow *mw = currentMainWindow();
	PCBSketchWidget *pcbView = mw ? mw->pcbView() : nullptr;
	if (!pcbView) {
		return FMcpResult::fail(QStringLiteral("No active PCB view"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	QString err;
	ItemBase *board = resolveSinglePcbBoard(pcbView, spec.boardRef, err);
	if (!board) {
		return FMcpResult::fail(err, QStringLiteral("BOARD_NOT_FOUND"));
	}
	auto *resBoard = qobject_cast<ResizableBoard *>(board);
	if (!resBoard) {
		return FMcpResult::fail(QStringLiteral("Board '%1' is not resizable").arg(board->instanceTitle()), QStringLiteral("NOT_RESIZABLE"));
	}

	QSizeF oldSize = resBoard->getSizeMM();
	auto *parentCmd = new QUndoCommand(QObject::tr("Resize board to %1x%2 mm").arg(spec.widthMm).arg(spec.heightMm));
	new ResizeBoardCommand(pcbView, resBoard->id(), oldSize.width(), oldSize.height(), spec.widthMm, spec.heightMm, parentCmd);
	pcbView->undoStack()->push(parentCmd);

	QJsonObject data;
	data[QStringLiteral("boardId")] = static_cast<qint64>(resBoard->id());
	data[QStringLiteral("widthMm")] = spec.widthMm;
	data[QStringLiteral("heightMm")] = spec.heightMm;
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::setPcbLayers(int layers) {
	MainWindow *mw = currentMainWindow();
	PCBSketchWidget *pcbView = mw ? mw->pcbView() : nullptr;
	if (!pcbView) {
		return FMcpResult::fail(QStringLiteral("No active PCB view"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	if (pcbView->findBoard().isEmpty()) {
		return FMcpResult::fail(QStringLiteral("No PCB board found in sketch"), QStringLiteral("BOARD_NOT_FOUND"));
	}
	mw->changeBoardLayers(layers, true);
	QJsonObject data;
	data[QStringLiteral("layers")] = pcbView->boardLayers();
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::runDesignRulesCheck() {
	MainWindow *mw = currentMainWindow();
	PCBSketchWidget *pcbView = mw ? mw->pcbView() : nullptr;
	if (!pcbView) {
		return FMcpResult::fail(QStringLiteral("No active PCB view"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	if (pcbView->findBoard().isEmpty()) {
		return FMcpResult::fail(QStringLiteral("No PCB board found in sketch"), QStringLiteral("BOARD_NOT_FOUND"));
	}

	mw->setCurrentView(ViewLayer::PCBView);
	QStringList messages = mw->newDesignRulesCheck(false);
	QJsonObject data;
	data[QStringLiteral("passed")] = messages.isEmpty();
	data[QStringLiteral("violationCount")] = messages.size();
	data[QStringLiteral("violations")] = QJsonArray::fromStringList(messages);
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::setPcbCopperFill(const QString &mode) {
	MainWindow *mw = currentMainWindow();
	PCBSketchWidget *pcbView = mw ? mw->pcbView() : nullptr;
	if (!pcbView) {
		return FMcpResult::fail(QStringLiteral("No active PCB view"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	if (pcbView->findBoard().isEmpty()) {
		return FMcpResult::fail(QStringLiteral("No PCB board found in sketch"), QStringLiteral("BOARD_NOT_FOUND"));
	}

	mw->setCurrentView(ViewLayer::PCBView);
	if (mode == QStringLiteral("remove")) {
		mw->removeGroundFill();
	} else if (mode == QStringLiteral("ground")) {
		mw->groundFill();
	} else {
		mw->copperFill();
	}

	QJsonObject data;
	data[QStringLiteral("mode")] = mode;
	return FMcpResult::ok(data);
}

static QString generateCurrentSpiceNetlist(MainWindow *mw) {
	QList<QList<ConnectorItem *> *> netList;
	QSet<ItemBase *> itemBases;
	QString netlist = mw->getSpiceNetlist(QStringLiteral("mcp_spice"), netList, itemBases);
	for (QList<ConnectorItem *> *net : netList) {
		delete net;
	}
	return netlist;
}

QJsonObject FMcpWorkspaceAdapter::getSpiceNetlist() {
	MainWindow *mw = currentMainWindow();
	if (!mw) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	QJsonObject data;
	data[QStringLiteral("netlist")] = generateCurrentSpiceNetlist(mw);
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::controlSimulation(const QString &action) {
	MainWindow *mw = currentMainWindow();
	if (!mw) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	if (action == QStringLiteral("start")) {
		mw->enableSimulator(true);
		mw->triggerSimulator();
	} else if (action == QStringLiteral("stop")) {
		mw->enableSimulator(false);
	}
	QJsonObject data;
	data[QStringLiteral("action")] = action;
	data[QStringLiteral("enabled")] = mw->isSimulatorEnabled();
	data[QStringLiteral("netlist")] = generateCurrentSpiceNetlist(mw);
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::exportBomCsv(const QString &outputPath) {
	MainWindow *mw = currentMainWindow();
	if (!mw) {
		return FMcpResult::fail(QStringLiteral("No active sketch window"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}
	QString csv = mw->getExportBOM_CSV();
	if (!outputPath.isEmpty()) {
		QFile outFile(outputPath);
		if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
			return FMcpResult::fail(QStringLiteral("Cannot write BOM file '%1'").arg(outputPath), QStringLiteral("IO_ERROR"));
		}
		outFile.write(csv.toUtf8());
		outFile.close();
	}
	QJsonObject data;
	data[QStringLiteral("csv")] = csv;
	if (!outputPath.isEmpty()) {
		data[QStringLiteral("outputPath")] = QFileInfo(outputPath).absoluteFilePath();
	}
	return FMcpResult::ok(data);
}

QJsonObject FMcpWorkspaceAdapter::exportGerber(const FMcpGerberSpec &spec) {
	MainWindow *mw = currentMainWindow();
	PCBSketchWidget *pcbView = mw ? mw->pcbView() : nullptr;
	if (!pcbView) {
		return FMcpResult::fail(QStringLiteral("No active PCB view"), QStringLiteral("NO_ACTIVE_SKETCH"));
	}

	QString err;
	ItemBase *board = resolveSinglePcbBoard(pcbView, QString(), err);
	if (!board) {
		return FMcpResult::fail(err, QStringLiteral("BOARD_NOT_FOUND"));
	}

	QDir dir(spec.outputDir);
	if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
		return FMcpResult::fail(QStringLiteral("Cannot create output directory '%1'").arg(spec.outputDir), QStringLiteral("IO_ERROR"));
	}

	QString absDir = dir.absolutePath();
	QString prefix = spec.prefix.isEmpty() ? QStringLiteral("fritzing_pcb") : spec.prefix;
	GerberGenerator::exportToGerber(prefix, absDir, board, pcbView, false);

	QStringList files = dir.entryList({prefix + QStringLiteral("*")}, QDir::Files, QDir::Name);
	QJsonObject data;
	data[QStringLiteral("outputDir")] = absDir;
	data[QStringLiteral("prefix")] = prefix;
	data[QStringLiteral("files")] = QJsonArray::fromStringList(files);
	data[QStringLiteral("fileCount")] = files.size();
	return FMcpResult::ok(data);
}
