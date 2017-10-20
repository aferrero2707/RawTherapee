########################################################################
# Package the binaries built on Travis-CI as an AppImage
# By Simon Peter 2016
# For more information, see http://appimage.org/
########################################################################


PREFIX=app

# Move blacklisted files to a special folder
move_blacklisted()
{
  mkdir -p ./usr/lib-blacklisted
  echo "APPIMAGEBASE: $APPIMAGEBASE"
  ls $APPIMAGEBASE
  #BLACKLISTED_FILES=$(wget -q https://github.com/probonopd/AppImages/raw/master/excludelist -O - | sed '/^\s*$/d' | sed '/^#.*$/d')
  BLACKLISTED_FILES=$(cat "$APPIMAGEBASE/excludelist" | sed '/^\s*$/d' | sed '/^#.*$/d')
  echo $BLACKLISTED_FILES
  for FILE in $BLACKLISTED_FILES ; do
    FOUND=$(find . -type f -name "${FILE}" 2>/dev/null)
    if [ ! -z "$FOUND" ] ; then
      echo "Removing blacklisted ${FOUND}"
      rm -f "${FOUND}"
      #mv "${FOUND}" ./usr/lib-blacklisted
    fi
  done
}


fix_pango()
{
    
    version=$(pango-querymodules --version | tail -n 1 | tr -d " " | cut -d':' -f 2)
    cat /$PREFIX/lib/pango/$version/modules.cache | sed "s|/$PREFIX/lib/pango/$version/modules/||g" > usr/lib/pango/$version/modules.cache
}


strip_binaries()
{
  chmod u+w -R "$APPDIR"
  {
    find $APPDIR/usr -type f -name "rawtherapee*" -print0
    find "$APPDIR" -type f -regex '.*\.so\(\.[0-9.]+\)?$' -print0
  } | xargs -0 --no-run-if-empty --verbose -n1 strip
}


export ARCH=$(arch)

export APPIMAGEBASE=$(pwd)

APP=RawTherapee
LOWERAPP=${APP,,}

wget -q https://github.com/probonopd/AppImages/raw/master/functions.sh -O ./functions.sh
. ./functions.sh

cd $APP.AppDir

export APPDIR=$(pwd)

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
echo "AppImage has been uploaded to the URL above; use something like GitHub Releases for permanent storage"
