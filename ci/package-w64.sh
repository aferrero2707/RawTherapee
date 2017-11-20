#!/bin/bash

# transfer.sh
transfer() 
{ 
	if [ $# -eq 0 ]; then 
		echo "No arguments specified. Usage:\necho transfer /tmp/test.md\ncat /tmp/test.md | transfer test.md"; 		
		return 1; 
	fi
	tmpfile=$( mktemp -t transferXXX ); 
	if tty -s; then 
		basefile=$(basename "$1" | sed -e 's/[^a-zA-Z0-9._-]/-/g'); 
		curl --progress-bar --upload-file "$1" "https://transfer.sh/$basefile" >> $tmpfile; 
	else 
		curl --progress-bar --upload-file "-" "https://transfer.sh/$1" >> $tmpfile ; 
	fi; 
	cat $tmpfile; 
	rm -f $tmpfile; 
}

/usr/bin/x86_64-w64-mingw32-gcc -v

mkdir -p /work/w64-build && cd /work/w64-build

crossroad w64 w64-build --run=$TRAVIS_BUILD_DIR/ci/build-w64.sh

ls /work/w64-build/Release/rawtherapee.exe || exit 1

bundle_package=rawtherapee
bundle_version="w64-$(date +%Y%m%d)_$(date +%H%M)-git-${TRAVIS_BRANCH}"

# stuff is in here
basedir=`pwd`
 
# download zips to here
packagedir=packages

# unzip to here
installdir=$HOME/.local/share/crossroad/roads/w64/w64-build

# jhbuild will download sources to here 
#checkoutdir=source

mingw_prefix=x86_64-w64-mingw32-

repackagedir=$TRAVIS_BUILD_DIR/$bundle_package-$bundle_version

echo "Contents of \"$installdir/bin\":"
ls -l $installdir/bin
echo "================="; echo ""

echo "copying install area \"$installdir\""

rm -rf $repackagedir
(cp -r $installdir $repackagedir) || exit 1
rm -rf $repackagedir/bin
rm -rf $repackagedir/wine
mkdir $repackagedir/bin
(cp -L $installdir/bin/* $repackagedir/bin) || exit 1
(cp -a /work/w64-build/Release/* $repackagedir) || exit 1
(cp -L $installdir/lib/*.dll $repackagedir/) || exit 1
(cp -L $installdir/bin/*.dll $repackagedir/) || exit 1
echo "================="; echo ""

echo "Contents of \"$repackagedir\":"
ls -l $repackagedir
echo "================="; echo ""
echo "Contents of \"$repackagedir/bin\":"
ls -l $repackagedir/bin
echo "================="; echo ""

echo "cleaning build \"$repackagedir\""

if [ ! -e $repackagedir/bin ]; then echo "$repackagedir/bin not found."; exit; fi
if [ ! -e $repackagedir/lib ]; then echo "$repackagedir/lib not found."; exit; fi

#(cd $repackagedir/bin; wget ftp://ftp.equation.com/gdb/64/gdb.exe)

echo "Before cleaning $repackagedir/bin"
pwd
#read dummy

#( cd $repackagedir/bin ; echo "$repackagedir/bin before cleaning:"; ls $repackagedir/bin; mkdir poop ; mv *photoflow* pfbatch.exe gdb.exe phf_stack.exe gdk-pixbuf-query-loaders.exe update-mime-database.exe camconst.json gmic_def.gmic poop ; mv *.dll poop ; rm -f * ; mv poop/* . ; rmdir poop )

#( cd $repackagedir/bin ; rm -f libvipsCC-15.dll run-nip2.sh *-vc100-*.dll *-vc80-*.dll *-vc90-*.dll  )

#( cd $repackagedir/bin ; strip --strip-unneeded *.exe )

# for some reason we can't strip zlib1
#( cd $repackagedir/bin ; mkdir poop ; mv zlib1.dll poop ; strip --strip-unneeded *.dll ; mv poop/zlib1.dll . ; rmdir poop )


( cd $repackagedir/share ; rm -rf aclocal applications doc glib-2.0 gtk-2.0 gtk-doc ImageMagick-* info jhbuild man mime pixmaps xml goffice locale icons)

( cd $repackagedir ; rm -rf include )

# we need some lib stuff at runtime for goffice and the theme
( cd $repackagedir/lib ; mkdir ../poop ; mv goffice gtk-2.0 gdk-pixbuf-2.0 ../poop ; rm -rf * ; mv ../poop/* . ; rmdir ../poop )

# we don't need a lot of it though
( cd $repackagedir/lib/gtk-2.0 ; find . -name "*.la" -exec rm {} \; )
( cd $repackagedir/lib/gtk-2.0 ; find . -name "*.a" -exec rm {} \; )
( cd $repackagedir/lib/gtk-2.0 ; find . -name "*.h" -exec rm {} \; )

( cd $repackagedir ; rm -rf make man manifest src bin share/gdb)
echo "================="; echo ""

# we need to copy the C++ runtime dlls in there
gccmingwlibdir=/usr/lib/gcc/x86_64-w64-mingw32
mingwlibdir=/usr/x86_64-w64-mingw32/lib
cp -L $gccmingwlibdir/*/*.dll $repackagedir/
cp -L $mingwlibdir/*.dll $repackagedir/

#wine $repackagedir/bin/gdk-pixbuf-query-loaders.exe | sed -e "s%Z:$(pwd)/$repackagedir%..%g" > $repackagedir/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache
#cat $repackagedir/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache

#rm -rf $repackagedir/share/mime
#cp -a /usr/share/mime $repackagedir/share/mime
#rm $repackagedir/share/mime/application/vnd.ms-*

mkdir -p $repackagedir/share/glib-2.0/schemas
cp -a $installdir/share/glib-2.0/schemas/gschemas.compiled $repackagedir/share/glib-2.0/schemas

apt-get install -y liblensfun-bin
lensfun-update-data
mkdir -p $repackagedir/share/lensfun
cp -a /var/lib/lensfun-updates/version_1/* $repackagedir/share/lensfun

(cd $repackagedir && \
wget http://ftp.gnome.org/pub/gnome/sources/adwaita-icon-theme/3.26/adwaita-icon-theme-3.26.0.tar.xz && \
tar xJf adwaita-icon-theme-3.26.0.tar.xz && cp -a adwaita-icon-theme-3.26.0/Adwaita $repackagedir/share/icons && \
rm -rf adwaita-icon-theme-3.26.0*) || exit 1

#exit

#echo creating $bundle_package-$bundle_version.zip
#rm -f $bundle_package-$bundle_version.zip
#zip -r -qq $bundle_package-$bundle_version.zip $bundle_package-$bundle_version

rm -f $TRAVIS_BUILD_DIR/$bundle_package-$bundle_version.zip
cd $repackagedir/../
zip -r $TRAVIS_BUILD_DIR/$bundle_package-$bundle_version.zip $bundle_package-$bundle_version

transfer $TRAVIS_BUILD_DIR/$bundle_package-$bundle_version.zip

exit

# have to make in a subdir to make sure makensis does not grab other stuff
echo building installer nsis/$photoflow_package-$photoflow_version-setup.exe
( cd nsis ; rm -rf $photoflow_package-$photoflow_version ; 
#unzip -qq -o ../$photoflow_package-$photoflow_version.zip ;
rm -rf $photoflow_package-$photoflow_version
mv ../$photoflow_package-$photoflow_version .
#makensis -DVERSION=$photoflow_version $photoflow_package.nsi > makensis.log 
)
cd nsis
rm -f $photoflow_package-$photoflow_version.zip
zip -r $photoflow_package-$photoflow_version.zip $photoflow_package-$photoflow_version
rm -rf $photoflow_package-$photoflow_version
rm -f $photoflow_package-$photoflow_version-setup.zip
#zip $photoflow_package-$photoflow_version-setup.zip $photoflow_package-$photoflow_version-setup.exe
