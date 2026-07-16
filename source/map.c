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

// <ZZ> This file contains stuff for making the map work...
//      map_clear           - Clears the map
//      map_add_room        - Adds a room to the map
//      map_remove_room     - Removes a room from the map

#define MAP_ROOM_FLAG_FOUND       128
#define MAP_ROOM_FLAG_DUAL_LEVEL  64
#define MAP_ROOM_FLAG_TOWN        32
#define MAP_ROOM_FLAG_BOSS        16
#define MAP_ROOM_FLAG_VIRTUE      8
#define MAP_ROOM_FLAG_OUTSIDE     4

#define MAX_MAP_ROOM 4000
unsigned short num_map_room = 0;
unsigned char map_room_data[MAX_MAP_ROOM][40];

#define MAX_AUTOMAP_ROOM 200
unsigned short num_automap_room = 0;
unsigned short automap_room_list[MAX_AUTOMAP_ROOM];

unsigned short map_current_room = 0;
unsigned short map_last_town_room = 0;
unsigned char map_room_objects_hosted = TRUE;  // FALSE if this visit only has network puppets

float map_room_door_pushback = 0.0f;  // For figgerin' door xyz location on room change...

//-----------------------------------------------------------------------------------------------
void map_clear()
{
    // <ZZ> This function clears the map...
    num_map_room = 0;
}

//-----------------------------------------------------------------------------------------------
unsigned char map_doors_overlap(unsigned short room_a, unsigned char room_a_wall, unsigned short room_b, unsigned char room_b_wall)
{
    // <ZZ> This function returns TRUE if the hallway between room_a and room_b intersects
    //      any other room on the map...
    unsigned char* room_a_srf;
    float room_a_xyz[3];
    float room_a_door_xyz[3];
    unsigned short room_a_rotation;
    unsigned char room_a_level;
    unsigned char* room_b_srf;
    float room_b_xyz[3];
    float room_b_door_xyz[3];
    unsigned short room_b_rotation;
    unsigned char room_b_level;
    unsigned short room_c;
    unsigned char room_c_level;
    float check_vertex_xy[3][2];

    unsigned char* room_c_srf;
    unsigned short room_c_rotation;
    unsigned short room_c_num_vertex;
    unsigned short room_c_num_minimap_triangle;
    unsigned char* room_c_vertex_data;
    unsigned char* room_c_minimap_data;
    unsigned char* data;
    unsigned char* vertex_data;             // Needed for room_draw_srf_vertex_helper
    float sine, cosine;                     // Needed for room_draw_srf_vertex_helper
    float x, y, z;                          // Needed for room_draw_srf_vertex_helper
    float angle;
    float vertex_xyz[3], temp_xyz[3], check_xy[2], dis_xy[2];
    float triangle_vertex_xy[3][2];
    float triangle_normal_xy[3][2];
    float dot;
    unsigned short i, j, k, m;
    unsigned char positive_count;



    // Find the endpoints of the hallway...
    room_a_xyz[X] = ((*((unsigned short*) (map_room_data[room_a]+4)))-32768.0f)*10.0f;
    room_a_xyz[Y] = ((*((unsigned short*) (map_room_data[room_a]+6)))-32768.0f)*10.0f;
    room_a_xyz[Z] = 0.0f;
    room_a_srf = model_slot_get_ptr(map_room_data[room_a]);
    room_a_rotation = *((unsigned short*) (map_room_data[room_a]+8));
    room_find_wall_center(room_a_srf, room_a_rotation, room_a_wall, room_a_door_xyz, room_a_xyz, 0.0f);

    room_b_xyz[X] = ((*((unsigned short*) (map_room_data[room_b]+4)))-32768.0f)*10.0f;
    room_b_xyz[Y] = ((*((unsigned short*) (map_room_data[room_b]+6)))-32768.0f)*10.0f;
    room_b_xyz[Z] = 0.0f;
    room_b_srf = model_slot_get_ptr(map_room_data[room_b]);
    room_b_rotation = *((unsigned short*) (map_room_data[room_b]+8));
    room_find_wall_center(room_b_srf, room_b_rotation, room_b_wall, room_b_door_xyz, room_b_xyz, 0.0f);


    // Find 3 points along the line to check...
    check_vertex_xy[0][X] = room_a_door_xyz[X]*0.75f + room_b_door_xyz[X]*0.25f;
    check_vertex_xy[0][Y] = room_a_door_xyz[Y]*0.75f + room_b_door_xyz[Y]*0.25f;
    check_vertex_xy[1][X] = room_a_door_xyz[X]*0.50f + room_b_door_xyz[X]*0.50f;
    check_vertex_xy[1][Y] = room_a_door_xyz[Y]*0.50f + room_b_door_xyz[Y]*0.50f;
    check_vertex_xy[2][X] = room_a_door_xyz[X]*0.25f + room_b_door_xyz[X]*0.75f;
    check_vertex_xy[2][Y] = room_a_door_xyz[Y]*0.25f + room_b_door_xyz[Y]*0.75f;


    // Now check our hallway against every other room...
    room_a_level = map_room_data[room_a][12];
    room_b_level = map_room_data[room_b][12];
    repeat(room_c, num_map_room)
    {
        if(room_c != room_a && room_c != room_b)
        {
            room_c_level = map_room_data[room_c][12];
            if(room_c_level == room_a_level || room_c_level == room_b_level)
            {
                room_c_srf = model_slot_get_ptr(map_room_data[room_c]);
                room_c_rotation = *((unsigned short*) (map_room_data[room_c]+8));
                angle = room_c_rotation * (2.0f * PI / 65536.0f);
                sine = (float) sin(angle);
                cosine = (float) cos(angle);
                x = ((*((unsigned short*) (map_room_data[room_c]+4)))-32768.0f)*10.0f;
                y = ((*((unsigned short*) (map_room_data[room_c]+6)))-32768.0f)*10.0f;
                z = 0.0f;


                // Read the SRF file header...
                room_c_vertex_data = room_c_srf + sdf_read_unsigned_int(room_c_srf+SRF_VERTEX_OFFSET);
                room_c_num_vertex = sdf_read_unsigned_short(room_c_vertex_data);  room_c_vertex_data+=2;
                room_c_minimap_data = room_c_srf + sdf_read_unsigned_int(room_c_srf+SRF_MINIMAP_OFFSET);
                room_c_num_minimap_triangle = sdf_read_unsigned_short(room_c_minimap_data);  room_c_minimap_data+=2;


                // Go through each of our check vertices....
                repeat(i, 3)
                {
                    check_xy[X] = check_vertex_xy[i][X];
                    check_xy[Y] = check_vertex_xy[i][Y];


                    // Go through each minimap triangle of room_c
                    vertex_data = room_c_vertex_data;
                    data = room_c_minimap_data;
                    repeat(j, room_c_num_minimap_triangle)
                    {
                        // Find location of each vertex of this triangle...
                        repeat(k, 3)
                        {
                            m = sdf_read_unsigned_short(data);  data+=2;
                            room_draw_srf_vertex_helper(m);
                            triangle_vertex_xy[k][X] = vertex_xyz[X];
                            triangle_vertex_xy[k][Y] = vertex_xyz[Y];
                        }


                        // Now find a normal vector for each edge of this triangle...
                        repeat(k, 3)
                        {
                            m = (k+1)%3;
                            triangle_normal_xy[k][X] = -(triangle_vertex_xy[m][Y] - triangle_vertex_xy[k][Y]);
                            triangle_normal_xy[k][Y] = triangle_vertex_xy[m][X] - triangle_vertex_xy[k][X];
                        }

                        // Now check the check vertex against each edge normal...
                        positive_count = 0;
                        repeat(k, 3)
                        {
                            dis_xy[X] = check_xy[X] - triangle_vertex_xy[k][X];
                            dis_xy[Y] = check_xy[Y] - triangle_vertex_xy[k][Y];
                            dot = (dis_xy[X]*triangle_normal_xy[k][X]) + (dis_xy[Y]*triangle_normal_xy[k][Y]);
                            if(dot > 0.0f)
                            {
                                positive_count++;
                            }
                        }


                        // If all 3 dot products were positive, it means our check vertex lies within the triangle...
                        if(positive_count == 3)
                        {
                            return TRUE;
                        }
                    }
                }
            }
        }
    }
    return FALSE;
}


//-----------------------------------------------------------------------------------------------
unsigned char map_connect_rooms(unsigned short room_a, unsigned short room_b)
{
    // <ZZ> This function adds a door between two rooms...  Returns TRUE if it
    //      worked, or FALSE if it didn't...  May fail if rooms are too far
    //      apart, or if there are too many doors in the rooms already, or if
    //      the best walls aren't useable (walls might have doors in 'em already
    //      or walls might not be flagged for doors)...
    unsigned short i;
    unsigned char num_door_a, num_door_b;
    unsigned char room_a_wall, room_b_wall;
    unsigned short room_a_rotation;
    unsigned short room_b_rotation;
    float room_a_xy[2];
    float room_b_xy[2];
    float vector_xy[2];
    float dis;
    unsigned char* room_a_srf_file;
    unsigned char* room_b_srf_file;
    unsigned char allow_bottom_doors;


    if(room_a < num_map_room && room_b < num_map_room)
    {
        num_door_a = 0;
        num_door_b = 0;
        repeat(i, 5)
        {
            if(*((unsigned short*) (map_room_data[room_a]+14+(i<<1))) < num_map_room) { num_door_a++; }
            if(*((unsigned short*) (map_room_data[room_b]+14+(i<<1))) < num_map_room) { num_door_b++; }
        }

        if(num_door_a < 5 && num_door_b < 5)
        {
            // Find the best wall for each room (wall normal must be pointing in general direction of
            // other room & be flagged as doorable & be unused)...
            room_a_srf_file = model_slot_get_ptr(map_room_data[room_a]);
            room_a_xy[X] = ((*((unsigned short*) (map_room_data[room_a]+4))) - 32768.0f) * 10.0f;
            room_a_xy[Y] = ((*((unsigned short*) (map_room_data[room_a]+6))) - 32768.0f) * 10.0f;
            room_a_rotation = *((unsigned short*) (map_room_data[room_a]+8));
            room_b_srf_file = model_slot_get_ptr(map_room_data[room_b]);
            room_b_xy[X] = ((*((unsigned short*) (map_room_data[room_b]+4))) - 32768.0f) * 10.0f;
            room_b_xy[Y] = ((*((unsigned short*) (map_room_data[room_b]+6))) - 32768.0f) * 10.0f;
            room_b_rotation = *((unsigned short*) (map_room_data[room_b]+8));
            vector_xy[X] = room_b_xy[X] - room_a_xy[X];
            vector_xy[Y] = room_b_xy[Y] - room_a_xy[Y];
            dis = ((float) sqrt(vector_xy[X]*vector_xy[X] + vector_xy[Y]*vector_xy[Y])) + 0.0001f;



            vector_xy[X]/=dis;
            vector_xy[Y]/=dis;
            allow_bottom_doors = TRUE;
            if(map_room_data[room_a][13] & MAP_ROOM_FLAG_DUAL_LEVEL)
            {
                allow_bottom_doors = FALSE;
                if(map_room_data[room_b][12] > map_room_data[room_a][12])
                {
                    allow_bottom_doors = TRUE;
                }
            }
            room_a_wall = room_find_best_wall(room_a_srf_file, allow_bottom_doors, room_a_rotation, vector_xy, (map_room_data[room_a]+24));



            vector_xy[X] = -vector_xy[X];
            vector_xy[Y] = -vector_xy[Y];
            allow_bottom_doors = TRUE;
            if(map_room_data[room_b][13] & MAP_ROOM_FLAG_DUAL_LEVEL)
            {
                allow_bottom_doors = FALSE;
                if(map_room_data[room_a][12] > map_room_data[room_b][12])
                {
                    allow_bottom_doors = TRUE;
                }
            }
            room_b_wall = room_find_best_wall(room_b_srf_file, allow_bottom_doors, room_b_rotation, vector_xy, (map_room_data[room_b]+24));
            if(room_a_wall < 255 && room_b_wall < 255)
            {
                // Make sure the hallway doesn't run into any other room ('cause that gets confusing)
                // Hallways can crisscross one another, though...
                if(map_doors_overlap(room_a, room_a_wall, room_b, room_b_wall) == FALSE)
                {
                    // Add the door to each door list...
                    *((unsigned short*) (map_room_data[room_a]+14+(num_door_a<<1))) = room_b;
                    *((unsigned short*) (map_room_data[room_b]+14+(num_door_b<<1))) = room_a;

                    // Remember which wall each door should attach to...
                    map_room_data[room_a][num_door_a+24] = room_a_wall;
                    map_room_data[room_b][num_door_b+24] = room_b_wall;
//log_message("INFO:   Connected room %d to %d...", room_a, room_b);
//log_message("INFO:     Wall %d of room %d (%d doors now)", room_a_wall, room_a, num_door_a+1);
//log_message("INFO:     Wall %d of room %d (%d doors now)", room_b_wall, room_b, num_door_b+1);
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

//-----------------------------------------------------------------------------------------------
unsigned char map_rooms_overlap_elaborate(unsigned char* room_a_srf, float* room_a_xy, unsigned short room_a_rotation, unsigned char* room_b_srf, float* room_b_xy, unsigned short room_b_rotation)
{
    // <ZZ> This function returns TRUE if any exterior wall vertex of room_a lie within any
    //      minimap triangle of room_b...
    unsigned short room_a_num_vertex;
    unsigned short room_a_num_exterior_wall;
    unsigned char* room_a_vertex_data;
    unsigned char* room_a_exterior_wall_data;
    unsigned short room_b_num_vertex;
    unsigned short room_b_num_minimap_triangle;
    unsigned char* room_b_vertex_data;
    unsigned char* room_b_minimap_data;
    unsigned char* data;
    unsigned char* vertex_data;             // Needed for room_draw_srf_vertex_helper
    float sine, cosine;                     // Needed for room_draw_srf_vertex_helper
    float x, y, z;                          // Needed for room_draw_srf_vertex_helper
    float angle;
    float vertex_xyz[3], temp_xyz[3], check_xy[2], dis_xy[2];
    float triangle_vertex_xy[3][2];
    float triangle_normal_xy[3][2];
    float dot;
    float room_a_sine, room_a_cosine;
    float room_b_sine, room_b_cosine;
    unsigned short i, j, k, m;
    unsigned char positive_count;

    angle = room_a_rotation * (2.0f * PI / 65536.0f);
    room_a_sine = (float) sin(angle);
    room_a_cosine = (float) cos(angle);
    angle = room_b_rotation * (2.0f * PI / 65536.0f);
    room_b_sine = (float) sin(angle);
    room_b_cosine = (float) cos(angle);
    z = 0.0f;

    // Read the SRF file headers...
    room_a_vertex_data = room_a_srf + sdf_read_unsigned_int(room_a_srf+SRF_VERTEX_OFFSET);
    room_a_num_vertex = sdf_read_unsigned_short(room_a_vertex_data);  room_a_vertex_data+=2;
    room_a_exterior_wall_data = room_a_srf + sdf_read_unsigned_int(room_a_srf+SRF_EXTERIOR_WALL_OFFSET);
    room_a_num_exterior_wall = sdf_read_unsigned_short(room_a_exterior_wall_data);  room_a_exterior_wall_data+=2;
    room_b_vertex_data = room_b_srf + sdf_read_unsigned_int(room_b_srf+SRF_VERTEX_OFFSET);
    room_b_num_vertex = sdf_read_unsigned_short(room_b_vertex_data);  room_b_vertex_data+=2;
    room_b_minimap_data = room_b_srf + sdf_read_unsigned_int(room_b_srf+SRF_MINIMAP_OFFSET);
    room_b_num_minimap_triangle = sdf_read_unsigned_short(room_b_minimap_data);  room_b_minimap_data+=2;


    // Go through each exterior wall vertex of room_a
    repeat(i, room_a_num_exterior_wall)
    {
        vertex_data = room_a_vertex_data;
        sine = room_a_sine;
        cosine = room_a_cosine;
        x = room_a_xy[X];
        y = room_a_xy[Y];
        m = sdf_read_unsigned_short(room_a_exterior_wall_data);  room_a_exterior_wall_data+=3;
        room_draw_srf_vertex_helper(m);
        check_xy[X] = vertex_xyz[X];
        check_xy[Y] = vertex_xyz[Y];


        // Go through each minimap triangle of room_b
        vertex_data = room_b_vertex_data;
        sine = room_b_sine;
        cosine = room_b_cosine;
        x = room_b_xy[X];
        y = room_b_xy[Y];
        data = room_b_minimap_data;
        repeat(j, room_b_num_minimap_triangle)
        {
            // Find location of each vertex of this triangle...
            repeat(k, 3)
            {
                m = sdf_read_unsigned_short(data);  data+=2;
                room_draw_srf_vertex_helper(m);
                triangle_vertex_xy[k][X] = vertex_xyz[X];
                triangle_vertex_xy[k][Y] = vertex_xyz[Y];
            }


            // Now find a normal vector for each edge of this triangle...
            repeat(k, 3)
            {
                m = (k+1)%3;
                triangle_normal_xy[k][X] = -(triangle_vertex_xy[m][Y] - triangle_vertex_xy[k][Y]);
                triangle_normal_xy[k][Y] = triangle_vertex_xy[m][X] - triangle_vertex_xy[k][X];
            }

            // Now check the check vertex against each edge normal...
            positive_count = 0;
            repeat(k, 3)
            {
                dis_xy[X] = check_xy[X] - triangle_vertex_xy[k][X];
                dis_xy[Y] = check_xy[Y] - triangle_vertex_xy[k][Y];
                dot = (dis_xy[X]*triangle_normal_xy[k][X]) + (dis_xy[Y]*triangle_normal_xy[k][Y]);
                if(dot > 0.0f)
                {
                    positive_count++;
                }
            }


            // If all 3 dot products were positive, it means our check vertex lies within the triangle...
            if(positive_count == 3)
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

//-----------------------------------------------------------------------------------------------
#define OVERLAP_DISTANCE 200.0f
#define OVERLAP_DISTANCE_SQUARED (OVERLAP_DISTANCE*OVERLAP_DISTANCE)
#define MAX_MULTICONNECT 64
unsigned short num_multiconnect = 0;
unsigned short map_multiconnect_list[MAX_MULTICONNECT];
unsigned char map_rooms_overlap(unsigned short room_a, unsigned short room_b)
{
    // <ZZ> This function returns TRUE if the two rooms overlap one another...  Should only
    //      be called during a room add (before doors are assigned and such), so it's easy
    //      to remove the bad room...  Returns FALSE if the two rooms do not overlap...
    //      If they don't overlap, but are close to one another, room_b is added to the
    //      map_multiconnect_list[]...  num_multiconnect should be manually reset...
    unsigned char* room_a_srf;
    float room_a_xy[2];
    unsigned short room_a_rotation;
    unsigned char room_a_level;
    unsigned char room_a_flags;
    unsigned char* room_b_srf;
    float room_b_xy[2];
    unsigned short room_b_rotation;
    unsigned char room_b_level;
    unsigned char room_b_flags;
    float x, y, dis;


    if(room_a < MAX_MAP_ROOM && room_b < MAX_MAP_ROOM)
    {
        // Check if they're on the same level...
        room_a_level = map_room_data[room_a][12];
        room_a_flags = map_room_data[room_a][13];
        room_b_level = map_room_data[room_b][12];
        room_b_flags = map_room_data[room_b][13];
        if(room_a_level == room_b_level || ((room_a_level+1) == room_b_level && (room_a_flags & MAP_ROOM_FLAG_DUAL_LEVEL)) || ((room_b_level+1) == room_a_level && (room_b_flags & MAP_ROOM_FLAG_DUAL_LEVEL)))
        {
            // They are...  But are they anywhere near one another?
            room_a_xy[X] = ((*((unsigned short*) (map_room_data[room_a]+4)))-32768.0f)*10.0f;
            room_a_xy[Y] = ((*((unsigned short*) (map_room_data[room_a]+6)))-32768.0f)*10.0f;
            room_b_xy[X] = ((*((unsigned short*) (map_room_data[room_b]+4)))-32768.0f)*10.0f;
            room_b_xy[Y] = ((*((unsigned short*) (map_room_data[room_b]+6)))-32768.0f)*10.0f;
            x = room_a_xy[X] - room_b_xy[X];
            y = room_a_xy[Y] - room_b_xy[Y];
            dis = x*x + y*y;
            if(dis < OVERLAP_DISTANCE_SQUARED)
            {
                // They are pretty close to one another...  We'll have to do an elaborate
                // intersection test...  Check each exterior wall vertex of room against each
                // minimap triangle of the other.
                room_a_srf = model_slot_get_ptr(map_room_data[room_a]);
                room_a_rotation = *((unsigned short*) (map_room_data[room_a]+8));
                room_b_srf = model_slot_get_ptr(map_room_data[room_b]);
                room_b_rotation = *((unsigned short*) (map_room_data[room_b]+8));


                // Check once a to b...
                if(map_rooms_overlap_elaborate(room_a_srf, room_a_xy, room_a_rotation, room_b_srf, room_b_xy, room_b_rotation))
                {
                    return TRUE;
                }


                // Then check b to a...
                if(map_rooms_overlap_elaborate(room_b_srf, room_b_xy, room_b_rotation, room_a_srf, room_a_xy, room_a_rotation))
                {
                    return TRUE;
                }


                // They didn't overlap, but they were close - so let's put room_b into the
                // multiconnect list...
                if(num_multiconnect < MAX_MULTICONNECT)
                {
                    map_multiconnect_list[num_multiconnect] = room_b;
                    num_multiconnect++;
                }
            }
        }
    }
    return FALSE;
}

//-----------------------------------------------------------------------------------------------
unsigned char*  map_add_srf_file;
float           map_add_x;
float           map_add_y;
unsigned short  map_add_rotation;
unsigned char   map_add_seed;
unsigned char   map_add_twset;
unsigned char   map_add_level;
unsigned char   map_add_flags;
unsigned char   map_add_difficulty;
unsigned char   map_add_area;
unsigned short  map_add_from_room;
unsigned char   map_add_multi_connect;
unsigned char map_add_room(unsigned char* srf_file, float x, float y, unsigned short rotation, unsigned char seed, unsigned char twset, unsigned char level, unsigned char flags, unsigned char difficulty, unsigned char area, unsigned short from_room, unsigned char multi_connect)
{
    // <ZZ> This function adds a room to the map...  Returns TRUE if it worked, FALSE if it didn't...
    //      Should fail if proposed room would overlap an existing room...  Should automagically
    //      connect to nearby rooms on the same level if multi_connect is TRUE...
    unsigned short i, j;
    unsigned char connected;
    if(num_map_room < MAX_MAP_ROOM)
    {
        model_slot_set_ptr(map_room_data[num_map_room], srf_file);
        *((unsigned short*) (map_room_data[num_map_room]+4)) = (unsigned short) ((x*0.1f)+32768.0f);
        *((unsigned short*) (map_room_data[num_map_room]+6)) = (unsigned short) ((y*0.1f)+32768.0f);
        *((unsigned short*) (map_room_data[num_map_room]+8)) = rotation;
        map_room_data[num_map_room][10] = seed;
        map_room_data[num_map_room][11] = twset;
        map_room_data[num_map_room][12] = level;
        map_room_data[num_map_room][13] = flags;


        // Do our overlap test to make sure our chosen location is invalid...
        num_multiconnect = 0;
        repeat(i, num_map_room)
        {
            if(map_rooms_overlap(num_map_room, i))
            {
                // Uh, oh...  We've picked a bad spot...  The room has only been partially
                // added though, so it isn't important to remove it...  We can just return...
                return FALSE;
            }
        }


        i = 14;
        while(i < 29)
        {
            // Start as connected to room 65535 - which means not connected...
            // And start used wall list as all 255's...
            map_room_data[num_map_room][i] = 255;
            i++;
        }
        map_room_data[num_map_room][29] = 0;  // Door open'd flags
        map_room_data[num_map_room][30] = difficulty;
        map_room_data[num_map_room][31] = area;
        i = 32;
        while(i < 40)
        {
            map_room_data[num_map_room][i] = 0;
            i++;
        }
        i = num_map_room;
        num_map_room++;
        connected = map_connect_rooms(from_room, i);
        if(multi_connect)
        {
            repeat(j, num_multiconnect)
            {
                if(map_multiconnect_list[j] != from_room)
                {
                    connected = connected | map_connect_rooms(map_multiconnect_list[j], i);
                }
            }
        }
        if(connected == FALSE)
        {
            // Uh, oh...  We weren't able to connect this room...  Might not be a problem if
            // we specified no connection (like for the first room spawned)
            if(from_room < MAX_MAP_ROOM)
            {
                // Yup, it's a problem...  We'll just unplop this one and nobody'll notice...
                num_map_room--;
                return FALSE;
            }
        }
        return TRUE;
    }
    return FALSE;
}

//-----------------------------------------------------------------------------------------------
void map_remove_room(void)
{
    // <ZZ> This function removes the last room from the map...
    unsigned short i, j;

    if(num_map_room > 0)
    {
        num_map_room--;
        repeat(i, num_map_room)
        {
            repeat(j, 5)
            {
                if(*((unsigned short*) (map_room_data[i]+(j<<1)+14)) >= num_map_room)
                {
                    *((unsigned short*) (map_room_data[i]+(j<<1)+14)) = 65535;
                    *((unsigned short*) (map_room_data[i]+j+24)) = 255;
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------------------------
unsigned char automap_prime_level;
void map_automap_prime(float centerx, float centery, unsigned char level, float tolerance)
{
    unsigned short i;
    float roomx, roomy, dx, dy;
    unsigned char last_level;

    automap_prime_level = level;
    last_level = level;
    last_level--;
    num_automap_room = 0;
    repeat(i, num_map_room)
    {
        if(map_room_data[i][13] & MAP_ROOM_FLAG_FOUND)
        {
            if(map_room_data[i][12] == level || ((map_room_data[i][13] & MAP_ROOM_FLAG_DUAL_LEVEL) && map_room_data[i][12] == last_level))
            {
                roomx = ((*((unsigned short*) (map_room_data[i]+4))) - 32768.0f) * 10.0f;
                roomy = ((*((unsigned short*) (map_room_data[i]+6))) - 32768.0f) * 10.0f;
                dx = roomx - centerx;
                dy = roomy - centery;
                if(dx > -tolerance && dx < tolerance && dy > -tolerance && dy < tolerance)
                {
                    // Room should be drawn, so add it to the list...
                    if(num_automap_room < MAX_AUTOMAP_ROOM)
                    {
                        automap_room_list[num_automap_room] = i;
                        num_automap_room++;
                    }
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------------------------
void map_automap_draw()
{
    // <ZZ> This function draws all of the map rooms that were found by the prime function...
    unsigned short i, j, k;
    unsigned short room_a, room_b;
    float room_a_xyz[3];
    float room_b_xyz[3];
    float room_a_door_xyz[3];
    float room_b_door_xyz[3];
    float side_xy[2];
    float vertex_xy[4][2];
    float distance;
    unsigned short room_a_rotation;
    unsigned short room_b_rotation;
    unsigned char* room_a_srf_file;
    unsigned char* room_b_srf_file;
    unsigned short room_a_wall;
    unsigned short room_b_wall;


    display_texture_off();
    display_zbuffer_off();


    // Draw the connections to other rooms first, so they underlay the room drawings...
    repeat(i, num_automap_room)
    {
        room_a = automap_room_list[i];
        room_a_srf_file = model_slot_get_ptr(map_room_data[room_a]);
        room_a_xyz[X] = ((*((unsigned short*) (map_room_data[room_a]+4))) - 32768.0f) * 10.0f;
        room_a_xyz[Y] = ((*((unsigned short*) (map_room_data[room_a]+6))) - 32768.0f) * 10.0f;
        room_a_xyz[Z] = 0.0f;
        room_a_rotation = *((unsigned short*) (map_room_data[room_a]+8));
        repeat(j, 5)
        {
            room_b = *((unsigned short*) (map_room_data[room_a]+14+(j<<1)));
            if(room_b < num_map_room)
            {
                if(room_b < room_a || (map_room_data[room_a][12]!=map_room_data[room_b][12]) || !(map_room_data[room_b][13]&MAP_ROOM_FLAG_FOUND))
                {
                    room_b_srf_file = model_slot_get_ptr(map_room_data[room_b]);
                    room_b_xyz[X] = ((*((unsigned short*) (map_room_data[room_b]+4))) - 32768.0f) * 10.0f;
                    room_b_xyz[Y] = ((*((unsigned short*) (map_room_data[room_b]+6))) - 32768.0f) * 10.0f;
                    room_b_xyz[Z] = 0.0f;
                    room_b_rotation = *((unsigned short*) (map_room_data[room_b]+8));


                    // Find the center of the door wall for room_a...
                    room_a_wall = (unsigned short) map_room_data[room_a][24+j];
                    room_find_wall_center(room_a_srf_file, room_a_rotation, room_a_wall, room_a_door_xyz, room_a_xyz, 0.0f);
                    room_a_door_xyz[Z] = 0.0f;


                    // Now find which wall is used for this door in room_b (which one connects to room_a?)
                    room_b_wall = 65535;
                    repeat(k, 5)
                    {
                        if(*((unsigned short*) (map_room_data[room_b]+14+(k<<1))) == room_a)
                        {
                            room_b_wall = (unsigned short) map_room_data[room_b][24+k];
                            k = 5;
                        }
                    }


                    // And find the center of the door wall for room_b...
                    room_find_wall_center(room_b_srf_file, room_b_rotation, room_b_wall, room_b_door_xyz, room_b_xyz, 0.0f);
                    room_b_door_xyz[Z] = 0.0f;


                    // We want to draw the door line as a fat line, and we want to use triangles
                    // to do it (since line drawing runs slow on some cards & we want to keep the
                    // scaling consistant)...  So we'll need to find the corners of the rectangle...
                    side_xy[X] = -(room_b_door_xyz[Y] - room_a_door_xyz[Y]);
                    side_xy[Y] = room_b_door_xyz[X] - room_a_door_xyz[X];
                    distance = 0.15f * (((float) sqrt(side_xy[X]*side_xy[X] + side_xy[Y]*side_xy[Y])) + 0.0000001f);
                    side_xy[X]/=distance;
                    side_xy[Y]/=distance;

                    vertex_xy[0][X] = room_a_door_xyz[X] - side_xy[X] - side_xy[Y];
                    vertex_xy[0][Y] = room_a_door_xyz[Y] - side_xy[Y] + side_xy[X];

                    vertex_xy[1][X] = room_b_door_xyz[X] - side_xy[X] + side_xy[Y];
                    vertex_xy[1][Y] = room_b_door_xyz[Y] - side_xy[Y] - side_xy[X];

                    vertex_xy[2][X] = room_b_door_xyz[X] + side_xy[X] + side_xy[Y];
                    vertex_xy[2][Y] = room_b_door_xyz[Y] + side_xy[Y] - side_xy[X];

                    vertex_xy[3][X] = room_a_door_xyz[X] + side_xy[X] - side_xy[Y];
                    vertex_xy[3][Y] = room_a_door_xyz[Y] + side_xy[Y] + side_xy[X];



                    // Use yellow lines for doors from current room...
                    if(room_a == map_current_room || room_b == map_current_room)
                    {
                        display_color(med_yellow);
                    }
                    else
                    {
                        display_color(dark_yellow);
                    }


                    display_start_fan();
                        display_point(vertex_xy[0]);
                        display_point(vertex_xy[1]);
                        display_point(vertex_xy[2]);
                        display_point(vertex_xy[3]);
                    display_end();
                }
            }
        }
    }


    // Draw the rooms...
    repeat(i, num_automap_room)
    {
        room_a = automap_room_list[i];
        room_a_srf_file = model_slot_get_ptr(map_room_data[room_a]);
        room_a_xyz[X] = ((*((unsigned short*) (map_room_data[room_a]+4))) - 32768.0f) * 10.0f;
        room_a_xyz[Y] = ((*((unsigned short*) (map_room_data[room_a]+6))) - 32768.0f) * 10.0f;
        room_a_rotation = *((unsigned short*) (map_room_data[room_a]+8));

        // Current room is drawn as bright yellow...
        if(room_a == map_current_room)
        {
            room_draw_srf(room_a_xyz[X], room_a_xyz[Y], 0.0f, room_a_srf_file, yellow, room_a_rotation, ROOM_MODE_MINIMAP);
        }
        else
        {
            room_draw_srf(room_a_xyz[X], room_a_xyz[Y], 0.0f, room_a_srf_file, dark_yellow, room_a_rotation, ROOM_MODE_MINIMAP);
        }
    }


    // Draw the room overlays...
    display_texture_on();
    display_blend_trans();
    display_color(white);
    repeat(i, num_automap_room)
    {
        room_a = automap_room_list[i];
        if(map_room_data[room_a][13] & (MAP_ROOM_FLAG_DUAL_LEVEL | MAP_ROOM_FLAG_TOWN | MAP_ROOM_FLAG_BOSS | MAP_ROOM_FLAG_VIRTUE))
        {
            room_a_xyz[X] = ((*((unsigned short*) (map_room_data[room_a]+4))) - 32768.0f) * 10.0f;
            room_a_xyz[Y] = ((*((unsigned short*) (map_room_data[room_a]+6))) - 32768.0f) * 10.0f;
            side_xy[X] = -map_side_xy[Y]*50.0f;
            side_xy[Y] = map_side_xy[X]*50.0f;

            vertex_xy[0][X] = room_a_xyz[X] - side_xy[X] - side_xy[Y];
            vertex_xy[0][Y] = room_a_xyz[Y] - side_xy[Y] + side_xy[X];

            vertex_xy[1][X] = room_a_xyz[X] - side_xy[X] + side_xy[Y];
            vertex_xy[1][Y] = room_a_xyz[Y] - side_xy[Y] - side_xy[X];

            vertex_xy[2][X] = room_a_xyz[X] + side_xy[X] + side_xy[Y];
            vertex_xy[2][Y] = room_a_xyz[Y] + side_xy[Y] - side_xy[X];

            vertex_xy[3][X] = room_a_xyz[X] + side_xy[X] - side_xy[Y];
            vertex_xy[3][Y] = room_a_xyz[Y] + side_xy[Y] + side_xy[X];


            if(map_room_data[room_a][13] & MAP_ROOM_FLAG_DUAL_LEVEL)
            {
                display_pick_texture(texture_automap_stair);
            }
            if(map_room_data[room_a][13] & MAP_ROOM_FLAG_TOWN)
            {
                display_pick_texture(texture_automap_town);
            }
            if(map_room_data[room_a][13] & MAP_ROOM_FLAG_BOSS)
            {
                display_pick_texture(texture_automap_boss);
            }
            if(map_room_data[room_a][13] & MAP_ROOM_FLAG_VIRTUE)
            {
                display_pick_texture(texture_automap_virtue);
            }
            if((map_room_data[room_a][13] & MAP_ROOM_FLAG_DUAL_LEVEL) && map_room_data[room_a][12] < automap_prime_level)
            {
                // Draw < for stairs by flipping > facing image...
                display_start_fan();
                    display_texpos_xy(1.0f, 0.0f);  display_point(vertex_xy[0]);
                    display_texpos_xy(0.0f, 0.0f);  display_point(vertex_xy[1]);
                    display_texpos_xy(0.0f, 1.0f);  display_point(vertex_xy[2]);
                    display_texpos_xy(1.0f, 1.0f);  display_point(vertex_xy[3]);
                display_end();
            }
            else
            {
                // Draw the image...
                display_start_fan();
                    display_texpos_xy(0.0f, 0.0f);  display_point(vertex_xy[0]);
                    display_texpos_xy(1.0f, 0.0f);  display_point(vertex_xy[1]);
                    display_texpos_xy(1.0f, 1.0f);  display_point(vertex_xy[2]);
                    display_texpos_xy(0.0f, 1.0f);  display_point(vertex_xy[3]);
                display_end();
            }
        }
    }






    display_zbuffer_on();
}

//-----------------------------------------------------------------------------------------------
static unsigned char map_index_is_local_player(unsigned short index)
{
    unsigned short player, owner, owner_owner;
    unsigned char* character_data;

    repeat(player, MAX_LOCAL_PLAYER)
    {
        if(local_player_character[player] == index)
        {
            return TRUE;
        }
    }
    if(index >= MAX_CHARACTER || !main_character_on[index])
    {
        return FALSE;
    }
    character_data = main_character_data[index];
    owner = *((unsigned short*) (character_data+76));
    repeat(player, MAX_LOCAL_PLAYER)
    {
        if(local_player_character[player] == owner)
        {
            return TRUE;
        }
    }
    if(owner < MAX_CHARACTER && main_character_on[owner])
    {
        owner_owner = *((unsigned short*) (main_character_data[owner]+76));
        repeat(player, MAX_LOCAL_PLAYER)
        {
            if(local_player_character[player] == owner_owner)
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

//-----------------------------------------------------------------------------------------------
static void map_get_door_xyz(unsigned short room, unsigned short from_room, float pushback, float* xyz, unsigned short* spin)
{
    unsigned char door;
    unsigned char door_found;
    unsigned char* srf_file;
    unsigned short rotation;
    float offset_xyz[3];

    xyz[X] = 0.0f;
    xyz[Y] = 0.0f;
    xyz[Z] = 0.0f;
    if(spin)
    {
        *spin = 0;
    }
    if(room >= num_map_room)
    {
        return;
    }

    door_found = 255;
    repeat(door, 5)
    {
        if(*((unsigned short*) (map_room_data[room]+14+(door<<1))) == from_room)
        {
            door_found = door;
            break;
        }
    }
    if(door_found >= 5)
    {
        return;
    }

    // Characters live in room-local space (same as MAPGEN.GetRoomDoorXYZ / SYS_MAPROOM
    // door queries), so the wall-center offset must be 0 - not the room's map XY.
    offset_xyz[X] = 0.0f;
    offset_xyz[Y] = 0.0f;
    offset_xyz[Z] = 0.0f;
    srf_file = model_slot_get_ptr(map_room_data[room]);
    rotation = *((unsigned short*) (map_room_data[room]+8));
    map_room_door_pushback = pushback;
    room_find_wall_center(srf_file, rotation, map_room_data[room][24+door_found], xyz, offset_xyz, pushback);
    if(spin)
    {
        *spin = map_room_door_spin;
    }
}

//-----------------------------------------------------------------------------------------------
static void map_room_apply_textures(unsigned char* srf_file)
{
    // <ZZ> Same job as MAPGEN.TextureSet() - room_uncompress only copies texture flags,
    //      the actual RGB bindings have to be filled in afterwards or walls stay white.
    unsigned int tex_section;
    unsigned char* name_ptr;
    unsigned char* room_tex;
    unsigned char* rgb_file;
    unsigned char* rgb_data;
    unsigned short i;
    char name[16];

    if(srf_file == NULL || roombuffer == NULL)
    {
        return;
    }

    tex_section = sdf_read_unsigned_int(srf_file + SRF_TEXTURE_OFFSET);
    name_ptr = srf_file + tex_section + 32;
    room_tex = roombuffer + (*((unsigned int*) (roombuffer + SRF_TEXTURE_OFFSET)));

    repeat(i, 32)
    {
        memcpy(name, name_ptr, 8);
        name[8] = 0;
        make_uppercase(name);

        if(strcmp(name, "DECAL") == 0)   { strcpy(name, "=MPDECAL"); }
        if(strcmp(name, "CAVE") == 0)    { strcpy(name, "MPFL00A"); }
        if(strcmp(name, "CAVEBR") == 0)  { strcpy(name, "=MPBR00A"); }
        if(strcmp(name, "LAWN") == 0)    { strcpy(name, "MPFL01A"); }
        if(strcmp(name, "LAWNBR") == 0)  { strcpy(name, "=MPBR01A"); }
        if(strcmp(name, "WOOD") == 0)    { strcpy(name, "MPFL05A"); }
        if(strcmp(name, "WOODBR") == 0)  { strcpy(name, "MPBR05A"); }
        if(strcmp(name, "FLOOR") == 0)   { strcpy(name, "MPFL07A"); }
        if(strcmp(name, "FLOOR2") == 0)  { strcpy(name, "MPFL03A"); }
        if(strcmp(name, "WALL") == 0)    { strcpy(name, "MPWL05A"); }
        if(strcmp(name, "WALLCAP") == 0) { strcpy(name, "MPCP05A"); }
        if(strcmp(name, "FENCE") == 0)   { strcpy(name, "MPFENCE"); }
        if(strcmp(name, "PAVE") == 0)    { strcpy(name, "MPPAVE"); }

        rgb_file = sdf_find_filetype(name, SDF_FILE_IS_RGB);
        if(rgb_file)
        {
            rgb_data = sdf_index_get_data(rgb_file);
            if(rgb_data)
            {
                *((unsigned int*) (room_tex + (i<<3))) = *((unsigned int*) (rgb_data + 2));
            }
        }
        name_ptr += 8;
    }
}

//-----------------------------------------------------------------------------------------------
static void map_load_current_room(unsigned short room)
{
    unsigned char* srf_file;
    unsigned char* wall_file;
    unsigned char twset;

    if(room >= num_map_room)
    {
        return;
    }

    map_current_room = room;
    if(map_room_data[room][13] & MAP_ROOM_FLAG_TOWN)
    {
        map_last_town_room = room;
    }

    obj_poof_all(PARTICLE);
    srf_file = model_slot_get_ptr(map_room_data[room]);
    twset = map_room_data[room][11];
    if(twset == 0)
    {
        wall_file = sdf_find_filetype("WALLSET0", SDF_FILE_IS_DDD);
    }
    else
    {
        wall_file = sdf_find_filetype("WALLSET1", SDF_FILE_IS_DDD);
    }
    if(srf_file && wall_file)
    {
        wall_file = sdf_index_get_data(wall_file);
        room_uncompress(srf_file, roombuffer, wall_file, *((unsigned short*) (map_room_data[room]+8)), map_room_data[room]+24, map_room_data[room]+32, map_room_data[room][30], map_room_data[room][10], map_room_data[room][10]);
        map_room_apply_textures(srf_file);
    }
    global_room_changed = TRUE;
    main_timer_length = 32;
}

//-----------------------------------------------------------------------------------------------
void map_record_current_room_objects(void)
{
    unsigned short j, k, m;
    unsigned char opcode;

    if(map_current_room >= num_map_room)
    {
        return;
    }

    // Guests only see NETLIST props as puppets (no object-index 249).  Wiping the
    // defeated bitmask here would mark every statue/squire/etc. gone, so they never
    // come back on a later solo re-entry.  Only the machine that hosted the spawn
    // may rewrite that list.
    if(network_game_active && !map_room_objects_hosted)
    {
        return;
    }

    repeat(j, 8)
    {
        map_room_data[map_current_room][32+j] = 255;
    }
    repeat(j, MAX_CHARACTER)
    {
        if(main_character_on[j])
        {
            k = main_character_data[j][249];
            if(k < 64)
            {
                opcode = FALSE;
                repeat(m, MAX_LOCAL_PLAYER)
                {
                    opcode = opcode || (*((unsigned short*)(main_character_data[j]+76)) == local_player_character[m]);
                }
                if(main_character_data[j][78] == TEAM_GOOD && opcode)
                {
                }
                else
                {
                    map_room_data[map_current_room][32+(k>>3)] = map_room_data[map_current_room][32+(k>>3)] & (255 - (1<<(k&7)));
                    main_character_data[j][249] = 255;
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------------------------
static void map_poof_room_leavers(void)
{
    unsigned short i;

    repeat(i, MAX_CHARACTER)
    {
        if(main_character_on[i] && !map_index_is_local_player(i))
        {
            obj_destroy(main_character_data[i]);
        }
    }
}

//-----------------------------------------------------------------------------------------------
static void map_place_local_players_at_door(unsigned short next_room, unsigned short from_room)
{
    unsigned short i, new_spin, old_spin;
    unsigned char* character_data;
    float* character_xyz;
    float x, y, z, pushback;
    unsigned char placed;

    // Match CDOOR.ChangeRoom: door facing for spin, then keep camera relative to that turn.
    map_get_door_xyz(next_room, from_room, 8.0f, script_matrix, &new_spin);
    old_spin = 0;
    placed = FALSE;
    repeat(i, MAX_CHARACTER)
    {
        if(main_character_on[i] && map_index_is_local_player(i))
        {
            character_data = main_character_data[i];
            if(!placed)
            {
                old_spin = *((unsigned short*) (character_data+56));
            }
            character_xyz = (float*) character_data;
            pushback = 8.0f - *((float*) (character_data+160));
            map_get_door_xyz(next_room, from_room, pushback, script_matrix, &new_spin);
            x = script_matrix[X];
            y = script_matrix[Y];
            z = script_matrix[Z];
            character_xyz[X] = x;
            character_xyz[Y] = y;
            character_xyz[Z] = z;
            *((float*) (character_data+12)) = x;
            *((float*) (character_data+16)) = y;
            *((float*) (character_data+24)) = 0.0f;
            *((float*) (character_data+28)) = 0.0f;
            *((float*) (character_data+32)) = 0.0f;
            *((float*) (character_data+20)) = z + 1.0f;
            *((unsigned short*) (character_data+56)) = new_spin;
            *((unsigned short*) (character_data+166)) = 60;
            character_data[191] = 60;
            placed = TRUE;
        }
    }

    if(placed)
    {
        camera_rotation_xy[X] = camera_rotation_xy[X] - new_spin + (unsigned short)(old_spin + 32768);
    }
    target_xyz[X] = 0.0f;
    target_xyz[Y] = 0.0f;
    target_xyz[Z] = 5.0f;
    display_camera_position(1, 0.0f, 0.0f);
}

//-----------------------------------------------------------------------------------------------
void map_sync_to_peer_room(unsigned short next_room, unsigned short from_room)
{
    // <ZZ> Loads a room because a network peer entered it...  All players stay in the
    //      same room, so every machine follows the first peer to walk through a door...
    if(next_room >= num_map_room || next_room == map_current_room)
    {
        return;
    }

    map_record_current_room_objects();
    map_poof_room_leavers();
    map_place_local_players_at_door(next_room, from_room);
    map_load_current_room(next_room);
    character_update_all();

    log_message("NET:    Synced to room %d (from room %d)", next_room, from_room);
}

//-----------------------------------------------------------------------------------------------
