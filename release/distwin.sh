#!/bin/bash
# Stage and build the CanDuet Windows installer (run from the release/ folder in Git Bash).
# Needs: a Release x64 build in ../build, Inno Setup's iscc on PATH, signtool, and CERTFILE set.

if [ -z "$1" ] ; then
   echo "Usage: $0 <version> <certpassword>"
   exit 1
fi

VERSION=$1

CERTPASS=$2

if [ -z "$CERTFILE" ] ; then
  echo You need to define CERTFILE env variable to sign anything
  exit 2
fi

BUILDDIR='../build/SonoBus_artefacts/Release'

rm -rf CanDuet
mkdir -p CanDuet

cp -v ${BUILDDIR}/Standalone/CanDuet.exe CanDuet/ || exit 3
# video window (T-009) needs the WebView2 loader next to the exe
cp -v ${BUILDDIR}/Standalone/WebView2Loader.dll CanDuet/ || exit 3

mkdir -p instoutput
rm -f instoutput/*

iscc /O"instoutput" "/Ssigntool=signtool.exe sign /t http://timestamp.digicert.com /f ${CERTFILE} /p ${CERTPASS} \$f"  /DSBVERSION="${VERSION}" wininstaller.iss || exit 4

EXEFILE=canduet-${VERSION}-win.exe
rm -f ${EXEFILE}
cp instoutput/CanDuet-${VERSION}-Installer.exe ${EXEFILE}
