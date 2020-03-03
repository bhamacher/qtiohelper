#ifndef QSPIDEVICE_GLOBAL_H
#define QSPIDEVICE_GLOBAL_H

#include <QtCore/qglobal.h>

#ifndef QT_STATIC
#if defined(QT_BUILD_SPIDEVICE_LIB)
#  define QTSPIDEVICESHARED_EXPORT Q_DECL_EXPORT
#else
#  define QTSPIDEVICESHARED_EXPORT Q_DECL_IMPORT
#endif
#endif

#endif // QSPIDEVICE_GLOBAL_H
