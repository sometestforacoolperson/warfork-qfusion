/*
Copyright (C) 2002-2007 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// r_alias.c: Quake III Arena .md3 model format support

#include "r_local.h"

/*
* Mod_AliasBuildStaticVBOForMesh
* 
* Builds a static vertex buffer object for given alias model mesh
*/
static void Mod_AliasBuildStaticVBOForMesh( maliasmesh_t *mesh )
{
	int i;
	mesh_t aliasmesh;
	vattribmask_t vattribs;
	
	vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT | VATTRIB_NORMAL_BIT | VATTRIB_SVECTOR_BIT;
	for( i = 0; i < mesh->numskins; i++ ) {
		vattribs |= mesh->skins[i].shader->vattribs;
	}

	mesh->vbo = R_CreateMeshVBO( ( void * )mesh, 
		mesh->numverts, mesh->numtris * 3, 0, vattribs, VBO_TAG_MODEL, vattribs );

	if( !mesh->vbo ) {
		return;
	}

	memset( &aliasmesh, 0, sizeof( aliasmesh ) );

	aliasmesh.elems = mesh->elems;
	aliasmesh.numElems = mesh->numtris * 3;
	aliasmesh.numVerts = mesh->numverts;

	aliasmesh.xyzArray = mesh->xyzArray;
	aliasmesh.stArray = mesh->stArray;
	aliasmesh.normalsArray = mesh->normalsArray;
	aliasmesh.sVectorsArray = mesh->sVectorsArray;

	R_UploadVBOVertexData( mesh->vbo, 0, vattribs, &aliasmesh, VBO_HINT_NONE );
	R_UploadVBOElemData( mesh->vbo, 0, 0, &aliasmesh, VBO_HINT_NONE );
}

/*
* Mod_AliasBuildMeshesForFrame0
*/
static void Mod_AliasBuildMeshesForFrame0( model_t *mod )
{
	int i, j, k;
	size_t size;
	maliasframe_t *frame;
	maliasmodel_t *aliasmodel = ( maliasmodel_t * )mod->extradata;

	frame = &aliasmodel->frames[0];
	for( k = 0; k < aliasmodel->nummeshes; k++ )
	{
		maliasmesh_t *mesh = &aliasmodel->meshes[k];

		size = sizeof( vec4_t ) + sizeof( vec4_t ); // xyz and normals
		size += sizeof( vec4_t );       // s-vectors
		size *= mesh->numverts;

		mesh->xyzArray = ( vec4_t * )Mod_Malloc( mod, size );
		mesh->normalsArray = ( vec4_t * )( ( qbyte * )mesh->xyzArray + mesh->numverts * sizeof( vec4_t ) );
		mesh->sVectorsArray = ( vec4_t * )( ( qbyte * )mesh->normalsArray + mesh->numverts * sizeof( vec4_t ) );

		for( i = 0; i < mesh->numverts; i++ )
		{
			for( j = 0; j < 3; j++ ) {
				mesh->xyzArray[i][j] = frame->translate[j] + frame->scale[j] * mesh->vertexes[i].point[j];
			}
			mesh->xyzArray[i][3] = 1;

			R_LatLongToNorm4( mesh->vertexes[i].latlong, mesh->normalsArray[i] );
		}

		R_BuildTangentVectors( mesh->numverts, mesh->xyzArray, mesh->normalsArray, mesh->stArray, mesh->numtris, mesh->elems, mesh->sVectorsArray );

		if( glConfig.ext.vertex_buffer_object ) {
			// build a static vertex buffer object to be used for rendering simple models, such as items
			Mod_AliasBuildStaticVBOForMesh( mesh );
		}
	}
}

/*
* Mod_TouchAliasModel
*/
static void Mod_TouchAliasModel( model_t *mod )
{
	int i, j;
	maliasmesh_t *mesh;
	maliasskin_t *skin;
	maliasmodel_t *aliasmodel = ( maliasmodel_t * )mod->extradata;

	mod->registrationSequence = rf.registrationSequence;

	for( i = 0, mesh = aliasmodel->meshes; i < aliasmodel->nummeshes; i++, mesh++ ) {
		// register needed skins and images
		for( j = 0, skin = mesh->skins; j < mesh->numskins; j++, skin++ ) {
			if( skin->shader ) {
				R_TouchShader( skin->shader );
			}
		}

		if( mesh->vbo ) {
			R_TouchMeshVBO( mesh->vbo );
		}
	}
}

/*
==============================================================================

MD3 MODELS

==============================================================================
*/

/*
* Mod_LoadAliasMD3Model
*/
void Mod_LoadAliasMD3Model( model_t *mod, model_t *parent, void *buffer, bspFormatDesc_t *unused )
{
	int version, i, j, l;
	int bufsize, numverts;
	qbyte *buf;
	dmd3header_t *pinmodel;
	dmd3frame_t *pinframe;
	dmd3tag_t *pintag;
	dmd3mesh_t *pinmesh;
	dmd3skin_t *pinskin;
	dmd3coord_t *pincoord;
	dmd3vertex_t *pinvert;
	elem_t *pinelem, *poutelem;
	maliasvertex_t *poutvert;
	vec2_t *poutcoord;
	maliasskin_t *poutskin;
	maliasmesh_t *poutmesh;
	maliastag_t *pouttag;
	maliasframe_t *poutframe;
	maliasmodel_t *poutmodel;
	drawSurfaceAlias_t *drawSurf;

	pinmodel = ( dmd3header_t * )buffer;
	version = LittleLong( pinmodel->version );

	if( version != MD3_ALIAS_VERSION )
		ri.Com_Error( ERR_DROP, "%s has wrong version number (%i should be %i)",
		mod->name, version, MD3_ALIAS_VERSION );

	mod->type = mod_alias;
	mod->extradata = poutmodel = Mod_Malloc( mod, sizeof( maliasmodel_t ) );
	mod->radius = 0;
	mod->registrationSequence = rf.registrationSequence;
	mod->touch = &Mod_TouchAliasModel;

	ClearBounds( mod->mins, mod->maxs );

	// byte swap the header fields and sanity check
	poutmodel->numframes = LittleLong( pinmodel->num_frames );
	poutmodel->numtags = LittleLong( pinmodel->num_tags );
	poutmodel->nummeshes = LittleLong( pinmodel->num_meshes );
	poutmodel->numskins = 0;

	if( poutmodel->numframes <= 0 )
		ri.Com_Error( ERR_DROP, "model %s has no frames", mod->name );
	//	else if( poutmodel->numframes > MD3_MAX_FRAMES )
	//		ri.Com_Error( ERR_DROP, "model %s has too many frames", mod->name );

	if( poutmodel->numtags > MD3_MAX_TAGS )
		ri.Com_Error( ERR_DROP, "model %s has too many tags", mod->name );
	else if( poutmodel->numtags < 0 )
		ri.Com_Error( ERR_DROP, "model %s has invalid number of tags", mod->name );

	if( poutmodel->nummeshes < 0 )
		ri.Com_Error( ERR_DROP, "model %s has invalid number of meshes", mod->name );
	else if( !poutmodel->nummeshes && !poutmodel->numtags )
		ri.Com_Error( ERR_DROP, "model %s has no meshes and no tags", mod->name );
	//	else if( poutmodel->nummeshes > MD3_MAX_MESHES )
	//		ri.Com_Error( ERR_DROP, "model %s has too many meshes", mod->name );

	bufsize = poutmodel->numframes * ( sizeof( maliasframe_t ) + sizeof( maliastag_t ) * poutmodel->numtags ) +
		poutmodel->nummeshes * sizeof( maliasmesh_t ) + 
		poutmodel->nummeshes * sizeof( drawSurfaceAlias_t );
	buf = Mod_Malloc( mod, bufsize );

	//
	// load the frames
	//
	pinframe = ( dmd3frame_t * )( ( qbyte * )pinmodel + LittleLong( pinmodel->ofs_frames ) );
	poutframe = poutmodel->frames = ( maliasframe_t * )buf; buf += sizeof( maliasframe_t ) * poutmodel->numframes;
	for( i = 0; i < poutmodel->numframes; i++, pinframe++, poutframe++ )
	{
		for( j = 0; j < 3; j++ )
		{
			poutframe->scale[j] = MD3_XYZ_SCALE;
			poutframe->translate[j] = LittleFloat( pinframe->translate[j] );
		}

		// never trust the modeler utility and recalculate bbox and radius
		ClearBounds( poutframe->mins, poutframe->maxs );
	}

	//
	// load the tags
	//
	pintag = ( dmd3tag_t * )( ( qbyte * )pinmodel + LittleLong( pinmodel->ofs_tags ) );
	pouttag = poutmodel->tags = ( maliastag_t * )buf; buf += sizeof( maliastag_t ) * poutmodel->numframes * poutmodel->numtags;
	for( i = 0; i < poutmodel->numframes; i++ )
	{
		for( l = 0; l < poutmodel->numtags; l++, pintag++, pouttag++ )
		{
			mat3_t axis;

			for( j = 0; j < 3; j++ )
			{
				axis[AXIS_FORWARD+j] = LittleFloat( pintag->axis[0][j] );
				axis[AXIS_RIGHT+j] = LittleFloat( pintag->axis[1][j] );
				axis[AXIS_UP+j] = LittleFloat( pintag->axis[2][j] );
				pouttag->origin[j] = LittleFloat( pintag->origin[j] );
			}

			Quat_FromMatrix3( axis, pouttag->quat );
			Quat_Normalize( pouttag->quat );

			Q_strncpyz( pouttag->name, pintag->name, MD3_MAX_PATH );
		}
	}

	//
	// allocate drawSurfs
	//
	drawSurf = poutmodel->drawSurfs = ( drawSurfaceAlias_t * )buf; buf += sizeof( drawSurfaceAlias_t ) * poutmodel->nummeshes;
	for( i = 0; i < poutmodel->nummeshes; i++, drawSurf++ )
	{
		drawSurf->type = ST_ALIAS;
		drawSurf->model = mod;
		drawSurf->mesh = poutmodel->meshes + i;
	}

	//
	// load meshes
	//
	pinmesh = ( dmd3mesh_t * )( ( qbyte * )pinmodel + LittleLong( pinmodel->ofs_meshes ) );
	poutmesh = poutmodel->meshes = ( maliasmesh_t * )buf; buf += sizeof( maliasmesh_t ) * poutmodel->nummeshes;
	for( i = 0; i < poutmodel->nummeshes; i++, poutmesh++ )
	{
		if( strncmp( (const char *)pinmesh->id, IDMD3HEADER, 4 ) )
			ri.Com_Error( ERR_DROP, "mesh %s in model %s has wrong id (%s should be %s)",
			pinmesh->name, mod->name, pinmesh->id, IDMD3HEADER );

		Q_strncpyz( poutmesh->name, pinmesh->name, MD3_MAX_PATH );

		Mod_StripLODSuffix( poutmesh->name );

		poutmesh->numtris = LittleLong( pinmesh->num_tris );
		poutmesh->numskins = LittleLong( pinmesh->num_skins );
		poutmesh->numverts = numverts = LittleLong( pinmesh->num_verts );

		/*		if( poutmesh->numskins <= 0 )
		ri.Com_Error( ERR_DROP, "mesh %i in model %s has no skins", i, mod->name );
		else*/ if( poutmesh->numskins > MD3_MAX_SHADERS )
			ri.Com_Error( ERR_DROP, "mesh %i in model %s has too many skins", i, mod->name );
		if( poutmesh->numtris <= 0 )
			ri.Com_Error( ERR_DROP, "mesh %i in model %s has no elements", i, mod->name );
		else if( poutmesh->numtris > MD3_MAX_TRIANGLES )
			ri.Com_Error( ERR_DROP, "mesh %i in model %s has too many triangles", i, mod->name );
		if( poutmesh->numverts <= 0 )
			ri.Com_Error( ERR_DROP, "mesh %i in model %s has no vertices", i, mod->name );
		else if( poutmesh->numverts > MD3_MAX_VERTS )
			ri.Com_Error( ERR_DROP, "mesh %i in model %s has too many vertices", i, mod->name );

		bufsize = sizeof( maliasskin_t ) * poutmesh->numskins + poutmesh->numtris * sizeof( elem_t ) * 3 +
			numverts * ( sizeof( vec2_t ) + sizeof( maliasvertex_t ) * poutmodel->numframes );
		buf = Mod_Malloc( mod, bufsize );

		//
		// load the skins
		//
		pinskin = ( dmd3skin_t * )( ( qbyte * )pinmesh + LittleLong( pinmesh->ofs_skins ) );
		poutskin = poutmesh->skins = ( maliasskin_t * )buf; buf += sizeof( maliasskin_t ) * poutmesh->numskins;
		for( j = 0; j < poutmesh->numskins; j++, pinskin++, poutskin++ ) {
			Q_strncpyz( poutskin->name, pinskin->name, sizeof( poutskin->name ) );
			poutskin->shader = R_RegisterSkin( poutskin->name );
		}
		
		//
		// load the elems
		//
		pinelem = ( elem_t * )( ( qbyte * )pinmesh + LittleLong( pinmesh->ofs_elems ) );
		poutelem = poutmesh->elems = ( elem_t * )buf; buf += poutmesh->numtris * sizeof( elem_t ) * 3;
		for( j = 0; j < poutmesh->numtris; j++, pinelem += 3, poutelem += 3 )
		{
			poutelem[0] = (elem_t)LittleLong( pinelem[0] );
			poutelem[1] = (elem_t)LittleLong( pinelem[1] );
			poutelem[2] = (elem_t)LittleLong( pinelem[2] );
		}

		//
		// load the texture coordinates
		//
		pincoord = ( dmd3coord_t * )( ( qbyte * )pinmesh + LittleLong( pinmesh->ofs_tcs ) );
		poutcoord = poutmesh->stArray = ( vec2_t * )buf; buf += poutmesh->numverts * sizeof( vec2_t );
		for( j = 0; j < poutmesh->numverts; j++, pincoord++ )
		{
			poutcoord[j][0] = LittleFloat( pincoord->st[0] );
			poutcoord[j][1] = LittleFloat( pincoord->st[1] );
		}

		//
		// load the vertexes and normals
		//
		pinvert = ( dmd3vertex_t * )( ( qbyte * )pinmesh + LittleLong( pinmesh->ofs_verts ) );
		poutvert = poutmesh->vertexes = ( maliasvertex_t * )buf;
		for( l = 0, poutframe = poutmodel->frames; l < poutmodel->numframes; l++, poutframe++, pinvert += poutmesh->numverts, poutvert += poutmesh->numverts )
		{
			vec3_t v;

			for( j = 0; j < poutmesh->numverts; j++ )
			{
				poutvert[j].point[0] = LittleShort( pinvert[j].point[0] );
				poutvert[j].point[1] = LittleShort( pinvert[j].point[1] );
				poutvert[j].point[2] = LittleShort( pinvert[j].point[2] );

				poutvert[j].latlong[0] = pinvert[j].norm[0];
				poutvert[j].latlong[1] = pinvert[j].norm[1];

				VectorCopy( poutvert[j].point, v );
				AddPointToBounds( v, poutframe->mins, poutframe->maxs );
			}
		}

		pinmesh = ( dmd3mesh_t * )( ( qbyte * )pinmesh + LittleLong( pinmesh->meshsize ) );
	}

	//
	// setup drawSurfs
	//
	for( i = 0; i < poutmodel->nummeshes; i++ )
	{
		drawSurf = poutmodel->drawSurfs + i;
		drawSurf->type = ST_ALIAS;
		drawSurf->model = mod;
		drawSurf->mesh = poutmodel->meshes + i;
	}

	//
	// build S and T vectors for frame 0
	//
	Mod_AliasBuildMeshesForFrame0( mod );

	//
	// calculate model bounds
	//
	poutframe = poutmodel->frames;
	for( i = 0; i < poutmodel->numframes; i++, poutframe++ )
	{
		VectorMA( poutframe->translate, MD3_XYZ_SCALE, poutframe->mins, poutframe->mins );
		VectorMA( poutframe->translate, MD3_XYZ_SCALE, poutframe->maxs, poutframe->maxs );
		poutframe->radius = RadiusFromBounds( poutframe->mins, poutframe->maxs );

		AddPointToBounds( poutframe->mins, mod->mins, mod->maxs );
		AddPointToBounds( poutframe->maxs, mod->mins, mod->maxs );
		mod->radius = max( mod->radius, poutframe->radius );
	}
}

/*
* R_AliasModelLOD
*/
static model_t *R_AliasModelLOD( const entity_t *e )
{
	int lod;

	if( !e->model->numlods || ( e->flags & RF_FORCENOLOD ) )
		return e->model;

	lod = R_LODForSphere( e->origin, e->model->radius );

	if( lod < 1 )
		return e->model;
	return e->model->lods[min( lod, e->model->numlods )-1];
}

/*
* R_AliasModelLerpBBox
*/
static float R_AliasModelLerpBBox( const entity_t *e, const model_t *mod, vec3_t mins, vec3_t maxs )
{
	int i;
	int framenum = framenum = e->frame, oldframenum = e->oldframe;
	const maliasmodel_t *aliasmodel = ( const maliasmodel_t * )mod->extradata;
	const maliasframe_t *pframe, *poldframe;

	if( !aliasmodel->nummeshes )
	{
		ClearBounds( mins, maxs );
		return 0;
	}

	if( ( framenum >= aliasmodel->numframes ) || ( framenum < 0 ) )
	{
#ifndef PUBLIC_BUILD
		ri.Com_DPrintf( "R_AliasModelLerpBBox %s: no such frame %d\n", mod->name, framenum );
#endif
		framenum = 0;
	}

	if( ( oldframenum >= aliasmodel->numframes ) || ( oldframenum < 0 ) )
	{
#ifndef PUBLIC_BUILD
		ri.Com_DPrintf( "R_AliasModelLerpBBox %s: no such oldframe %d\n", mod->name, oldframenum );
#endif
		oldframenum = 0;
	}

	pframe = aliasmodel->frames + framenum;
	poldframe = aliasmodel->frames + oldframenum;

	// compute axially aligned mins and maxs
	if( pframe == poldframe )
	{
		VectorCopy( pframe->mins, mins );
		VectorCopy( pframe->maxs, maxs );
		if( e->scale == 1 ) {
			return pframe->radius;
		}
	}
	else
	{
		const float
			*thismins = pframe->mins, 
			*oldmins = poldframe->mins, 
			*thismaxs = pframe->maxs, 
			*oldmaxs = poldframe->maxs;

		for( i = 0; i < 3; i++ )
		{
			mins[i] = min( thismins[i], oldmins[i] );
			maxs[i] = max( thismaxs[i], oldmaxs[i] );
		}
	}

	VectorScale( mins, e->scale, mins );
	VectorScale( maxs, e->scale, maxs );
	return RadiusFromBounds( mins, maxs );
}

/*
* R_AliasModelLerpTag
*/
qboolean R_AliasModelLerpTag( orientation_t *orient, const maliasmodel_t *aliasmodel, int oldframenum, int framenum, float lerpfrac, const char *name )
{
	int i;
	quat_t quat;
	maliastag_t *tag, *oldtag;

	// find the appropriate tag
	for( i = 0; i < aliasmodel->numtags; i++ )
	{
		if( !Q_stricmp( aliasmodel->tags[i].name, name ) )
			break;
	}

	if( i == aliasmodel->numtags )
	{
		//ri.Com_DPrintf ("R_AliasModelLerpTag: no such tag %s\n", name );
		return qfalse;
	}

	// ignore invalid frames
	if( ( framenum >= aliasmodel->numframes ) || ( framenum < 0 ) )
	{
#ifndef PUBLIC_BUILD
		ri.Com_DPrintf( "R_AliasModelLerpTag %s: no such oldframe %i\n", name, framenum );
#endif
		framenum = 0;
	}
	if( ( oldframenum >= aliasmodel->numframes ) || ( oldframenum < 0 ) )
	{
#ifndef PUBLIC_BUILD
		ri.Com_DPrintf( "R_AliasModelLerpTag %s: no such oldframe %i\n", name, oldframenum );
#endif
		oldframenum = 0;
	}

	tag = aliasmodel->tags + framenum * aliasmodel->numtags + i;
	oldtag = aliasmodel->tags + oldframenum * aliasmodel->numtags + i;

	// interpolate axis and origin
	Quat_Lerp( oldtag->quat, tag->quat, lerpfrac, quat );
	Quat_ToMatrix3( quat, orient->axis );

	orient->origin[0] = oldtag->origin[0] + ( tag->origin[0] - oldtag->origin[0] ) * lerpfrac;
	orient->origin[1] = oldtag->origin[1] + ( tag->origin[1] - oldtag->origin[1] ) * lerpfrac;
	orient->origin[2] = oldtag->origin[2] + ( tag->origin[2] - oldtag->origin[2] ) * lerpfrac;

	return qtrue;
}

/*
* R_DrawAliasSurf
* 
* Interpolates between two frames and origins
*/
qboolean R_DrawAliasSurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, drawSurfaceAlias_t *drawSurf )
{
	int i;
	int framenum, oldframenum;
	float backv[3], frontv[3];
	vec3_t normal, oldnormal;
	qboolean calcVerts, calcNormals, calcSTVectors;
	vec3_t move;
	const maliasframe_t *frame, *oldframe;
	const maliasvertex_t *v, *ov;
	float backlerp = e->backlerp;
	const maliasmodel_t *model = ( const maliasmodel_t * )drawSurf->model->extradata;
	const maliasmesh_t *aliasmesh = drawSurf->mesh;
	vattribmask_t vattribs;

	// see what vertex attribs backend needs
	vattribs = RB_GetVertexAttribs();

	framenum = bound( e->frame, 0, model->numframes );
	oldframenum = bound( e->oldframe, 0, model->numframes );

	frame = model->frames + framenum;
	oldframe = model->frames + oldframenum;
	for( i = 0; i < 3; i++ )
		move[i] = frame->translate[i] + ( oldframe->translate[i] - frame->translate[i] ) * backlerp;

	// based on backend's needs
	calcNormals = calcSTVectors = qfalse;
	calcNormals = ( ( vattribs & VATTRIB_NORMAL_BIT ) != 0 ) && ( ( framenum != 0 ) || ( oldframenum != 0 ) );
	calcSTVectors = ( ( vattribs & VATTRIB_SVECTOR_BIT ) != 0 ) && calcNormals;

	if( aliasmesh->vbo != NULL && !framenum && !oldframenum )
	{
		RB_BindVBO( aliasmesh->vbo->index, GL_TRIANGLES );

		RB_DrawElements( 0, aliasmesh->numverts, 0, aliasmesh->numtris * 3 );
	}
	else
	{
		mesh_t *rb_mesh;
		vec4_t *inVertsArray;
		vec4_t *inNormalsArray;
		vec4_t *inSVectorsArray;

		RB_BindVBO( RB_VBO_STREAM, GL_TRIANGLES );

		rb_mesh = RB_MapBatchMesh( aliasmesh->numverts, aliasmesh->numtris * 3 );
		if( !rb_mesh ) {
			ri.Com_DPrintf( S_COLOR_YELLOW "R_DrawAliasSurf: RB_MapBatchMesh returned NULL for (%s)(%s)", 
				drawSurf->model->name, aliasmesh->name );
			return qfalse;
		}

		inVertsArray = rb_mesh->xyzArray;
		inNormalsArray = rb_mesh->normalsArray;
		inSVectorsArray = rb_mesh->sVectorsArray;

		if( !framenum && !oldframenum )
		{
			calcVerts = qfalse;

			if( calcNormals )
			{
				v = aliasmesh->vertexes;
				for( i = 0; i < aliasmesh->numverts; i++, v++ )
					R_LatLongToNorm( v->latlong, inNormalsArray[i] );
			}
		}
		else if( framenum == oldframenum )
		{
			calcVerts = qtrue;

			for( i = 0; i < 3; i++ )
				frontv[i] = frame->scale[i];

			v = aliasmesh->vertexes + framenum * aliasmesh->numverts;
			for( i = 0; i < aliasmesh->numverts; i++, v++ )
			{
				Vector4Set( inVertsArray[i],
					move[0] + v->point[0]*frontv[0],
					move[1] + v->point[1]*frontv[1],
					move[2] + v->point[2]*frontv[2],
					1);

				if( calcNormals )
					R_LatLongToNorm4( v->latlong, inNormalsArray[i] );
			}
		}
		else
		{
			calcVerts = qtrue;

			for( i = 0; i < 3; i++ )
			{
				backv[i] = backlerp * oldframe->scale[i];
				frontv[i] = ( 1.0f - backlerp ) * frame->scale[i];
			}

			v = aliasmesh->vertexes + framenum * aliasmesh->numverts;
			ov = aliasmesh->vertexes + oldframenum * aliasmesh->numverts;
			for( i = 0; i < aliasmesh->numverts; i++, v++, ov++ )
			{
				VectorSet( inVertsArray[i],
					move[0] + v->point[0]*frontv[0] + ov->point[0]*backv[0],
					move[1] + v->point[1]*frontv[1] + ov->point[1]*backv[1],
					move[2] + v->point[2]*frontv[2] + ov->point[2]*backv[2] );

				if( calcNormals )
				{
					R_LatLongToNorm( v->latlong, normal );
					R_LatLongToNorm( ov->latlong, oldnormal );

					VectorSet( inNormalsArray[i],
						normal[0] + ( oldnormal[0] - normal[0] ) * backlerp,
						normal[1] + ( oldnormal[1] - normal[1] ) * backlerp,
						normal[2] + ( oldnormal[2] - normal[2] ) * backlerp );
				}
			}
		}

		if( calcSTVectors )
			R_BuildTangentVectors( aliasmesh->numverts, inVertsArray, inNormalsArray, aliasmesh->stArray, aliasmesh->numtris, aliasmesh->elems, inSVectorsArray );

		if( !calcVerts ) {
			rb_mesh->xyzArray = aliasmesh->xyzArray;
		}
		rb_mesh->elems = aliasmesh->elems;
		rb_mesh->numElems = aliasmesh->numtris * 3;
		rb_mesh->numVerts = aliasmesh->numverts;

		rb_mesh->stArray = aliasmesh->stArray;
		if( !calcNormals ) {
			rb_mesh->normalsArray = aliasmesh->normalsArray;
		}
		if( !calcSTVectors ) {
			rb_mesh->sVectorsArray = aliasmesh->sVectorsArray;
		}

		RB_UploadMesh( rb_mesh );

		RB_EndBatch();
	}

	return qfalse;
}

/*
* R_AliasModelBBox
*/
float R_AliasModelBBox( const entity_t *e, vec3_t mins, vec3_t maxs )
{
	const model_t *mod;

	mod = R_AliasModelLOD( e );
	if( !mod )
		return 0;

	return R_AliasModelLerpBBox( e, mod, mins, maxs );
}

/*
* R_AliasModelFrameBounds
*/
void R_AliasModelFrameBounds( const model_t *mod, int frame, vec3_t mins, vec3_t maxs )
{
	const maliasframe_t *pframe;
	const maliasmodel_t *aliasmodel = ( const maliasmodel_t * )mod->extradata;

	if( !aliasmodel->nummeshes )
	{
		ClearBounds( mins, maxs );
		return;
	}

	if( ( frame >= (int)aliasmodel->numframes ) || ( frame < 0 ) )
	{
#ifndef PUBLIC_BUILD
		ri.Com_DPrintf( "R_SkeletalModelFrameBounds %s: no such frame %d\n", mod->name, frame );
#endif
		ClearBounds( mins, maxs );
		return;
	}

	pframe = aliasmodel->frames + frame;
	VectorCopy( pframe->mins, mins );
	VectorCopy( pframe->maxs, maxs );
}

/*
* R_AddAliasModelToDrawList
*
* Returns true if the entity is added to draw list
*/
qboolean R_AddAliasModelToDrawList( const entity_t *e )
{
	int i, j;
	const model_t *mod;
	const maliasmodel_t *aliasmodel;
	const mfog_t *fog;
	const shader_t *shader;
	const maliasmesh_t *mesh;
	vec3_t mins, maxs;
	float radius;
	float distance;
	int clipped;

	mod = R_AliasModelLOD( e );
	if( !( aliasmodel = ( ( const maliasmodel_t * )mod->extradata ) ) || !aliasmodel->nummeshes )
		return qfalse;

	radius = R_AliasModelLerpBBox( e, mod, mins, maxs );
	clipped = R_CullModelEntity( e, mins, maxs, radius, qtrue );
	if( clipped )
		return qfalse;

	// never render weapon models or non-occluders into shadowmaps
	if( rn.params & RP_SHADOWMAPVIEW ) {
		if( e->renderfx & RF_WEAPONMODEL ) {
			return qtrue;
		}
		if( rsc.entShadowGroups[R_ENT2NUM(e)] != rn.shadowGroup->id ) {
			return qtrue;
		}
	}

	// make sure weapon model is always closest to the viewer
	if( e->renderfx & RF_WEAPONMODEL ) {
		distance = 0;
	}
	else {
		distance = Distance( e->origin, rn.viewOrigin ) + 1;
	}

	fog = R_FogForSphere( e->origin, radius );
#if 0
	if( !( e->flags & RF_WEAPONMODEL ) && fog )
	{
		R_AliasModelLerpBBox( e, mod );
		if( R_CompletelyFogged( fog, e->origin, radius ) )
			return qfalse;
	}
#endif

	for( i = 0, mesh = aliasmodel->meshes; i < aliasmodel->nummeshes; i++, mesh++ )
	{
		shader = NULL;

		if( e->customSkin ) {
			shader = R_FindShaderForSkinFile( e->customSkin, mesh->name );
		} else if( e->customShader ) {
			shader = e->customShader;
		} else if( mesh->numskins ) 	{
			for( j = 0; j < mesh->numskins; j++ ) {
				shader = mesh->skins[j].shader;
				if( shader ) {
					R_AddDSurfToDrawList( e, fog, shader, distance, 0, NULL, aliasmodel->drawSurfs + i );
				}
			}
			continue;
		}

		if( shader ) {
			R_AddDSurfToDrawList( e, fog, shader, distance, 0, NULL, aliasmodel->drawSurfs + i );
		}
	}

	return qtrue;
}
