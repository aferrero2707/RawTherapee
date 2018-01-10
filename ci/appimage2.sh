#!/usr/bin/env bash

########################################################################
# Package the binaries built on Travis-CI as an AppImage
# By Andrea Ferrero and Morgan Hardwood 2017-2018
# For more information, see http://appimage.org/
# Report issues to https://github.com/Beep6581/RawTherapee/issues
########################################################################

# Fail handler
die () {
  printf '%s\n' "" "Aborting!" ""
  set +x
  exit 1
}

trap die HUP INT QUIT ABRT TERM

# Enable debugging output until this is ready for merging
set -x

# Program name
APP="RawTherapee"
# TODO Seems unused
#LOWERAPP=${APP,,}

# Prefix (without the leading "/") in which RawTherapee and its dependencies are installed:
export PREFIX="${APP}"

#TODO This seems like it does nothing, as "arch" is not set, but does get set later to x86_64
# Get system architecture from ???
#export ARCH="$(arch)"

# Print some info:
printf '%s\n' "" "Current directory:"
pwd
printf '%s\n' "" "ls:"
ls
printf '%s\n' "" "ls -lh build:"
ls -lh build
printf '%s\n' "" "ls -lh build/appimage:"
ls -lh build/appimage

# Go into the folder created when running the Docker container:
printf '%s\n' "" "sudo chown -R $USER build"
sudo chown -R "$USER" build || exit 1
cd build/appimage || exit 1

export APPIMAGEBASE="$(pwd)"
export APPDIR="$(pwd)/${APP}.AppDir"

# Get the latest version of the AppImage helper functions,
# or use a fallback copy if not available:
if ! wget "https://github.com/probonopd/AppImages/raw/master/functions.sh" --output-document="./functions.sh"; then
    cp -a "${TRAVIS_BUILD_DIR}/ci/functions.sh" ./functions.sh || exit 1
fi

# Source the script:
. ./functions.sh

# Generate AppImage; this expects $ARCH, $APP and $VERSION to be set
glibcVer="$(glibc_needed)"
ver="git-${TRAVIS_BRANCH}-$(date '+%Y%m%d_%H%M')-glibc${glibcVer}"
ARCH="x86_64"
VERSION="${ver}"
mkdir -p ../out/ || exit 1
generate_appimage || exit 1
#generate_type2_appimage

pwd
ls ../out/*

# Upload the AppDir
transfer ../out/*

printf '%s\n' "" "The AppImage has been uploaded to the URL above." ""
