#pragma once

#include <QList>
#include <QObject>

class Controller;

/// Base class handling discovery and enumeration of DJ controllers.
///
/// This class handles discovery and enumeration of DJ controllers and
/// must be inherited by a class that implements it on some API.
class ControllerEnumerator : public QObject {
    Q_OBJECT
  public:
    ControllerEnumerator();
    // In this function, the inheriting class must delete the Controllers it
    // creates
    virtual ~ControllerEnumerator();

    virtual QList<Controller*> queryDevices() = 0;

    /// Called (with every controller closed) before queryDevices() runs again after start-up, to
    /// pick up a controller plugged in since. Retires the Controllers found so far (they are kept,
    /// closed, until the app ends, so a pointer someone still holds never dangles) and, where the
    /// host API only looks at start-up, restarts it.
    virtual void prepareRescan() {
    }

  protected:
    /// Controllers from an earlier scan, closed and no longer listed.
    QList<Controller*> m_retiredDevices;
};
