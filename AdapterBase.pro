TEMPLATE=lib
DEST=lib
OBJECTS_DIR=.obj
SOURCES=AdapterBase.cpp
HEADERS=AdapterBase.h
INCLUDEPATH += $$(LUNA_STAGING)/include
DEFINES+=XP_WEBOS=1 XP_UNIX=1
CONFIG += staticlib

unix {
CONFIG += link_pkgconfig
PKGCONFIG += glib-2.0
}

#install headers
headers.path = $${INCLUDEPATH}
headers.files += \
    AdapterBase.h

INSTALLS += headers

#install lib
target.path = $$(LUNA_STAGING)/lib
INSTALLS += target

