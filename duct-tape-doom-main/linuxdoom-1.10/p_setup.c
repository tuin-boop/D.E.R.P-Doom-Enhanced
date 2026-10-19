// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Log:$
//
// DESCRIPTION:
//	Do all the WAD I/O, get map description,
//	set up initial state and misc. LUTs.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: p_setup.c,v 1.5 1997/02/03 22:45:12 b1 Exp $";


#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "z_zone.h"

#include "m_swap.h"
#include "m_bbox.h"
#include "m_argv.h"

#include "g_game.h"

#include "i_system.h"
#include "w_wad.h"

#include "doomdef.h"
#include "p_local.h"
#include "p_digwall.h"
#include "p_fluid.h"

#include "s_sound.h"

#include "doomstat.h"


void	P_SpawnMapThing (mapthing_t*	mthing);


//
// MAP related Lookup tables.
// Store VERTEXES, LINEDEFS, SIDEDEFS, etc.
//
int		numvertexes;
vertex_t*	vertexes;

int		numsegs;
seg_t*		segs;

int		numsectors;
sector_t*	sectors;

int		numsubsectors;
subsector_t*	subsectors;

int		numnodes;
node_t*		nodes;

int		numlines;
line_t*		lines;

int		numsides;
side_t*		sides;


// BLOCKMAP
// Created from axis aligned bounding box
// of the map, a rectangular array of
// blocks of size ...
// Used to speed up collision detection
// by spatial subdivision in 2D.
//
// Blockmap size.
int		bmapwidth;
int		bmapheight;	// size in mapblocks
int32_t*	blockmap;	// int for larger maps
// offsets in blockmap are from here
int32_t*	blockmaplump;		
// origin of block map
fixed_t		bmaporgx;
fixed_t		bmaporgy;
// for thing chains
mobj_t**	blocklinks;		


// REJECT
// For fast sight rejection.
// Speeds up enemy AI by skipping detailed
//  LineOf Sight calculation.
// Without special effect, this could be
//  used as a PVS lookup as well.
//
byte*		rejectmatrix;


// Maintain single and multi player starting spots.
#define MAX_DEATHMATCH_STARTS	10

mapthing_t	deathmatchstarts[MAX_DEATHMATCH_STARTS];
mapthing_t*	deathmatch_p;
mapthing_t	playerstarts[MAXPLAYERS];


// Map lumps store indices as 16-bit fields. Vanilla read them signed,
// which broke maps with more than 32767 vertexes, sides or lines.
static int P_MapIndex (int16_t value)
{
    return (uint16_t)SHORT(value);
}

static int P_SideIndex (int16_t value)
{
    int index = P_MapIndex (value);
    return index == 0xffff ? -1 : index;
}

static void P_CheckIndex (int index, int count, const char* what)
{
    if (index < 0 || index >= count)
	I_Error ("Map has a bad %s reference %i (of %i)", what, index, count);
}





//
// P_LoadVertexes
//
void P_LoadVertexes (int lump)
{
    byte*		data;
    int			i;
    mapvertex_t*	ml;
    vertex_t*		li;

    // Determine number of lumps:
    //  total lump length / vertex record length.
    numvertexes = W_LumpLength (lump) / sizeof(mapvertex_t);

    // Allocate zone memory for buffer.
    vertexes = Z_Malloc (numvertexes*sizeof(vertex_t),PU_LEVEL,0);	

    // Load data into cache.
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ml = (mapvertex_t *)data;
    li = vertexes;

    // Copy and convert vertex coordinates,
    // internal representation as fixed.
    for (i=0 ; i<numvertexes ; i++, li++, ml++)
    {
	li->x = SHORT(ml->x)<<FRACBITS;
	li->y = SHORT(ml->y)<<FRACBITS;
    }

    // Free buffer memory.
    Z_Free (data);
}



//
// P_LoadSegs
//
void P_LoadSegs (int lump)
{
    byte*		data;
    int			i;
    mapseg_t*		ml;
    seg_t*		li;
    line_t*		ldef;
    int			linedef;
    int			side;
	
    numsegs = W_LumpLength (lump) / sizeof(mapseg_t);
    segs = Z_Malloc (numsegs*sizeof(seg_t),PU_LEVEL,0);	
    memset (segs, 0, numsegs*sizeof(seg_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ml = (mapseg_t *)data;
    li = segs;
    for (i=0 ; i<numsegs ; i++, li++, ml++)
    {
	P_CheckIndex (P_MapIndex (ml->v1), numvertexes, "seg vertex");
	P_CheckIndex (P_MapIndex (ml->v2), numvertexes, "seg vertex");
	li->v1 = &vertexes[P_MapIndex (ml->v1)];
	li->v2 = &vertexes[P_MapIndex (ml->v2)];
					
	li->angle = (SHORT(ml->angle))<<16;
	li->offset = (SHORT(ml->offset))<<16;
	linedef = P_MapIndex (ml->linedef);
	P_CheckIndex (linedef, numlines, "seg linedef");
	ldef = &lines[linedef];
	li->linedef = ldef;
	side = SHORT(ml->side) & 1;
	P_CheckIndex (ldef->sidenum[side], numsides, "seg sidedef");
	li->sidedef = &sides[ldef->sidenum[side]];
	li->frontsector = sides[ldef->sidenum[side]].sector;
	if ((ldef-> flags & ML_TWOSIDED) && ldef->sidenum[side^1] != -1)
	    li->backsector = sides[ldef->sidenum[side^1]].sector;
	else
	    li->backsector = 0;
    }
	
    Z_Free (data);
}


//
// P_LoadSubsectors
//
void P_LoadSubsectors (int lump)
{
    byte*		data;
    int			i;
    mapsubsector_t*	ms;
    subsector_t*	ss;
	
    numsubsectors = W_LumpLength (lump) / sizeof(mapsubsector_t);
    subsectors = Z_Malloc (numsubsectors*sizeof(subsector_t),PU_LEVEL,0);	
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ms = (mapsubsector_t *)data;
    memset (subsectors,0, numsubsectors*sizeof(subsector_t));
    ss = subsectors;
    
    for (i=0 ; i<numsubsectors ; i++, ss++, ms++)
    {
	ss->numlines = P_MapIndex (ms->numsegs);
	ss->firstline = P_MapIndex (ms->firstseg);
    }
	
    Z_Free (data);
}



//
// P_LoadSectors
//
void P_LoadSectors (int lump)
{
    byte*		data;
    int			i;
    mapsector_t*	ms;
    sector_t*		ss;
	
    numsectors = W_LumpLength (lump) / sizeof(mapsector_t);
    sectors = Z_Malloc (numsectors*sizeof(sector_t),PU_LEVEL,0);	
    memset (sectors, 0, numsectors*sizeof(sector_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ms = (mapsector_t *)data;
    ss = sectors;
    for (i=0 ; i<numsectors ; i++, ss++, ms++)
    {
	ss->floorheight = SHORT(ms->floorheight)<<FRACBITS;
	ss->ceilingheight = SHORT(ms->ceilingheight)<<FRACBITS;
	ss->floorpic = R_FlatNumForName(ms->floorpic);
	ss->ceilingpic = R_FlatNumForName(ms->ceilingpic);
	ss->lightlevel = SHORT(ms->lightlevel);
	ss->special = SHORT(ms->special);
	ss->tag = SHORT(ms->tag);
	ss->thinglist = NULL;
    }
	
    Z_Free (data);
}


//
// P_LoadNodes
//
void P_LoadNodes (int lump)
{
    byte*	data;
    int		i;
    int		j;
    int		k;
    mapnode_t*	mn;
    node_t*	no;
	
    numnodes = W_LumpLength (lump) / sizeof(mapnode_t);
    nodes = Z_Malloc (numnodes*sizeof(node_t),PU_LEVEL,0);	
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    mn = (mapnode_t *)data;
    no = nodes;
    
    for (i=0 ; i<numnodes ; i++, no++, mn++)
    {
	no->x = SHORT(mn->x)<<FRACBITS;
	no->y = SHORT(mn->y)<<FRACBITS;
	no->dx = SHORT(mn->dx)<<FRACBITS;
	no->dy = SHORT(mn->dy)<<FRACBITS;
	for (j=0 ; j<2 ; j++)
	{
	    no->children[j] = SHORT(mn->children[j]);
	    for (k=0 ; k<4 ; k++)
		no->bbox[j][k] = SHORT(mn->bbox[j][k])<<FRACBITS;
	}
    }
	
    Z_Free (data);
}


//
// P_CheckNodeFormat
// ZDoom/DeePBSP extended nodes keep everything in NODES and leave SEGS and
// SSECTORS empty; reading them as vanilla data crashed at the player start.
//
static void P_CheckNodeFormat (int lumpnum)
{
    static const char* formats[] = { "XNOD", "ZNOD", "XGLN", "ZGLN",
				     "XGL2", "ZGL2", "XGL3", "ZGL3", "xNd4" };
    char	magic[4] = {0};
    char	mapname[9];
    int		i;

    for (i=0 ; i<8 && lumpinfo[lumpnum].name[i] ; i++)
	mapname[i] = lumpinfo[lumpnum].name[i];
    mapname[i] = 0;

    if (W_LumpLength (lumpnum+ML_NODES) >= 4)
    {
	byte* data = W_CacheLumpNum (lumpnum+ML_NODES, PU_STATIC);
	memcpy (magic, data, 4);
	Z_Free (data);
    }
    for (i=0 ; i<(int)(sizeof(formats)/sizeof(formats[0])) ; i++)
	if (!memcmp (magic, formats[i], 4))
	    I_Error ("%s uses extended %.4s nodes, which this engine does "
		     "not support.\nThis WAD needs a Boom- or ZDoom-compatible "
		     "source port.", mapname, formats[i]);

    if (!W_LumpLength (lumpnum+ML_SEGS) || !W_LumpLength (lumpnum+ML_SSECTORS))
	I_Error ("%s has no SEGS or SSECTORS. The map needs a node "
		 "builder run, or a source port with built-in nodes.", mapname);
}


//
// P_CheckNodeIndices
// Bad references here otherwise surface as wild pointers much later.
//
static void P_CheckNodeIndices (void)
{
    int		i;
    int		j;

    for (i=0 ; i<numsubsectors ; i++)
	if (subsectors[i].numlines <= 0
	    || subsectors[i].firstline + subsectors[i].numlines > numsegs)
	    I_Error ("Map has a bad subsector %i (segs %i+%i of %i)", i,
		     subsectors[i].firstline, subsectors[i].numlines, numsegs);

    for (i=0 ; i<numnodes ; i++)
	for (j=0 ; j<2 ; j++)
	{
	    int child = nodes[i].children[j];
	    if (child & NF_SUBSECTOR)
		P_CheckIndex (child & ~NF_SUBSECTOR, numsubsectors, "node subsector");
	    else
		P_CheckIndex (child, numnodes, "node child");
	}
}


//
// P_LoadThings
//
void P_LoadThings (int lump)
{
    byte*		data;
    int			i;
    mapthing_t*		mt;
    int			numthings;
    boolean		spawn;
	
    data = W_CacheLumpNum (lump,PU_STATIC);
    numthings = W_LumpLength (lump) / sizeof(mapthing_t);
	
    mt = (mapthing_t *)data;
    for (i=0 ; i<numthings ; i++, mt++)
    {
	spawn = true;

	// Do not spawn cool, new monsters if !commercial
	if ( gamemode != commercial)
	{
	    switch(mt->type)
	    {
	      case 68:	// Arachnotron
	      case 64:	// Archvile
	      case 88:	// Boss Brain
	      case 89:	// Boss Shooter
	      case 69:	// Hell Knight
	      case 67:	// Mancubus
	      case 71:	// Pain Elemental
	      case 65:	// Former Human Commando
	      case 66:	// Revenant
	      case 84:	// Wolf SS
		spawn = false;
		break;
	    }
	}
	if (spawn == false)
	    break;

	// Do spawn all other stuff. 
	mt->x = SHORT(mt->x);
	mt->y = SHORT(mt->y);
	mt->angle = SHORT(mt->angle);
	mt->type = SHORT(mt->type);
	mt->options = SHORT(mt->options);
	
	P_SpawnMapThing (mt);
    }
	
    Z_Free (data);
}


//
// P_LoadLineDefs
// Also counts secret lines for intermissions.
//
void P_LoadLineDefs (int lump)
{
    byte*		data;
    int			i;
    maplinedef_t*	mld;
    line_t*		ld;
    vertex_t*		v1;
    vertex_t*		v2;
	
    numlines = W_LumpLength (lump) / sizeof(maplinedef_t);
    lines = Z_Malloc (numlines*sizeof(line_t),PU_LEVEL,0);	
    memset (lines, 0, numlines*sizeof(line_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    mld = (maplinedef_t *)data;
    ld = lines;
    for (i=0 ; i<numlines ; i++, mld++, ld++)
    {
	ld->flags = SHORT(mld->flags);
	ld->special = SHORT(mld->special);
	ld->tag = SHORT(mld->tag);
	P_CheckIndex (P_MapIndex (mld->v1), numvertexes, "linedef vertex");
	P_CheckIndex (P_MapIndex (mld->v2), numvertexes, "linedef vertex");
	v1 = ld->v1 = &vertexes[P_MapIndex (mld->v1)];
	v2 = ld->v2 = &vertexes[P_MapIndex (mld->v2)];
	ld->dx = v2->x - v1->x;
	ld->dy = v2->y - v1->y;
	
	if (!ld->dx)
	    ld->slopetype = ST_VERTICAL;
	else if (!ld->dy)
	    ld->slopetype = ST_HORIZONTAL;
	else
	{
	    if (FixedDiv (ld->dy , ld->dx) > 0)
		ld->slopetype = ST_POSITIVE;
	    else
		ld->slopetype = ST_NEGATIVE;
	}
		
	if (v1->x < v2->x)
	{
	    ld->bbox[BOXLEFT] = v1->x;
	    ld->bbox[BOXRIGHT] = v2->x;
	}
	else
	{
	    ld->bbox[BOXLEFT] = v2->x;
	    ld->bbox[BOXRIGHT] = v1->x;
	}

	if (v1->y < v2->y)
	{
	    ld->bbox[BOXBOTTOM] = v1->y;
	    ld->bbox[BOXTOP] = v2->y;
	}
	else
	{
	    ld->bbox[BOXBOTTOM] = v2->y;
	    ld->bbox[BOXTOP] = v1->y;
	}

	ld->sidenum[0] = P_SideIndex (mld->sidenum[0]);
	ld->sidenum[1] = P_SideIndex (mld->sidenum[1]);
	if (ld->sidenum[0] != -1)
	    P_CheckIndex (ld->sidenum[0], numsides, "linedef sidedef");
	if (ld->sidenum[1] != -1)
	    P_CheckIndex (ld->sidenum[1], numsides, "linedef sidedef");

	if (ld->sidenum[0] != -1)
	    ld->frontsector = sides[ld->sidenum[0]].sector;
	else
	    ld->frontsector = 0;

	if (ld->sidenum[1] != -1)
	    ld->backsector = sides[ld->sidenum[1]].sector;
	else
	    ld->backsector = 0;
    }
	
    Z_Free (data);
}


//
// P_LoadSideDefs
//
void P_LoadSideDefs (int lump)
{
    byte*		data;
    int			i;
    mapsidedef_t*	msd;
    side_t*		sd;
	
    numsides = W_LumpLength (lump) / sizeof(mapsidedef_t);
    sides = Z_Malloc (numsides*sizeof(side_t),PU_LEVEL,0);	
    memset (sides, 0, numsides*sizeof(side_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    msd = (mapsidedef_t *)data;
    sd = sides;
    for (i=0 ; i<numsides ; i++, msd++, sd++)
    {
	sd->textureoffset = SHORT(msd->textureoffset)<<FRACBITS;
	sd->rowoffset = SHORT(msd->rowoffset)<<FRACBITS;
	sd->toptexture = R_TextureNumForName(msd->toptexture);
	sd->bottomtexture = R_TextureNumForName(msd->bottomtexture);
	sd->midtexture = R_TextureNumForName(msd->midtexture);
	P_CheckIndex (P_MapIndex (msd->sector), numsectors, "sidedef sector");
	sd->sector = &sectors[P_MapIndex (msd->sector)];
    }
	
    Z_Free (data);
}


//
// P_LoadBlockMap
//
// True when the segment crosses or touches the axis-aligned box.
static boolean P_LineTouchesBox (double x1, double y1, double x2, double y2,
				 double left, double bottom, double right, double top)
{
    double t0 = 0, t1 = 1;
    double p[4] = { x1 - x2, x2 - x1, y1 - y2, y2 - y1 };
    double q[4] = { x1 - left, right - x1, y1 - bottom, top - y1 };
    int i;

    for (i = 0; i < 4; i++)
    {
	if (p[i] == 0)
	{
	    if (q[i] < 0)
		return false;
	    continue;
	}
	double t = q[i] / p[i];
	if (p[i] < 0) { if (t > t1) return false; if (t > t0) t0 = t; }
	else { if (t < t0) return false; if (t < t1) t1 = t; }
    }
    return true;
}

//
// P_CreateBlockMap
// Builds the blockmap for maps whose lump is missing or too large for
// 16-bit offsets. Lists start with line 0, as vanilla node builders do.
//
static void P_CreateBlockMap (void)
{
    int		i, x, y;
    int		minx = INT32_MAX, miny = INT32_MAX, maxx = INT32_MIN, maxy = INT32_MIN;
    int		numblocks;
    int*	counts;
    int*	fill;
    int32_t	size;

    for (i = 0; i < numvertexes; i++)
    {
	int vx = vertexes[i].x >> FRACBITS, vy = vertexes[i].y >> FRACBITS;
	if (vx < minx) minx = vx;
	if (vx > maxx) maxx = vx;
	if (vy < miny) miny = vy;
	if (vy > maxy) maxy = vy;
    }
    if (!numvertexes)
	minx = miny = maxx = maxy = 0;
    // A margin below the map, as the original node builders left.
    minx -= 8;
    miny -= 8;
    bmapwidth = ((maxx - minx) >> (MAPBLOCKSHIFT-FRACBITS)) + 1;
    bmapheight = ((maxy - miny) >> (MAPBLOCKSHIFT-FRACBITS)) + 1;
    numblocks = bmapwidth * bmapheight;
    counts = calloc (numblocks, sizeof(*counts));
    fill = calloc (numblocks, sizeof(*fill));
    if (!counts || !fill)
	I_Error ("P_CreateBlockMap: out of memory");

    // Two passes: count each block's lines, then write the lists.
    for (int pass = 0; pass < 2; pass++)
    {
	for (i = 0; i < numlines; i++)
	{
	    double x1 = (lines[i].v1->x >> FRACBITS) - minx;
	    double y1 = (lines[i].v1->y >> FRACBITS) - miny;
	    double x2 = (lines[i].v2->x >> FRACBITS) - minx;
	    double y2 = (lines[i].v2->y >> FRACBITS) - miny;
	    int bx0 = (int)(x1 < x2 ? x1 : x2) / MAPBLOCKUNITS;
	    int bx1 = (int)(x1 > x2 ? x1 : x2) / MAPBLOCKUNITS;
	    int by0 = (int)(y1 < y2 ? y1 : y2) / MAPBLOCKUNITS;
	    int by1 = (int)(y1 > y2 ? y1 : y2) / MAPBLOCKUNITS;

	    for (y = by0; y <= by1; y++)
		for (x = bx0; x <= bx1; x++)
		{
		    double left = x * MAPBLOCKUNITS, bottom = y * MAPBLOCKUNITS;
		    int block = y * bmapwidth + x;
		    if (!P_LineTouchesBox (x1, y1, x2, y2, left, bottom,
					   left + MAPBLOCKUNITS, bottom + MAPBLOCKUNITS))
			continue;
		    if (pass == 0)
			counts[block]++;
		    else
			blockmaplump[fill[block]++] = i;
		}
	}
	if (pass == 0)
	{
	    size = 4 + numblocks;
	    for (i = 0; i < numblocks; i++)
		size += counts[i] + 2;
	    blockmaplump = Z_Malloc (size * sizeof(*blockmaplump), PU_LEVEL, 0);
	    size = 4 + numblocks;
	    for (i = 0; i < numblocks; i++)
	    {
		blockmaplump[4 + i] = size;
		blockmaplump[size] = 0;
		fill[i] = size + 1;
		size += counts[i] + 2;
		blockmaplump[size - 1] = -1;
	    }
	}
    }
    blockmaplump[0] = minx;
    blockmaplump[1] = miny;
    blockmaplump[2] = bmapwidth;
    blockmaplump[3] = bmapheight;
    free (counts);
    free (fill);
}

void P_LoadBlockMap (int lump)
{
    int		i;
    int		count;
	
    count = W_LumpLength (lump)/2;
    if (M_CheckParm ("-blockmap") || count < 4 || count >= 0x10000)
	P_CreateBlockMap ();
    else
    {
	int16_t* data = W_CacheLumpNum (lump,PU_STATIC);

	// Offsets and line numbers are unsigned; 0xffff ends a list.
	blockmaplump = Z_Malloc (count * sizeof(*blockmaplump), PU_LEVEL, 0);
	blockmaplump[0] = SHORT(data[0]);
	blockmaplump[1] = SHORT(data[1]);
	blockmaplump[2] = P_MapIndex (data[2]);
	blockmaplump[3] = P_MapIndex (data[3]);
	for (i=4 ; i<count ; i++)
	{
	    int value = P_MapIndex (data[i]);
	    blockmaplump[i] = value == 0xffff ? -1 : value;
	}
	Z_Free (data);
	if (count < 4 + blockmaplump[2] * blockmaplump[3])
	{
	    Z_Free (blockmaplump);
	    P_CreateBlockMap ();
	}
    }
    blockmap = blockmaplump+4;
		
    bmaporgx = blockmaplump[0]<<FRACBITS;
    bmaporgy = blockmaplump[1]<<FRACBITS;
    bmapwidth = blockmaplump[2];
    bmapheight = blockmaplump[3];
	
    // clear out mobj chains
    count = sizeof(*blocklinks)* bmapwidth*bmapheight;
    blocklinks = Z_Malloc (count,PU_LEVEL, 0);
    memset (blocklinks, 0, count);
}



//
// P_GroupLines
// Builds sector line lists and subsector sector numbers.
// Finds block bounding boxes for sectors.
//
void P_GroupLines (void)
{
    line_t**		linebuffer;
    int			i;
    int			j;
    int			total;
    line_t*		li;
    sector_t*		sector;
    subsector_t*	ss;
    seg_t*		seg;
    fixed_t		bbox[4];
    int			block;
	
    // look up sector number for each subsector
    ss = subsectors;
    for (i=0 ; i<numsubsectors ; i++, ss++)
    {
	seg = &segs[ss->firstline];
	ss->sector = seg->sidedef->sector;
    }

    // count number of lines in each sector
    li = lines;
    total = 0;
    for (i=0 ; i<numlines ; i++, li++)
    {
	total++;
	li->frontsector->linecount++;

	if (li->backsector && li->backsector != li->frontsector)
	{
	    li->backsector->linecount++;
	    total++;
	}
    }
	
    // build line tables for each sector	
    linebuffer = Z_Malloc (total*sizeof(*linebuffer), PU_LEVEL, 0);
    sector = sectors;
    for (i=0 ; i<numsectors ; i++, sector++)
    {
	M_ClearBox (bbox);
	sector->lines = linebuffer;
	li = lines;
	for (j=0 ; j<numlines ; j++, li++)
	{
	    if (li->frontsector == sector || li->backsector == sector)
	    {
		*linebuffer++ = li;
		M_AddToBox (bbox, li->v1->x, li->v1->y);
		M_AddToBox (bbox, li->v2->x, li->v2->y);
	    }
	}
	if (linebuffer - sector->lines != sector->linecount)
	    I_Error ("P_GroupLines: miscounted");
			
	// set the degenmobj_t to the middle of the bounding box
	sector->soundorg.x = (bbox[BOXRIGHT]+bbox[BOXLEFT])/2;
	sector->soundorg.y = (bbox[BOXTOP]+bbox[BOXBOTTOM])/2;
		
	// adjust bounding box to map blocks
	block = (bbox[BOXTOP]-bmaporgy+MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block >= bmapheight ? bmapheight-1 : block;
	sector->blockbox[BOXTOP]=block;

	block = (bbox[BOXBOTTOM]-bmaporgy-MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block < 0 ? 0 : block;
	sector->blockbox[BOXBOTTOM]=block;

	block = (bbox[BOXRIGHT]-bmaporgx+MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block >= bmapwidth ? bmapwidth-1 : block;
	sector->blockbox[BOXRIGHT]=block;

	block = (bbox[BOXLEFT]-bmaporgx-MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block < 0 ? 0 : block;
	sector->blockbox[BOXLEFT]=block;
    }
	
}


//
// P_SetupLevel
//
void
P_SetupLevel
( int		episode,
  int		map,
  int		playermask,
  skill_t	skill)
{
    int		i;
    char	lumpname[9];
    int		lumpnum;
	
    P_FluidReset(); P_GlobalDigReset(); ++r_levelserial;
    totalkills = totalitems = totalsecret = wminfo.maxfrags = 0;
    wminfo.partime = 180;
    for (i=0 ; i<MAXPLAYERS ; i++)
    {
	players[i].killcount = players[i].secretcount 
	    = players[i].itemcount = 0;
    }

    // Initial height of PointOfView
    // will be set by player think.
    players[consoleplayer].viewz = 1; 

    // Make sure all sounds are stopped before Z_FreeTags.
    S_Start ();			

    
#if 0 // UNUSED
    if (debugfile)
    {
	Z_FreeTags (PU_LEVEL, MAXINT);
	Z_FileDumpHeap (debugfile);
    }
    else
#endif
	Z_FreeTags (PU_LEVEL, PU_PURGELEVEL-1);


    // UNUSED W_Profile ();
    P_InitThinkers ();

    // if working with a devlopment map, reload it
    W_Reload ();			
	   
    // find map name
    if ( gamemode == commercial)
    {
	if (map<10)
	    sprintf (lumpname,"map0%i", map);
	else
	    sprintf (lumpname,"map%i", map);
    }
    else
    {
	lumpname[0] = 'E';
	lumpname[1] = '0' + episode;
	lumpname[2] = 'M';
	lumpname[3] = '0' + map;
	lumpname[4] = 0;
    }

    lumpnum = W_GetNumForName (lumpname);
	
    leveltime = 0;
	
    // note: most of this ordering is important	
    P_LoadVertexes (lumpnum+ML_VERTEXES);
    P_LoadSectors (lumpnum+ML_SECTORS);
    P_LoadSideDefs (lumpnum+ML_SIDEDEFS);

    P_LoadLineDefs (lumpnum+ML_LINEDEFS);
    // After the lines, so a missing blockmap can be rebuilt from them.
    P_LoadBlockMap (lumpnum+ML_BLOCKMAP);
    P_CheckNodeFormat (lumpnum);
    P_LoadSubsectors (lumpnum+ML_SSECTORS);
    P_LoadNodes (lumpnum+ML_NODES);
    P_LoadSegs (lumpnum+ML_SEGS);
    P_CheckNodeIndices ();
	
    {
	// Vanilla read past a short REJECT lump; pad it with zeros (visible).
	int needed = (numsectors*numsectors + 7) / 8;
	int length = W_LumpLength (lumpnum+ML_REJECT);

	if (length >= needed)
	    rejectmatrix = W_CacheLumpNum (lumpnum+ML_REJECT,PU_LEVEL);
	else
	{
	    rejectmatrix = Z_Malloc (needed, PU_LEVEL, 0);
	    memset (rejectmatrix, 0, needed);
	    if (length)
		W_ReadLump (lumpnum+ML_REJECT, rejectmatrix);
	}
    }
    P_GroupLines ();

    bodyqueslot = 0;
    deathmatch_p = deathmatchstarts;
    P_LoadThings (lumpnum+ML_THINGS);
    
    // if deathmatch, randomly spawn the active players
    if (deathmatch)
    {
	for (i=0 ; i<MAXPLAYERS ; i++)
	    if (playeringame[i])
	    {
		players[i].mo = NULL;
		G_DeathMatchSpawnPlayer (i);
	    }
			
    }

    // clear special respawning que
    iquehead = iquetail = 0;		
	
    // set up world state
    P_SpawnSpecials ();
	
    // build subsector connect matrix
    //	UNUSED P_ConnectSubsectors ();

    // preload graphics
    if (precache)
	R_PrecacheLevel ();

    //printf ("free memory: 0x%x\n", Z_FreeMemory());

}



//
// P_Init
//
void P_Init (void)
{
    P_InitSwitchList ();
    P_InitPicAnims ();
    R_InitSprites (sprnames);
}

