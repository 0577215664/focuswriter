#!/bin/sh


echo -n 'Preparing files...'
cd ..

rm -f focuswriter.desktop.in.h
rm -f focuswriter.desktop.in
cp focuswriter.desktop focuswriter.desktop.in
sed -e '/^Name\[/ d' \
	-e '/^GenericName\[/ d' \
	-e '/^Comment\[/ d' \
	-e 's/^Name/_Name/' \
	-e 's/^GenericName/_GenericName/' \
	-e 's/^Comment/_Comment/' \
	-i focuswriter.desktop.in
intltool-extract --quiet --type=gettext/ini focuswriter.desktop.in

rm -f focuswriter.appdata.xml.in.h
rm -f focuswriter.appdata.xml.in
cp focuswriter.appdata.xml focuswriter.appdata.xml.in
sed -e '/p xml:lang/ d' \
	-e '/summary xml:lang/ d' \
	-e '/name xml:lang/ d' \
	-e 's/<p>/<_p>/' \
	-e 's/<\/p>/<\/_p>/' \
	-e 's/<summary>/<_summary>/' \
	-e 's/<\/summary>/<\/_summary>/' \
	-e 's/<name>/<_name>/' \
	-e 's/<\/name>/<\/_name>/' \
	-i focuswriter.appdata.xml.in
intltool-extract --quiet --type=gettext/xml focuswriter.appdata.xml.in

cd po
echo ' DONE'


echo -n 'Extracting messages...'
xgettext --from-code=UTF-8 --c++ --keyword=_ --keyword=N_:1 \
	--package-name='FocusWriter' --copyright-holder='Graeme Gott' \
	--output=description.pot ../*.h
sed 's/CHARSET/UTF-8/' -i description.pot
echo ' DONE'


echo -n 'Updating translations...'
for POFILE in *.po;
do
	echo -n " $POFILE"
	msgmerge --quiet --update --backup=none $POFILE description.pot
done
echo ' DONE'


echo -n 'Merging translations...'
cd ..

intltool-merge --quiet --desktop-style po focuswriter.desktop.in focuswriter.desktop
rm -f focuswriter.desktop.in.h
rm -f focuswriter.desktop.in

intltool-merge --quiet --xml-style po focuswriter.appdata.xml.in focuswriter.appdata.xml
rm -f focuswriter.appdata.xml.in.h
rm -f focuswriter.appdata.xml.in

echo ' DONE'
