/*
sv_frame.c - server world snapshot
Copyright (C) 2008 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "server.h"
#include "sv_workers.h"
#include "const.h"
#include "net_encode.h"

typedef struct
{
	int		num_entities;
	entity_state_t	entities[MAX_VISIBLE_PACKET];
	byte		sended[MAX_EDICTS_BYTES];
} sv_ents_t;

static int	c_fullsend;	// just a debug counter
static int	c_notsend;

/*
=======================
SV_EntityNumbers
=======================
*/
static int SV_EntityNumbers( const void *a, const void *b )
{
	int	ent1 = ((entity_state_t *)a)->number;
	int	ent2 = ((entity_state_t *)b)->number;

	// watcom libc compares ents with itself
	if( ent1 == ent2 )
		return 0;

	if( ent1 < ent2 )
		return -1;
	return 1;
}

/*
=============
SV_AddEntitiesToPacket

=============
*/
static void SV_AddEntitiesToPacket( edict_t *pViewEnt, edict_t *pClient, client_frame_t *frame, sv_ents_t *ents, qboolean from_client )
{
	byte		*clientpvs = NULL;
	byte		*clientphs = NULL;
	qboolean		fullvis = false;
	sv_client_t	*cl = NULL;

	// during an error shutdown message we may need to transmit
	// the shutdown message after the server has shutdown, so
	// specifically check for it
	if( sv.state == ss_dead )
		return;

	cl = SV_ClientFromEdict( pClient, true );

	ASSERT( cl != NULL );

	// portals can't change hostflags
	if( from_client )
	{
		// setup hostflags
		if( FBitSet( cl->flags, FCL_LOCAL_WEAPONS ))
			SetBits( sv.hostflags, SVF_SKIPLOCALHOST );
		else ClearBits( sv.hostflags, SVF_SKIPLOCALHOST );

		// reset viewents each frame
		cl->num_viewents = 0;
	}

	svgame.dllFuncs.pfnSetupVisibility( pViewEnt, pClient, &clientpvs, &clientphs );
	if( !clientpvs ) fullvis = true;

	// g-cont: of course we can send world but not want to do it :-)
	for( int e = 1; e < svgame.numEntities; e++ )
	{
		byte	*pset;
		edict_t	*ent = SV_EdictNum( e );
		qboolean	player;
		entity_state_t	*state;

		// don't double add an entity through portals (in case this already added)
		if( CHECKVISBIT( ents->sended, e ))
			continue;

		if( e >= 1 && e <= svs.maxclients )
			player = 1;
		else player = 0;

		if( player )
		{
			sv_client_t *cl = &svs.clients[e - 1];

			if( cl->state != cs_spawned )
				continue;

			if( FBitSet( cl->flags, FCL_HLTV_PROXY ))
				continue;
		}

		if( FBitSet( ent->v.effects, EF_REQUEST_PHS ))
			pset = clientphs;
		else pset = clientpvs;

		state = &ents->entities[ents->num_entities];

		// add entity to the net packet
		if( svgame.dllFuncs.pfnAddToFullPack( state, e, ent, pClient, sv.hostflags, player, pset ))
		{
			// to prevent adds it twice through portals
			SETVISBIT( ents->sended, e );

			if( SV_IsValidEdict( ent->v.aiment ) && FBitSet( ent->v.aiment->v.effects, EF_MERGE_VISIBILITY ))
			{
				if( cl->num_viewents < MAX_VIEWENTS )
				{
					cl->viewentity[cl->num_viewents] = ent->v.aiment;
					cl->num_viewents++;
				}
			}

			// if we are full, silently discard entities
			if( ents->num_entities < ( MAX_VISIBLE_PACKET - 1 ))
			{
				ents->num_entities++;	// entity accepted
				c_fullsend++;		// debug counter

			}
			else
			{
				// visibility list is full
				// continue counting entities,
				// so we know how many it's ovreflowed
				c_notsend++;
			}
		}

		if( fullvis ) continue; // portal ents will be added anyway, ignore recursion

		// if it's a portal entity, add everything visible from its camera position
		if( from_client && FBitSet( ent->v.effects, EF_MERGE_VISIBILITY ))
		{
			SetBits( sv.hostflags, SVF_MERGE_VISIBILITY );
			SV_AddEntitiesToPacket( ent, pClient, frame, ents, false );
			ClearBits( sv.hostflags, SVF_MERGE_VISIBILITY );
		}
	}
}

/*
=============================================================================

Encode a client frame onto the network channel

=============================================================================
*/
/*
=============
SV_FindBestBaseline

trying to deltas with previous entities
set frame to NULL to check for static entities
=============
*/
int SV_FindBestBaseline( int index, entity_state_t **baseline, entity_state_t *to, client_frame_t *frame, qboolean player )
{
	int	bestBitCount;
	int	bestfound, j;

	bestBitCount = j = Delta_TestBaseline( *baseline, to, player, sv.time );
	bestfound = index;

	// lookup backward for previous 64 states and try to interpret current delta as baseline
	for( int i = index - 1; bestBitCount > 0 && i >= 0 && ( index - i ) < ( MAX_CUSTOM_BASELINES - 1 ); i-- )
	{
		// don't worry about underflow in circular buffer
		entity_state_t *test;

		// if set, then it's normal entity
		if( frame != NULL )
			test = &svs.packet_entities[(frame->first_entity+i) % svs.num_client_entities];
		else
			test = &svs.static_entities[i];

		if( to->entityType == test->entityType )
		{
			int	bitCount = Delta_TestBaseline( test, to, player, sv.time );

			if( bitCount < bestBitCount )
			{
				bestBitCount = bitCount;
				bestfound = i;
			}
		}
	}

	// using delta from previous entity as baseline for current
	if( index != bestfound )
	{
		if( frame != NULL )
			*baseline = &svs.packet_entities[(frame->first_entity+bestfound) % svs.num_client_entities];
		else
			*baseline = &svs.static_entities[bestfound];
	}
	return index - bestfound;
}

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entity_state_t list to the message->
=============
*/
static void SV_EmitPacketEntities( sv_client_t *cl, client_frame_t *to, sizebuf_t *msg )
{
	entity_state_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	qboolean		player;
	int		oldmax;
	client_frame_t	*from;

	// this is the frame that we are going to delta update from
	if( cl->delta_sequence != -1 )
	{
		from = &cl->frames[cl->delta_sequence & SV_UPDATE_MASK];
		oldmax = from->num_entities;

		// the snapshot's entities may still have rolled off the buffer, though
		if( from->first_entity <= ( svs.next_client_entities - svs.num_client_entities ))
		{
			Con_DPrintf( S_WARN "%s: delta request from out of date entities.\n", cl->name );
			MSG_BeginServerCmd( msg, svc_packetentities );
			MSG_WriteUBitLong( msg, to->num_entities - 1, MAX_VISIBLE_PACKET_BITS );

			from = NULL;
			oldmax = 0;
		}
		else
		{
			MSG_BeginServerCmd( msg, svc_deltapacketentities );
			MSG_WriteUBitLong( msg, to->num_entities - 1, MAX_VISIBLE_PACKET_BITS );
			MSG_WriteByte( msg, cl->delta_sequence );
		}
	}
	else
	{
		from = NULL;
		oldmax = 0;

		MSG_BeginServerCmd( msg, svc_packetentities );
		MSG_WriteUBitLong( msg, to->num_entities - 1, MAX_VISIBLE_PACKET_BITS );
	}

	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;

	while( newindex < to->num_entities || oldindex < oldmax )
	{
		if( newindex >= to->num_entities )
		{
			newnum = MAX_ENTNUMBER;
			player = false;
		}
		else
		{
			newent = &svs.packet_entities[(to->first_entity+newindex) % svs.num_client_entities];
			player = SV_IsPlayerIndex( newent->number );
			newnum = newent->number;
		}

		if( oldindex >= oldmax )
		{
			oldnum = MAX_ENTNUMBER;
		}
		else
		{
			oldent = &svs.packet_entities[(from->first_entity+oldindex) % svs.num_client_entities];
			oldnum = oldent->number;
		}

		if( newnum == oldnum )
		{
			// delta update from old position
			// because the force parm is false, this will not result
			// in any bytes being emited if the entity has not changed at all
			MSG_WriteDeltaEntity( oldent, newent, msg, false, player, sv.time, 0 );
			oldindex++;
			newindex++;
			continue;
		}

		if( newnum < oldnum )
		{
			entity_state_t	*baseline = &svs.baselines[newnum];
			const char	*classname = SV_ClassName( SV_EdictNum( newnum ));
			int		offset = 0;

			// trying to reduce message by select optimal baseline
			if( !sv_instancedbaseline.value || !sv.num_instanced || sv.last_valid_baseline > newnum )
			{
				offset = SV_FindBestBaseline( newindex, &baseline, newent, to, player );
			}
			else
			{
				for( int i = 0; i < sv.num_instanced; i++ )
				{
					if( !Q_strcmp( classname, sv.instanced[i].classname ))
					{
						baseline = &sv.instanced[i].baseline;
						offset = -i - 1; // to avoid zero offset
						break;
					}
				}
			}

			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity( baseline, newent, msg, true, player, sv.time, offset );
			newindex++;
			continue;
		}

		if( newnum > oldnum )
		{
			edict_t	*ed = SV_EdictNum( oldent->number );
			qboolean	force = false;

			// check if entity completely removed from server
			if( ed->free || FBitSet( ed->v.flags, FL_KILLME ))
				force = true;

			// remove from message
			MSG_WriteDeltaEntity( oldent, NULL, msg, force, false, sv.time, 0 );
			oldindex++;
			continue;
		}
	}

	MSG_WriteUBitLong( msg, LAST_EDICT, MAX_ENTITY_BITS ); // end of packetentities
}

/*
=============
SV_EmitEvents

=============
*/
static void SV_EmitEvents( sv_client_t *cl, client_frame_t *to, sizebuf_t *msg )
{
	event_state_t	*es;
	event_info_t	*info;
	event_args_t	nullargs;
	int		ev_count = 0;
	int		count, ent_index;
	int		i, j;

	memset( &nullargs, 0, sizeof( nullargs ));
	es = &cl->events;

	// count events
	for( int ev = 0; ev < MAX_EVENT_QUEUE; ev++ )
	{
		if( es->ei[ev].index )
			ev_count++;
	}

	// nothing to send
	if( !ev_count ) return; // nothing to send

	if ( ev_count >= MAX_EVENT_QUEUE / 2 )
		ev_count = ( MAX_EVENT_QUEUE / 2 ) - 1;

	for( i = 0; i < MAX_EVENT_QUEUE; i++ )
	{
		info = &es->ei[i];
		if( info->index == 0 )
			continue;

		ent_index = info->entity_index;

		for( j = 0; j < to->num_entities; j++ )
		{
			entity_state_t	*state = &svs.packet_entities[(to->first_entity+j) % svs.num_client_entities];
			if( state->number == ent_index )
				break;
		}

		if( j < to->num_entities )
		{
			info->packet_index = j;
			info->args.ducking = 0;

			if( !FBitSet( info->args.flags, FEVENT_ORIGIN ))
				VectorClear( info->args.origin );

			if( !FBitSet( info->args.flags, FEVENT_ANGLES ))
				VectorClear( info->args.angles );

			VectorClear( info->args.velocity );
		}
		else
		{
			// couldn't find
			info->packet_index = to->num_entities;
			info->args.entindex = ent_index;
		}
	}

	MSG_BeginServerCmd( msg, svc_event );	// create message
	MSG_WriteUBitLong( msg, ev_count, 5 );	// up to MAX_EVENT_QUEUE events

	for( count = i = 0; i < MAX_EVENT_QUEUE; i++ )
	{
		info = &es->ei[i];

		if( info->index == 0 )
		{
			info->packet_index = -1;
			info->entity_index = -1;
			continue;
		}

		// only send if there's room
		if( count < ev_count )
		{
			MSG_WriteUBitLong( msg, info->index, MAX_EVENT_BITS ); // 1024 events

			if( info->packet_index == -1 )
			{
				MSG_WriteOneBit( msg, 0 );
			}
			else
			{
				MSG_WriteOneBit( msg, 1 );
				MSG_WriteUBitLong( msg, info->packet_index, MAX_ENTITY_BITS );

				if( !memcmp( &nullargs, &info->args, sizeof( event_args_t )))
				{
					MSG_WriteOneBit( msg, 0 );
				}
				else
				{
					MSG_WriteOneBit( msg, 1 );
					MSG_WriteDeltaEvent( msg, &nullargs, &info->args );
				}
			}

			if( info->fire_time )
			{
				MSG_WriteOneBit( msg, 1 );
				MSG_WriteWord( msg, ( info->fire_time * 100.0f ));
			}
			else MSG_WriteOneBit( msg, 0 );
		}

		info->index = 0;
		info->packet_index = -1;
		info->entity_index = -1;
		count++;
	}
}

/*
=============
SV_EmitPings

=============
*/
static void SV_EmitPings( sizebuf_t *msg )
{
	sv_client_t *cl;
	int i;

	MSG_BeginServerCmd( msg, svc_pings );

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		int packet_loss, ping;

		if( cl->state != cs_spawned )
			continue;

		SV_GetPlayerStats( cl, &ping, &packet_loss );

		// there are 25 bits for each client
		MSG_WriteOneBit( msg, 1 );
		MSG_WriteUBitLong( msg, i, MAX_CLIENT_BITS );
		MSG_WriteUBitLong( msg, ping, 12 );
		MSG_WriteUBitLong( msg, packet_loss, 7 );
	}

	// end marker
	MSG_WriteOneBit( msg, 0 );
}

/*
==================
SV_WriteClientdataToMessage

==================
*/
static void SV_WriteClientdataToMessage( sv_client_t *cl, sizebuf_t *msg )
{
	clientdata_t	nullcd;
	clientdata_t	*from_cd, *to_cd;
	weapon_data_t	nullwd;
	weapon_data_t	*from_wd, *to_wd;
	client_frame_t	*frame;
	edict_t		*clent;

	memset( &nullcd, 0, sizeof( nullcd ));
	frame = &cl->frames[cl->netchan.outgoing_sequence & SV_UPDATE_MASK];
	frame->senttime = host.realtime;
	frame->ping_time = -1.0f;
	clent = cl->edict;

	if( cl->chokecount != 0 )
	{
		MSG_BeginServerCmd( msg, svc_choke );
		cl->chokecount = 0;
	}

	// update client fixangle
	switch( clent->v.fixangle )
	{
	case 1:
		MSG_BeginServerCmd( msg, svc_setangle );
		MSG_WriteVec3Angles( msg, clent->v.angles );
		break;
	case 2:
		MSG_BeginServerCmd( msg, svc_addangle );
		MSG_WriteBitAngle( msg, clent->v.avelocity[YAW], 16 );
		clent->v.avelocity[YAW] = 0.0f;
		break;
	}

	clent->v.fixangle = 0; // reset fixangle

	memset( &frame->clientdata, 0, sizeof( frame->clientdata ));

	// update clientdata_t
	SV_ProfileBegin( SVP_SEND_GATHER );
	svgame.dllFuncs.pfnUpdateClientData( clent, FBitSet( cl->flags, FCL_LOCAL_WEAPONS ), &frame->clientdata );
	SV_ProfileEnd( SVP_SEND_GATHER );

	MSG_BeginServerCmd( msg, svc_clientdata );
	if( FBitSet( cl->flags, FCL_HLTV_PROXY )) return;	// don't send more nothing

	if( cl->delta_sequence == -1 ) from_cd = &nullcd;
	else from_cd = &cl->frames[cl->delta_sequence & SV_UPDATE_MASK].clientdata;
	to_cd = &frame->clientdata;

	if( cl->delta_sequence == -1 )
	{
		MSG_WriteOneBit( msg, 0 );	// no delta-compression
	}
	else
	{
		MSG_WriteOneBit( msg, 1 );	// we are delta-ing from
		MSG_WriteByte( msg, cl->delta_sequence );
	}

	// write clientdata_t
	MSG_WriteClientData( msg, from_cd, to_cd, sv.time );

	SV_ProfileBegin( SVP_SEND_GATHER );
	qboolean weapons = FBitSet( cl->flags, FCL_LOCAL_WEAPONS ) && svgame.dllFuncs.pfnGetWeaponData( clent, frame->weapondata );
	SV_ProfileEnd( SVP_SEND_GATHER );

	if( weapons )
	{
		memset( &nullwd, 0, sizeof( nullwd ));

		for( int i = 0; i < MAX_LOCAL_WEAPONS; i++ )
		{
			if( cl->delta_sequence == -1 ) from_wd = &nullwd;
			else from_wd = &cl->frames[cl->delta_sequence & SV_UPDATE_MASK].weapondata[i];
			to_wd = &frame->weapondata[i];

			MSG_WriteWeaponData( msg, from_wd, to_wd, sv.time, i );
		}
	}

	// end marker
	MSG_WriteOneBit( msg, 0 );
}

/*
==================
SV_WriteEntitiesToClient

==================
*/
static void SV_WriteEntitiesToClient( sv_client_t *cl, sizebuf_t *msg )
{
	client_frame_t	*frame;
	static sv_ents_t	frame_ents;
	int		send_pings;

	frame = &cl->frames[cl->netchan.outgoing_sequence & SV_UPDATE_MASK];
	send_pings = SV_ShouldUpdatePing( cl );

	memset( frame_ents.sended, 0, sizeof( frame_ents.sended ));
	ClearBits( sv.hostflags, SVF_MERGE_VISIBILITY );

	// clear everything in this snapshot
	frame_ents.num_entities = c_fullsend = c_notsend = 0;

	// add all the entities directly visible to the eye, which
	// may include portal entities that merge other viewpoints
	SV_ProfileBegin( SVP_SEND_GATHER );
	SV_AddEntitiesToPacket( cl->pViewEntity, cl->edict, frame, &frame_ents, true );
	SV_ProfileEnd( SVP_SEND_GATHER );

	if( c_notsend != cl->ignored_ents )
	{
		if( c_notsend > 0 )
			Con_Printf( S_ERROR "Too many entities in visible packet list. Ignored %d entities\n", c_notsend );
		cl->ignored_ents = c_notsend;
	}

	// if there were portals visible, there may be out of order entities
	// in the list which will need to be resorted for the delta compression
	// to work correctly.  This also catches the error condition
	// of an entity being included twice.
	qsort( frame_ents.entities, frame_ents.num_entities, sizeof( frame_ents.entities[0] ), SV_EntityNumbers );

	// it will break all connected clients, but it takes more than one week to overflow it
	if(( (uint)svs.next_client_entities ) + frame_ents.num_entities >= 0x7FFFFFFE )
	{
		svs.next_client_entities = 0;

		// delta is broken for now, cannot keep connected clients
		SV_FinalMessage( "Server will restart due delta is outdated\n", true );
	}

	// copy the entity states out
	frame->first_entity = svs.next_client_entities;
	frame->num_entities = 0;

	for( int i = 0; i < frame_ents.num_entities; i++ )
	{
		// add it to the circular packet_entities array
		entity_state_t	*state = &svs.packet_entities[svs.next_client_entities % svs.num_client_entities];
		*state = frame_ents.entities[i];
		svs.next_client_entities++;
		frame->num_entities++;
	}

	SV_ProfileCount( SVC_ENTITIES_SENT, frame->num_entities );

	SV_EmitPacketEntities( cl, frame, msg );
	SV_EmitEvents( cl, frame, msg );
	if( send_pings ) SV_EmitPings( msg );
}

/*
===============================================================================

FRAME UPDATES

===============================================================================
*/
/*
=======================
SV_SendClientDatagram
=======================
*/
static void SV_SendClientDatagram( sv_client_t *cl )
{
	byte	msg_buf[MAX_DATAGRAM];
	sizebuf_t	msg;

	SV_ProfileBegin( SVP_SEND_DATAGRAM );

	memset( msg_buf, 0, sizeof( msg_buf ));
	MSG_Init( &msg, "Datagram", msg_buf, sizeof( msg_buf ));

	// always send servertime at new frame
	MSG_BeginServerCmd( &msg, svc_time );
	MSG_WriteFloat( &msg, sv.time );

	SV_WriteClientdataToMessage( cl, &msg );
	SV_WriteEntitiesToClient( cl, &msg );

	// copy the accumulated multicast datagram
	// for this client out to the message
	if( MSG_CheckOverflow( &cl->datagram ))
	{
		Con_Printf( S_WARN "%s overflowed for %s\n", MSG_GetName( &cl->datagram ), cl->name );
	}
	else
	{
		if( MSG_GetNumBytesWritten( &cl->datagram ) < MSG_GetNumBytesLeft( &msg ))
			MSG_WriteBits( &msg, MSG_GetData( &cl->datagram ), MSG_GetNumBitsWritten( &cl->datagram ));
		else if( host.realtime > cl->overflow_warn_time )
		{
			Con_DPrintf( S_WARN "Ignoring unreliable datagram for %s, would overflow on msg\n", cl->name );
			cl->overflow_warn_time = host.realtime + 5.0f;
		}
	}

	MSG_Clear( &cl->datagram );

	if( MSG_CheckOverflow( &msg ))
	{
		// must have room left for the packet header
		Con_Printf( S_ERROR "%s overflowed for %s\n", MSG_GetName( &msg ), cl->name );
		MSG_Clear( &msg );
	}

	SV_ProfileEnd( SVP_SEND_DATAGRAM );

	// send the datagram
	SV_ProfileBegin( SVP_SEND_TRANSMIT );
	Netchan_TransmitBits( &cl->netchan, MSG_GetNumBitsWritten( &msg ), MSG_GetData( &msg ));
	SV_ProfileEnd( SVP_SEND_TRANSMIT );
	SV_ProfileCount( SVC_CLIENTS_SENT, 1 );
}


/*
===============================================================================

SNAPSHOTS GATHERED FIRST (stories 045-047)

With sv_parallel_send 1, SV_SendClientMessages gathers the snapshots of all
the clients due this frame on the main thread first: every game library call
(SetupVisibility, AddToFullPack, UpdateClientData, GetWeaponData, the custom
delta encoders, whose results are recorded as masks), the events and the
pings. Then the snapshots are encoded, which reads only gathered data and
engine state nothing writes meanwhile, then sent in client order. The bytes
are those of SV_SendClientDatagram.

===============================================================================
*/
typedef struct
{
	const entity_state_t *from;
	const entity_state_t *to; // NULL: removed
	int          delta_type;
	int          baseline;
	qboolean     force;
	qboolean     has_mask;
	delta_mask_t mask;
} snapshot_delta_t;

#define SNAPSHOT_TAIL_SIZE 8192 // events and pings

typedef struct
{
	sv_client_t    *cl;
	qboolean       datagram;  // false: the reliable stream only
	qboolean       skip;      // the delta frame rolled off the entity buffer meanwhile

	// SV_WriteClientdataToMessage
	client_frame_t *frame;
	int            delta_sequence;
	qboolean       choke;
	int            fixangle;
	vec3_t         angles;
	float          avelocity_yaw;
	qboolean       hltv;
	qboolean       weapons;

	// SV_EmitPacketEntities
	int            entities_cmd;
	const client_frame_t *entities_from;
	int            num_deltas;
	int            max_deltas;
	snapshot_delta_t *deltas;

	// SV_EmitEvents and SV_EmitPings, written during the gather
	sizebuf_t      tail;
	byte           tail_buf[SNAPSHOT_TAIL_SIZE];

	// the datagram
	sizebuf_t      msg;
	byte           msg_buf[MAX_DATAGRAM];
	qboolean       warn_datagram_overflow;
	qboolean       warn_datagram_ignored;
	qboolean       warn_msg_overflow;
} client_snapshot_t;

static client_snapshot_t *snapshots;
static int num_snapshot_slots;
static int num_snapshots;

static client_snapshot_t *SV_NewSnapshot( sv_client_t *cl, qboolean datagram )
{
	client_snapshot_t *snap;

	if( num_snapshot_slots < svs.maxclients )
	{
		int i;

		// the buffers of the old slots go with them
		for( i = 0; i < num_snapshot_slots; i++ )
			Mem_Free( snapshots[i].deltas );
		if( snapshots )
			Mem_Free( snapshots );

		snapshots = Mem_Calloc( host.mempool, sizeof( *snapshots ) * svs.maxclients );
		num_snapshot_slots = svs.maxclients;
	}

	snap = &snapshots[num_snapshots++];
	snap->cl = cl;
	snap->datagram = datagram;
	snap->skip = false;
	snap->num_deltas = 0;
	snap->warn_datagram_overflow = snap->warn_datagram_ignored = snap->warn_msg_overflow = false;
	return snap;
}

static void SV_PlanDelta( client_snapshot_t *snap, const entity_state_t *from, const entity_state_t *to, qboolean force, int delta_type, int baseline )
{
	snapshot_delta_t *d;

	if( snap->num_deltas == snap->max_deltas )
	{
		snap->max_deltas = snap->max_deltas ? snap->max_deltas * 2 : 256;
		snap->deltas = Mem_Realloc( host.mempool, snap->deltas, sizeof( *snap->deltas ) * snap->max_deltas );
	}

	d = &snap->deltas[snap->num_deltas++];
	d->from = from;
	d->to = to;
	d->force = force;
	d->delta_type = delta_type;
	d->baseline = baseline;

	// the game's custom encoder runs here, on the main thread
	d->has_mask = to ? Delta_EntityEncoderMask( from, to, delta_type, &d->mask ) : false;
}

/*
=============
SV_PlanPacketEntities

SV_EmitPacketEntities, recording the deltas instead of writing them
=============
*/
static void SV_PlanPacketEntities( client_snapshot_t *snap, client_frame_t *to )
{
	sv_client_t	*cl = snap->cl;
	entity_state_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	qboolean		player;
	int		oldmax;
	client_frame_t	*from;

	// this is the frame that we are going to delta update from
	if( cl->delta_sequence != -1 )
	{
		from = &cl->frames[cl->delta_sequence & SV_UPDATE_MASK];
		oldmax = from->num_entities;

		// the snapshot's entities may still have rolled off the buffer, though
		if( from->first_entity <= ( svs.next_client_entities - svs.num_client_entities ))
		{
			Con_DPrintf( S_WARN "%s: delta request from out of date entities.\n", cl->name );
			snap->entities_cmd = svc_packetentities;
			from = NULL;
			oldmax = 0;
		}
		else snap->entities_cmd = svc_deltapacketentities;
	}
	else
	{
		from = NULL;
		oldmax = 0;
		snap->entities_cmd = svc_packetentities;
	}

	snap->entities_from = from;
	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;

	while( newindex < to->num_entities || oldindex < oldmax )
	{
		if( newindex >= to->num_entities )
		{
			newnum = MAX_ENTNUMBER;
			player = false;
		}
		else
		{
			newent = &svs.packet_entities[(to->first_entity+newindex) % svs.num_client_entities];
			player = SV_IsPlayerIndex( newent->number );
			newnum = newent->number;
		}

		if( oldindex >= oldmax )
		{
			oldnum = MAX_ENTNUMBER;
		}
		else
		{
			oldent = &svs.packet_entities[(from->first_entity+oldindex) % svs.num_client_entities];
			oldnum = oldent->number;
		}

		if( newnum == oldnum )
		{
			SV_PlanDelta( snap, oldent, newent, false, player, 0 );
			oldindex++;
			newindex++;
			continue;
		}

		if( newnum < oldnum )
		{
			entity_state_t	*baseline = &svs.baselines[newnum];
			const char	*classname = SV_ClassName( SV_EdictNum( newnum ));
			int		offset = 0;

			// trying to reduce message by select optimal baseline
			if( !sv_instancedbaseline.value || !sv.num_instanced || sv.last_valid_baseline > newnum )
			{
				offset = SV_FindBestBaseline( newindex, &baseline, newent, to, player );
			}
			else
			{
				for( int i = 0; i < sv.num_instanced; i++ )
				{
					if( !Q_strcmp( classname, sv.instanced[i].classname ))
					{
						baseline = &sv.instanced[i].baseline;
						offset = -i - 1; // to avoid zero offset
						break;
					}
				}
			}

			// this is a new entity, send it from the baseline
			SV_PlanDelta( snap, baseline, newent, true, player, offset );
			newindex++;
			continue;
		}

		if( newnum > oldnum )
		{
			edict_t	*ed = SV_EdictNum( oldent->number );
			qboolean	force = false;

			// check if entity completely removed from server
			if( ed->free || FBitSet( ed->v.flags, FL_KILLME ))
				force = true;

			// remove from message
			SV_PlanDelta( snap, oldent, NULL, force, false, 0 );
			oldindex++;
			continue;
		}
	}
}

/*
=============
SV_GatherClientDatagram

everything of SV_SendClientDatagram that calls the game library or changes
state, on the main thread
=============
*/
static void SV_GatherClientDatagram( client_snapshot_t *snap )
{
	sv_client_t	*cl = snap->cl;
	static sv_ents_t	frame_ents;
	client_frame_t	*frame;
	edict_t		*clent = cl->edict;
	qboolean	send_pings;

	SV_ProfileBegin( SVP_SEND_DATAGRAM );

	// SV_WriteClientdataToMessage
	frame = &cl->frames[cl->netchan.outgoing_sequence & SV_UPDATE_MASK];
	frame->senttime = host.realtime;
	frame->ping_time = -1.0f;
	snap->frame = frame;
	snap->delta_sequence = cl->delta_sequence;

	snap->choke = cl->chokecount != 0;
	cl->chokecount = 0;

	snap->fixangle = clent->v.fixangle;
	VectorCopy( clent->v.angles, snap->angles );
	if( clent->v.fixangle == 2 )
	{
		snap->avelocity_yaw = clent->v.avelocity[YAW];
		clent->v.avelocity[YAW] = 0.0f;
	}
	clent->v.fixangle = 0; // reset fixangle

	memset( &frame->clientdata, 0, sizeof( frame->clientdata ));

	SV_ProfileBegin( SVP_SEND_GATHER );
	svgame.dllFuncs.pfnUpdateClientData( clent, FBitSet( cl->flags, FCL_LOCAL_WEAPONS ), &frame->clientdata );
	SV_ProfileEnd( SVP_SEND_GATHER );

	snap->hltv = FBitSet( cl->flags, FCL_HLTV_PROXY ) ? true : false;
	snap->weapons = false;
	if( !snap->hltv )
	{
		SV_ProfileBegin( SVP_SEND_GATHER );
		snap->weapons = FBitSet( cl->flags, FCL_LOCAL_WEAPONS ) && svgame.dllFuncs.pfnGetWeaponData( clent, frame->weapondata );
		SV_ProfileEnd( SVP_SEND_GATHER );
	}

	// SV_WriteEntitiesToClient
	send_pings = SV_ShouldUpdatePing( cl );

	memset( frame_ents.sended, 0, sizeof( frame_ents.sended ));
	ClearBits( sv.hostflags, SVF_MERGE_VISIBILITY );
	frame_ents.num_entities = c_fullsend = c_notsend = 0;

	SV_ProfileBegin( SVP_SEND_GATHER );
	SV_AddEntitiesToPacket( cl->pViewEntity, cl->edict, frame, &frame_ents, true );
	SV_ProfileEnd( SVP_SEND_GATHER );

	if( c_notsend != cl->ignored_ents )
	{
		if( c_notsend > 0 )
			Con_Printf( S_ERROR "Too many entities in visible packet list. Ignored %d entities\n", c_notsend );
		cl->ignored_ents = c_notsend;
	}

	qsort( frame_ents.entities, frame_ents.num_entities, sizeof( frame_ents.entities[0] ), SV_EntityNumbers );

	// it will break all connected clients, but it takes more than one week to overflow it
	if(( (uint)svs.next_client_entities ) + frame_ents.num_entities >= 0x7FFFFFFE )
	{
		svs.next_client_entities = 0;
		SV_FinalMessage( "Server will restart due delta is outdated\n", true );
	}

	frame->first_entity = svs.next_client_entities;
	frame->num_entities = 0;

	for( int i = 0; i < frame_ents.num_entities; i++ )
	{
		entity_state_t	*state = &svs.packet_entities[svs.next_client_entities % svs.num_client_entities];
		*state = frame_ents.entities[i];
		svs.next_client_entities++;
		frame->num_entities++;
	}

	SV_ProfileCount( SVC_ENTITIES_SENT, frame->num_entities );

	SV_ProfileBegin( SVP_SEND_GATHER );
	SV_PlanPacketEntities( snap, frame );
	SV_ProfileEnd( SVP_SEND_GATHER );

	// zeroed like the datagram it is copied into: the copy takes whole bytes
	memset( snap->tail_buf, 0, sizeof( snap->tail_buf ));
	MSG_Init( &snap->tail, "SnapshotTail", snap->tail_buf, sizeof( snap->tail_buf ));
	SV_EmitEvents( cl, frame, &snap->tail );
	if( send_pings ) SV_EmitPings( &snap->tail );

	SV_ProfileEnd( SVP_SEND_DATAGRAM );
}

/*
=============
SV_EncodeClientDatagram

the datagram of a gathered snapshot; never calls the game library nor
writes shared state, so that it can run on a worker
=============
*/
static void SV_EncodeClientDatagram( client_snapshot_t *snap )
{
	sv_client_t	*cl = snap->cl;
	sizebuf_t	*msg = &snap->msg;
	clientdata_t	nullcd;
	weapon_data_t	nullwd;
	int		i;

	memset( snap->msg_buf, 0, sizeof( snap->msg_buf ));
	MSG_Init( msg, "Datagram", snap->msg_buf, sizeof( snap->msg_buf ));

	// always send servertime at new frame
	MSG_BeginServerCmd( msg, svc_time );
	MSG_WriteFloat( msg, sv.time );

	if( snap->choke )
		MSG_BeginServerCmd( msg, svc_choke );

	switch( snap->fixangle )
	{
	case 1:
		MSG_BeginServerCmd( msg, svc_setangle );
		MSG_WriteVec3Angles( msg, snap->angles );
		break;
	case 2:
		MSG_BeginServerCmd( msg, svc_addangle );
		MSG_WriteBitAngle( msg, snap->avelocity_yaw, 16 );
		break;
	}

	MSG_BeginServerCmd( msg, svc_clientdata );
	if( !snap->hltv )
	{
		const clientdata_t *from_cd;

		memset( &nullcd, 0, sizeof( nullcd ));

		if( snap->delta_sequence == -1 )
		{
			from_cd = &nullcd;
			MSG_WriteOneBit( msg, 0 );	// no delta-compression
		}
		else
		{
			from_cd = &cl->frames[snap->delta_sequence & SV_UPDATE_MASK].clientdata;
			MSG_WriteOneBit( msg, 1 );	// we are delta-ing from
			MSG_WriteByte( msg, snap->delta_sequence );
		}

		MSG_WriteClientDataMasked( msg, from_cd, &snap->frame->clientdata, sv.time );

		if( snap->weapons )
		{
			memset( &nullwd, 0, sizeof( nullwd ));

			for( i = 0; i < MAX_LOCAL_WEAPONS; i++ )
			{
				const weapon_data_t *from_wd;

				if( snap->delta_sequence == -1 ) from_wd = &nullwd;
				else from_wd = &cl->frames[snap->delta_sequence & SV_UPDATE_MASK].weapondata[i];

				MSG_WriteWeaponDataMasked( msg, from_wd, &snap->frame->weapondata[i], sv.time, i );
			}
		}

		// end marker
		MSG_WriteOneBit( msg, 0 );
	}

	// packet entities
	MSG_BeginServerCmd( msg, snap->entities_cmd );
	MSG_WriteUBitLong( msg, snap->frame->num_entities - 1, MAX_VISIBLE_PACKET_BITS );
	if( snap->entities_cmd == svc_deltapacketentities )
		MSG_WriteByte( msg, snap->delta_sequence );

	for( i = 0; i < snap->num_deltas; i++ )
	{
		const snapshot_delta_t *d = &snap->deltas[i];
		MSG_WriteDeltaEntityMasked( d->from, d->to, msg, d->force, d->delta_type, sv.time, d->baseline, d->has_mask ? &d->mask : NULL );
	}

	MSG_WriteUBitLong( msg, LAST_EDICT, MAX_ENTITY_BITS ); // end of packetentities

	// events and pings
	MSG_WriteBits( msg, MSG_GetData( &snap->tail ), MSG_GetNumBitsWritten( &snap->tail ));

	// copy the accumulated multicast datagram for this client out to the message
	if( MSG_CheckOverflow( &cl->datagram ))
	{
		snap->warn_datagram_overflow = true;
	}
	else
	{
		if( MSG_GetNumBytesWritten( &cl->datagram ) < MSG_GetNumBytesLeft( msg ))
			MSG_WriteBits( msg, MSG_GetData( &cl->datagram ), MSG_GetNumBitsWritten( &cl->datagram ));
		else if( host.realtime > cl->overflow_warn_time )
		{
			snap->warn_datagram_ignored = true;
			cl->overflow_warn_time = host.realtime + 5.0f;
		}
	}

	MSG_Clear( &cl->datagram );

	if( MSG_CheckOverflow( msg ))
	{
		// must have room left for the packet header
		snap->warn_msg_overflow = true;
		MSG_Clear( msg );
	}
}

/*
=============
SV_TransmitSnapshot

main thread, in client order
=============
*/
static void SV_TransmitSnapshot( client_snapshot_t *snap )
{
	sv_client_t *cl = snap->cl;

	if( !snap->datagram )
	{
		Netchan_TransmitBits( &cl->netchan, 0, NULL ); // just update reliable
		return;
	}

	if( snap->skip )
		return;

	if( snap->warn_datagram_overflow )
		Con_Printf( S_WARN "%s overflowed for %s\n", MSG_GetName( &cl->datagram ), cl->name );
	if( snap->warn_datagram_ignored )
		Con_DPrintf( S_WARN "Ignoring unreliable datagram for %s, would overflow on msg\n", cl->name );
	if( snap->warn_msg_overflow )
		Con_Printf( S_ERROR "%s overflowed for %s\n", MSG_GetName( &snap->msg ), cl->name );

	SV_ProfileBegin( SVP_SEND_TRANSMIT );
	Netchan_TransmitBits( &cl->netchan, MSG_GetNumBitsWritten( &snap->msg ), MSG_GetData( &snap->msg ));
	SV_ProfileEnd( SVP_SEND_TRANSMIT );
	SV_ProfileCount( SVC_CLIENTS_SENT, 1 );
}

/*
=============
SV_EncodeSnapshots

after the gather; story 047 spreads this over the workers
=============
*/
static client_snapshot_t *encode_list[MAX_CLIENTS];
static int num_encode;

static void SV_EncodeSnapshotItem( int index, void *ctx )
{
	SV_EncodeClientDatagram( encode_list[index] );
}

static void SV_EncodeSnapshots( void )
{
	uint32_t workers_before;
	int i;

	// a delta frame overwritten by the later gathers of this frame: the
	// entity buffer turned almost all the way around in one frame. The
	// client gets no snapshot this frame rather than a wrong one.
	for( i = 0; i < num_snapshots; i++ )
	{
		client_snapshot_t *snap = &snapshots[i];

		if( snap->datagram && snap->entities_from && snap->entities_from->first_entity <= ( svs.next_client_entities - svs.num_client_entities ))
		{
			Con_DPrintf( S_WARN "%s: snapshot skipped, its delta frame rolled off the entity buffer\n", snap->cl->name );
			snap->skip = true;
		}
	}

	// the snapshots to encode, spread over the workers (story 047)
	num_encode = 0;
	for( i = 0; i < num_snapshots; i++ )
	{
		if( snapshots[i].datagram && !snapshots[i].skip )
			encode_list[num_encode++] = &snapshots[i];
	}

	SV_ProfileBegin( SVP_SEND_DATAGRAM );
	workers_before = SV_WorkersItems( );
	SV_ParallelFor( num_encode, SV_EncodeSnapshotItem, NULL );
	SV_ProfileCount( SVC_SEND_WORKER_ITEMS, SV_WorkersItems( ) - workers_before );
	SV_ProfileEnd( SVP_SEND_DATAGRAM );
}

static qboolean SV_GatherFirst( void )
{
	// a custom encoder on these would have to run while encoding
	return sv_parallel_send.value != 0.0f && !Delta_HasCustomEncoder( DT_CLIENTDATA_T ) && !Delta_HasCustomEncoder( DT_WEAPONDATA_T );
}

/*
=======================
SV_UpdateUserInfo
=======================
*/
static void SV_UpdateUserInfo( sv_client_t *cl )
{
	SV_FullClientUpdate( cl, &sv.reliable_datagram );
	ClearBits( cl->flags, FCL_RESEND_USERINFO );
	cl->next_sendinfotime = host.realtime + 1.0;
}

/*
=======================
SV_UpdateToReliableMessages
=======================
*/
static void SV_UpdateToReliableMessages( void )
{
	sv_client_t	*cl;
	int		i;

	// check for changes to be sent over the reliable streams to all clients
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( !cl->edict ) continue;	// not in game yet

		if( cl->state != cs_spawned )
			continue;

		if( FBitSet( cl->flags, FCL_RESEND_USERINFO ) && cl->next_sendinfotime <= host.realtime )
		{
			if( MSG_GetNumBytesLeft( &sv.reliable_datagram ) >= ( Q_strlen( cl->userinfo ) + 6 ))
				SV_UpdateUserInfo( cl );
		}

		if( FBitSet( cl->flags, FCL_RESEND_MOVEVARS ))
		{
			SV_FullUpdateMovevars( cl, &cl->netchan.message );
			ClearBits( cl->flags, FCL_RESEND_MOVEVARS );
		}
	}

	// clear the server datagram if it overflowed.
	if( MSG_CheckOverflow( &sv.datagram ))
	{
		Con_DPrintf( S_ERROR "sv.datagram overflowed!\n" );
		MSG_Clear( &sv.datagram );
	}

	// clear the server datagram if it overflowed.
	if( MSG_CheckOverflow( &sv.spec_datagram ))
	{
		Con_DPrintf( S_ERROR "sv.spec_datagram overflowed!\n" );
		MSG_Clear( &sv.spec_datagram );
	}

	// now send the reliable and server datagrams to all clients.
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state < cs_connected || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;	// reliables go to all connected or spawned

		if( MSG_GetNumBytesWritten( &sv.reliable_datagram ) < MSG_GetNumBytesLeft( &cl->netchan.message ))
			MSG_WriteBits( &cl->netchan.message, MSG_GetData( &sv.reliable_datagram ), MSG_GetNumBitsWritten( &sv.reliable_datagram ));
		else Netchan_CreateFragments( &cl->netchan, &sv.reliable_datagram );

		if( MSG_GetNumBytesWritten( &sv.datagram ) < MSG_GetNumBytesLeft( &cl->datagram ))
			MSG_WriteBits( &cl->datagram, MSG_GetData( &sv.datagram ), MSG_GetNumBitsWritten( &sv.datagram ));
		else Con_Reportf( S_WARN "Ignoring unreliable datagram for %s, would overflow\n", cl->name );

		if( FBitSet( cl->flags, FCL_HLTV_PROXY ))
		{
			if( MSG_GetNumBytesWritten( &sv.spec_datagram ) < MSG_GetNumBytesLeft( &cl->datagram ))
				MSG_WriteBits( &cl->datagram, MSG_GetData( &sv.spec_datagram ), MSG_GetNumBitsWritten( &sv.spec_datagram ));
			else Con_Reportf( S_WARN "Ignoring spectator datagram for %s, would overflow\n", cl->name );
		}
	}

	// now clear the reliable and datagram buffers.
	MSG_Clear( &sv.reliable_datagram );
	MSG_Clear( &sv.spec_datagram );
	MSG_Clear( &sv.datagram );
}

/*
=======================
SV_CanSendDatagram
=======================
*/
static qboolean SV_CanSendDatagram( sv_client_t *cl )
{
	if( !FBitSet( cl->flags, FCL_HOLD_FIRST_DATAGRAM ))
		return true;

	// the game queues its HUD setup messages from the first pfnUpdateClientData call
	// and the client starts drawing the HUD as soon as the first datagram arrives,
	// so the first datagram has to carry the reliable stream along with it
	if( cl->netchan.reliable_length || cl->netchan.fragbufs[FRAG_NORMAL_STREAM] || cl->netchan.waitlist[FRAG_NORMAL_STREAM] )
		return false;

	ClearBits( cl->flags, FCL_HOLD_FIRST_DATAGRAM );
	return true;
}

/*
=======================
SV_NextSpreadMessageTime

Time of the next snapshot for a client, on a fixed per-client phase within
its update interval (story 039). With "now + interval", every client becomes
due in the same frame after any long frame and they stay aligned for good, so
all the snapshots of the server are built in one frame out of N. A fixed phase
spreads them evenly over the frames and keeps the update rate exact.
=======================
*/
static double SV_NextSpreadMessageTime( const sv_client_t *cl )
{
	double interval = cl->next_messageinterval;
	double now = host.realtime + sv.frametime;
	double phase;

	if( interval <= 0.0 )
		return now;

	phase = interval * ( cl - svs.clients ) / svs.maxclients;

	// first phase-aligned time at least half an interval ahead
	return phase + interval * floor(( now + 1.5 * interval - phase ) / interval );
}

/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages( void )
{
	sv_client_t *cl;
	int          i;
	qboolean     gather_first;

	if( sv.state == ss_dead )
		return;

	gather_first = SV_GatherFirst( );
	num_snapshots = 0;

	SV_UpdateToReliableMessages ();

	// sv_maxlook goes to the clients the way the game's physinfo keys do; left
	// empty, a key the game or a plugin set stays
	if( sv_maxlook.string[0] )
	{
		for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
		{
			if( cl->state >= cs_connected && Q_strcmp( Info_ValueForKey( cl->physinfo, "maxlook" ), sv_maxlook.string ))
				Info_SetValueForKey( cl->physinfo, "maxlook", sv_maxlook.string, MAX_INFO_STRING );
		}
	}

	// send a message to each connected client
	for( i = 0, sv.current_client = svs.clients; i < svs.maxclients; i++, sv.current_client++ )
	{
		cl = sv.current_client;

		if( cl->state <= cs_zombie || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		if( FBitSet( cl->flags, FCL_SKIP_NET_MESSAGE ))
		{
			ClearBits( cl->flags, FCL_SKIP_NET_MESSAGE );
			continue;
		}

		if( !host_limitlocal.value && NET_IsLocalAddress( cl->netchan.remote_address ))
			SetBits( cl->flags, FCL_SEND_NET_MESSAGE );

		if( cl->state == cs_spawned )
		{
			// Try to send a message as soon as we can.
			// If the target time for sending is within the next frame interval ( based on last frame ),
			// trigger the send now. Note that in single player,
			// FCL_SEND_NET_MESSAGE flag is also set any time a packet arrives from the client.
			double	time_until_next_message = cl->next_messagetime - ( host.realtime + sv.frametime );
			if( time_until_next_message <= 0.0 )
				SetBits( cl->flags, FCL_SEND_NET_MESSAGE );
			else if( time_until_next_message > 2.0 ) // something got hosed
				SetBits( cl->flags, FCL_SEND_NET_MESSAGE );
		}

		// if the reliable message overflowed, drop the client
		if( MSG_CheckOverflow( &cl->netchan.message ))
		{
			MSG_Clear( &cl->netchan.message );
			MSG_Clear( &cl->datagram );
			SV_BroadcastPrintf( NULL, "%s overflowed\n", cl->name );
			Con_DPrintf( S_ERROR "reliable overflow for %s\n", cl->name );
			SV_DropClient( cl, false );
			SetBits( cl->flags, FCL_SEND_NET_MESSAGE );
			cl->netchan.cleartime = 0.0;	// don't choke this message
		}
		else if( FBitSet( cl->flags, FCL_SEND_NET_MESSAGE ))
		{
			// If we haven't gotten a message in sv_failuretime seconds, then stop sending messages to this client
			// until we get another packet in from the client. This prevents crash/drop and reconnect where they are
			// being hosed with "sequenced packet without connection" packets.
			if( sv_failuretime.value < ( host.realtime - cl->netchan.last_received ))
				ClearBits( cl->flags, FCL_SEND_NET_MESSAGE );
		}

		// only send messages if the client has sent one
		// and the bandwidth is not choked
		if( FBitSet( cl->flags, FCL_SEND_NET_MESSAGE ))
		{
			// bandwidth choke active?
			if( !Netchan_CanPacket( &cl->netchan, cl->state == cs_spawned ))
			{
				cl->chokecount++;
				continue;
			}

			// now that we were able to send, reset timer to point to next possible send time.
			// check here also because sv_max/minupdaterate could been changed in runtime
			if( sv_snapshot_spread.value )
				cl->next_messagetime = SV_NextSpreadMessageTime( cl );
			else cl->next_messagetime = host.realtime + sv.frametime + cl->next_messageinterval;
			ClearBits( cl->flags, FCL_SEND_NET_MESSAGE );

			// NOTE: we should send frame even if server is not simulated to prevent overflow
			if( gather_first )
			{
				// sent below, in the same client order
				if( cl->state == cs_spawned && SV_CanSendDatagram( cl ))
					SV_GatherClientDatagram( SV_NewSnapshot( cl, true ));
				else SV_NewSnapshot( cl, false );
			}
			else if( cl->state == cs_spawned && SV_CanSendDatagram( cl ))
				SV_SendClientDatagram( cl );
			else Netchan_TransmitBits( &cl->netchan, 0, NULL ); // just update reliable
		}
	}

	// reset current client
	sv.current_client = NULL;

	if( gather_first )
	{
		SV_EncodeSnapshots( );

		for( i = 0; i < num_snapshots; i++ )
			SV_TransmitSnapshot( &snapshots[i] );
	}
}

/*
=======================
SV_SkipUpdates

used before changing level
=======================
*/
void SV_SkipUpdates( void )
{
	sv_client_t	*cl;
	int		i;

	if( sv.state == ss_dead )
		return;

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		SetBits( cl->flags, FCL_SKIP_NET_MESSAGE );
	}
}

/*
=======================
SV_InactivateClients

Purpose: Prepare for level transition, etc.
=======================
*/
void SV_InactivateClients( void )
{
	int		i;
	sv_client_t	*cl;

	if( sv.state == ss_dead )
		return;

	// send a message to each connected client
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( !cl->state || !cl->edict )
			continue;

		if( !cl->edict  )
			continue;

		if( FBitSet( cl->edict->v.flags, FL_FAKECLIENT ))
		{
			SV_DropClient( cl, false );
			continue;
		}

		if( cl->state > cs_connected )
		{
			cl->state = cs_connected;

			// bump connect timeout
			cl->connection_started = host.realtime;
		}

		COM_ClearCustomizationList( &cl->customdata, false );
		ClearBits( cl->flags, FCL_SEND_RESOURCES );
		memset( cl->physinfo, 0, sizeof( cl->physinfo ));

		// NOTE: many mods sending messages that must be applied on a next level
		// e.g. CryOfFear sending HideHud and PlayMp3 that affected after map change
		if( svgame.globals->changelevel )
			continue;

		MSG_Clear( &cl->netchan.message );
		MSG_Clear( &cl->datagram );
	}
}
