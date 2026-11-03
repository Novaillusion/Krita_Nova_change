// Tests for KisPalmRejection (Krita Nova patch 09)
#include <QApplication>
#include <QScreen>
#include <QTabletEvent>
#include <QTest>
#include <QTouchEvent>
#include <QWidget>
#include <cmath>
#include <cstdio>

#include "KisPalmRejection.h"
#include "kis_config.h"
#include "kis_config_notifier.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void sendTablet(QObject *receiver, QEvent::Type type)
{
    const Qt::MouseButton button = (type == QEvent::TabletPress || type == QEvent::TabletRelease) ? Qt::LeftButton : Qt::NoButton;
    QTabletEvent ev(type, QPointF(10, 10), QPointF(10, 10), QTabletEvent::Stylus, QTabletEvent::Pen,
                    0.5, 0, 0, 0, 0, 0, Qt::NoModifier, 1, button,
                    type == QEvent::TabletRelease ? Qt::NoButton : Qt::MouseButtons(button));
    QCoreApplication::sendEvent(receiver, &ev);
}

static QTouchEvent makeTouch(qreal diameterPx)
{
    QTouchEvent::TouchPoint p(1);
    p.setState(Qt::TouchPointPressed);
    p.setPos(QPointF(50, 50));
    p.setScreenPos(QPointF(50, 50));
    p.setEllipseDiameters(QSizeF(diameterPx, diameterPx));
    return QTouchEvent(QEvent::TouchBegin, nullptr, Qt::NoModifier, Qt::TouchPointPressed, {p});
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QWidget canvas;
    canvas.resize(200, 200);

    // 1. unit conversion
    CHECK(std::abs(KisPalmRejection::contactSizeMm(100, 254) - 10.0) < 1e-9);
    CHECK(KisPalmRejection::contactSizeMm(0, 254) == 0.0);
    CHECK(KisPalmRejection::contactSizeMm(50, 0) == 0.0);

    KisPalmRejection *palm = KisPalmRejection::instance();
    CHECK(palm->isEnabled());

    const qreal dpi = QGuiApplication::primaryScreen()->physicalDotsPerInch();
    std::printf("offscreen physical dpi: %.1f\n", dpi);
    const qreal fingerPx = 8.0 / 25.4 * dpi;   // 8 mm fingertip
    const qreal palmPx = 30.0 / 25.4 * dpi;    // 30 mm palm

    // 2. no pen yet: finger accepted, palm-sized contact rejected, unknown size accepted
    {
        QTouchEvent finger = makeTouch(fingerPx);
        QTouchEvent big = makeTouch(palmPx);
        QTouchEvent unknown = makeTouch(0);
        CHECK(!palm->isPenActive());
        CHECK(!palm->shouldRejectTouchBegin(&finger, &canvas));
        CHECK(palm->shouldRejectTouchBegin(&big, &canvas));
        CHECK(!palm->shouldRejectTouchBegin(&unknown, &canvas));
        CHECK(palm->hasPalmSizedContact(&big, &canvas));
        CHECK(!palm->hasPalmSizedContact(&finger, &canvas));
    }

    // 3. pen down -> all touch rejected; after release: rejected for the grace period only
    {
        QTouchEvent finger = makeTouch(fingerPx);
        sendTablet(&canvas, QEvent::TabletPress);
        CHECK(palm->isPenActive());
        CHECK(palm->shouldRejectTouchBegin(&finger, &canvas));
        sendTablet(&canvas, QEvent::TabletRelease);
        CHECK(palm->isPenActive());              // within 500 ms
        sendTablet(&app, QEvent::TabletLeaveProximity);
        CHECK(palm->isPenActive());              // still within 500 ms
        QTest::qWait(650);
        CHECK(!palm->isPenActive());
        CHECK(!palm->shouldRejectTouchBegin(&finger, &canvas));
    }

    // 4. hovering pen (proximity) keeps rejecting beyond the grace period
    {
        QTouchEvent finger = makeTouch(fingerPx);
        sendTablet(&app, QEvent::TabletEnterProximity);
        QTest::qWait(650);
        CHECK(palm->isPenActive());
        CHECK(palm->shouldRejectTouchBegin(&finger, &canvas));
        sendTablet(&app, QEvent::TabletLeaveProximity);
        QTest::qWait(650);
        CHECK(!palm->isPenActive());
    }

    // 5. hover moves count as pen use
    {
        sendTablet(&canvas, QEvent::TabletMove);
        CHECK(palm->isPenActive());
        sendTablet(&app, QEvent::TabletLeaveProximity);
        QTest::qWait(650);
        CHECK(!palm->isPenActive());
    }

    // 6. settings: disabled -> nothing rejected; custom size threshold
    {
        QTouchEvent big = makeTouch(palmPx);
        KisConfig::overrides()["palmRejectionEnabled"] = false;
        KisConfigNotifier::instance()->notifyConfigChanged();
        CHECK(!palm->isEnabled());
        sendTablet(&canvas, QEvent::TabletPress);
        CHECK(!palm->shouldRejectTouchBegin(&big, &canvas));
        sendTablet(&canvas, QEvent::TabletRelease);
        sendTablet(&app, QEvent::TabletLeaveProximity);

        KisConfig::overrides()["palmRejectionEnabled"] = true;
        KisConfig::overrides()["palmRejectionMaxContactMm"] = 40;
        KisConfig::overrides()["palmRejectionDelay"] = 0;
        KisConfigNotifier::instance()->notifyConfigChanged();
        QTest::qWait(20);
        CHECK(!palm->isPenActive());             // grace period 0
        CHECK(!palm->shouldRejectTouchBegin(&big, &canvas));   // 30 mm < 40 mm

        KisConfig::overrides()["palmRejectionLargeContacts"] = false;
        KisConfig::overrides()["palmRejectionMaxContactMm"] = 10;
        KisConfigNotifier::instance()->notifyConfigChanged();
        CHECK(!palm->shouldRejectTouchBegin(&big, &canvas));   // size check off
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
