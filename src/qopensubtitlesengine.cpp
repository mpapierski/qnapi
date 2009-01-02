/*****************************************************************************
** QNapi
** Copyright (C) 2008 Krzemin <pkrzemin@o2.pl>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
*****************************************************************************/

#include "qopensubtitlesengine.h"

// konstruktor klasy
QOpenSubtitlesEngine::QOpenSubtitlesEngine(const QString & movieFile, const QString & subtitlesFile)
	: QNapiAbstractEngine(movieFile, subtitlesFile)
{
	rpc.setHost(openSubtitlesXmlRpcHost, openSubtitlesXmlRpcPort, openSubtitlesXmlRpcPath);
	p7zipPath = GlobalConfig().p7zipPath();
	lang = GlobalConfig().language();
	noBackup = GlobalConfig().noBackup();
	tmpPackedFile =  QString("%1/%2").arg(tmpPath).arg(generateTmpFileName());
}

// destruktor klasy
QOpenSubtitlesEngine::~QOpenSubtitlesEngine()
{
	cleanup();
	if(isLogged())
		logout();
}

// zwraca nazwe modulu
QString QOpenSubtitlesEngine::engineName()
{
	return "OpenSubtitles";
}

// zwraca informacje nt. modulu
QString QOpenSubtitlesEngine::engineInfo()
{
	return "Modul pobierania napisów z bazy <b>www.opensubtitles.org</b>\n"
			"Copyright (c) 2008 by Krzemin";
}

// zwraca ikone w formacie XMP
QIcon QOpenSubtitlesEngine::engineIcon()
{
	static const char *icon[]={
		"16 16 14 1",
		". c #000000",
		"h c #111111",
		"c c #222222",
		"j c #333333",
		"g c #444444",
		"l c #555555",
		"e c #777777",
		"k c #888888",
		"a c #999999",
		"b c #aaaaaa",
		"f c #cccccc",
		"i c #dddddd",
		"d c #eeeeee",
		"# c #ffffff",
		"................",
		".##.##.##.##.##.",
		".##.##.##.##.##.",
		"................",
		"................",
		"...a##b..cd##d..",
		"..ea..be.fg.hi..",
		"..ic..hi.ig.....",
		"..dh..hd.g##ij..",
		"..ea..ak.k..gi..",
		"...b##b..fd##l..",
		"................",
		"................",
		".##.##.##.##.##.",
		".##.##.##.##.##.",
		"................"};
	return QIcon(icon);
}

// oblicza sume kontrolna dla pliku filmowego
QString QOpenSubtitlesEngine::checksum(QString filename)
{
	if(filename.isEmpty())
		filename = movie;

	QFile file(filename);
	if(!file.open(QIODevice::ReadOnly))
		return QString("");

	fileSize = file.size();
	quint64 hash = fileSize;
	quint64 tmp, i;

	for(tmp = 0, i = 0; i < 65536/sizeof(tmp) && file.read((char*)&tmp, sizeof(tmp)); i++, hash += tmp) ;
	file.seek(qMax(0, (int)((qint64)fileSize - 65536)));
	for(tmp = 0, i = 0; i < 65536/sizeof(tmp) && file.read((char*)&tmp, sizeof(tmp)); i++, hash += tmp) ;

	return (checkSum = QString("%1").arg(hash, 16, 16, QChar('0')));
}

// szuka napisow
bool QOpenSubtitlesEngine::lookForSubtitles(QString lang)
{
	if(checkSum.isEmpty()) return false;
	if(!isLogged() && !login()) return false;

	subtitlesList.clear();

	QMap<QString, xmlrpc::Variant> paramsMap;
	QList<xmlrpc::Variant> requestList;

	paramsMap["sublanguageid"] = QString("pol");
	paramsMap["moviehash"] = checkSum;
	paramsMap["moviebytesize"] = (uint) fileSize;

	requestList << paramsMap;

//qDebug() << "\n\nrequestList:\n\n" << requestList;

	rpc.request("SearchSubtitles", token, requestList);

	QMap<QString, QVariant> responseMap = rpc.result().toMap();

//qDebug() << "\n\nresponseMap:\n\n" << responseMap;

	if(!responseMap.contains("data"))
		return false;


	QList<QVariant> dataList = responseMap["data"].toList();
	QList<QVariant>::iterator i;

	i = dataList.begin();
	while(i != dataList.end())
	{
		responseMap = (*i).toMap();

		if((checkSum == responseMap["MovieHash"]) && (fileSize == responseMap["MovieByteSize"]))
		{
			subtitlesList << QNapiSubtitleInfo(	responseMap["ISO639"].toString(),
												engineName(),
												responseMap["IDSubtitleFile"].toString(),
												responseMap["MovieName"].toString(),
												responseMap["SubAuthorComment"].toString(),
												QFileInfo(responseMap["SubFileName"].toString()).suffix());
			
			/*
			qDebug("tryDownload: znaleziono napisy!");
			//subtitlesUrl = responseMap["SubDownloadLink"].toString();
			if(subtitlesUrl.isEmpty())
			{
				subtitlesUrl = responseMap["ZipDownloadLink"].toString();
				lastDownloadedIsZip = true;
			}
			subFileName = responseMap["SubFileName"].toString();
			qDebug("tryDownload: SubDownloadLink = %s", qPrintable(subtitlesUrl));

			break;*/
		}

		++i;
	}

	return true;
}

// wyniki wyszukiwania
QList<QNapiSubtitleInfo> QOpenSubtitlesEngine::listSubtitles()
{
	return subtitlesList;
}

// Probuje pobrac napisy do filmu z serwera OpenSubtitles
bool QOpenSubtitlesEngine::download(int idx)
{
	//qDebug() << "idx = " << idx;
	

	QList<xmlrpc::Variant> paramsList;
	QList<xmlrpc::Variant> requestList;

	paramsList << subtitlesList.at(idx).url;
	subFileName = generateTmpFileName() + "." + subtitlesList.at(idx).format;

	requestList << paramsList;

//qDebug() << "\n\nrequestList:\n\n" << requestList;

	rpc.request("DownloadSubtitles", token, requestList);

	QMap<QString, QVariant> responseMap = rpc.result().toMap();

//qDebug() << "\n\nresponseMap:\n\n" << responseMap;

	if(!responseMap.contains("data"))
		return false;

//	qDebug() << "still alive!";

	QVariantList responseList;
	responseList << responseMap["data"].toList();

	if(responseList.size() == 0)
		return false;


	QByteArray subtitleContent = QByteArray::fromBase64(responseList.at(0).toMap()["data"].toByteArray());

	QFile file(tmpPackedFile);
	if(file.exists()) file.remove();
	if(!file.open(QIODevice::WriteOnly))
		return false;

	int r = file.write(subtitleContent);
	file.close();
	//qDebug("download: r = %d", r);

	return (bool)r;
}

// Probuje dopasowac napisy do filmu
bool QOpenSubtitlesEngine::unpack()
{
	if(!QFile::exists(tmpPackedFile)) return false;
	if(!QFile::exists(movie)) return false;	

	QStringList args;
	args << "e" << "-y" << ("-o" + tmpPath) << tmpPackedFile;

	QProcess p7zip;
	p7zip.start(p7zipPath, args);

	if(!p7zip.waitForFinished()) return false;

	QString unpackedTmp = tmpPath + QDir::separator() + QFileInfo(tmpPackedFile).completeBaseName();
	subtitlesTmp = tmpPath + QDir::separator() + subFileName;

	QFile::copy(unpackedTmp, subtitlesTmp);
	QFile::remove(unpackedTmp);

	return true;
}

void QOpenSubtitlesEngine::cleanup()
{
/*	QStringList tmpFiles = QDir(tmpDir).entryList(QDir::Files | QDir::NoDotAndDotDot);
	foreach(QString tmpF, tmpFiles)
	{
		qDebug("QOpenSubtitlesEngine::cleanup() tmpF = %s", qPrintable(tmpDir + "/" + tmpF));
		QFile::remove(tmpDir + "/" + tmpF);
	}
	QDir().rmdir(tmpDir);*/


	QFile::remove(tmpPackedFile);
	QFile::remove(subtitlesTmp);
}

bool QOpenSubtitlesEngine::login()
{
	QString userAgent = QString("QNapi v%1").arg(QNAPI_VERSION);

	rpc.request("LogIn", QString(""), QString(""),  lang.toLower(), userAgent);
	token = (rpc.result().toMap())["token"].toString();

	return !token.isEmpty();
}

void QOpenSubtitlesEngine::logout()
{
	rpc.request("LogOut", token);
	token = "";
}
