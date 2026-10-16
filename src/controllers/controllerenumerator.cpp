#include "controllers/controllerenumerator.h"

#include <QtDebug>

#include "moc_controllerenumerator.cpp"

ControllerEnumerator::ControllerEnumerator() = default;
ControllerEnumerator::~ControllerEnumerator() {
    qDeleteAll(m_retiredDevices);
}
