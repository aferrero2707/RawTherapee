########################################################################
# Package the binaries built on Travis-CI as an AppImage
# By Andrea Ferrero 2017
# For more information, see http://appimage.org/
########################################################################


APP=RawTherapee
LOWERAPP=${APP,,}

########################################################################
# prefix (without the leading "/") in which RawTherapee and its dependencies are installed
########################################################################

export PREFIX=app


# get system architecture from ???
export ARCH=$(arch)

echo ""; echo "current directory:"
pwd
echo ""; echo "ls:"
ls
echo ""

########################################################################
# Go into the folder created when running the Docker container
########################################################################

cd build/appimage
export APPIMAGEBASE=$(pwd)
echo "sudo chown -R $USER $APP.AppDir"
sudo chown -R $USER $APP.AppDir
export APPDIR=$(pwd)/$APP.AppDir


########################################################################
# get the latest version of the AppImage helper functions,
# or use a fallback copy if not available
########################################################################

(wget -q https://github.com/probonopd/AppImages/raw/master/functions.sh -O ./functions.sh) || (cp -a ${TRAVIS_BUILD_DIR}/ci/functions.sh ./functions.sh)
. ./functions.sh


########################################################################
# Determine the version of the app; also include needed glibc version
########################################################################

GLIBC_NEEDED=$(glibc_needed)
VERSION=git-${TRAVIS_BRANCH}-$(date +%Y%m%d)_$(date +%H%M)-glibc${GLIBC_NEEDED}


mkdir -p ../out/
ARCH="x86_64"
generate_appimage
#generate_type2_appimage

pwd
ls ../out/*

########################################################################
# Upload the AppDir
########################################################################

transfer ../out/*
echo ""
echo "AppImage has been uploaded to the URL above; use something like GitHub Releases for permanent storage"
