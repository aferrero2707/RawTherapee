########################################################################
# Package the binaries built on Travis-CI as an AppImage
# By Simon Peter 2016
# For more information, see http://appimage.org/
########################################################################


PREFIX=app

export ARCH=$(arch)

export APPIMAGEBASE=$(pwd)

APP=RawTherapee
LOWERAPP=${APP,,}

wget -q https://github.com/probonopd/AppImages/raw/master/functions.sh -O ./functions.sh
. ./functions.sh

pwd
ls -lh
echo "sudo chown -R $USER $APP.AppDir"
sudo chown -R $USER $APP.AppDir
ls -lh

#cd $APP.AppDir

transfer $APP.AppDir.tgz
echo ""
transfer $APP.tgz
echo ""

export APPDIR=$(pwd)/$APP.AppDir

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
