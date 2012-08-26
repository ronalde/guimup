/*
 *  gm_library_mpdCom.cc
 *  GUIMUP mpd communicator class for the library window
 *  (c) 2008-2009 Johan Spee
 *
 *  This file is part of Guimup
 *
 *  GUIMUP is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GUIMUP is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see http://www.gnu.org/licenses/.
 */

#include "gm_library_mpdcom.h"

library_mpdCom::library_mpdCom()
{
    conn = NULL;
    b_mpdconf_found = false;
    b_connecting = false;
    b_statCheckBusy = false;
    b_dbaseUpdating = false;
    b_shuffle_busy = false;
    mpdconf_path = "";
    current_playlist = -1;
    current_songID = MPD_SONG_NO_ID;
    current_songNum = MPD_SONG_NO_NUM;
    current_status = -10;
    plistlength = 0;
    plistMax = get_plistMax();

    // first try ~/.mpdconf
    ustring homeDir = getenv("HOME");
    if (homeDir.rfind("/") != homeDir.length()-1)
        homeDir += "/";
        mpdconf_path = homeDir + ".mpdconf";

    std::ifstream conffile (mpdconf_path.data());
    if (!conffile.is_open())
    {   // next try /etc/mpd.conf
        mpdconf_path = "/etc/mpd.conf";
        std::ifstream conffile (mpdconf_path.data());
        if (conffile.is_open())
        {
            b_mpdconf_found = true;
            conffile.close();
        }
    }
    else
    {
        b_mpdconf_found = true;
        conffile.close();
    }
}


int library_mpdCom::get_version()
{
	int version = 0;
	
	if (conn == NULL)
		return version;
	
	// version 1.12 returns as 112
    version = (conn->version[0]* 100) + conn->version[1];
	return version;
}

void library_mpdCom::set_random(bool status)
{
    if (conn == NULL)
    	return;
	mpd_sendRandomCommand(conn, status);
	mpd_finishCommand(conn);
	errorCheck("mpd_sendRandomCommand");
}

void library_mpdCom::set_repeat(bool status)
{
	if (conn == NULL)
    	return;
	mpd_sendRepeatCommand(conn, status);
	mpd_finishCommand(conn);
	errorCheck("mpd_sendRepeatCommand");
}

void library_mpdCom::set_single(bool status)
{
    if (conn == NULL)
    	return;
	mpd_sendSingleCommand(conn, status);
	mpd_finishCommand(conn);
	errorCheck("mpd_sendRandomCommand");
}

void library_mpdCom::set_consume(bool status)
{
	if (conn == NULL)
    	return;
	mpd_sendConsumeCommand(conn, status);
	mpd_finishCommand(conn);
	errorCheck("mpd_sendRepeatCommand");
}

ustring library_mpdCom::get_string(ustring key)
{
    ustring result = "";
    if (!b_mpdconf_found)
        return result;

    std::string s_line;

    std::ifstream conffile (mpdconf_path.data());
    if (conffile.is_open())
    {
        while (! conffile.eof() )
        {
            getline (conffile, s_line);
            // skip empty lines and comments
            if (s_line.empty() || s_line.find("#") == 0)
                  continue;
            if (s_line.find(key) == 0)
            {
                // substr refuses to do this in one step, so take two steps:
                s_line = s_line.substr(s_line.find("\"")+1, s_line.length());
                s_line = s_line.substr(0, s_line.rfind("\""));
                result = s_line;
                break;
            }
        }
        conffile.close();
    }
    return result;
}


ustring library_mpdCom::get_musicPath()
{
    ustring result = get_string("music_directory");
    if (!result.empty())
    {
        if (result.rfind("/") != result.length()-1)
        result += "/";
    }
    return result;
}

ustring library_mpdCom::get_playlistPath()
{
    ustring result = get_string("playlist_directory");
    if (!result.empty())
    {
        if (result.rfind("/") != result.length()-1)
        result += "/";
    }
    return result;
}


int library_mpdCom::get_plistMax()
{
    int result = 0;
    ustring get = get_string("max_playlist_length");
    std::string str_int = get.data();
    std::istringstream input(str_int);
    input >> result;
    if (result == 0)
	{
        result = 16384;
		cout << "Default max playlist length: 16384" << endl;
	}
	else
		cout << "Configured max playlist length: " << result << endl;
    return result;
}


bool library_mpdCom::mpd_connect(ustring host, int port, ustring pwd)
{
    // already connected
    if (conn != NULL)
        return true;

    serverPort = port;
    serverName = host;
    serverPassword = pwd;

    b_connecting = true;

    // no server, no go.
    if (serverName.empty())
        serverName = "localhost";

    // port number must be > 1024
    if (serverPort <= 1024)
        serverPort = 6600;

    // Connect (host, port, connection timeout (sec))
    conn = mpd_newConnection(serverName.data(), serverPort ,2);
	
    if (conn->error)
    {
		mpd_clearError(conn);

		if (conn != NULL)
        {
            mpd_closeConnection(conn);
            conn = NULL;
        }
    }
	
	if (conn == NULL)
	{
		b_connecting = false;
        return false;
	}
	else
		mpd_finishCommand(conn);

    // Let's tickle MPD
    mpd_sendStatusCommand(conn);
    mpd_finishCommand(conn);
    if (conn->error) // Password required?
    {
        mpd_clearError(conn);
		mpd_finishCommand(conn);
        if (!serverPassword.empty())
        {
            mpd_sendPasswordCommand(conn, serverPassword.data());
            mpd_finishCommand(conn);
            if (conn->error) // password refused
            {
                mpd_clearError(conn);
				mpd_finishCommand(conn);
                mpd_closeConnection(conn);
                conn = NULL;
                b_connecting = false;
                return false;
            }
        }
        else
        {   // no password, so we quit
            mpd_closeConnection(conn);
            conn = NULL;
            b_connecting = false;
            return false;
        }
    }
    else // no password required
    {
        if (!serverPassword.empty())
             serverPassword = ""; // for server_reconnect
    }
    // we're in business
    b_connecting = false;
    statusCheck();
    // poll mpd every 250 msec
    signal_connected.emit(true);
    statusLoop = Glib::signal_timeout().connect(sigc::mem_fun(*this, &library_mpdCom::statusCheck), 250);
    return true;
}


bool library_mpdCom::mpd_reconnect()
{
    // previous loop has not finished
    if (b_connecting)
        return true;

    // try to connect
    b_connecting = true;
    conn = mpd_newConnection(serverName.data(), serverPort, 1);
    if (conn->error || conn == NULL)
    {
        if (conn->error)
		{
            mpd_clearError(conn);
		}

        if (conn != NULL)
        {
            mpd_closeConnection(conn);
            conn = NULL;
        }
		
        b_connecting = false;
        return true;
    }
	else
		mpd_finishCommand(conn);

    // login if there is a password
    if (!serverPassword.empty())
    {
        mpd_sendPasswordCommand(conn, serverPassword.data());
        if (conn->error)
        {
            mpd_clearError(conn);
			mpd_finishCommand(conn);
            mpd_closeConnection(conn);
            conn = NULL;
            b_connecting = false;
            return false;
        }
		mpd_finishCommand(conn);
    }

    // connected!
    b_connecting = false;
    reconnectLoop.disconnect();
    current_playlist = -1;
    current_songID = -1;
    current_songNum = -1;
    current_status = -1;
    signal_connected.emit(true);
    statusCheck();
    // poll mpd every 250 msec
    statusLoop = Glib::signal_timeout().connect(sigc::mem_fun(*this, &library_mpdCom::statusCheck), 250);
    return false;
}


void library_mpdCom::play_this(int songnr, bool stream)
{
    if (conn == NULL)
        return;
	// Stream will trigger a playlist update within a sec. We stay one step ahead:
	if (stream)
		current_playlist++;	
    mpd_sendPlayCommand(conn, songnr);
    mpd_finishCommand(conn);
    errorCheck("mpd_sendPlayCommand");
}


void library_mpdCom::clear_list()
{
    if (conn == NULL)
        return;
    mpd_sendClearCommand(conn);
    mpd_finishCommand(conn);
    errorCheck("mpd_sendClearCommand");
}


void library_mpdCom::mpd_disconnect()
{
	statusLoop.disconnect();
    if (conn != NULL)
	{
    	mpd_closeConnection(conn);
    	conn = NULL;
	}
    signal_connected.emit(false);
    // set empty playlist
    gm_songList newPlayList;
    signal_songList.emit(newPlayList);
    newPlayList.clear();
    // reset
    current_status = -10;
    current_playlist = -1;
    current_songNum = -1;
    plistlength = 0;
}


bool library_mpdCom::errorCheck(ustring action)
{
    if (conn == NULL)
    {
        statusLoop.disconnect();
        mpd_reconnect();
        // still no luck: try to reconnect every 4 seconds
        if (conn == NULL);
        {
            // set empty playlist
            gm_songList newPlayList;
            signal_songList.emit(newPlayList);
            newPlayList.clear();
            // reset
            current_status = -10;
            current_playlist = -1;
            current_songNum = -1;
            plistlength = 0;
            reconnectLoop = Glib::signal_timeout().connect(sigc::mem_fun(*this, &library_mpdCom::mpd_reconnect), 2000);
        }
        return false;
    }

    if (conn->error)
    {
        // If it is a non-fatal error (no permission etc.).
        if (conn->error == MPD_ERROR_ACK)
        {
         mpd_clearError(conn);
			mpd_finishCommand(conn);
            return true;
        }

        // A fatal error occurred: disconnnect & reconnect
		mpd_finishCommand(conn);
        mpd_disconnect();
        mpd_reconnect();
        // still no luck: reconnect every 4 seconds
        if (conn == NULL);
        {
            // set empty playlist
            gm_songList newPlayList;
            signal_songList.emit(newPlayList);
            newPlayList.clear();
            // reset
            current_status = -10;
            current_playlist = -1;
            current_songNum = -1;
            plistlength = 0;
            reconnectLoop = Glib::signal_timeout().connect(sigc::mem_fun(*this, &library_mpdCom::mpd_reconnect), 2000);
        }
        return false;
    }

    // No problems
    mpd_clearError(conn);
    return true;
}


bool library_mpdCom::statusCheck()
{

    if (conn == NULL)
    {
		signal_connected.emit(false);
        mpd_reconnect();
        // still no luck: try to reconnect every 4 seconds
        if (conn == NULL);
        {
            // set empty playlist
            gm_songList newPlayList;
            signal_songList.emit(newPlayList);
            newPlayList.clear();
            // reset
            current_status = -10;
            current_playlist = -1;
            current_songNum = -1;
            plistlength = 0;
            reconnectLoop = Glib::signal_timeout().connect(sigc::mem_fun(*this, &library_mpdCom::mpd_reconnect), 2000);
			statusLoop.disconnect();
			return false;
        }
    }

    // return "true" keeps the loop going
    if (b_statCheckBusy)
        return true;

    b_statCheckBusy = true;

    // get the current server status
    mpd_sendStatusCommand(conn);
    mpd_Status * servStatus = mpd_getStatus(conn);
    mpd_finishCommand(conn);
    if (!errorCheck("mpd_sendStatsCommand") || servStatus == NULL)
    {
        b_statCheckBusy = false;
        if (servStatus != NULL)
            mpd_freeStatus(servStatus);
        return true;
    }

    // database update was requested && has finished
    if (b_dbaseUpdating && !servStatus->updatingDb)
    {
        get_statistics();
        b_dbaseUpdating = false;
        signal_update_ready.emit();
    }

    // playlist was updated
    if (current_playlist < servStatus->playlist)
    {
        get_playlist();
        get_statistics();
        current_playlist = servStatus->playlist;
        // trigger song update
        current_songNum = -1;
        current_status = -1;
    }

    // check if there is a current song
    mpd_InfoEntity * ntity = NULL;
    mpd_sendCurrentSongCommand(conn);
    ntity = mpd_getNextInfoEntity(conn);
    mpd_finishCommand(conn);
    if (ntity == NULL || ntity->info.song == NULL)
    {
        servStatus->state = MPD_STATUS_STATE_UNKNOWN;
		servStatus->songid = -1;
		// adjust playlist status
        signal_songchange(-2, servStatus->state); // -2, because in playlist current is -1
        current_songNum = -1;
        current_songID = 0;
        current_status = MPD_STATUS_STATE_UNKNOWN;
    }
	else
    if ( (current_songID != servStatus->songid) || (current_status != servStatus->state)  )
    {   // adjust playlist status
        signal_songchange(servStatus->songid, servStatus->state);
        current_songNum = servStatus->song;
        current_songID = servStatus->songid;
        current_status = servStatus->state;
    }

    // free memory
    if (servStatus != NULL)
        mpd_freeStatus(servStatus);

    // done
    b_statCheckBusy = false;
    return true;
}


bool library_mpdCom::save_list(ustring listname)
{
    if (conn == NULL)
        return false;

    bool success = true;

    mpd_sendSaveCommand(conn, listname.data());
    mpd_finishCommand(conn);

    // errorCheck returns true on ACK errors, like when the
    // name is already in use. So we get all errors:
    if (conn->error)
        success = false;
    errorCheck("mpd_sendSaveCommand");
    return success;
}


void library_mpdCom::shuffle_list()
{
    if (plistlength < 2 || b_shuffle_busy || conn == NULL)
        return;

    b_shuffle_busy = true;
    int moveTo = 0;

    // move current song to the top
    if (current_songNum > 0)
    {
            current_playlist++; // prevent reloading
            mpd_sendMoveCommand(conn, current_songNum, moveTo);
            mpd_finishCommand(conn);
            errorCheck("mpd_sendMoveCommand");
    }

    mpd_sendCommandListBegin(conn);
    mpd_finishCommand(conn);
    errorCheck("Shuffle: mpd_sendCommandListBegin");

    // randomize the rest 2 times
    for (int i = 0; i < 2; i++)
    {
        moveTo = 1;
        while(moveTo < plistlength)
        {
            int startPos = rand()%(plistlength-moveTo) + moveTo;
            if (startPos > moveTo)
            {
                current_playlist++; // prevent reloading
                mpd_sendMoveCommand(conn, startPos, moveTo);
                mpd_finishCommand(conn);
                errorCheck("mpd_sendMoveCommand");
            }
            moveTo++;
        }
    }

    mpd_sendCommandListEnd(conn);
    mpd_finishCommand(conn);
    errorCheck("Shuffle: mpd_sendCommandListEnd");

    // trigger reloading the list
    current_playlist = -1;
    b_shuffle_busy = false;
}


void library_mpdCom::data_update()
{
    if (conn == NULL)
    {
        b_dbaseUpdating = false;
        signal_update_ready.emit();
        return;
    }
    b_dbaseUpdating = TRUE;
    mpd_sendUpdateCommand(conn, (char*)"/");
    mpd_finishCommand(conn);
    errorCheck("mpd_sendUpdateCommand");
    // when ready: see statusloop
}


gm_songList library_mpdCom::get_IDlist()
{
    gm_songList IDlist;

    if (conn == NULL)
        return IDlist;

    mpd_InfoEntity * ntity;
    // use -1 to get the WHOLE list
    mpd_sendPlaylistInfoCommand(conn, -1);
    if (errorCheck("Error requesting playlist info"))
    {
        while((ntity = mpd_getNextInfoEntity(conn)))
        {
            if (ntity->type != MPD_INFO_ENTITY_TYPE_SONG
                || !errorCheck("Error getting playlist item"))
            {
                mpd_freeInfoEntity(ntity);
                continue;
            }

            songInfo newSong;
            newSong.songID = ntity->info.song->id;
            IDlist.push_back(newSong);
            mpd_freeInfoEntity(ntity);
        }
    }
    mpd_finishCommand(conn); // sendPlaylistInfoCommand
    errorCheck("Get Song IDs");
    return IDlist;
}


void library_mpdCom::get_playlist()
{
    gm_songList newPlayList;
    plistlength = 0;

    if (conn == NULL)
    {
        signal_songList.emit(newPlayList);
        newPlayList.clear();
        return;
    }

    mpd_InfoEntity * ntity;
    // use -1 to get the WHOLE list
    mpd_sendPlaylistInfoCommand(conn, -1);
    if (errorCheck("Error requesting playlist info"))
    {
        while((ntity = mpd_getNextInfoEntity(conn)))
        {
            if (ntity->type != MPD_INFO_ENTITY_TYPE_SONG
                || !errorCheck("Error getting playlist item"))
            {
                mpd_freeInfoEntity(ntity);
                continue;
            }

            songInfo newSong = get_songInfo_from(ntity->info.song);
            newPlayList.push_back(newSong);
            mpd_freeInfoEntity(ntity);
            plistlength++;
        }
    }
    mpd_finishCommand(conn); // sendPlaylistInfoCommand
    errorCheck("Playlist update");

    // signal the library window
    signal_songList.emit(newPlayList);
    newPlayList.clear();
}


songInfo library_mpdCom::get_songInfo_from(mpd_Song *theSong)
{
    songInfo newSong;
    if (theSong == NULL)
        newSong.nosong = true;
    else
    {
        newSong.nosong = false;

        if (theSong->pos >= 0)
            newSong.songNr = theSong->pos;
        else
            newSong.songNr = -1;

        if (theSong->id >= 0)
            newSong.songID = theSong->id;
        else
            newSong.songID = -1;

        if (theSong->time >= 0)
            newSong.time = theSong->time;
        else
            newSong.time = 0;

        if (theSong->file != NULL)
            newSong.file = theSong->file;
        else
            newSong.file = "";

        if (theSong->name != NULL)
            newSong.name = theSong->name;
        else
            newSong.name = "";

        if (theSong->artist != NULL)
            newSong.artist = theSong->artist;
        else
            newSong.artist = "";

        if (theSong->title != NULL)
            newSong.title = theSong->title;
        else
            newSong.title = "";

        if (theSong->album != NULL)
            newSong.album = theSong->album;
        else
            newSong.album = "";

        if (theSong->track != NULL)
            newSong.track = fixTrackformat(theSong->track);
        else
            newSong.track = "";

        if (theSong->disc != NULL)
            newSong.disc = theSong->disc;
        else
            newSong.disc = "";

        if (theSong->date != NULL)
            newSong.date = theSong->date;
        else
            newSong.date = "";

        if (theSong->genre != NULL)
            newSong.genre = theSong->genre;
        else
            newSong.genre = "";

        if (theSong->composer != NULL)
            newSong.composer = theSong->composer;
        else
            newSong.composer = "";

        if (theSong->performer != NULL)
            newSong.performer = theSong->performer;
        else
            newSong.performer = "";

        if (theSong->comment != NULL)
            newSong.comment = theSong->comment;
        else
            newSong.comment = "";
    }
    return newSong;
}


void library_mpdCom::get_statistics()
{
    if (conn == NULL)
        return;

    mpd_Stats *sData;
    mpd_sendStatsCommand(conn);
    sData = mpd_getStats(conn);
    mpd_finishCommand(conn);
    if (!errorCheck("mpd_sendStatsCommand") || sData == NULL)
    {
        signal_stats.emit(-1 , 0, 0);
        if (sData != NULL)
            mpd_freeStats(sData);
        return;
    }

    signal_stats.emit(sData->numberOfArtists, sData->numberOfAlbums, sData->numberOfSongs);

    if (sData != NULL)
    mpd_freeStats(sData);
}

// cmdlist must start with the highest songnr!
// If the lower numbers were to be handled first,
// the higher numbers would change in the process.
void library_mpdCom::execute_cmds(gm_commandList cmdlist, bool resetlist)
{
    if(cmdlist.empty() ||conn == NULL)
        return;

    mpd_sendCommandListBegin(conn);
    if (!errorCheck("mpd_sendCommandListBegin"))
    {
        mpd_finishCommand(conn);
        cmdlist.clear();
        return;
    }

    bool b_max_reached = false;
    std::list<gm_cmdStruct>::iterator iter;
    for (iter = cmdlist.begin(); iter != cmdlist.end(); ++iter)
    {
        gm_cmdStruct curCommand = *iter;
		
        switch (curCommand.cmd)
        {
			case CMD_DPL:
			{
				cout << "deleting playlists: " << curCommand.file << endl;

				mpd_sendRmCommand(conn, curCommand.file.data());
        		mpd_finishCommand(conn);
				errorCheck("mpd_sendRmCommand");
				break;
			}
				
            case CMD_ADD:
            {
                if (plistlength + 1  > plistMax)
                {
					if (!b_max_reached)
					{
                		cout << "Reached max playlist size: " << plistMax << endl;
						b_max_reached = true;
					}
                	break;
                }
                current_playlist++; // prevent update
                plistlength++;
                mpd_sendAddCommand(conn, curCommand.file.data());
                mpd_finishCommand(conn);
                errorCheck("mpd_sendAddCommand");
                break;
            }

            case CMD_INS:
            {
                if (plistlength + 1  > plistMax)
                {
					if (!b_max_reached)
					{
                		cout << "Reached max playlist size: " << plistMax << endl;
						b_max_reached = true;
					}
                	break;
                }
                current_playlist++; // prevent update
                plistlength++;
                // add
                mpd_sendAddCommand(conn, curCommand.file.data());
                mpd_finishCommand(conn);
                errorCheck("mpd_sendAddCommand");
                // move into position
                current_playlist++; // prevent update
                mpd_sendMoveCommand(conn, plistlength-1, curCommand.moveTo);
                mpd_finishCommand(conn);
                errorCheck("mpd_sendMoveCommand");
                break;
            }

            case CMD_MOV:
            {
                current_playlist++; // prevent update
                mpd_sendMoveIdCommand(conn, curCommand.songid, curCommand.moveTo);
                mpd_finishCommand(conn);
                errorCheck("mpd_sendMoveCommand");
                break;
            }

            case CMD_DEL:
            {
                current_playlist++; // prevent update
                plistlength--;
                mpd_sendDeleteIdCommand(conn, curCommand.songid);
                mpd_finishCommand(conn);
                errorCheck("mpd_sendDeleteCommand");
				b_max_reached = false;
                break;
            }

            default:
            {
                break;
            }
        } // end switch
    }
    mpd_sendCommandListEnd(conn);
    mpd_finishCommand(conn);
    errorCheck("mpd_sendCommandListEnd");

    cmdlist.clear();
	current_songID = -1;
    if (resetlist)
        current_playlist = -1;
}


gm_itemList library_mpdCom::get_albumlist(ustring artist)
{
    gm_itemList itemlist;

    if (conn == NULL)
        return itemlist;

    if (!artist.empty())
    {
        mpd_InfoEntity *ntity = NULL;
        mpd_sendFindCommand (conn, MPD_TABLE_ARTIST, artist.data());
        if (errorCheck("mpd_getNextInfoEntity"))
        {
            while ((ntity = mpd_getNextInfoEntity(conn)) != NULL)
            {
                if(ntity->type == MPD_INFO_ENTITY_TYPE_SONG)
                {
                    mpd_Song *newSong = mpd_songDup (ntity->info.song);
                    if (newSong->album == NULL)
                    {
                        listItem newItem;
                        newItem.type = TP_ALBUM;
                        newItem.album = "";
                        newItem.artist = artist;
						newItem.sorter = newItem.artist + newItem.album;
                        itemlist.push_back(newItem);
						mpd_freeSong(newSong);
                        mpd_freeInfoEntity(ntity);
                        break;
                    }
                    mpd_freeSong(newSong);
                }
                mpd_freeInfoEntity(ntity);
            }
        }
        mpd_finishCommand (conn);
    }


    char *albumname = NULL;
    ustring theAlbum;
	
    if (artist == "*_*")	// 	"*_*" indicates 'undefined'
        mpd_sendListCommand (conn, MPD_TABLE_ALBUM, NULL);
    else
        mpd_sendListCommand (conn, MPD_TABLE_ALBUM, artist.data());
	
    if (errorCheck("mpd_sendLsInfoCommand"))
    { // ustring = NULL segfaults, that's why we use the char*
        while ((albumname = mpd_getNextAlbum(conn)) != NULL)
        {
            listItem newItem;
            newItem.type = TP_ALBUM;
       		newItem.artist = artist;
            theAlbum = albumname;
			newItem.album = theAlbum;
			newItem.sorter = newItem.artist + newItem.album;
            itemlist.push_back(newItem);
            free(albumname);
        }
    }
    mpd_finishCommand (conn);
    free(albumname);
    return itemlist;
}

gm_itemList library_mpdCom::get_folderlist(ustring dirPath)
{
    gm_itemList itemlist;

    if (conn == NULL)
        return itemlist;

    mpd_InfoEntity * ntity;
	
    mpd_sendLsInfoCommand(conn, dirPath.data());
    if (errorCheck("pd_sendLsInfoCommand"))
    {
        while((ntity = mpd_getNextInfoEntity(conn)) != NULL)
        {
            listItem newItem;
            if(ntity->type == MPD_INFO_ENTITY_TYPE_DIRECTORY)
            {
                ustring path = ntity->info.directory->path;
                newItem.dirpath = path;
                // put only the folder name in 'file'
                newItem.name = path.substr(path.rfind("/")+1, path.length());
                newItem.type = TP_FOLDER;

				struct stat info;
				int file_descriptor;
				ustring fullpath = "/home/jayes/.mpd/music/" + path + "/";
				const char *fn = fullpath.c_str();
				if ((file_descriptor = open(fn, O_RDONLY)) >= 0)
				{
						if (fstat(file_descriptor, &info) == 0) // on success 0 is returned
						{
							newItem.sorter = ustring(into_string((int)info.st_mtime));
						}
						else
						{
							perror("open() error");
							newItem.sorter = newItem.name;
						}
						close(file_descriptor);
						unlink(fn);
				}
				else
				{
					perror("open() error");
					newItem.sorter = newItem.name;
				}
				
				itemlist.push_back(newItem);
            }
            else
            if(ntity->type == MPD_INFO_ENTITY_TYPE_SONG)
            {
                ustring file = ntity->info.song->file;
                newItem.file = file;
                // put only the file name in 'label'
                newItem.name = file.substr(file.rfind("/")+1, file.length());
				newItem.sorter = newItem.name;
                mpd_Song *newSong = mpd_songDup (ntity->info.song);
                newItem.type = TP_SONG;
                newItem.artist = "";
                if (newSong->artist != NULL)
                    newItem.artist = newSong->artist;
                newItem.album = "";
                if (newSong->album != NULL)
                    newItem.album = newSong->album;
                newItem.title = "";
                if (newSong->title != NULL)
                    newItem.title = newSong->title;
                newItem.track = "";
                if (newSong->track != NULL)
                    newItem.track = fixTrackformat(newSong->track);
                newItem.file = "";
                if (newSong->file != NULL)
                    newItem.file = newSong->file;
				newItem.genre = "";
                if (newSong->genre != NULL)
                    newItem.genre = newSong->genre;				
                newItem.time = 0;
                if (newSong->time != 0)
                    newItem.time = newSong->time;
                mpd_freeSong(newSong);
                itemlist.push_back(newItem);
            }
            else
            {
                // No file, no dir. That's weird. Skip!
                mpd_freeInfoEntity(ntity);
                continue;
            }
            mpd_freeInfoEntity(ntity);
        } // end while
    }
    mpd_finishCommand (conn);
    return itemlist;
}


gm_itemList library_mpdCom::get_songsbygenre(ustring genre)
{
    gm_itemList itemlist;

    if (conn == NULL)
        return itemlist;

    mpd_InfoEntity * ntity;
    mpd_startSearch(conn, 1); // 1 is Exact. 0 is Not Exact.
    mpd_addConstraintSearch(conn, MPD_TAG_ITEM_GENRE, genre.data());
    mpd_commitSearch(conn);
    if (errorCheck("mpd_commitSearch"))
    {
        while((ntity = mpd_getNextInfoEntity(conn)) != NULL)
        {
            if(ntity->type == MPD_INFO_ENTITY_TYPE_SONG)
            {
                mpd_Song *newSong = mpd_songDup (ntity->info.song);
                listItem newItem;
                newItem.type = TP_SONG;
                newItem.album = "";
                if (newSong->album != NULL)
                    newItem.album = newSong->album;
                newItem.artist = "";
                if (newSong->artist != NULL)
                    newItem.artist = newSong->artist;
                newItem.title = "";
                if (newSong->title != NULL)
                    newItem.title = newSong->title;
                newItem.track = "";
                if (newSong->track != NULL)
                    newItem.track = fixTrackformat(newSong->track);
                newItem.file = "";
                if (newSong->file != NULL)
                    newItem.file = newSong->file;
				newItem.genre = "";
                if (newSong->genre != NULL)
                    newItem.genre = newSong->genre;
                newItem.time = 0;
                if (newSong->time != 0)
                    newItem.time = newSong->time;
				newItem.sorter = newItem.artist + newItem.title;
                mpd_freeSong(newSong);
                itemlist.push_back(newItem);
            }
            mpd_freeInfoEntity(ntity);
        }
        mpd_finishCommand(conn);
    }
    return itemlist;
}


gm_itemList library_mpdCom::get_genrelist()
{
    gm_itemList itemlist;

    if (conn == NULL)
        return itemlist;

    char *genreName;
    mpd_startFieldSearch(conn, MPD_TAG_ITEM_GENRE);
    mpd_commitSearch(conn);
    if (errorCheck("mpd_commitSearch"))
    {
        while ( (genreName = mpd_getNextTag(conn, MPD_TAG_ITEM_GENRE)) != NULL )
        {
            listItem newItem;
            newItem.name = genreName;
			newItem.sorter = newItem.name;
            newItem.type = TP_GENRE;
            itemlist.push_back(newItem);
            free(genreName);
        }
        mpd_finishCommand(conn);
    }
    return itemlist;
}


gm_itemList library_mpdCom::get_songsbytitle(ustring title)
{
    gm_itemList itemlist;

    if (conn == NULL)
        return itemlist;

    mpd_InfoEntity * ntity;
    mpd_sendSearchCommand(conn, MPD_TABLE_TITLE, title.data());
    if (errorCheck("mpd_sendSearchCommand"))
    {
        while((ntity = mpd_getNextInfoEntity(conn)) != NULL)
        {
            if(ntity->type == MPD_INFO_ENTITY_TYPE_SONG)
            {
                mpd_Song *newSong = mpd_songDup (ntity->info.song);
                listItem newItem;
                newItem.type = TP_SONG;
                newItem.album = "";
                if (newSong->album != NULL)
                    newItem.album = newSong->album;
                newItem.artist = "";
                if (newSong->artist != NULL)
                    newItem.artist = newSong->artist;
                newItem.title = "";
                if (newSong->title != NULL)
                    newItem.title = newSong->title;
                newItem.track = "";
                if (newSong->track != NULL)
                    newItem.track = fixTrackformat(newSong->track);
                newItem.file = "";
                if (newSong->file != NULL)
                    newItem.file = newSong->file;
				newItem.genre = "";
                if (newSong->genre != NULL)
                    newItem.genre = newSong->genre;
                newItem.time = 0;
                if (newSong->time != 0)
                    newItem.time = newSong->time;
				newItem.sorter = newItem.artist + newItem.title;
                mpd_freeSong(newSong);
                itemlist.push_back(newItem);
            }
            mpd_freeInfoEntity(ntity);
        }
    }
    mpd_finishCommand(conn);
    return itemlist;
}

gm_itemList library_mpdCom::get_songlist(ustring album, ustring artist)
{
    gm_itemList itemlist;

    if (conn == NULL)
        return itemlist;
	
    if (album == "")
    {
        // get all songs for the artist,but only add when album tag is empty
        mpd_InfoEntity *ntity = NULL;
        mpd_sendFindCommand (conn, MPD_TABLE_ARTIST, artist.data());
        if (errorCheck("mpd_getNextInfoEntity"))
        {
            while ((ntity = mpd_getNextInfoEntity(conn)) != NULL)
            {
                if(ntity->type == MPD_INFO_ENTITY_TYPE_SONG)
                {
                    mpd_Song *newSong = mpd_songDup (ntity->info.song);
                    if (newSong->album == NULL)
                    {
                        listItem newItem;
                        newItem.type = TP_SONG;
                        newItem.album = "";
                        newItem.artist = artist;
                        newItem.title = "";
						if (newSong->title != NULL)
                            newItem.title = newSong->title;
                        newItem.track = "";
                        if (newSong->track != NULL)
						{
                            newItem.track = fixTrackformat(newSong->track);
							newItem.sorter = artist + newItem.track + newItem.title;
						}
						else
							newItem.sorter = artist + "00" + newItem.title;
                        newItem.file = "";
                        if (newSong->file != NULL)
                            newItem.file = newSong->file;
						newItem.genre = "";
            			if (newSong->genre != NULL)
                			newItem.genre = newSong->genre;
                        newItem.time = 0;
                        if (newSong->time != 0)
                            newItem.time = newSong->time;
                        itemlist.push_back(newItem);
                    }
					mpd_freeSong(newSong);
                }
                mpd_freeInfoEntity(ntity);
            }
        }
        mpd_finishCommand (conn);
        return itemlist;
    } //  album == ""

    mpd_InfoEntity *ntity = NULL;
    mpd_sendFindCommand (conn, MPD_TABLE_ALBUM, album.data() );
    if (errorCheck("mpd_sendFindCommand"))
    {
        while ((ntity = mpd_getNextInfoEntity(conn)) != NULL)
        {
            if(ntity->type == MPD_INFO_ENTITY_TYPE_SONG)
            {
                mpd_Song *newSong = mpd_songDup (ntity->info.song);
				
				ustring thisartist = ""; 
				if (newSong->artist != NULL)
					thisartist = newSong->artist;
		
                // different artists can have albums with the same name.
				if ( (thisartist == artist) || artist == "*_*")		// 	"*_*" indicates 'undefined'
                {
		      		listItem newItem;
		            newItem.type = TP_SONG;
		            newItem.artist = thisartist;
		            newItem.album = album;
		            newItem.title = "";
		            if (newSong->title != NULL)
		                newItem.title = newSong->title;
		            newItem.track = "";
		            if (newSong->track != NULL)
					{
		        		newItem.track = fixTrackformat(newSong->track);
						newItem.sorter = thisartist + newItem.track + newItem.title;
					}
					else
						newItem.sorter = thisartist + "00" + newItem.title;
		            newItem.file = "";
		            if (newSong->file != NULL)
		                newItem.file = newSong->file;
					newItem.genre = "";
		            if (newSong->genre != NULL)
		                newItem.genre = newSong->genre;
		            newItem.time = 0;
		            if (newSong->time != 0)
		                newItem.time = newSong->time;
		            itemlist.push_back(newItem);
				}
				mpd_freeSong(newSong);
            }
            mpd_freeInfoEntity(ntity);
        }
    }
    mpd_finishCommand (conn);
}


int library_mpdCom::get_listChange()
{
    int id = -1;
    if (conn == NULL)
        return id;

    mpd_InfoEntity *ntity = NULL;
    mpd_sendPlChangesPosIdCommand (conn, current_playlist-1);
    if (errorCheck("get_listChange"))
    {
        while (( ntity = mpd_getNextInfoEntity(conn)) != NULL)
        {
            if(ntity->type == MPD_INFO_ENTITY_TYPE_SONG)
            {
                // get the first that comes along
                mpd_Song *newSong = mpd_songDup (ntity->info.song);
                id = newSong->id;
                mpd_freeSong(newSong);
            }
            mpd_freeInfoEntity(ntity);
            if (id != -1)
                break;
        }
    }
    mpd_finishCommand(conn);
    return id;
}


gm_itemList library_mpdCom::get_artistlist()
{
    gm_itemList itemlist;

    if (conn == NULL)
        return itemlist;

    char *artistname = NULL;
    mpd_sendListCommand (conn, MPD_TABLE_ARTIST, NULL);
    if (errorCheck("mpd_sendLsInfoCommand"))
    { // ustring = NULL segfaults, that's why we use the char*
        while ((artistname = mpd_getNextArtist(conn)) != NULL)
        {
            listItem newItem;
            newItem.type = TP_ARTIST;
            newItem.artist = artistname;
			newItem.sorter = newItem.artist;
            itemlist.push_back(newItem);
            free(artistname);
        }
    }
    mpd_finishCommand (conn);
    free(artistname);
    return itemlist;
}

// add a saved playlist to the current playlist
void library_mpdCom::load_savedplaylist(ustring listName)
{
    if (conn == NULL)
    return;
	// Load the dropped list
    mpd_sendLoadCommand(conn, listName.data());
    mpd_finishCommand(conn);
    errorCheck("mpd_sendLoadCommand");
    current_playlist = -1;
}


gm_itemList library_mpdCom::get_playlistlist()
{
    gm_itemList itemlist;

    if (conn == NULL)
        return itemlist;

    mpd_InfoEntity *ntity = NULL;
    mpd_sendLsInfoCommand(conn, "");
    if (errorCheck("mpd_sendLsInfoCommand"))
    {
        while ((ntity = mpd_getNextInfoEntity(conn)) != NULL)
        {
            if(ntity->type == MPD_INFO_ENTITY_TYPE_PLAYLISTFILE)
            {
                listItem newItem;
                newItem.type = TP_PLAYLIST;
                ustring path = ntity->info.playlistFile->path;
                newItem.dirpath = path;
                // put only the list name in 'name'
                newItem.name = path.substr(path.rfind("/")+1, path.length());
				newItem.sorter = newItem.name;
                itemlist.push_back(newItem);
            }
            mpd_freeInfoEntity(ntity);
        }
    }
    mpd_finishCommand (conn);
    return itemlist;
}

ustring library_mpdCom::get_date(ustring album, ustring artist)
{
    ustring date = " ? ";

    if (conn == NULL)
        return date;

    mpd_InfoEntity *ntity = NULL;
    mpd_sendFindCommand (conn, MPD_TABLE_ALBUM, album.data());
    if (errorCheck("mpd_sendFindCommand"))
    {
        while ((ntity = mpd_getNextInfoEntity(conn)) != NULL)
        {
            if(ntity->type == MPD_INFO_ENTITY_TYPE_SONG)
            {
                mpd_Song *newSong = mpd_songDup (ntity->info.song);
				
				ustring thisartist = ""; 
				if (newSong->artist != NULL)
					thisartist = newSong->artist;
				
                // different artists can have albums with the same name.
				if (thisartist == artist && newSong->date != NULL)	
                {
                    date = newSong->date;
					mpd_freeSong(newSong);
                    mpd_freeInfoEntity(ntity);
                    break;
                }
                mpd_freeSong(newSong);
            }
            mpd_freeInfoEntity(ntity);
        }
    }
    mpd_finishCommand (conn);
    return date;
}

listItem library_mpdCom::get_item_from(ustring filename)
{
	listItem newItem;
	// in case the file does not exist:
	newItem.artist = "* File not found! ";
	newItem.track = " ";
	newItem.title = " ";
	newItem.album = " ";
	newItem.genre = "";
	newItem.time = 0;
	
    mpd_InfoEntity * ntity;
    mpd_startSearch(conn, 1); // 1 is Exact. 0 is Not Exact.
    mpd_addConstraintSearch(conn, MPD_TAG_ITEM_FILENAME, filename.data());
    mpd_commitSearch(conn);
    if (errorCheck("mpd_commitSearch"))
    {
        while((ntity = mpd_getNextInfoEntity(conn)) != NULL)
        {
            if(ntity->type == MPD_INFO_ENTITY_TYPE_SONG)
            {
                mpd_Song *newSong = mpd_songDup (ntity->info.song);

                newItem.type = TP_SONG;
                newItem.album = "";
                if (newSong->album != NULL)
                    newItem.album = newSong->album;
                newItem.artist = "";
                if (newSong->artist != NULL)
                    newItem.artist = newSong->artist;
                newItem.title = "";
                if (newSong->title != NULL)
                    newItem.title = newSong->title;
                newItem.track = "";
                if (newSong->track != NULL)
                    newItem.track = fixTrackformat(newSong->track);
                newItem.file = filename;
				newItem.genre = "";
                if (newSong->genre != NULL)
                    newItem.genre = newSong->genre;
                newItem.time = 0;
                if (newSong->time != 0)
                    newItem.time = newSong->time;

                mpd_freeSong(newSong);
				mpd_freeInfoEntity(ntity);
	            return newItem;			
                break;
            }
            mpd_freeInfoEntity(ntity);
        }
    }
}



ustring library_mpdCom::into_string(int in)
{
    std::ostringstream ssIn;
    ssIn << in;
    Glib::ustring strOut = ssIn.str();

    return strOut;
}

ustring library_mpdCom::fixTrackformat (ustring instring)
{
	// remove the x/xx format
	ustring str = instring;
	str =  instring.substr(0, str.find("/"));
    // add leading zero, when required
	if (str.length() == 1)
		str = "0" + str;
	return str;
}

library_mpdCom::~library_mpdCom()
{
}