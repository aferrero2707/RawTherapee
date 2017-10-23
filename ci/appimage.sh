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


sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test && apt-get -y update
sudo apt-get install -y libiptcdata0-dev curl fuse libfuse2 gcc-5 g++-5 
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-5 60 --slave /usr/bin/g++ g++ /usr/bin/g++-5

mkdir -p /work/build/rt

#cd /work && wget https://cmake.org/files/v3.8/cmake-3.8.2.tar.gz && tar xzvf cmake-3.8.2.tar.gz && cd cmake-3.8.2 && ./bootstrap --prefix=/work/inst --parallel=2 && make -j 2 && make install
#cd /work && wget https://downloads.sourceforge.net/lcms/lcms2-2.8.tar.gz && tar xzvf lcms2-2.8.tar.gz && cd lcms2-2.8 && ./configure --prefix=/app && make -j 2 && make install

cd /work/build/rt
cmake -DCMAKE_BUILD_TYPE=Release -DCACHE_NAME_SUFFIX="_appimage" -DPROC_TARGET_NUMBER=0 -DBUILD_BUNDLE=ON -DBUNDLE_BASE_INSTALL_DIR="/app" /sources
make -j 2
make install

#exit

mkdir -p /work/build/appimage
cd /work/build/appimage
cp /sources/ci/excludelist .

export ARCH=$(arch)

export APPIMAGEBASE=$(pwd)

APP=RawTherapee
LOWERAPP=${APP,,}

mkdir -p $APP.AppDir/usr/

wget -q https://github.com/probonopd/AppImages/raw/master/functions.sh -O ./functions.sh
. ./functions.sh

cd $APP.AppDir

export APPDIR=$(pwd)

#sudo chown -R $USER /${PREFIX}/

cp -r /${PREFIX}/* ./usr/
rm -f ./usr/$LOWERAPP.real
mv ./usr/$LOWERAPP ./usr/$LOWERAPP.real

cat > usr/bin/$LOWERAPP <<\EOF
#! /bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"

export LD_LIBRARY_PATH=$HERE/../lib:$HERE/../lib/x86_64-linux-gnu:$HERE/../../lib:$LD_LIBRARY_PATH
#echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH"

export XDG_DATA_DIRS=$HERE/../share/:$HERE/../share/mime/:$XDG_DATA_DIRS
#echo "XDG_DATA_DIRS=$XDG_DATA_DIRS"

export GTK_PATH=$HERE/../lib/gtk-2.0:$GTK_PATH
#echo "GTK_PATH=${GTK_PATH}"

export PANGO_LIBDIR=$HERE/../lib
#echo "PANGO_LIBDIR=${PANGO_LIBDIR}"

export GCONV_PATH=$HERE/../lib/gconv
#echo "GCONV_PATH=${GCONV_PATH}"

GDK_PIXBUF_MODULEDIR=$HERE/../lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders
GDK_PIXBUF_MODULE_FILE=$HERE/../lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache
#echo "GDK_PIXBUF_MODULEDIR: $GDK_PIXBUF_MODULEDIR"
#echo "GDK_PIXBUF_MODULE_FILE: $GDK_PIXBUF_MODULE_FILE"
#cat $GDK_PIXBUF_MODULE_FILE

#ldd "$HERE/LOWERAPP.real"
#echo -n "$HERE/LOWERAPP.real "
#echo "$@"
cd $HERE
cd ..
./LOWERAPP.real "$@"
#gdb -ex "run" $HERE/LOWERAPP.real
EOF
sed -i -e "s|LOWERAPP|$LOWERAPP|g" usr/bin/$LOWERAPP
chmod u+x usr/bin/$LOWERAPP


########################################################################
# Copy desktop and icon file to AppDir for AppRun to pick them up
########################################################################

get_apprun
get_desktop
get_icon

########################################################################
# Other application-specific finishing touches
########################################################################

cd ..

generate_status

cd ./$APP.AppDir/

# Workaround for:
# python2.7: symbol lookup error: /usr/lib/x86_64-linux-gnu/libgtk-3.so.0: undefined symbol: gdk__private__

#cp /usr/lib/x86_64-linux-gnu/libg*k-3.so.0 usr/lib/x86_64-linux-gnu/

# Compile Glib schemas
( mkdir -p usr/share/glib-2.0/schemas/ ; cd usr/share/glib-2.0/schemas/ ; glib-compile-schemas . )


cp -a /usr/lib/x86_64-linux-gnu/gconv usr/lib

mkdir -p usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0
cp -a /usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0
cp -a /usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0

# Copy the pixmap theme engine
mkdir -p usr/lib/gtk-2.0/engines
gtk_libdir=$(pkg-config --variable=libdir gtk+-2.0)
pixmap_lib=$(find ${gtk_libdir}/gtk-2.0 -name libpixmap.so)
if [ x"${pixmap_lib}" != "x" ]; then
	cp -L "${pixmap_lib}" usr/lib/gtk-2.0/engines
fi


mkdir -p usr/share
cp -a /usr/share/mime usr/share

########################################################################
# Copy in the dependencies that cannot be assumed to be available
# on all target systems
########################################################################

copy_deps; copy_deps; copy_deps;

cp -a /$PREFIX/lib/* usr/lib
cp -a /$PREFIX/lib64/* usr/lib64
#rm -rf $PREFIX
rm -rf usr/lib/python*
rm -rf usr/lib64/python*

#ls usr/lib
move_lib
echo "After move_lib"
#ls usr/lib

rm -rf usr/include usr/libexec usr/_jhbuild usr/share/doc

########################################################################
# Delete stuff that should not go into the AppImage
########################################################################

# Delete dangerous libraries; see
# https://github.com/probonopd/AppImages/blob/master/excludelist
move_blacklisted
#delete_blacklisted

fix_pango

########################################################################
# desktopintegration asks the user on first run to install a menu item
########################################################################

get_desktopintegration $LOWERAPP

########################################################################
# Determine the version of the app; also include needed glibc version
########################################################################

GLIBC_NEEDED=$(glibc_needed)
VERSION=$(date +%Y%m%d)_$(date +%H%M)-git-${TRAVIS_BRANCH}-${TRAVIS_COMMIT}.glibc${GLIBC_NEEDED}
#VERSION=${RELEASE_VERSION}-glibc$GLIBC_NEEDED

########################################################################
# Patch away absolute paths; it would be nice if they were relative
########################################################################

pwd
echo "INSTALL_PREFIX before patching:"
strings ./usr/$LOWERAPP.real | grep INSTALL_PREFIX

find usr/ -type f -exec sed -i -e 's|/usr/|././/|g' {} \; -exec echo -n "Patched /usr in " \; -exec echo {} \; >& patch1.log
find usr/ -type f -exec sed -i -e 's|/${PREFIX}/|././/|g' {} \; -exec echo -n "Patched /${PREFIX} in " \; -exec echo {} \; >& patch1.log

# The fonts configuration should not be patched, copy back original one
cp /$PREFIX/etc/fonts/fonts.conf usr/etc/fonts/fonts.conf

# Workaround for:
# ImportError: /usr/lib/x86_64-linux-gnu/libgdk-x11-2.0.so.0: undefined symbol: XRRGetMonitors
cp $(ldconfig -p | grep libgdk-x11-2.0.so.0 | cut -d ">" -f 2 | xargs) ./usr/lib/
cp $(ldconfig -p | grep libgtk-x11-2.0.so.0 | cut -d ">" -f 2 | xargs) ./usr/lib/


# Strip binaries.
echo "APPDIR: $APPDIR"
strip_binaries



########################################################################
# AppDir complete
# Now packaging it as an AppImage
########################################################################

cd .. # Go out of AppImage

tar czf /sources/$APP.AppDir.tgz $APP.AppDir
cd /
tar czf /sources/$APP.tgz $PREFIX

exit

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
