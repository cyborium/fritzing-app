/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

********************************************************************/

#include "FMcpValidator.h"

#include <cmath>

bool FMcpValidator::requireNonEmptyString(const QJsonObject &params, const QString &key, QString &outVal, QString &err) {
	if (!params.contains(key) || !params.value(key).isString()) {
		err = QStringLiteral("Parameter '%1' must be a non-empty string").arg(key);
		return false;
	}
	outVal = params.value(key).toString().trimmed();
	if (outVal.isEmpty()) {
		err = QStringLiteral("Parameter '%1' must not be empty").arg(key);
		return false;
	}
	return true;
}

bool FMcpValidator::optionalString(const QJsonObject &params, const QString &key, const QString &defaultVal, QString &outVal, QString &err) {
	if (!params.contains(key) || params.value(key).isNull()) {
		outVal = defaultVal;
		return true;
	}
	if (!params.value(key).isString()) {
		err = QStringLiteral("Parameter '%1' must be a string").arg(key);
		return false;
	}
	outVal = params.value(key).toString().trimmed();
	return true;
}

bool FMcpValidator::requireFiniteCoord(const QJsonObject &params, const QString &key, double &outVal, QString &err) {
	if (!params.contains(key) || !params.value(key).isDouble()) {
		err = QStringLiteral("Parameter '%1' must be a finite number").arg(key);
		return false;
	}
	double v = params.value(key).toDouble(std::nan(""));
	if (!std::isfinite(v) || std::abs(v) > MaxCoordinateAbs) {
		err = QStringLiteral("Parameter '%1' is out of valid coordinate range").arg(key);
		return false;
	}
	outVal = v;
	return true;
}

bool FMcpValidator::optionalFiniteCoord(const QJsonObject &params, const QString &key, double defaultVal, double &outVal, QString &err) {
	if (!params.contains(key) || params.value(key).isNull()) {
		outVal = defaultVal;
		return true;
	}
	return requireFiniteCoord(params, key, outVal, err);
}

bool FMcpValidator::validateViewName(const QString &rawView, bool allowEmpty, QString &normalized, QString &err) {
	QString v = rawView.trimmed().toLower();
	if (v.isEmpty() && allowEmpty) {
		normalized.clear();
		return true;
	}
	if (v == QStringLiteral("breadboard") || v == QStringLiteral("schematic") || v == QStringLiteral("pcb")) {
		normalized = v;
		return true;
	}
	err = QStringLiteral("Invalid view '%1' (expected 'breadboard', 'schematic', or 'pcb')").arg(rawView);
	return false;
}

bool FMcpValidator::validateRotation(const QJsonObject &params, double &normalizedDegrees, QString &err) {
	double deg = 0.0;
	if (!requireFiniteCoord(params, QStringLiteral("degrees"), deg, err)) {
		return false;
	}
	double rem = std::fmod(deg, 360.0);
	if (std::abs(rem) < 1e-6) {
		err = QStringLiteral("Rotation angle must be non-zero");
		return false;
	}
	double check45 = std::fmod(std::abs(rem), 45.0);
	if (check45 > 1e-4 && std::abs(check45 - 45.0) > 1e-4) {
		err = QStringLiteral("Rotation angle must be a multiple of 45 degrees");
		return false;
	}
	normalizedDegrees = rem;
	return true;
}

bool FMcpValidator::validateSearchParams(const QJsonObject &params, QString &query, int &limit, QString &err) {
	if (!requireNonEmptyString(params, QStringLiteral("query"), query, err)) {
		return false;
	}
	limit = 25;
	if (!params.contains(QStringLiteral("limit")) || params.value(QStringLiteral("limit")).isNull()) {
		return true;
	}
	QJsonValue lVal = params.value(QStringLiteral("limit"));
	if (!lVal.isDouble()) {
		err = QStringLiteral("Parameter 'limit' must be an integer");
		return false;
	}
	int parsed = lVal.toInt(-1);
	if (parsed <= 0 || parsed > MaxSearchLimit) {
		err = QStringLiteral("Parameter 'limit' must be between 1 and %1").arg(MaxSearchLimit);
		return false;
	}
	limit = parsed;
	return true;
}

bool FMcpValidator::validatePartAddParams(const QJsonObject &params, FMcpPartSpec &spec, QString &err) {
	if (!requireNonEmptyString(params, QStringLiteral("moduleID"), spec.moduleID, err)) {
		return false;
	}
	if (!optionalFiniteCoord(params, QStringLiteral("x"), 0.0, spec.x, err)) {
		return false;
	}
	if (!optionalFiniteCoord(params, QStringLiteral("y"), 0.0, spec.y, err)) {
		return false;
	}
	QString rawView;
	if (!optionalString(params, QStringLiteral("view"), QString(), rawView, err)) {
		return false;
	}
	return validateViewName(rawView, true, spec.viewName, err);
}

bool FMcpValidator::validatePartMoveParams(const QJsonObject &params, FMcpMoveSpec &spec, QString &err) {
	if (!requireNonEmptyString(params, QStringLiteral("part"), spec.partRef, err)) {
		return false;
	}
	spec.relative = params.value(QStringLiteral("relative")).toBool(false);
	QString xKey = spec.relative ? QStringLiteral("dx") : QStringLiteral("x");
	QString yKey = spec.relative ? QStringLiteral("dy") : QStringLiteral("y");
	if (!requireFiniteCoord(params, xKey, spec.x, err)) {
		return false;
	}
	return requireFiniteCoord(params, yKey, spec.y, err);
}

bool FMcpValidator::validateSetPropertyParams(const QJsonObject &params, QString &partRef, QString &propName, QString &propValue, QString &err) {
	if (!requireNonEmptyString(params, QStringLiteral("part"), partRef, err)) {
		return false;
	}
	if (!requireNonEmptyString(params, QStringLiteral("property"), propName, err)) {
		return false;
	}
	return requireNonEmptyString(params, QStringLiteral("value"), propValue, err);
}

bool FMcpValidator::validateWireConnectParams(const QJsonObject &params, FMcpWireSpec &spec, QString &err) {
	if (!requireNonEmptyString(params, QStringLiteral("fromPart"), spec.fromPart, err) ||
		!requireNonEmptyString(params, QStringLiteral("fromPin"), spec.fromPin, err) ||
		!requireNonEmptyString(params, QStringLiteral("toPart"), spec.toPart, err) ||
		!requireNonEmptyString(params, QStringLiteral("toPin"), spec.toPin, err)) {
		return false;
	}
	if (spec.fromPart == spec.toPart && spec.fromPin == spec.toPin) {
		err = QStringLiteral("Cannot connect a pin to itself");
		return false;
	}
	QString rawView;
	if (!optionalString(params, QStringLiteral("view"), QString(), rawView, err) ||
		!optionalString(params, QStringLiteral("color"), QString(), spec.color, err)) {
		return false;
	}
	return validateViewName(rawView, true, spec.viewName, err);
}

bool FMcpValidator::validateWireDeleteParams(const QJsonObject &params, long &wireId, QString &err) {
	if (!params.contains(QStringLiteral("wireId")) || !params.value(QStringLiteral("wireId")).isDouble()) {
		err = QStringLiteral("Parameter 'wireId' must be a positive integer");
		return false;
	}
	double rawId = params.value(QStringLiteral("wireId")).toDouble(-1.0);
	if (!std::isfinite(rawId) || rawId <= 0.0 || std::floor(rawId) != rawId) {
		err = QStringLiteral("Parameter 'wireId' must be a positive integer");
		return false;
	}
	wireId = static_cast<long>(rawId);
	return true;
}

static bool validateDimensionField(const QJsonObject &params, const QString &key, int &outDim, QString &err) {
	if (!params.contains(key) || params.value(key).isNull()) {
		return true;
	}
	int v = params.value(key).toInt(-1);
	if (!params.value(key).isDouble() || v < FMcpValidator::MinRenderDimension || v > FMcpValidator::MaxRenderDimension) {
		err = QStringLiteral("Parameter '%1' must be between %2 and %3")
			.arg(key).arg(FMcpValidator::MinRenderDimension).arg(FMcpValidator::MaxRenderDimension);
		return false;
	}
	outDim = v;
	return true;
}

bool FMcpValidator::validateRenderParams(const QJsonObject &params, FMcpRenderSpec &spec, QString &err) {
	QString rawView;
	if (!optionalString(params, QStringLiteral("view"), QString(), rawView, err) ||
		!validateViewName(rawView, true, spec.viewName, err) ||
		!optionalString(params, QStringLiteral("outputPath"), QString(), spec.outputPath, err)) {
		return false;
	}
	QString rawFmt;
	if (!optionalString(params, QStringLiteral("format"), QStringLiteral("png"), rawFmt, err)) {
		return false;
	}
	spec.format = rawFmt.toLower();
	if (spec.format != QStringLiteral("png") && spec.format != QStringLiteral("svg")) {
		err = QStringLiteral("Invalid format '%1' (expected 'png' or 'svg')").arg(rawFmt);
		return false;
	}
	spec.cropToItems = params.value(QStringLiteral("cropToItems")).toBool(true);
	return validateDimensionField(params, QStringLiteral("width"), spec.width, err) &&
		   validateDimensionField(params, QStringLiteral("height"), spec.height, err);
}

bool FMcpValidator::validateSketchPath(const QJsonObject &params, QString &filePath, QString &err) {
	if (!requireNonEmptyString(params, QStringLiteral("filePath"), filePath, err)) {
		return false;
	}
	QString lower = filePath.toLower();
	if (!lower.endsWith(QStringLiteral(".fz")) && !lower.endsWith(QStringLiteral(".fzz"))) {
		err = QStringLiteral("Sketch file path must end with '.fz' or '.fzz'");
		return false;
	}
	return true;
}

bool FMcpValidator::validateBoardResizeParams(const QJsonObject &params, FMcpBoardResizeSpec &spec, QString &err) {
	if (!optionalString(params, QStringLiteral("board"), QString(), spec.boardRef, err)) {
		return false;
	}
	if (!requireFiniteCoord(params, QStringLiteral("widthMm"), spec.widthMm, err) ||
		!requireFiniteCoord(params, QStringLiteral("heightMm"), spec.heightMm, err)) {
		return false;
	}
	if (spec.widthMm < MinBoardDimensionMm || spec.widthMm > MaxBoardDimensionMm ||
		spec.heightMm < MinBoardDimensionMm || spec.heightMm > MaxBoardDimensionMm) {
		err = QStringLiteral("Board dimensions must be between %1 and %2 mm").arg(MinBoardDimensionMm).arg(MaxBoardDimensionMm);
		return false;
	}
	return true;
}

bool FMcpValidator::validateBoardLayers(const QJsonObject &params, int &layers, QString &err) {
	if (!params.contains(QStringLiteral("layers")) || !params.value(QStringLiteral("layers")).isDouble()) {
		err = QStringLiteral("Parameter 'layers' must be 1 or 2");
		return false;
	}
	double raw = params.value(QStringLiteral("layers")).toDouble(-1.0);
	if (raw != 1.0 && raw != 2.0) {
		err = QStringLiteral("Parameter 'layers' must be 1 or 2");
		return false;
	}
	layers = static_cast<int>(raw);
	return true;
}

bool FMcpValidator::validateCopperFillMode(const QJsonObject &params, QString &mode, QString &err) {
	QString raw;
	if (!requireNonEmptyString(params, QStringLiteral("mode"), raw, err)) {
		return false;
	}
	mode = raw.toLower();
	if (mode != QStringLiteral("ground") && mode != QStringLiteral("copper") && mode != QStringLiteral("remove")) {
		err = QStringLiteral("Invalid mode '%1' (expected 'ground', 'copper', or 'remove')").arg(raw);
		return false;
	}
	return true;
}

bool FMcpValidator::validateSimControlAction(const QJsonObject &params, QString &action, QString &err) {
	QString raw;
	if (!requireNonEmptyString(params, QStringLiteral("action"), raw, err)) {
		return false;
	}
	action = raw.toLower();
	if (action != QStringLiteral("start") && action != QStringLiteral("stop") && action != QStringLiteral("status")) {
		err = QStringLiteral("Invalid action '%1' (expected 'start', 'stop', or 'status')").arg(raw);
		return false;
	}
	return true;
}

bool FMcpValidator::validateGerberParams(const QJsonObject &params, FMcpGerberSpec &spec, QString &err) {
	if (!requireNonEmptyString(params, QStringLiteral("outputDir"), spec.outputDir, err)) {
		return false;
	}
	return optionalString(params, QStringLiteral("prefix"), QString(), spec.prefix, err);
}

bool FMcpValidator::validateAutorouteParams(const QJsonObject &params, int &maxCycles, QString &err) {
	maxCycles = 16;
	if (!params.contains(QStringLiteral("maxCycles")) || params.value(QStringLiteral("maxCycles")).isNull()) {
		return true;
	}
	if (!params.value(QStringLiteral("maxCycles")).isDouble()) {
		err = QStringLiteral("Parameter 'maxCycles' must be an integer");
		return false;
	}
	int parsed = params.value(QStringLiteral("maxCycles")).toInt(-1);
	if (parsed < 1 || parsed > MaxAutorouteCycles) {
		err = QStringLiteral("Parameter 'maxCycles' must be between 1 and %1").arg(MaxAutorouteCycles);
		return false;
	}
	maxCycles = parsed;
	return true;
}

bool FMcpValidator::validateTraceStyleParams(const QJsonObject &params, FMcpTraceStyleSpec &spec, QString &err) {
	if (!validateWireDeleteParams(params, spec.wireId, err)) {
		return false;
	}
	if (!optionalFiniteCoord(params, QStringLiteral("widthMils"), -1.0, spec.widthMils, err)) {
		return false;
	}
	bool hasWidth = params.contains(QStringLiteral("widthMils")) && !params.value(QStringLiteral("widthMils")).isNull();
	if (hasWidth && (spec.widthMils < MinTraceWidthMils || spec.widthMils > MaxTraceWidthMils)) {
		err = QStringLiteral("Parameter 'widthMils' must be between %1 and %2").arg(MinTraceWidthMils).arg(MaxTraceWidthMils);
		return false;
	}
	QString rawLayer;
	if (!optionalString(params, QStringLiteral("layer"), QString(), rawLayer, err)) {
		return false;
	}
	spec.layer = rawLayer.toLower();
	if (!spec.layer.isEmpty() && spec.layer != QStringLiteral("top") && spec.layer != QStringLiteral("bottom")) {
		err = QStringLiteral("Invalid layer '%1' (expected 'top', 'bottom', or empty)").arg(rawLayer);
		return false;
	}
	if (!hasWidth && spec.layer.isEmpty()) {
		err = QStringLiteral("Either 'widthMils' or 'layer' must be specified");
		return false;
	}
	return true;
}

bool FMcpValidator::validateFzpzImportParams(const QJsonObject &params, QString &filePath, bool &addToBin, QString &err) {
	if (!requireNonEmptyString(params, QStringLiteral("filePath"), filePath, err)) {
		return false;
	}
	if (!filePath.toLower().endsWith(QStringLiteral(".fzpz"))) {
		err = QStringLiteral("Part file path must end with '.fzpz'");
		return false;
	}
	addToBin = params.value(QStringLiteral("addToBin")).toBool(true);
	return true;
}

bool FMcpValidator::validateNoteParams(const QJsonObject &params, FMcpNoteSpec &spec, QString &err) {
	if (!requireNonEmptyString(params, QStringLiteral("text"), spec.text, err) ||
		!optionalFiniteCoord(params, QStringLiteral("x"), 0.0, spec.x, err) ||
		!optionalFiniteCoord(params, QStringLiteral("y"), 0.0, spec.y, err)) {
		return false;
	}
	QString rawView;
	if (!optionalString(params, QStringLiteral("view"), QString(), rawView, err)) {
		return false;
	}
	return validateViewName(rawView, true, spec.viewName, err);
}

bool FMcpValidator::validatePowerSymbolParams(const QJsonObject &params, FMcpPowerSymbolSpec &spec, QString &err) {
	QString rawType;
	if (!optionalString(params, QStringLiteral("symbolType"), QStringLiteral("gnd"), rawType, err) ||
		!optionalString(params, QStringLiteral("voltage"), QStringLiteral("5V"), spec.voltage, err) ||
		!optionalFiniteCoord(params, QStringLiteral("x"), 0.0, spec.x, err) ||
		!optionalFiniteCoord(params, QStringLiteral("y"), 0.0, spec.y, err)) {
		return false;
	}
	spec.symbolType = rawType.toLower();
	if (spec.symbolType != QStringLiteral("gnd") && spec.symbolType != QStringLiteral("vcc") && spec.symbolType != QStringLiteral("netlabel")) {
		err = QStringLiteral("Invalid symbolType '%1' (expected 'gnd', 'vcc', or 'netlabel')").arg(rawType);
		return false;
	}
	return true;
}

bool FMcpValidator::validateVectorExportParams(const QJsonObject &params, const QString &expectedExt, FMcpVectorExportSpec &spec, QString &err) {
	if (!requireNonEmptyString(params, QStringLiteral("outputPath"), spec.outputPath, err)) {
		return false;
	}
	if (!spec.outputPath.toLower().endsWith(expectedExt.toLower())) {
		err = QStringLiteral("Output path must end with '%1'").arg(expectedExt);
		return false;
	}
	QString rawView;
	if (!optionalString(params, QStringLiteral("view"), QString(), rawView, err) ||
		!validateViewName(rawView, true, spec.viewName, err)) {
		return false;
	}
	spec.removeBackground = params.value(QStringLiteral("removeBackground")).toBool(true);
	return true;
}
