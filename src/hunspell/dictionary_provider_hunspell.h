/***********************************************************************
 *
 * Copyright (C) 2013 Graeme Gott <graeme@gottcode.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#ifndef DICTIONARY_PROVIDER_HUNSPELL_H
#define DICTIONARY_PROVIDER_HUNSPELL_H

#include "../abstract_dictionary.h"
#include "../abstract_dictionary_provider.h"

class Hunspell;

class QString;
class QStringList;
class QStringRef;
class QTextCodec;

class DictionaryHunspell : public AbstractDictionary
{
public:
	DictionaryHunspell(const QString& language);
	~DictionaryHunspell();

	QStringRef check(const QString& string, int start_at) const;
	QStringList suggestions(const QString& word) const;

	void addToPersonal(const QString& word);
	void addToSession(const QStringList& words);
	void removeFromSession(const QStringList& words);

	static void setIgnoreNumbers(bool ignore);
	static void setIgnoreUppercase(bool ignore);

private:
	Hunspell* m_dictionary;
	QTextCodec* m_codec;
};

class DictionaryProviderHunspell : public AbstractDictionaryProvider
{
public:
	DictionaryProviderHunspell();

	QStringList availableDictionaries() const;
	AbstractDictionary* requestDictionary(const QString& language) const;
};

#endif
