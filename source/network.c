//    SoulFu - 3D Rogue-like dungeon crawler
//    Copyright (C) 2007 Aaron Bishop
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//    web:   http://www.aaronbishopgames.com
//    email: aaron@aaronbishopgames.com

// <ZZ> This file contains functions to handle networking
//
// SoulFu multiplayer is peer-to-peer over UDP.  One machine "hosts" a game
// (which just means it has a game running and answers join requests), other
// machines join it by address.  On joining, the new peer receives the game
// seed (so it generates an identical world map) and the address list of all
// other peers.  Every machine owns ("hosts") the characters it spawned
// locally; it broadcasts their state in ROOM_UPDATE packets several times a
// second.  Receivers mirror those characters as network puppets, which are
// driven by the EVENT_NETWORK_UPDATE script event (see STANDARD.SRC).
//
// A puppet character stores (owning remote slot + 1) as an unsigned int at
// character_data+252.  Zero means the character is hosted locally.  Remotes
// are identified by (host, port) pairs, so several instances can run on one
// machine for testing.

#define UDP_PORT 17859          // Orangeville, PA
#define UDP_PORT_SCAN 16        // Try UDP_PORT..UDP_PORT+15 so several instances can share a machine
#define MAX_REMOTE 16           // Maximum number of network'd computers

// Timing (milliseconds)...
#define ROOM_UPDATE_INTERVAL_MS 150
#define I_AM_HERE_INTERVAL_MS   1000
#define JOIN_RETRY_MS           1000
#define JOIN_TIMEOUT_MS         15000


unsigned char network_on = FALSE;           // The UDP socket is open
unsigned char network_game_active = FALSE;  // We're hosting or have joined a network game
unsigned char network_verbose = FALSE;      // Log every packet (--net-verbose)
unsigned short network_port = 0;            // The port we actually bound
unsigned char* netlist = NULL;


UDPsocket       remote_socket;
IPaddress       remote_address[MAX_REMOTE];
unsigned short  remote_room_number[MAX_REMOTE];
unsigned int    remote_last_heard_ms[MAX_REMOTE];
unsigned char   remote_on[MAX_REMOTE];
unsigned short  num_remote = 0;
#define REMOTE_TIMEOUT_MS 15000


// Join handshake state...  join_progress values match WNETMAP.SRC's
// SYS_JOINPROGRESS display (0 == idle, 1 == request sent, 4 == joined)...
unsigned char   join_progress = 0;
IPaddress       join_address;
unsigned int    join_started_ms = 0;
unsigned int    join_last_send_ms = 0;


// Statistics for debugging...
unsigned int network_packets_sent = 0;
unsigned int network_packets_received = 0;
unsigned int network_packets_rejected = 0;


// Legacy version-check globals (SYS_VERSIONERROR)...
unsigned char global_version_error = FALSE;
unsigned short required_executable_version = 65535;
unsigned short required_data_version = 65535;


#define MAX_PACKET_SIZE 8192
#define PACKET_HEADER_SIZE  3
#define PACKET_TYPE_CHAT                    0
#define PACKET_TYPE_I_AM_HERE               1
#define PACKET_TYPE_ROOM_UPDATE             2
#define PACKET_TYPE_I_WANNA_PLAY            3
#define PACKET_TYPE_OKAY_YOU_CAN_PLAY       4
#define PACKET_TYPE_GOODBYE                 5
unsigned char packet_buffer[MAX_PACKET_SIZE];
unsigned short packet_length;
unsigned short packet_counter;
unsigned short packet_readpos;
unsigned char packet_seed;
unsigned char packet_checksum;


unsigned char  network_script_newly_spawned;
unsigned char  network_script_extra_data;
unsigned char  network_script_remote_index;
unsigned char  network_script_netlist_index;
unsigned short network_script_x;
unsigned short network_script_y;
unsigned char  network_script_z;
unsigned char  network_script_facing;
unsigned char  network_script_action;
unsigned char  network_script_team;
unsigned char  network_script_poison;
unsigned char  network_script_petrify;
unsigned char  network_script_alpha;
unsigned char  network_script_deflect;
unsigned char  network_script_haste;
unsigned char  network_script_other_enchant;
unsigned char  network_script_eqleft;
unsigned char  network_script_eqright;
unsigned char  network_script_eqcol01;
unsigned char  network_script_eqcol23;              // high-data only
unsigned char  network_script_eqspec1;              // high-data only
unsigned char  network_script_eqspec2;              // high-data only
unsigned char  network_script_eqhelm;               // high-data only
unsigned char  network_script_eqbody;               // high-data only
unsigned char  network_script_eqlegs;               // high-data only
unsigned char  network_script_class;                // high-data only
unsigned short network_script_mount_index;          // high-data only

void network_send_room_update(void);




//-----------------------------------------------------------------------------------------------
// Logging...  net_log() goes to the logfile and to the in-game message window,
// net_verbose_log() goes to the logfile only, and only with --net-verbose...
//-----------------------------------------------------------------------------------------------
char net_log_string[256];
void net_log(char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(net_log_string, sizeof(net_log_string), format, args);
    va_end(args);
    log_message("NET:    %s", net_log_string);
    message_add(net_log_string, "NET", FALSE);
}

#define net_verbose_log(...) { if(network_verbose) log_message("NET:    " __VA_ARGS__); }

// Helpers for printing an address as 4 ints and a readable port...
#define ADDRESS_ARGS(ADDRESS) ((unsigned char*)&(ADDRESS).host)[0], ((unsigned char*)&(ADDRESS).host)[1], ((unsigned char*)&(ADDRESS).host)[2], ((unsigned char*)&(ADDRESS).host)[3], ((((unsigned char*)&(ADDRESS).port)[0]<<8) | ((unsigned char*)&(ADDRESS).port)[1])




//-----------------------------------------------------------------------------------------------
// Packet macros...
//-----------------------------------------------------------------------------------------------
#define packet_begin(type)                                                  \
{                                                                           \
    packet_length = PACKET_HEADER_SIZE;                                     \
    packet_buffer[0] = (unsigned char) type;                                \
    packet_buffer[1] = 0;                                                   \
    packet_buffer[2] = 0;                                                   \
}
// packet_buffer[0] is the packet type...
// packet_buffer[1] is the random seed
// packet_buffer[2] is the checksum


//-----------------------------------------------------------------------------------------------
#define packet_add_string(string)                                           \
{                                                                           \
    packet_counter = 0;                                                     \
    while(string[packet_counter] != 0)                                      \
    {                                                                       \
        packet_buffer[packet_length] = string[packet_counter];              \
        packet_length++;                                                    \
        packet_counter++;                                                   \
    }                                                                       \
    packet_buffer[packet_length] = 0;                                       \
    packet_length++;                                                        \
}

//-----------------------------------------------------------------------------------------------
#define packet_add_unsigned_int(number)                                     \
{                                                                           \
    packet_buffer[packet_length] = (unsigned char) ((number)>>24);          \
    packet_buffer[packet_length+1] = (unsigned char) ((number)>>16);        \
    packet_buffer[packet_length+2] = (unsigned char) ((number)>>8);         \
    packet_buffer[packet_length+3] = (unsigned char) (number);              \
    packet_length+=4;                                                       \
}

//-----------------------------------------------------------------------------------------------
#define packet_add_unsigned_short(number)                                   \
{                                                                           \
    packet_buffer[packet_length] = (unsigned char) ((number)>>8);           \
    packet_buffer[packet_length+1] = (unsigned char) (number);              \
    packet_length+=2;                                                       \
}

//-----------------------------------------------------------------------------------------------
#define packet_add_unsigned_char(number)                                    \
{                                                                           \
    packet_buffer[packet_length] = (unsigned char) (number);                \
    packet_length++;                                                        \
}

//-----------------------------------------------------------------------------------------------
#define packet_read_string(string)                                          \
{                                                                           \
    packet_counter = 0;                                                     \
    while(packet_buffer[packet_readpos] != 0 && packet_readpos < MAX_PACKET_SIZE && packet_counter < 250)    \
    {                                                                       \
        string[packet_counter] = packet_buffer[packet_readpos];             \
        packet_counter++;                                                   \
        packet_readpos++;                                                   \
    }                                                                       \
    string[packet_counter] = 0;                                             \
    packet_readpos++;                                                       \
}

//-----------------------------------------------------------------------------------------------
#define packet_read_unsigned_int(to_set)                                    \
{                                                                           \
    to_set = packet_buffer[packet_readpos];                                 \
    to_set = to_set << 8;                                                   \
    to_set |= packet_buffer[packet_readpos+1];                              \
    to_set = to_set << 8;                                                   \
    to_set |= packet_buffer[packet_readpos+2];                              \
    to_set = to_set << 8;                                                   \
    to_set |= packet_buffer[packet_readpos+3];                              \
    packet_readpos+=4;                                                      \
}

//-----------------------------------------------------------------------------------------------
#define packet_read_unsigned_short(to_set)                                  \
{                                                                           \
    to_set = packet_buffer[packet_readpos];                                 \
    to_set = to_set << 8;                                                   \
    to_set |= packet_buffer[packet_readpos+1];                              \
    packet_readpos+=2;                                                      \
}

//-----------------------------------------------------------------------------------------------
#define packet_read_unsigned_char(to_set)                                   \
{                                                                           \
    to_set = packet_buffer[packet_readpos];                                 \
    packet_readpos++;                                                       \
}

//-----------------------------------------------------------------------------------------------
#define packet_encrypt()                                                    \
{                                                                           \
    packet_seed = random_number;                                            \
    packet_buffer[1] = packet_seed;                                         \
    packet_counter = PACKET_HEADER_SIZE;                                    \
    while(packet_counter < packet_length)                                   \
    {                                                                       \
        packet_buffer[packet_counter] += random_table[(packet_seed+2173-packet_counter)&and_random];         \
        packet_counter++;                                                   \
    }                                                                       \
}

//-----------------------------------------------------------------------------------------------
#define packet_decrypt()                                                    \
{                                                                           \
    packet_counter = PACKET_HEADER_SIZE;                                    \
    packet_seed = packet_buffer[1];                                         \
    while(packet_counter < packet_length)                                   \
    {                                                                       \
        packet_buffer[packet_counter] -= random_table[(packet_seed+2173-packet_counter)&and_random];         \
        packet_counter++;                                                   \
    }                                                                       \
}

//-----------------------------------------------------------------------------------------------
#define calculate_packet_checksum()                                         \
{                                                                           \
    packet_checksum = 0;                                                    \
    packet_counter = PACKET_HEADER_SIZE;                                    \
    while(packet_counter < packet_length)                                   \
    {                                                                       \
        packet_checksum += packet_buffer[packet_counter];                   \
        packet_counter++;                                                   \
    }                                                                       \
}

//-----------------------------------------------------------------------------------------------
#define packet_end()                                                        \
{                                                                           \
    calculate_packet_checksum();                                            \
    packet_buffer[2] = packet_checksum;                                     \
    packet_encrypt();                                                       \
}

//-----------------------------------------------------------------------------------------------
unsigned char packet_valid()
{
    calculate_packet_checksum();
    return (packet_buffer[2] == packet_checksum);
}




//-----------------------------------------------------------------------------------------------
// Remote list handling...
//-----------------------------------------------------------------------------------------------
void network_clear_remote_list()
{
    // <ZZ> This function clears the list of other network players...
    unsigned short i;
    num_remote = 0;
    repeat(i, MAX_REMOTE)
    {
        remote_on[i] = FALSE;
    }
}

//-----------------------------------------------------------------------------------------------
unsigned short network_find_remote(IPaddress* address)
{
    // <ZZ> Finds the remote slot for a given (host, port) pair...  Returns MAX_REMOTE if
    //      the address is unknown...
    unsigned short i;
    repeat(i, MAX_REMOTE)
    {
        if(remote_on[i] && remote_address[i].host == address->host && remote_address[i].port == address->port)
        {
            return i;
        }
    }
    return MAX_REMOTE;
}

//-----------------------------------------------------------------------------------------------
unsigned short network_add_remote(IPaddress* address)
{
    // <ZZ> This function adds a new network peer...  Returns the slot it went into,
    //      or MAX_REMOTE if the list is full or the peer was already known...
    unsigned short i;

    if(network_find_remote(address) != MAX_REMOTE)
    {
        return MAX_REMOTE;
    }
    repeat(i, MAX_REMOTE)
    {
        if(remote_on[i] == FALSE)
        {
            remote_address[i] = *address;
            remote_on[i] = TRUE;
            remote_room_number[i] = 65535;
            remote_last_heard_ms[i] = SDL_GetTicks();
            num_remote++;
            net_log("Peer %d.%d.%d.%d:%d added (slot %d, %d total)", ADDRESS_ARGS(*address), i, num_remote);
            return i;
        }
    }
    net_log("ERROR: Too many peers, can't add %d.%d.%d.%d:%d", ADDRESS_ARGS(*address));
    return MAX_REMOTE;
}

//-----------------------------------------------------------------------------------------------
void network_kill_remote_characters(unsigned short remote)
{
    // <ZZ> This function kills off all puppet characters owned by the given remote...
    unsigned short i;
    unsigned char* character_data;
    repeat(i, MAX_CHARACTER)
    {
        if(main_character_on[i])
        {
            character_data = main_character_data[i];
            if(*((unsigned int*)(character_data+252)) == ((unsigned int) remote)+1)
            {
                character_data[82] = 0;  // Give 'em 0 hits...
                character_data[67] = EVENT_DAMAGED;
                global_attacker = i;
                global_attack_spin = (*((unsigned short*) (character_data + 56))) + 32768;
                fast_run_script(main_character_script_start[i], FAST_FUNCTION_EVENT, character_data);
            }
        }
    }
}

//-----------------------------------------------------------------------------------------------
void network_delete_remote(unsigned short remote)
{
    if(remote < MAX_REMOTE)
    {
        if(remote_on[remote])
        {
            network_kill_remote_characters(remote);
            remote_on[remote] = FALSE;
            num_remote--;
            net_log("Peer %d.%d.%d.%d:%d removed (%d left)", ADDRESS_ARGS(remote_address[remote]), num_remote);
        }
    }
}




//-----------------------------------------------------------------------------------------------
// Setup & teardown...
//-----------------------------------------------------------------------------------------------
void network_leave_game(void);
void network_close(void)
{
    // <ZZ> This function shuts down the network...  Says goodbye to everybody first...
    network_leave_game();
    log_message("INFO:   Shutting down the network");
    SDLNet_Quit();
}

//-----------------------------------------------------------------------------------------------
unsigned char network_setup(void)
{
    // <ZZ> This function initializes all the networking stuff.  Returns TRUE if networking is
    //      available, FALSE if not.
    unsigned short i;

    network_on = FALSE;
    network_game_active = FALSE;
    join_progress = 0;
    network_clear_remote_list();


    log_message("INFO:   ------------------------------------------");
    log_message("INFO:   Looking for NETLIST.DAT...");
    netlist = sdf_find_filetype("NETLIST", SDF_FILE_IS_DAT);
    if(netlist)
    {
        netlist = sdf_index_get_data(netlist);
        log_message("INFO:   Found NETLIST.DAT...");
    }
    else
    {
        log_message("ERROR:  NETLIST.DAT is missing, characters won't be sent over network...");
    }


    log_message("INFO:   Trying to turn on networking...");
    if(SDLNet_Init() == 0)
    {
        // Try to open our UDP port...  If it's taken (another instance running on this
        // machine), scan upwards so every instance gets its own port...
        if(network_port == 0)
        {
            repeat(i, UDP_PORT_SCAN)
            {
                remote_socket = SDLNet_UDP_Open(UDP_PORT+i);
                if(remote_socket)
                {
                    network_port = UDP_PORT+i;
                    break;
                }
            }
        }
        else
        {
            // Port forced with --net-port...
            remote_socket = SDLNet_UDP_Open(network_port);
            if(!remote_socket)
            {
                network_port = 0;
            }
        }
        if(remote_socket)
        {
            network_on = TRUE;
            log_message("INFO:   Network started, listening on UDP port %d", network_port);
            atexit(network_close);
        }
        else
        {
            log_message("ERROR:  Couldn't open any UDP port...  No networking...  %s", SDLNet_GetError());
        }
    }
    else
    {
        log_message("ERROR:  SDLNet_Init failed...  %s", SDLNet_GetError());
    }
    log_message("INFO:   ------------------------------------------");
    return network_on;
}




//-----------------------------------------------------------------------------------------------
// Sending...
//-----------------------------------------------------------------------------------------------
void network_send_to(IPaddress* address)
{
    // <ZZ> This function sends the current packet_buffer to one specific address...
    UDPpacket udp_packet;
    udp_packet.channel = -1;
    udp_packet.data = packet_buffer;
    udp_packet.len = packet_length;
    udp_packet.maxlen = MAX_PACKET_SIZE;
    udp_packet.address = *address;
    net_verbose_log("Sending type %d packet (%d bytes) to %d.%d.%d.%d:%d", packet_buffer[0], packet_length, ADDRESS_ARGS(*address));
    if(!SDLNet_UDP_Send(remote_socket, -1, &udp_packet))
    {
        net_log("ERROR: Send to %d.%d.%d.%d:%d failed: %s", ADDRESS_ARGS(*address), SDLNet_GetError());
    }
    else
    {
        network_packets_sent++;
    }
}

//-----------------------------------------------------------------------------------------------
#define NETWORK_ALL_REMOTES_IN_GAME             0
#define NETWORK_ALL_REMOTES_IN_ROOM             1
void network_send(unsigned char send_code)
{
    // <ZZ> This function sends the current packet_buffer to all peers (or all peers known
    //      to be in the same room)...
    unsigned short i;
    if(network_on)
    {
        repeat(i, MAX_REMOTE)
        {
            if(remote_on[i])
            {
                if(send_code == NETWORK_ALL_REMOTES_IN_GAME || remote_room_number[i] == map_current_room)
                {
                    network_send_to(&remote_address[i]);
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------------------------
void network_send_i_wanna_play(void)
{
    packet_begin(PACKET_TYPE_I_WANNA_PLAY);
        packet_add_unsigned_char(TRUE);     // Request the peer list...
    packet_end();
    network_send_to(&join_address);
}

//-----------------------------------------------------------------------------------------------
void network_send_i_am_here(void)
{
    // <ZZ> This function tells all peers what room we're in (and, via the seed, what game
    //      we belong to)...  Also serves as the introduction when we've just joined, and as
    //      the heartbeat that keeps us from being timed out...  Room 65535 means we're not
    //      actually playing yet (character creation)...
    packet_begin(PACKET_TYPE_I_AM_HERE);
        if(play_game_active)
        {
            packet_add_unsigned_short(map_current_room);
        }
        else
        {
            packet_add_unsigned_short(65535);
        }
        packet_add_unsigned_int(game_seed);
    packet_end();
    network_send(NETWORK_ALL_REMOTES_IN_GAME);
}

//-----------------------------------------------------------------------------------------------
void network_on_local_room_change(void)
{
    // <ZZ> Tell all peers our new room immediately (don't wait for the periodic heartbeat)...
    if(network_on && network_game_active && play_game_active && num_remote > 0)
    {
        network_send_i_am_here();
        network_send_room_update();
    }
}




//-----------------------------------------------------------------------------------------------
// Game lifecycle...
//-----------------------------------------------------------------------------------------------
void network_leave_game(void)
{
    // <ZZ> This function leaves whatever network game we're in (or aborts a join attempt)...
    unsigned short i;
    if(network_game_active && num_remote > 0)
    {
        packet_begin(PACKET_TYPE_GOODBYE);
        packet_end();
        network_send(NETWORK_ALL_REMOTES_IN_GAME);
        net_log("Left the network game");
    }
    if(join_progress > 0 && join_progress < 4)
    {
        net_log("Join attempt aborted");
    }
    network_game_active = FALSE;
    join_progress = 0;
    if(num_remote > 0)
    {
        repeat(i, MAX_REMOTE)
        {
            remote_on[i] = FALSE;
        }
        num_remote = 0;
    }
}

//-----------------------------------------------------------------------------------------------
unsigned char network_host_game(void)
{
    // <ZZ> This function starts hosting a new network game...  Hosting just means having a
    //      game running and answering join requests - the world seed is ours...
    if(!network_on)
    {
        net_log("ERROR: Can't host, network is off");
        return FALSE;
    }
    network_leave_game();
    generate_game_seed();
    network_game_active = TRUE;
    main_game_active = TRUE;
    net_log("Hosting on port %d (seed %u)", network_port, game_seed);
    return TRUE;
}

//-----------------------------------------------------------------------------------------------
unsigned char network_join_game(char* target)
{
    // <ZZ> This function starts joining a network game at "host" or "host:port"...
    //      The handshake is finished in network_listen(); progress can be polled with
    //      SYS_JOINPROGRESS (4 == done)...
    char host_part[128];
    int port = UDP_PORT;
    int i;

    if(!network_on || target == NULL || target[0] == 0)
    {
        net_log("ERROR: Can't join, network is off or no address given");
        return FALSE;
    }


    // Split "host:port"...
    i = 0;
    while(target[i] != 0 && target[i] != ':' && i < 127)
    {
        host_part[i] = target[i];
        i++;
    }
    host_part[i] = 0;
    if(target[i] == ':')
    {
        port = atoi(target+i+1);
        if(port <= 0 || port > 65535) { port = UDP_PORT; }
    }


    if(SDLNet_ResolveHost(&join_address, host_part, (unsigned short) port) != 0)
    {
        net_log("ERROR: Can't resolve %s", host_part);
        return FALSE;
    }


    network_leave_game();
    join_progress = 1;
    join_started_ms = SDL_GetTicks();
    join_last_send_ms = join_started_ms;
    net_log("Joining %d.%d.%d.%d:%d ...", ADDRESS_ARGS(join_address));
    network_send_i_wanna_play();
    return TRUE;
}




//-----------------------------------------------------------------------------------------------
unsigned short network_find_remote_character(unsigned short remote, unsigned char local_index_on_remote)
{
    // <ZZ> This function attempts to find the index of a character on the local computer, who is
    //      hosted on the given remote - with the given local index number on that remote...
    //      This lets me find something like character number 23 on Bob's computer, which is
    //      handled as character 42 on my computer...  It returns the index on my computer, or
    //      MAX_CHARACTER if a match can't be found...
    unsigned short i;
    unsigned char* character_data;

    repeat(i, MAX_CHARACTER)
    {
        if(main_character_on[i])
        {
            character_data = main_character_data[i];
            if(*((unsigned int*)(character_data+252)) == ((unsigned int) remote)+1)
            {
                if(character_data[250] == local_index_on_remote)
                {
                    return i;
                }
            }
        }
    }
    return MAX_CHARACTER;
}

//-----------------------------------------------------------------------------------------------
unsigned char network_room_is_remotely_occupied(unsigned short room)
{
    // <ZZ> Returns TRUE if any known peer is currently in the given room...  Used to skip
    //      spawning our own copies of a room's monsters when a peer is already there (their
    //      copies arrive as network puppets instead)...
    unsigned short i;
    if(network_game_active)
    {
        repeat(i, MAX_REMOTE)
        {
            if(remote_on[i] && remote_room_number[i] == room)
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}




//-----------------------------------------------------------------------------------------------
// Receiving...
//-----------------------------------------------------------------------------------------------
void network_handle_room_update(unsigned short remote)
{
    // <ZZ> This function handles the body of a ROOM_UPDATE packet from the given remote...
    unsigned short room_number;
    unsigned short seed;
    unsigned char door_flags;
    unsigned short i, num_char;
    signed short length;
    unsigned char* script_file_start;
    float x, y, z;
    unsigned char found;
    unsigned char* character_data;
    unsigned char filename[9];

    packet_read_unsigned_short(room_number);        // The room number this sender is in...
    packet_read_unsigned_short(seed);               // Low 16 bits of the sender's game seed...
    if(seed != (unsigned short)(game_seed & 65535))
    {
        network_packets_rejected++;
        net_verbose_log("Rejected room update from slot %d (seed mismatch)", remote);
        return;
    }


    if(!netlist)
    {
        network_kill_remote_characters(remote);
        return;
    }

    if(room_number != map_current_room)
    {
        // Ignore stale room updates - room changes are handled by I_AM_HERE only.
        network_kill_remote_characters(remote);
        return;
    }
    remote_room_number[remote] = room_number;


    // Start to kill off any of this host's characters...  Ones that are still in the
    // packet get their hits restored; ones that aren't get finished off below...
    repeat(i, MAX_CHARACTER)
    {
        if(main_character_on[i])
        {
            character_data = main_character_data[i];
            if(*((unsigned int*)(character_data+252)) == ((unsigned int) remote)+1)
            {
                character_data[82] = 0;  // Give 'em 0 hits...
            }
        }
    }


    packet_read_unsigned_char(door_flags);      // The door flags for this room...
    if(map_current_room < num_map_room)
    {
        map_room_data[map_current_room][29] |= door_flags;  // Doors open'd remotely open here too...
    }
    packet_read_unsigned_char(num_char);        // The number of characters in this packet...
    length = packet_length - packet_readpos;    // The number of bytes remaining...
    while(num_char > 0 && length >= 11)
    {
        network_script_newly_spawned = FALSE;
        packet_read_unsigned_char(network_script_remote_index);
        packet_read_unsigned_char(network_script_netlist_index);
        packet_read_unsigned_char(network_script_z);
        packet_read_unsigned_char(network_script_x);
        packet_read_unsigned_char(network_script_y);
        network_script_x = network_script_x | ((network_script_z&192)<<2);
        network_script_y = network_script_y | ((network_script_z&48)<<4);
        network_script_z = network_script_z & 15;
        packet_read_unsigned_char(network_script_facing);
        packet_read_unsigned_char(network_script_action);
        network_script_extra_data = network_script_action>>7;
        network_script_action = network_script_action&127;
        packet_read_unsigned_char(network_script_team);
        network_script_poison = (network_script_team >> 5) & 1;
        network_script_petrify = (network_script_team >> 4) & 1;
        network_script_alpha = (network_script_team & 8) ? (64) : (255);
        network_script_deflect = (network_script_team >> 2) & 1;
        network_script_haste = (network_script_team >> 1) & 1;
        network_script_other_enchant = (network_script_team & 1);
        network_script_team = network_script_team >> 6;
        packet_read_unsigned_char(network_script_eqleft);
        packet_read_unsigned_char(network_script_eqright);
        packet_read_unsigned_char(network_script_eqcol01);
        if(network_script_extra_data && length >= 19)
        {
            // We've got more data coming...
            packet_read_unsigned_char(network_script_eqcol23);
            packet_read_unsigned_char(network_script_eqspec1);
            packet_read_unsigned_char(network_script_eqspec2);
            packet_read_unsigned_char(network_script_eqhelm);
            packet_read_unsigned_char(network_script_eqbody);
            packet_read_unsigned_char(network_script_eqlegs);
            packet_read_unsigned_char(network_script_class);
            packet_read_unsigned_char(network_script_mount_index);
            if(network_script_mount_index != network_script_remote_index)
            {
                // Character is riding a mount...
                network_script_mount_index = network_find_remote_character(remote, (unsigned char) network_script_mount_index);
            }
            else
            {
                network_script_mount_index = MAX_CHARACTER;
            }
        }
        else
        {
            // Script shouldn't ask for these, but just in case...
            network_script_eqcol23 = 0;
            network_script_eqspec1 = 0;
            network_script_eqspec2 = 0;
            network_script_eqhelm = 0;
            network_script_eqbody = 0;
            network_script_eqlegs = 0;
            network_script_class = 0;
            network_script_mount_index = MAX_CHARACTER;
        }


        // Okay, we've read all of the data for this character, now let's see if we need to spawn it...
        i = network_find_remote_character(remote, network_script_remote_index);
        found = FALSE;
        if(i < MAX_CHARACTER)
        {
            if(main_character_on[i])
            {
                // Looks like we've found the character...
                found = TRUE;
            }
        }
        if(!found)
        {
            // We didn't find this character - that means we'll have to try to spawn a new one of the appropriate type...
            x = (network_script_x - 512.0f) * 0.25f;
            y = (network_script_y - 512.0f) * 0.25f;
            z = room_heightmap_height(roombuffer, x, y);
            z = z + (network_script_z*2.0f);


            script_file_start = netlist + (network_script_netlist_index<<3);
            repeat(i, 8)
            {
                filename[i] = script_file_start[i];
            }
            filename[8] = 0;
            i = MAX_CHARACTER;
            script_file_start = sdf_find_filetype(filename, SDF_FILE_IS_RUN);
            if(script_file_start)
            {
                script_file_start = sdf_index_get_data(script_file_start);
                character_data = obj_spawn(CHARACTER, x, y, z, script_file_start, 65535);
                if(character_data)
                {
                    if(character_data >= main_character_data[0] && character_data <= main_character_data[MAX_CHARACTER-1])
                    {
                        network_script_newly_spawned = TRUE;
                        i = (character_data-main_character_data[0])/CHARACTER_SIZE;
                        i = i & (MAX_CHARACTER-1);
                        *((unsigned int*)(character_data+252)) = ((unsigned int) remote)+1;
                        character_data[250] = network_script_remote_index;
                        net_verbose_log("Spawned puppet %s (character %d) for slot %d", filename, i, remote);
                    }
                }
            }
        }
        // Now let's give the character a script function call, so we can handle the network data more precisely...
        if(i < MAX_CHARACTER)
        {
            if(main_character_on[i])
            {
                character_data = main_character_data[i];
                character_data[67] = EVENT_NETWORK_UPDATE;
                fast_run_script(main_character_script_start[i], FAST_FUNCTION_EVENT, character_data);
            }
        }


        num_char--;
        length = packet_length - packet_readpos;
    }


    // Finish killing off any character whose hits haven't been reset...
    repeat(i, MAX_CHARACTER)
    {
        if(main_character_on[i])
        {
            character_data = main_character_data[i];
            if(*((unsigned int*)(character_data+252)) == ((unsigned int) remote)+1)
            {
                if(character_data[82] == 0)
                {
                    character_data[67] = EVENT_DAMAGED;
                    global_attacker = i;
                    global_attack_spin = (*((unsigned short*) (character_data + 56))) + 32768;
                    fast_run_script(main_character_script_start[i], FAST_FUNCTION_EVENT, character_data);
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------------------------
void network_listen(void)
{
    // <ZZ> This function checks for incoming packets, and handles 'em all
    UDPpacket udp_packet;
    unsigned char character_class;
    unsigned short remote, slot;
    unsigned short room_number;
    unsigned int seed;
    unsigned short i, count;
    IPaddress address;


    if(!network_on) { return; }


    // Let's try to follow SDLNet's little format...
    udp_packet.channel = -1;
    udp_packet.data = packet_buffer;
    udp_packet.len = MAX_PACKET_SIZE;
    udp_packet.maxlen = MAX_PACKET_SIZE;


    while(SDLNet_UDP_Recv(remote_socket, &udp_packet) == 1)
    {
        network_packets_received++;
        net_verbose_log("Got type %d packet (%d bytes) from %d.%d.%d.%d:%d", packet_buffer[0], udp_packet.len, ADDRESS_ARGS(udp_packet.address));
        packet_length = udp_packet.len;
        packet_decrypt();
        if(!packet_valid())
        {
            network_packets_rejected++;
            net_verbose_log("Rejected packet (bad checksum)");
            continue;
        }
        packet_readpos = PACKET_HEADER_SIZE;
        remote = network_find_remote(&udp_packet.address);
        if(remote < MAX_REMOTE)
        {
            remote_last_heard_ms[remote] = SDL_GetTicks();
        }


        switch(packet_buffer[0])
        {
            case PACKET_TYPE_CHAT:
                if(remote == MAX_REMOTE) { network_packets_rejected++; break; }
                packet_read_unsigned_char(character_class);     // Speaker class (unused)
                packet_read_string(run_string[0]);              // Speaker name
                packet_read_string(run_string[1]);              // Message
                message_add(run_string[1], run_string[0], TRUE);
                break;


            case PACKET_TYPE_I_AM_HERE:
                packet_read_unsigned_short(room_number);
                packet_read_unsigned_int(seed);
                if(!network_game_active || seed != game_seed)
                {
                    network_packets_rejected++;
                    net_verbose_log("Rejected I_AM_HERE (not in a matching game)");
                    break;
                }
                if(remote == MAX_REMOTE)
                {
                    // A peer we don't know yet (someone else's joiner introducing itself)...
                    remote = network_add_remote(&udp_packet.address);
                    if(remote == MAX_REMOTE) { break; }
                }
                if(remote_room_number[remote] != room_number)
                {
                    remote_room_number[remote] = room_number;
                    if(room_number != map_current_room)
                    {
                        if(play_game_active && network_game_active && room_number < num_map_room)
                        {
                            map_sync_to_peer_room(room_number, map_current_room);
                        }
                        else
                        {
                            network_kill_remote_characters(remote);
                        }
                    }
                }
                break;


            case PACKET_TYPE_ROOM_UPDATE:
                if(remote == MAX_REMOTE) { network_packets_rejected++; break; }
                network_handle_room_update(remote);
                break;


            case PACKET_TYPE_I_WANNA_PLAY:
                if(!network_game_active)
                {
                    network_packets_rejected++;
                    net_verbose_log("Rejected join request (no game running)");
                    break;
                }
                if(remote == MAX_REMOTE)
                {
                    remote = network_add_remote(&udp_packet.address);
                    if(remote == MAX_REMOTE) { break; }
                }
                // Reply with the game seed, our room, and everybody else's address & room...
                // Rooms are included so the joiner knows who occupies which room BEFORE it
                // loads its first room (otherwise it would spawn its own copies of room
                // props/monsters and every machine would see duplicates)...
                packet_begin(PACKET_TYPE_OKAY_YOU_CAN_PLAY);
                    packet_add_unsigned_int(game_seed);
                    packet_add_unsigned_short(map_current_room);
                    count = 0;
                    repeat(i, MAX_REMOTE)
                    {
                        if(remote_on[i] && i != remote) { count++; }
                    }
                    packet_add_unsigned_short(count);
                    repeat(i, MAX_REMOTE)
                    {
                        if(remote_on[i] && i != remote)
                        {
                            // host & port are already network byte order in memory - send raw bytes...
                            packet_add_unsigned_char(((unsigned char*)&remote_address[i].host)[0]);
                            packet_add_unsigned_char(((unsigned char*)&remote_address[i].host)[1]);
                            packet_add_unsigned_char(((unsigned char*)&remote_address[i].host)[2]);
                            packet_add_unsigned_char(((unsigned char*)&remote_address[i].host)[3]);
                            packet_add_unsigned_char(((unsigned char*)&remote_address[i].port)[0]);
                            packet_add_unsigned_char(((unsigned char*)&remote_address[i].port)[1]);
                            packet_add_unsigned_short(remote_room_number[i]);
                        }
                    }
                packet_end();
                network_send_to(&udp_packet.address);
                break;


            case PACKET_TYPE_OKAY_YOU_CAN_PLAY:
                if(join_progress < 1 || join_progress >= 4)
                {
                    network_packets_rejected++;
                    break;
                }
                packet_read_unsigned_int(seed);
                game_seed = seed;
                packet_read_unsigned_short(room_number);
                if(remote == MAX_REMOTE)
                {
                    remote = network_add_remote(&udp_packet.address);
                }
                if(remote < MAX_REMOTE)
                {
                    remote_room_number[remote] = room_number;
                }
                packet_read_unsigned_short(count);
                repeat(i, count)
                {
                    // host & port arrive as raw network-byte-order bytes...
                    packet_read_unsigned_char(((unsigned char*)&address.host)[0]);
                    packet_read_unsigned_char(((unsigned char*)&address.host)[1]);
                    packet_read_unsigned_char(((unsigned char*)&address.host)[2]);
                    packet_read_unsigned_char(((unsigned char*)&address.host)[3]);
                    packet_read_unsigned_char(((unsigned char*)&address.port)[0]);
                    packet_read_unsigned_char(((unsigned char*)&address.port)[1]);
                    packet_read_unsigned_short(room_number);
                    slot = network_add_remote(&address);
                    if(slot < MAX_REMOTE)
                    {
                        remote_room_number[slot] = room_number;
                    }
                }
                join_progress = 4;
                network_game_active = TRUE;
                main_game_active = TRUE;
                net_log("Joined game (seed %u, %d peers)", game_seed, num_remote);
                // Introduce ourselves to everyone (peers other than the one we asked
                // don't know us yet)...
                network_send_i_am_here();
                break;


            case PACKET_TYPE_GOODBYE:
                if(remote < MAX_REMOTE)
                {
                    network_delete_remote(remote);
                }
                break;


            default:
                network_packets_rejected++;
                net_verbose_log("Rejected packet (unknown type %d)", packet_buffer[0]);
                break;
        }


        // Reset for the next packet...
        udp_packet.len = MAX_PACKET_SIZE;
    }
}

//-----------------------------------------------------------------------------------------------
unsigned char network_find_script_index(unsigned char* filename)
{
    // <ZZ> This function finds a character script filename in the NETLIST.DAT file...  So
    //      we don't have to send the whole thing over the network...  Returns 0 if it didn't
    //      find a match...
    unsigned short i, j;
    unsigned char found;
    unsigned char* checkname;
    if(netlist)
    {
        checkname = netlist+8;
        i = 1;
        while(i < 256)
        {
            found = TRUE;
            repeat(j, 8)
            {
                if(checkname[j] == filename[j])
                {
                    if(checkname[j] == 0)
                    {
                        return ((unsigned char) i);
                    }
                }
                else
                {
                    found = FALSE;
                    j = 8;
                }
            }
            if(found)
            {
                return ((unsigned char) i);
            }
            checkname+=8;
            i++;
        }
    }
    return 0;
}




//-----------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------
// Special functions to send certain types of packets...
//-----------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------
void network_send_chat(unsigned char speaker_class, unsigned char* speaker_name, unsigned char* message)
{
    // <ZZ> This function sends a chat message to all the players in the room (or if message starts with
    //      <ALL> it goes to all in the game)...
    unsigned char send_to_all;

    send_to_all = FALSE;
    if(message == NULL || speaker_name == NULL) { return; }
    if(message[0] == '<' && message[1] == 'A' && message[2] == 'L' && message[3] == 'L' && message[4] == '>')
    {
        send_to_all = TRUE;
        message+=5;
        if(message[0] == ' ')
        {
            message++;
        }
    }
    packet_begin(PACKET_TYPE_CHAT);
        packet_add_unsigned_char(speaker_class);
        packet_add_string(speaker_name);
        packet_add_string(message);
    packet_end();


    // Spit out the message on the local computer...
    message_add(message, speaker_name, TRUE);


    if(send_to_all)
    {
        network_send(NETWORK_ALL_REMOTES_IN_GAME);
    }
    else
    {
        network_send(NETWORK_ALL_REMOTES_IN_ROOM);
    }
}

//-----------------------------------------------------------------------------------------------
void network_send_room_update(void)
{
    // <ZZ> This function sends a room update packet to all peers...  The packet includes all
    //      characters that are hosted on the local machine...  Peers that aren't in our room
    //      use the packet to clean up our puppets on their side...
    unsigned short local_character_count, i, facing;
    unsigned char* character_data;
    unsigned short mount;
    float* character_xyz;
    unsigned short x, y;
    float fz;
    unsigned char z;
    unsigned char pmod;
    unsigned char action;
    unsigned char misc;

    // Make sure we're in a valid room...
    if(map_current_room < MAX_MAP_ROOM)
    {
        // Count how many characters we need to send over network...
        local_character_count = 0;
        repeat(i, MAX_CHARACTER)
        {
            // Only need to send characters that are used...
            if(main_character_on[i])
            {
                // Only need to send characters that are hosted locally...
                if(*((unsigned int*)(main_character_data[i]+252)) == 0)
                {
                    // Is this character's script in NETLIST.DAT?
                    if(main_character_data[i][251])
                    {
                        // Looks like we've got one to send...
                        local_character_count++;
                    }
                }
            }
        }


        if(local_character_count > 255)
        {
            local_character_count = 255;
        }
        net_verbose_log("Sending room update (%d characters in room %d)", local_character_count, map_current_room);


        packet_begin(PACKET_TYPE_ROOM_UPDATE);
            packet_add_unsigned_short(map_current_room);
            packet_add_unsigned_short(game_seed & 65535);
            packet_add_unsigned_char(map_room_data[map_current_room][29]);  // Door flags
            packet_add_unsigned_char(local_character_count);
            i = 0;
            while(i < MAX_CHARACTER && local_character_count > 0)
            {
                if(main_character_on[i])
                {
                    if(*((unsigned int*)(main_character_data[i]+252)) == 0)
                    {
                        if(main_character_data[i][251])
                        {
                            // This character is hosted on the local machine, so let's send it on over...
                            character_data = main_character_data[i];
                            character_xyz = (float*) character_data;
                            packet_add_unsigned_char(i);                        // Local index number
                            packet_add_unsigned_char(character_data[251]);      // Script index (in NETLIST.DAT)
                            x = ((unsigned short) ((character_xyz[X]*ROOM_HEIGHTMAP_PRECISION) + 512.0f))&1023;
                            y = ((unsigned short) ((character_xyz[Y]*ROOM_HEIGHTMAP_PRECISION) + 512.0f))&1023;
                            fz = (character_xyz[Z] - character_xyz[11])*0.5f;  clip(0.0f, fz, 15.0f);  z = (unsigned char) fz;
                            pmod = ((x>>8)<<6) | ((y>>8)<<4) | z;
                            packet_add_unsigned_char(pmod);                     // Position modifiers (top 2 bits for x) (mid 2 bits for y) (low 4 bits are z above floor)
                            packet_add_unsigned_char(x);                        // X position (with modifier should range from 0-1023)
                            packet_add_unsigned_char(y);                        // Y position (with modifier should range from 0-1023)
                            facing = *((unsigned short*) (character_data+56));
                            facing = facing>>8;
                            packet_add_unsigned_char(facing);                   // Facing (should range from 0-255)
                            action = character_data[65];
                            if(CHAR_FLAGS & CHAR_FULL_NETWORK)
                            {
                                action = action | 128;
                            }
                            mount = *((unsigned short*) (character_data+164));
                            if(mount < MAX_CHARACTER)
                            {
                                action = action | 128;
                            }
                            packet_add_unsigned_char(action);                   // Action (high bit used if extra character data is to be sent...)
                            misc = character_data[78]<<6;
                            if((*((unsigned short*) (character_data+40))) > 0)
                            {
                                misc = misc | 32;
                            }
                            if((*((unsigned short*) (character_data+42))) > 0)
                            {
                                misc = misc | 16;
                            }
                            if(character_data[79] < 128)
                            {
                                misc = misc | 8;
                            }
                            if(character_data[216] & ENCHANT_FLAG_DEFLECT)
                            {
                                misc = misc | 4;
                            }
                            if(character_data[216] & ENCHANT_FLAG_HASTE)
                            {
                                misc = misc | 2;
                            }
                            if(character_data[216] & (ENCHANT_FLAG_SUMMON_3 | ENCHANT_FLAG_LEVITATE | ENCHANT_FLAG_INVISIBLE | ENCHANT_FLAG_MORPH))
                            {
                                misc = misc | 1;
                            }
                            packet_add_unsigned_char(misc);                     // Miscellaneous (top 2 bits are team) (then 1 bit for poison) (then 1 bit for petrify) (then 1 bit for low alpha) (then 1 bit for enchant_deflect) (then 1 bit for enchant_haste) (then 1 bit if enchanted in any way other than deflect & haste)
                            packet_add_unsigned_char(character_data[242]);      // EqLeft
                            packet_add_unsigned_char(character_data[243]);      // EqRight
                            packet_add_unsigned_char(character_data[240]);      // EqCol01
                            if(action & 128)
                            {
                                // Character is a high-data character...  Extra character data is to be sent...
                                packet_add_unsigned_char(character_data[241]);      // EqCol23
                                packet_add_unsigned_char(character_data[244]);      // EqSpec1
                                packet_add_unsigned_char(character_data[245]);      // EqSpec2
                                packet_add_unsigned_char(character_data[246]);      // EqHelm
                                packet_add_unsigned_char(character_data[247]);      // EqBody
                                packet_add_unsigned_char(character_data[248]);      // EqLegs
                                packet_add_unsigned_char(character_data[204]);      // Character class
                                if(mount < MAX_CHARACTER)
                                {
                                    packet_add_unsigned_char(mount);                    // Local index number of mount
                                }
                                else
                                {
                                    packet_add_unsigned_char(i);                        // Mount is not valid, so send our own local index number again (since we're obviously not riding ourself)
                                }
                            }
                            local_character_count--;
                        }
                    }
                }
                i++;
            }
        packet_end();


        network_send(NETWORK_ALL_REMOTES_IN_GAME);
    }
}

//-----------------------------------------------------------------------------------------------
void network_state_dump(void)
{
    // <ZZ> Debug helper (--net-verbose)...  Logs every character in the current room so logs
    //      from different instances can be compared to check state synchronization...
    unsigned short i;
    unsigned char* character_data;
    float* xyz;
    repeat(i, MAX_CHARACTER)
    {
        if(main_character_on[i])
        {
            character_data = main_character_data[i];
            xyz = (float*) character_data;
            log_message("NET:    STATE room=%d char=%d script=%s owner=%u pos=(%.1f, %.1f, %.1f) hits=%d",
                map_current_room, i, main_character_script_name[i],
                *((unsigned int*)(character_data+252)), xyz[X], xyz[Y], xyz[Z], character_data[82]);
        }
    }
}

//-----------------------------------------------------------------------------------------------
void network_update(void)
{
    // <ZZ> This function is called once per main loop frame...  It receives everything that's
    //      pending, keeps the join handshake alive, and broadcasts our state periodically...
    static unsigned int last_room_update_ms = 0;
    static unsigned int last_here_ms = 0;
    static unsigned int last_dump_ms = 0;
    static unsigned short last_room = 65535;
    unsigned int now;
    unsigned short i;

    if(!network_on) { return; }


    network_listen();
    now = SDL_GetTicks();


    // Keep the join handshake going...
    if(join_progress >= 1 && join_progress < 4)
    {
        if(now - join_started_ms > JOIN_TIMEOUT_MS)
        {
            net_log("Join timed out (no answer from %d.%d.%d.%d:%d)", ADDRESS_ARGS(join_address));
            join_progress = 0;
        }
        else if(now - join_last_send_ms > JOIN_RETRY_MS)
        {
            join_last_send_ms = now;
            network_send_i_wanna_play();
        }
    }


    // Broadcast our presence & drop peers that went silent...
    if(network_game_active && num_remote > 0)
    {
        if((play_game_active && map_current_room != last_room) || (now - last_here_ms) > I_AM_HERE_INTERVAL_MS)
        {
            last_room = play_game_active ? map_current_room : 65535;
            last_here_ms = now;
            network_send_i_am_here();
        }
        repeat(i, MAX_REMOTE)
        {
            if(remote_on[i] && (now - remote_last_heard_ms[i]) > REMOTE_TIMEOUT_MS)
            {
                net_log("Peer %d.%d.%d.%d:%d timed out", ADDRESS_ARGS(remote_address[i]));
                network_delete_remote(i);
            }
        }
    }


    // Broadcast our characters...
    if(network_game_active && num_remote > 0 && play_game_active)
    {
        if((now - last_room_update_ms) > ROOM_UPDATE_INTERVAL_MS)
        {
            last_room_update_ms = now;
            network_send_room_update();
        }
        if(network_verbose && (now - last_dump_ms) > 5000)
        {
            last_dump_ms = now;
            network_state_dump();
        }
    }
}

//-----------------------------------------------------------------------------------------------
