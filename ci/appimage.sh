########################################################################
# Package the binaries built on Travis-CI as an AppImage
# By Simon Peter 2016
# For more information, see http://appimage.org/
########################################################################


APP=RawTherapee
LOWERAPP=${APP,,}

########################################################################
# prefix (without the leading "/") in which RawTherapee and its dependencies are installed
########################################################################

PREFIX=app


########################################################################
# get the latest version of the AppImage helper functions,
# or use a fallback copy if not available
########################################################################

(wget -q https://github.com/probonopd/AppImages/raw/master/functions.sh -O ./functions.sh) || (cp -a /sources/ci/functions.sh ./functions.sh)
. ./functions.sh


########################################################################
# Additional helper functions:
########################################################################

# Delete blacklisted libraries
delete_blacklisted2()
{
  echo "APPIMAGEBASE: $APPIMAGEBASE"
  ls $APPIMAGEBASE
  BLACKLISTED_FILES=$(cat "$APPIMAGEBASE/excludelist" | sed '/^\s*$/d' | sed '/^#.*$/d')
  echo $BLACKLISTED_FILES
  for FILE in $BLACKLISTED_FILES ; do
    FOUND=$(find . -type f -name "${FILE}" 2>/dev/null)
    if [ ! -z "$FOUND" ] ; then
      echo "Removing blacklisted ${FOUND}"
      rm -f "${FOUND}"
    fi
  done
}


# remove absolute paths from pango modules cache (if existing)
patch_pango()
{
  pqm=$(which pango-querymodules)
  if [ ! -z "$pqm" ]; then
    version=$(pango-querymodules --version | tail -n 1 | tr -d " " | cut -d':' -f 2)
    cat /$PREFIX/lib/pango/$version/modules.cache | sed "s|/$PREFIX/lib/pango/$version/modules/||g" > usr/lib/pango/$version/modules.cache
  fi
}


# remove debugging symbols from AppImage binaries and libraries
strip_binaries()
{
  chmod u+w -R "$APPDIR"
  {
    find $APPDIR/usr -type f -name "${LOWERAPP}*" -print0
    find "$APPDIR" -type f -regex '.*\.so\(\.[0-9.]+\)?$' -print0
  } | xargs -0 --no-run-if-empty --verbose -n1 strip
}


########################################################################
# add some required packages
########################################################################

(sudo apt-get -y update && sudo apt-get install -y libiptcdata0-dev wget curl fuse libfuse2 git) || exit 1


########################################################################
# set environment variables to allow finding the dependencies that are
# compiled from sources
########################################################################

export PATH=/$PREFIX/bin:/work/inst/bin:$PATH
export LD_LIBRARY_PATH=/$PREFIX/lib:/work/inst/lib:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=/$PREFIX/lib/pkgconfig:/work/inst/lib/pkgconfig:$PKG_CONFIG_PATH

locale-gen en_US.UTF-8
export LANG=en_US.UTF-8 
export LANGUAGE=en_US:en
export LC_ALL=en_US.UTF-8


########################################################################
# build the latest LensFun and install it under /$PREFIX
########################################################################

(cd /work && rm -rf lensfun* && wget https://sourceforge.net/projects/lensfun/files/0.3.2/lensfun-0.3.2.tar.gz && tar xzvf lensfun-0.3.2.tar.gz && cd lensfun-0.3.2 && mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="/$PREFIX" ../ && make -j 2 && make install) || exit 1


########################################################################
# build RawTherapee and install it under /$PREFIX
########################################################################

(mkdir -p /sources/build/appimage && cd /sources/build/appimage && cmake -DCMAKE_BUILD_TYPE=Release -DCACHE_NAME_SUFFIX="_appimage" -DPROC_TARGET_NUMBER=0 -DBUILD_BUNDLE=OFF -DBUNDLE_BASE_INSTALL_DIR="/$PREFIX" -DCMAKE_INSTALL_PREFIX="/$PREFIX" -DUSE_OLD_CXX_ABI="OFF" /sources && make -j 2 install) || exit 1


########################################################################
# Create a folder in the shared area where the AppImage structure will be copied
########################################################################

mkdir -p /sources/build/appimage
cd /sources/build/appimage
cp /sources/ci/excludelist .
export APPIMAGEBASE=$(pwd)

# get system architecture from ???
export ARCH=$(arch)

# remove old AppDir structure (if existing)
rm -rf $APP.AppDir
mkdir -p $APP.AppDir/usr/
cd $APP.AppDir
export APPDIR=$(pwd)

#sudo chown -R $USER /${PREFIX}/


########################################################################
# copy main RT executable into $APPDIR/usr/bin/rawtherapee.real
########################################################################

(mkdir -p ./usr/bin && cp -a /${PREFIX}/bin/$LOWERAPP ./usr/bin/$LOWERAPP.real) || exit 1


########################################################################
# create the launcher script
########################################################################

cat > usr/bin/$LOWERAPP <<\EOF
#! /bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH=$PATH:/sbin:/usr/sbin
export LD_LIBRARY_PATH=$HERE/../lib:$HERE/../lib/x86_64-linux-gnu:$HERE/../../lib:$LD_LIBRARY_PATH
export XDG_DATA_DIRS=$HERE/../share/:$HERE/../share/mime/:$XDG_DATA_DIRS
export GTK_PATH=$HERE/../lib/gtk-2.0:$GTK_PATH
export PANGO_LIBDIR=$HERE/../lib
export GCONV_PATH=$HERE/../lib/gconv
export GDK_PIXBUF_MODULEDIR=$HERE/../lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders
export GDK_PIXBUF_MODULE_FILE=$HERE/../lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache
if [ -e /etc/fonts/fonts.conf ]; then
  export FONTCONFIG_PATH=/etc/fonts
fi
# libstdc++ version detection
stdcxxlib=$(ldconfig -p | grep 'libstdc++.so.6 (libc6,x86-64)'| awk 'NR==1{print $NF}')
echo "System stdc++ library: \"$stdcxxlib\""
stdcxxver1=$(strings "$stdcxxlib" | grep '^GLIBCXX_[0-9].[0-9]*' | cut -d"_" -f 2 | sort -V | tail -n 1)
echo "System stdc++ library version: \"$stdcxxver1\""
stdcxxver2=$(strings "$HERE/../optional/libstdc++/libstdc++.so.6" | grep '^GLIBCXX_[0-9].[0-9]*' | cut -d"_" -f 2 | sort -V | tail -n 1)
echo "Bundled stdc++ library version: \"$stdcxxver2\""
stdcxxnewest=$(echo "$stdcxxver1 $stdcxxver2" | tr " " "\n" | sort -V | tail -n 1)
echo "Newest stdc++ library version: \"$stdcxxnewest\""
if [ x"$stdcxxnewest" = x"$stdcxxver1" ]; then
   echo "Using system stdc++ library"
else
   echo "Using bundled stdc++ library"
   export LD_LIBRARY_PATH=$HERE/../optional/libstdc++:$LD_LIBRARY_PATH
fi
cd $HERE && cd ..
ldd ./bin/LOWERAPP.real
./bin/LOWERAPP.real "$@"
EOF
(sed -i -e "s|LOWERAPP|$LOWERAPP|g" usr/bin/$LOWERAPP && chmod u+x usr/bin/$LOWERAPP) || exit 1


########################################################################
# Copy desktop and icon file to AppDir for AppRun to pick them up
########################################################################

(mkdir -p usr/share/applications/ && cp /$PREFIX/share/applications/rawtherapee.desktop usr/share/applications) || exit 1
(mkdir -p usr/share/icons && cp -r /$PREFIX/share/icons/hicolor usr/share/icons) || exit 1
get_apprun
get_desktop
get_icon


########################################################################
# Other application-specific finishing touches
########################################################################

cd ..
generate_status
cd "$APPDIR"


########################################################################
# Copy in the dependencies that cannot be assumed to be available
# on all target systems
########################################################################

copy_deps; copy_deps; copy_deps;

cp -L ./lib/x86_64-linux-gnu/*.* ./usr/lib; rm -rf ./lib/x86_64-linux-gnu
cp -L ./lib/*.* ./usr/lib; rm -rf ./lib;
cp -L ./usr/lib/x86_64-linux-gnu/*.* ./usr/lib; rm -rf ./usr/lib/x86_64-linux-gnu;
cp -L ./$PREFIX/lib/x86_64-linux-gnu/*.* ./usr/lib; rm -rf ./$PREFIX/lib/x86_64-linux-gnu;
cp -L ./$PREFIX/lib/*.* ./usr/lib; rm -rf ./$PREFIX/lib;

########################################################################
# Compile Glib schemas
( mkdir -p usr/share/glib-2.0/schemas/ ; cd usr/share/glib-2.0/schemas/ ; glib-compile-schemas . )

########################################################################
# Copy gconv
cp -a /usr/lib/x86_64-linux-gnu/gconv usr/lib

########################################################################
# Copy gdk-pixbuf modules and cache file, and patch the cache file
# so that modules are picked from the AppImage bundle
gdk_pixbuf_moduledir=$(pkg-config --variable=gdk_pixbuf_moduledir gdk-pixbuf-2.0)
gdk_pixbuf_cache_file=$(pkg-config --variable=gdk_pixbuf_cache_file gdk-pixbuf-2.0)
gdk_pixbuf_libdir_bundle=lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0
gdk_pixbuf_cache_file_bundle=usr/${gdk_pixbuf_libdir_bundle}/loaders.cache

mkdir -p "usr/${gdk_pixbuf_libdir_bundle}"
cp -a "$gdk_pixbuf_moduledir" "usr/${gdk_pixbuf_libdir_bundle}"
cp -a "$gdk_pixbuf_cache_file" "usr/${gdk_pixbuf_libdir_bundle}"

for m in $(ls "usr/${gdk_pixbuf_libdir_bundle}"/loaders/*.so); do
  sofile=$(basename "$m")
  sed -i -e"s|${gdk_pixbuf_moduledir}/${sofile}|./${gdk_pixbuf_libdir_bundle}/loaders/${sofile}|g" "$gdk_pixbuf_cache_file_bundle"
done

echo ""; echo "=================="; echo "gdk-pixbuf cache:"
cat "$gdk_pixbuf_cache_file_bundle"
echo "=================="; echo "gdk-pixbuf loaders:"
ls usr/${gdk_pixbuf_libdir_bundle}/loaders
echo "=================="

########################################################################
# Copy the pixmap theme engine
mkdir -p usr/lib/gtk-2.0/engines
gtk_libdir=$(pkg-config --variable=libdir gtk+-2.0)
pixmap_lib=$(find ${gtk_libdir}/gtk-2.0 -name libpixmap.so)
if [ x"${pixmap_lib}" != "x" ]; then
	cp -L "${pixmap_lib}" usr/lib/gtk-2.0/engines
fi


########################################################################
# Copy MIME files
(mkdir -p usr/share && cp -a /usr/share/mime usr/share)


########################################################################
# Copy RT's share folder
(mkdir -p usr/share && cp -a /$PREFIX/share/rawtherapee usr/share) || exit 1


########################################################################
# Update the LensFun database and put the newest version into the bundle
/$PREFIX/bin/lensfun-update-data
mkdir -p usr/share/lensfun/version_1
cp -a /var/lib/lensfun-updates/version_1/* usr/share/lensfun/version_1
echo ""; echo "=================="; echo "Contents of updated lensfun database:"
ls usr/share/lensfun/version_1


#cp -a /$PREFIX/lib/* usr/lib
#cp -a /$PREFIX/lib64/* usr/lib64
#rm -rf $PREFIX
#rm -rf usr/lib/python*
#rm -rf usr/lib64/python*


########################################################################
# Move all libraries into $APPDIR/usr/lib
########################################################################

move_lib


########################################################################
# Fix path of pango modules
########################################################################

fix_pango


########################################################################
# Delete stuff that should not go into the AppImage
########################################################################

rm -rf usr/include usr/libexec usr/_jhbuild usr/share/doc

# Delete dangerous libraries; see
# https://github.com/probonopd/AppImages/blob/master/excludelist
delete_blacklisted2


########################################################################
# Copy libstdc++.so.6 and libgomp.so.1 into the AppImage
# They will be used if they are newer than those of the host
# system in which the AppImage will be executed
########################################################################

stdcxxlib=$(ldconfig -p | grep 'libstdc++.so.6 (libc6,x86-64)'| awk 'NR==1{print $NF}')
echo "stdcxxlib: $stdcxxlib"
if [ x"$stdcxxlib" != "x" ]; then
    mkdir -p usr/optional/libstdc++
	cp -L "$stdcxxlib" usr/optional/libstdc++
fi

gomplib=$(ldconfig -p | grep 'libgomp.so.1 (libc6,x86-64)'| awk 'NR==1{print $NF}')
echo "gomplib: $gomplib"
if [ x"$gomplib" != "x" ]; then
    mkdir -p usr/optional/libstdc++
	cp -L "$gomplib" usr/optional/libstdc++
fi


########################################################################
# desktopintegration asks the user on first run to install a menu item
########################################################################

get_desktopintegration $LOWERAPP


########################################################################
# Patch away absolute paths; it would be nice if they were relative
########################################################################

find usr/ -type f -exec sed -i -e 's|/usr/|././/|g' {} \; -exec echo -n "Patched /usr in " \; -exec echo {} \; >& patch1.log
find usr/ -type f -exec sed -i -e "s|/${PREFIX}/|././/|g" {} \; -exec echo -n "Patched /${PREFIX} in " \; -exec echo {} \; >& patch2.log

# The fonts configuration should not be patched, copy back original one
if [ -e /$PREFIX/etc/fonts/fonts.conf ]; then
  mkdir -p usr/etc/fonts
  cp /$PREFIX/etc/fonts/fonts.conf usr/etc/fonts/fonts.conf
elif [ -e /usr/etc/fonts/fonts.conf ]; then
  mkdir -p usr/etc/fonts
  cp /usr/etc/fonts/fonts.conf usr/etc/fonts/fonts.conf
fi

# Workaround for:
# ImportError: /usr/lib/x86_64-linux-gnu/libgdk-x11-2.0.so.0: undefined symbol: XRRGetMonitors
cp $(ldconfig -p | grep libgdk-x11-2.0.so.0 | cut -d ">" -f 2 | xargs) ./usr/lib/
cp $(ldconfig -p | grep libgtk-x11-2.0.so.0 | cut -d ">" -f 2 | xargs) ./usr/lib/


# Strip binaries.
#strip_binaries


########################################################################
# AppDir complete
# Packaging it as an AppImage cannot be done within a Docker container
########################################################################
