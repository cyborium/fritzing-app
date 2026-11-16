/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

********************************************************************/

#include <QtTest/QtTest>
#include <QTcpSocket>
#include <cmath>
#include <limits>

#include "FMcpCommandRouter.h"
#include "FMcpProtocol.h"
#include "FMcpServer.h"
#include "FMcpTypes.h"
#include "FMcpValidator.h"
#include "IFritzingWorkspace.h"

class FakeWorkspace : public IFritzingWorkspace {
public:
	QString lastCall;
	QJsonObject nextResponse = FMcpResult::ok();

	QJsonObject getSketchState(const QString &viewName) override {
		lastCall = QStringLiteral("getSketchState:") + viewName;
		return nextResponse;
	}
	QJsonObject setActiveView(const QString &viewName) override {
		lastCall = QStringLiteral("setActiveView:") + viewName;
		return nextResponse;
	}
	QJsonObject undo() override {
		lastCall = QStringLiteral("undo");
		return nextResponse;
	}
	QJsonObject redo() override {
		lastCall = QStringLiteral("redo");
		return nextResponse;
	}
	QJsonObject renderImage(const FMcpRenderSpec &spec) override {
		lastCall = QStringLiteral("renderImage:%1:%2x%3").arg(spec.format).arg(spec.width).arg(spec.height);
		return nextResponse;
	}
	QJsonObject searchParts(const QString &query, int limit) override {
		lastCall = QStringLiteral("searchParts:%1:%2").arg(query).arg(limit);
		return nextResponse;
	}
	QJsonObject getPartInfo(const QString &moduleID) override {
		lastCall = QStringLiteral("getPartInfo:") + moduleID;
		return nextResponse;
	}
	QJsonObject addPart(const FMcpPartSpec &spec) override {
		lastCall = QStringLiteral("addPart:%1:%2").arg(spec.moduleID, spec.viewName);
		return nextResponse;
	}
	QJsonObject movePart(const FMcpMoveSpec &spec) override {
		lastCall = QStringLiteral("movePart:%1:%2").arg(spec.partRef).arg(spec.relative);
		return nextResponse;
	}
	QJsonObject rotatePart(const QString &partRef, double degrees) override {
		lastCall = QStringLiteral("rotatePart:%1:%2").arg(partRef).arg(static_cast<int>(degrees));
		return nextResponse;
	}
	QJsonObject deletePart(const QString &partRef) override {
		lastCall = QStringLiteral("deletePart:") + partRef;
		return nextResponse;
	}
	QJsonObject setPartProperty(const QString &partRef, const QString &propName, const QString &propValue) override {
		lastCall = QStringLiteral("setPartProperty:%1:%2=%3").arg(partRef, propName, propValue);
		return nextResponse;
	}
	QJsonObject connectWire(const FMcpWireSpec &spec) override {
		lastCall = QStringLiteral("connectWire:%1.%2->%3.%4").arg(spec.fromPart, spec.fromPin, spec.toPart, spec.toPin);
		return nextResponse;
	}
	QJsonObject deleteWire(long wireId) override {
		lastCall = QStringLiteral("deleteWire:%1").arg(wireId);
		return nextResponse;
	}
	QJsonObject getNetlist() override {
		lastCall = QStringLiteral("getNetlist");
		return nextResponse;
	}
};

class TestMcp : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void testTypesAndResultHelpers();
	void testProtocolHttpParsingValid();
	void testProtocolHttpParsingEdgeCases();
	void testProtocolRpcBodyParsing();
	void testValidatorStringsAndCoords();
	void testValidatorViewsAndRotation();
	void testValidatorSearchAndPartSpecs();
	void testValidatorWireAndRenderSpecs();
	void testRouterDispatchAllMethods();
	void testRouterValidationAndErrorPaths();
	void testServerHttpRouting();
	void testServerLiveTcpSocket();
};

void TestMcp::testTypesAndResultHelpers() {
	QJsonObject okObj = FMcpResult::ok({{QStringLiteral("val"), 42}});
	QVERIFY(okObj.value(QStringLiteral("ok")).toBool());
	QCOMPARE(okObj.value(QStringLiteral("val")).toInt(), 42);

	QJsonObject failObj = FMcpResult::fail(QStringLiteral("bad"), QStringLiteral("ERR_CODE"));
	QVERIFY(!failObj.value(QStringLiteral("ok")).toBool());
	QCOMPARE(failObj.value(QStringLiteral("error")).toString(), QStringLiteral("bad"));
	QCOMPARE(failObj.value(QStringLiteral("code")).toString(), QStringLiteral("ERR_CODE"));

	QString jsonStr = FMcpResult::toJsonString(okObj);
	QVERIFY(jsonStr.contains(QStringLiteral("\"ok\":true")));
}

void TestMcp::testProtocolHttpParsingValid() {
	QByteArray getReq = "GET /health?verbose=1 HTTP/1.1\r\nHost: localhost\r\n\r\n";
	FMcpHttpRequest parsedGet = FMcpProtocol::parseHttpRequest(getReq);
	QVERIFY(parsedGet.complete);
	QCOMPARE(parsedGet.statusCode, 200);
	QCOMPARE(parsedGet.method, QStringLiteral("GET"));
	QCOMPARE(parsedGet.path, QStringLiteral("/health"));

	QByteArray postReq = "POST /rpc HTTP/1.1\nContent-Length: 17\n\n{\"method\":\"undo\"}EXTRA";
	FMcpHttpRequest parsedPost = FMcpProtocol::parseHttpRequest(postReq);
	QVERIFY(parsedPost.complete);
	QCOMPARE(parsedPost.statusCode, 200);
	QCOMPARE(parsedPost.method, QStringLiteral("POST"));
	QCOMPARE(parsedPost.path, QStringLiteral("/rpc"));
	QCOMPARE(parsedPost.body, QByteArray("{\"method\":\"undo\"}"));
}

void TestMcp::testProtocolHttpParsingEdgeCases() {
	// Incomplete headers
	FMcpHttpRequest incHdr = FMcpProtocol::parseHttpRequest("GET /health HTTP/1.1\r\nHost: loc");
	QVERIFY(!incHdr.complete);

	// Incomplete body
	FMcpHttpRequest incBody = FMcpProtocol::parseHttpRequest("POST /rpc HTTP/1.1\r\nContent-Length: 50\r\n\r\nshort");
	QVERIFY(!incBody.complete);

	// Header too large without terminator
	QByteArray hugeRaw(FMcpProtocol::MaxHeaderBytes + 10, 'A');
	FMcpHttpRequest hugeNoSep = FMcpProtocol::parseHttpRequest(hugeRaw);
	QVERIFY(hugeNoSep.complete);
	QCOMPARE(hugeNoSep.statusCode, 413);

	// Header too large with terminator
	QByteArray hugeWithSep = hugeRaw + "\r\n\r\n";
	FMcpHttpRequest hugeSep = FMcpProtocol::parseHttpRequest(hugeWithSep);
	QVERIFY(hugeSep.complete);
	QCOMPARE(hugeSep.statusCode, 413);

	// Malformed request lines
	FMcpHttpRequest badLine1 = FMcpProtocol::parseHttpRequest("INVALID\r\n\r\n");
	QCOMPARE(badLine1.statusCode, 400);
	FMcpHttpRequest badLine2 = FMcpProtocol::parseHttpRequest("GET noslash HTTP/1.1\r\n\r\n");
	QCOMPARE(badLine2.statusCode, 400);

	// Invalid & Negative Content-Length
	FMcpHttpRequest badLen1 = FMcpProtocol::parseHttpRequest("POST /rpc HTTP/1.1\r\nContent-Length: abc\r\n\r\n");
	QCOMPARE(badLen1.statusCode, 400);
	FMcpHttpRequest badLen2 = FMcpProtocol::parseHttpRequest("POST /rpc HTTP/1.1\r\nContent-Length: -5\r\n\r\n");
	QCOMPARE(badLen2.statusCode, 400);

	// Oversized Content-Length
	FMcpHttpRequest hugeLen = FMcpProtocol::parseHttpRequest("POST /rpc HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n");
	QCOMPARE(hugeLen.statusCode, 413);
}

void TestMcp::testProtocolRpcBodyParsing() {
	QString method;
	QJsonObject params;
	QString err;

	QVERIFY(!FMcpProtocol::parseRpcBody("   ", method, params, err));
	QVERIFY(!FMcpProtocol::parseRpcBody("{broken", method, params, err));
	QVERIFY(!FMcpProtocol::parseRpcBody("[1, 2, 3]", method, params, err));
	QVERIFY(!FMcpProtocol::parseRpcBody("{}", method, params, err));
	QVERIFY(!FMcpProtocol::parseRpcBody("{\"method\": 123}", method, params, err));
	QVERIFY(!FMcpProtocol::parseRpcBody("{\"method\": \"   \"}", method, params, err));
	QVERIFY(!FMcpProtocol::parseRpcBody("{\"method\": \"test\", \"params\": [1]}", method, params, err));

	QVERIFY(FMcpProtocol::parseRpcBody("{\"method\": \"sketch.undo\"}", method, params, err));
	QCOMPARE(method, QStringLiteral("sketch.undo"));
	QVERIFY(params.isEmpty());

	QVERIFY(FMcpProtocol::parseRpcBody("{\"method\": \"sketch.undo\", \"params\": null}", method, params, err));
	QVERIFY(FMcpProtocol::parseRpcBody("{\"method\": \"part.delete\", \"params\": {\"part\": \"R1\"}}", method, params, err));
	QCOMPARE(method, QStringLiteral("part.delete"));
	QCOMPARE(params.value(QStringLiteral("part")).toString(), QStringLiteral("R1"));
}

void TestMcp::testValidatorStringsAndCoords() {
	QString s;
	QString err;
	QVERIFY(!FMcpValidator::requireNonEmptyString(QJsonObject(), QStringLiteral("k"), s, err));
	QVERIFY(!FMcpValidator::requireNonEmptyString({{QStringLiteral("k"), 10}}, QStringLiteral("k"), s, err));
	QVERIFY(!FMcpValidator::requireNonEmptyString({{QStringLiteral("k"), QStringLiteral("   ")}}, QStringLiteral("k"), s, err));
	QVERIFY(FMcpValidator::requireNonEmptyString({{QStringLiteral("k"), QStringLiteral(" val ")}}, QStringLiteral("k"), s, err));
	QCOMPARE(s, QStringLiteral("val"));

	QVERIFY(FMcpValidator::optionalString(QJsonObject(), QStringLiteral("k"), QStringLiteral("def"), s, err));
	QCOMPARE(s, QStringLiteral("def"));
	QVERIFY(!FMcpValidator::optionalString({{QStringLiteral("k"), 42}}, QStringLiteral("k"), QStringLiteral("def"), s, err));
	QVERIFY(FMcpValidator::optionalString({{QStringLiteral("k"), QStringLiteral("ok")}}, QStringLiteral("k"), QStringLiteral("def"), s, err));
	QCOMPARE(s, QStringLiteral("ok"));

	double c = 0.0;
	QVERIFY(!FMcpValidator::requireFiniteCoord(QJsonObject(), QStringLiteral("x"), c, err));
	QVERIFY(!FMcpValidator::requireFiniteCoord({{QStringLiteral("x"), QStringLiteral("10")}}, QStringLiteral("x"), c, err));
	QVERIFY(!FMcpValidator::requireFiniteCoord({{QStringLiteral("x"), 1e9}}, QStringLiteral("x"), c, err));
	QVERIFY(FMcpValidator::requireFiniteCoord({{QStringLiteral("x"), -125.5}}, QStringLiteral("x"), c, err));
	QCOMPARE(c, -125.5);

	QVERIFY(FMcpValidator::optionalFiniteCoord(QJsonObject(), QStringLiteral("x"), 15.0, c, err));
	QCOMPARE(c, 15.0);
}

void TestMcp::testValidatorViewsAndRotation() {
	QString norm;
	QString err;
	QVERIFY(FMcpValidator::validateViewName(QString(), true, norm, err));
	QVERIFY(!FMcpValidator::validateViewName(QString(), false, norm, err));
	QVERIFY(FMcpValidator::validateViewName(QStringLiteral("Breadboard"), false, norm, err));
	QCOMPARE(norm, QStringLiteral("breadboard"));
	QVERIFY(FMcpValidator::validateViewName(QStringLiteral("SCHEMATIC"), false, norm, err));
	QCOMPARE(norm, QStringLiteral("schematic"));
	QVERIFY(FMcpValidator::validateViewName(QStringLiteral("pcb"), false, norm, err));
	QCOMPARE(norm, QStringLiteral("pcb"));
	QVERIFY(!FMcpValidator::validateViewName(QStringLiteral("3d"), false, norm, err));

	double deg = 0.0;
	QVERIFY(!FMcpValidator::validateRotation(QJsonObject(), deg, err));
	QVERIFY(!FMcpValidator::validateRotation({{QStringLiteral("degrees"), 0.0}}, deg, err));
	QVERIFY(!FMcpValidator::validateRotation({{QStringLiteral("degrees"), 360.0}}, deg, err));
	QVERIFY(!FMcpValidator::validateRotation({{QStringLiteral("degrees"), 30.0}}, deg, err));
	QVERIFY(FMcpValidator::validateRotation({{QStringLiteral("degrees"), 90.0}}, deg, err));
	QCOMPARE(deg, 90.0);
	QVERIFY(FMcpValidator::validateRotation({{QStringLiteral("degrees"), -45.0}}, deg, err));
	QCOMPARE(deg, -45.0);
}

void TestMcp::testValidatorSearchAndPartSpecs() {
	QString query;
	int limit = 0;
	QString err;
	QVERIFY(!FMcpValidator::validateSearchParams(QJsonObject(), query, limit, err));
	QVERIFY(FMcpValidator::validateSearchParams({{QStringLiteral("query"), QStringLiteral("resistor")}}, query, limit, err));
	QCOMPARE(limit, 25);
	QVERIFY(!FMcpValidator::validateSearchParams({{QStringLiteral("query"), QStringLiteral("r")}, {QStringLiteral("limit"), QStringLiteral("bad")}}, query, limit, err));
	QVERIFY(!FMcpValidator::validateSearchParams({{QStringLiteral("query"), QStringLiteral("r")}, {QStringLiteral("limit"), 0}}, query, limit, err));
	QVERIFY(!FMcpValidator::validateSearchParams({{QStringLiteral("query"), QStringLiteral("r")}, {QStringLiteral("limit"), 999}}, query, limit, err));
	QVERIFY(FMcpValidator::validateSearchParams({{QStringLiteral("query"), QStringLiteral("r")}, {QStringLiteral("limit"), 10}}, query, limit, err));
	QCOMPARE(limit, 10);

	FMcpPartSpec pSpec;
	QVERIFY(!FMcpValidator::validatePartAddParams(QJsonObject(), pSpec, err));
	QVERIFY(!FMcpValidator::validatePartAddParams({{QStringLiteral("moduleID"), QStringLiteral("m1")}, {QStringLiteral("x"), 1e9}}, pSpec, err));
	QVERIFY(!FMcpValidator::validatePartAddParams({{QStringLiteral("moduleID"), QStringLiteral("m1")}, {QStringLiteral("y"), 1e9}}, pSpec, err));
	QVERIFY(!FMcpValidator::validatePartAddParams({{QStringLiteral("moduleID"), QStringLiteral("m1")}, {QStringLiteral("view"), 123}}, pSpec, err));
	QVERIFY(FMcpValidator::validatePartAddParams({{QStringLiteral("moduleID"), QStringLiteral("m1")}, {QStringLiteral("x"), 10.0}, {QStringLiteral("y"), 20.0}, {QStringLiteral("view"), QStringLiteral("pcb")}}, pSpec, err));

	FMcpMoveSpec mSpec;
	QVERIFY(!FMcpValidator::validatePartMoveParams(QJsonObject(), mSpec, err));
	QVERIFY(!FMcpValidator::validatePartMoveParams({{QStringLiteral("part"), QStringLiteral("R1")}}, mSpec, err));
	QVERIFY(!FMcpValidator::validatePartMoveParams({{QStringLiteral("part"), QStringLiteral("R1")}, {QStringLiteral("x"), 10.0}}, mSpec, err));
	QVERIFY(FMcpValidator::validatePartMoveParams({{QStringLiteral("part"), QStringLiteral("R1")}, {QStringLiteral("x"), 10.0}, {QStringLiteral("y"), 20.0}}, mSpec, err));
	QVERIFY(FMcpValidator::validatePartMoveParams({{QStringLiteral("part"), QStringLiteral("R1")}, {QStringLiteral("relative"), true}, {QStringLiteral("dx"), 5.0}, {QStringLiteral("dy"), -5.0}}, mSpec, err));

	QString pr, pn, pv;
	QVERIFY(!FMcpValidator::validateSetPropertyParams(QJsonObject(), pr, pn, pv, err));
	QVERIFY(!FMcpValidator::validateSetPropertyParams({{QStringLiteral("part"), QStringLiteral("R1")}}, pr, pn, pv, err));
	QVERIFY(!FMcpValidator::validateSetPropertyParams({{QStringLiteral("part"), QStringLiteral("R1")}, {QStringLiteral("property"), QStringLiteral("resistance")}}, pr, pn, pv, err));
	QVERIFY(FMcpValidator::validateSetPropertyParams({{QStringLiteral("part"), QStringLiteral("R1")}, {QStringLiteral("property"), QStringLiteral("resistance")}, {QStringLiteral("value"), QStringLiteral("10k")}}, pr, pn, pv, err));
}

void TestMcp::testValidatorWireAndRenderSpecs() {
	FMcpWireSpec wSpec;
	QString err;
	QVERIFY(!FMcpValidator::validateWireConnectParams(QJsonObject(), wSpec, err));
	QJsonObject selfWire = {
		{QStringLiteral("fromPart"), QStringLiteral("R1")},
		{QStringLiteral("fromPin"), QStringLiteral("connector0")},
		{QStringLiteral("toPart"), QStringLiteral("R1")},
		{QStringLiteral("toPin"), QStringLiteral("connector0")}
	};
	QVERIFY(!FMcpValidator::validateWireConnectParams(selfWire, wSpec, err));

	QJsonObject badColorWire = selfWire;
	badColorWire[QStringLiteral("toPart")] = QStringLiteral("LED1");
	badColorWire[QStringLiteral("color")] = 123;
	QVERIFY(!FMcpValidator::validateWireConnectParams(badColorWire, wSpec, err));

	QJsonObject validWire = selfWire;
	validWire[QStringLiteral("toPart")] = QStringLiteral("LED1");
	validWire[QStringLiteral("color")] = QStringLiteral("red");
	QVERIFY(FMcpValidator::validateWireConnectParams(validWire, wSpec, err));

	long wireId = 0;
	QVERIFY(!FMcpValidator::validateWireDeleteParams(QJsonObject(), wireId, err));
	QVERIFY(!FMcpValidator::validateWireDeleteParams({{QStringLiteral("wireId"), 0}}, wireId, err));
	QVERIFY(!FMcpValidator::validateWireDeleteParams({{QStringLiteral("wireId"), -5}}, wireId, err));
	QVERIFY(!FMcpValidator::validateWireDeleteParams({{QStringLiteral("wireId"), 3.5}}, wireId, err));
	QVERIFY(FMcpValidator::validateWireDeleteParams({{QStringLiteral("wireId"), 42}}, wireId, err));
	QCOMPARE(wireId, 42L);

	FMcpRenderSpec rSpec;
	QVERIFY(FMcpValidator::validateRenderParams(QJsonObject(), rSpec, err));
	QVERIFY(!FMcpValidator::validateRenderParams({{QStringLiteral("format"), QStringLiteral("bmp")}}, rSpec, err));
	QVERIFY(!FMcpValidator::validateRenderParams({{QStringLiteral("format"), 123}}, rSpec, err));
	QVERIFY(!FMcpValidator::validateRenderParams({{QStringLiteral("outputPath"), 123}}, rSpec, err));
	QVERIFY(!FMcpValidator::validateRenderParams({{QStringLiteral("width"), 10}}, rSpec, err));
	QVERIFY(!FMcpValidator::validateRenderParams({{QStringLiteral("height"), 99999}}, rSpec, err));
	QVERIFY(FMcpValidator::validateRenderParams({{QStringLiteral("format"), QStringLiteral("svg")}, {QStringLiteral("width"), 800}, {QStringLiteral("height"), 600}}, rSpec, err));
}

void TestMcp::testRouterDispatchAllMethods() {
	FakeWorkspace ws;
	FMcpCommandRouter router(&ws);
	QCOMPARE(router.registeredMethods().size(), 15);

	QVERIFY(router.dispatch(QStringLiteral("sketch.get_state"), {{QStringLiteral("view"), QStringLiteral("pcb")}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("getSketchState:pcb"));

	QVERIFY(router.dispatch(QStringLiteral("sketch.set_view"), {{QStringLiteral("view"), QStringLiteral("schematic")}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("setActiveView:schematic"));

	QVERIFY(router.dispatch(QStringLiteral("sketch.undo"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("undo"));

	QVERIFY(router.dispatch(QStringLiteral("sketch.redo"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("redo"));

	QVERIFY(router.dispatch(QStringLiteral("sketch.render_image"), {{QStringLiteral("width"), 640}, {QStringLiteral("height"), 480}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("renderImage:png:640x480"));

	QVERIFY(router.dispatch(QStringLiteral("parts.search"), {{QStringLiteral("query"), QStringLiteral("led")}, {QStringLiteral("limit"), 5}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("searchParts:led:5"));

	QVERIFY(router.dispatch(QStringLiteral("parts.get_info"), {{QStringLiteral("moduleID"), QStringLiteral("mod_led")}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("getPartInfo:mod_led"));

	QVERIFY(router.dispatch(QStringLiteral("part.add"), {{QStringLiteral("moduleID"), QStringLiteral("mod_r")}, {QStringLiteral("view"), QStringLiteral("breadboard")}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("addPart:mod_r:breadboard"));

	QVERIFY(router.dispatch(QStringLiteral("part.move"), {{QStringLiteral("part"), QStringLiteral("R1")}, {QStringLiteral("x"), 10.0}, {QStringLiteral("y"), 20.0}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("movePart:R1:0"));

	QVERIFY(router.dispatch(QStringLiteral("part.rotate"), {{QStringLiteral("part"), QStringLiteral("R1")}, {QStringLiteral("degrees"), 90.0}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("rotatePart:R1:90"));

	QVERIFY(router.dispatch(QStringLiteral("part.delete"), {{QStringLiteral("part"), QStringLiteral("R1")}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("deletePart:R1"));

	QVERIFY(router.dispatch(QStringLiteral("part.set_property"), {{QStringLiteral("part"), QStringLiteral("R1")}, {QStringLiteral("property"), QStringLiteral("resistance")}, {QStringLiteral("value"), QStringLiteral("220")}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("setPartProperty:R1:resistance=220"));

	QVERIFY(router.dispatch(QStringLiteral("wire.connect"), {{QStringLiteral("fromPart"), QStringLiteral("R1")}, {QStringLiteral("fromPin"), QStringLiteral("connector0")}, {QStringLiteral("toPart"), QStringLiteral("LED1")}, {QStringLiteral("toPin"), QStringLiteral("connector1")}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("connectWire:R1.connector0->LED1.connector1"));

	QVERIFY(router.dispatch(QStringLiteral("wire.delete"), {{QStringLiteral("wireId"), 99}}).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("deleteWire:99"));

	QVERIFY(router.dispatch(QStringLiteral("wire.get_netlist"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QCOMPARE(ws.lastCall, QStringLiteral("getNetlist"));
}

void TestMcp::testRouterValidationAndErrorPaths() {
	FMcpCommandRouter nullRouter(nullptr);
	QCOMPARE(nullRouter.dispatch(QStringLiteral("sketch.undo"), QJsonObject()).value(QStringLiteral("code")).toString(), QStringLiteral("WORKSPACE_UNAVAILABLE"));

	FakeWorkspace ws;
	FMcpCommandRouter router(&ws);
	QCOMPARE(router.dispatch(QStringLiteral("nonexistent"), QJsonObject()).value(QStringLiteral("code")).toString(), QStringLiteral("UNKNOWN_METHOD"));
	QCOMPARE(router.dispatchRawJson("{bad").value(QStringLiteral("code")).toString(), QStringLiteral("PARSE_ERROR"));

	QVERIFY(!router.dispatch(QStringLiteral("sketch.get_state"), {{QStringLiteral("view"), QStringLiteral("bad")}}).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("sketch.set_view"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("sketch.render_image"), {{QStringLiteral("width"), -1}}).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("parts.search"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("parts.get_info"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("part.add"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("part.move"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("part.rotate"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("part.delete"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("part.set_property"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("wire.connect"), QJsonObject()).value(QStringLiteral("ok")).toBool());
	QVERIFY(!router.dispatch(QStringLiteral("wire.delete"), QJsonObject()).value(QStringLiteral("ok")).toBool());
}

void TestMcp::testServerHttpRouting() {
	FakeWorkspace ws;
	FMcpServer server(&ws);

	QVERIFY(server.processRawHttp("GET /health HTTP/1.1\r\n").isEmpty());

	QByteArray healthResp = server.processRawHttp("GET /health HTTP/1.1\r\n\r\n");
	QVERIFY(healthResp.startsWith("HTTP/1.1 200 OK"));
	QVERIFY(healthResp.contains("fritzing-mcp-bridge"));

	QByteArray postHealthResp = server.processRawHttp("POST /health HTTP/1.1\r\n\r\n");
	QVERIFY(postHealthResp.startsWith("HTTP/1.1 405 Method Not Allowed"));

	QByteArray getRpcResp = server.processRawHttp("GET /rpc HTTP/1.1\r\n\r\n");
	QVERIFY(getRpcResp.startsWith("HTTP/1.1 405 Method Not Allowed"));

	QByteArray notFoundResp = server.processRawHttp("GET /missing HTTP/1.1\r\n\r\n");
	QVERIFY(notFoundResp.startsWith("HTTP/1.1 404 Not Found"));

	QByteArray badReqResp = server.processRawHttp("BADLINE\r\n\r\n");
	QVERIFY(badReqResp.startsWith("HTTP/1.1 400 Bad Request"));

	QByteArray rpcBody = "{\"method\":\"sketch.undo\"}";
	QByteArray rpcReq = "POST /rpc HTTP/1.1\r\nContent-Length: " + QByteArray::number(rpcBody.size()) + "\r\n\r\n" + rpcBody;
	QByteArray rpcResp = server.processRawHttp(rpcReq);
	QVERIFY(rpcResp.startsWith("HTTP/1.1 200 OK"));
	QCOMPARE(ws.lastCall, QStringLiteral("undo"));
}

void TestMcp::testServerLiveTcpSocket() {
	FakeWorkspace ws;
	FMcpServer server(&ws);
	QVERIFY(!server.isRunning());
	QVERIFY(server.start(0));
	QVERIFY(server.isRunning());
	QVERIFY(server.start(0)); // idempotent when already running
	quint16 port = server.serverPort();
	QVERIFY(port > 0);

	QTcpSocket client;
	client.connectToHost(QHostAddress::LocalHost, port);
	QTRY_VERIFY(client.state() == QAbstractSocket::ConnectedState);

	// Send request in two split chunks to test partial buffer handling
	QByteArray part1 = "POST /rpc HTTP/1.1\r\nContent-Length: 24\r\n\r\n{\"method\":";
	QByteArray part2 = "\"sketch.redo\"}";
	client.write(part1);
	client.flush();
	QTest::qWait(50);
	client.write(part2);
	client.flush();

	QTRY_VERIFY(client.bytesAvailable() > 0);
	QByteArray response = client.readAll();
	QVERIFY(response.startsWith("HTTP/1.1 200 OK"));
	QCOMPARE(ws.lastCall, QStringLiteral("redo"));
	QTRY_VERIFY(client.state() == QAbstractSocket::UnconnectedState);

	// Connect another socket and call server.stop() while active
	QTcpSocket client2;
	client2.connectToHost(QHostAddress::LocalHost, port);
	QTRY_VERIFY(client2.state() == QAbstractSocket::ConnectedState);
	QTest::qWait(30);
	server.stop();
	QVERIFY(!server.isRunning());
}

QTEST_GUILESS_MAIN(TestMcp)
#include "test_mcp.moc"
