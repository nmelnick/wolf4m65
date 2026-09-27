// WL_DRAW.C

#include "wl_def.h"
#ifdef MEGA65
#include "m65_surf.h"
#include "m65_video.h"
#endif
#pragma hdrstop

#include "wl_cloudsky.h"
#include "wl_atmos.h"
#include "wl_shade.h"

/*
=============================================================================

                               LOCAL CONSTANTS

=============================================================================
*/

// the door is the last picture before the sprites
#define DOORWALL        (PMSpriteStart-8)

#define ACTORSIZE       0x4000

/*
=============================================================================

                              GLOBAL VARIABLES

=============================================================================
*/

static byte *vbuf = NULL;
unsigned vbufPitch = 0;

int32_t    lasttimecount;
int32_t    frameon;
#ifdef MEGA65
boolean fpscounter = true;      // (for now: to see the speed on the hardware)
#else
boolean fpscounter;
#endif

int fps_frames=0, fps_time=0, fps=0;

#ifdef MEGA65
FarArray<int, M65_SCREEN_W> wallheight;       // (storage: VL_SetVGAPlaneMode)
#else
int *wallheight;
#endif
int min_wallheight;

//
// math tables
//
#ifdef MEGA65
FarArray<short, M65_SCREEN_W> pixelangle;
#else
short *pixelangle;
#endif
#ifdef MEGA65
FarArray<int32_t, FINEANGLES/4> finetangent;       // set up by BuildTables
FarArray<fixed, ANGLES+ANGLES/4> sintable;
FarArray<fixed, ANGLES> costable;
#else
int32_t finetangent[FINEANGLES/4];
fixed sintable[ANGLES+ANGLES/4];
fixed *costable = sintable+(ANGLES/4);
#endif

//
// refresh variables
//
fixed   viewx,viewy;                    // the focal point
short   viewangle;
fixed   viewsin,viewcos;

void    TransformActor (objtype *ob);
void    BuildTables (void);
void    ClearScreen (void);
int     CalcRotate (objtype *ob);
void    DrawScaleds (void);
void    CalcTics (void);
void    ThreeDRefresh (void);
#if defined(MEGA65) || defined(FRAMEDUMP)
extern "C" void FrameDumpHook (void);
#ifdef MEGA65
extern "C" void GameFrameHook (void);
#endif
#endif



//
// wall optimization variables
//
int     lastside;               // true for vertical
int32_t    lastintercept;
int     lasttilehit;
int     lasttexture;

//
// ray tracing variables
//
short    focaltx,focalty,viewtx,viewty;
longword xpartialup,xpartialdown,ypartialup,ypartialdown;

short   midangle,angle;

word    tilehit;
int     pixx;

short   xtile,ytile;
short   xtilestep,ytilestep;
int32_t    xintercept,yintercept;
word    xstep,ystep;
word    xspot,yspot;
int     texdelta;

word horizwall[MAXWALLTILES],vertwall[MAXWALLTILES];


/*
============================================================================

                           3 - D  DEFINITIONS

============================================================================
*/

/*
========================
=
= TransformActor
=
= Takes paramaters:
=   gx,gy               : globalx/globaly of point
=
= globals:
=   viewx,viewy         : point of view
=   viewcos,viewsin     : sin/cos of viewangle
=   scale               : conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
========================
*/


//
// transform actor
//
void TransformActor (objtype *ob)
{
    fixed gx,gy,gxt,gyt,nx,ny;

//
// translate point to view centered coordinates
//
    gx = ob->x-viewx;
    gy = ob->y-viewy;

//
// calculate newx
//
    gxt = FixedMul(gx,viewcos);
    gyt = FixedMul(gy,viewsin);
    nx = gxt-gyt-ACTORSIZE;         // fudge the shape forward a bit, because
                                    // the midpoint could put parts of the shape
                                    // into an adjacent wall

//
// calculate newy
//
    gxt = FixedMul(gx,viewsin);
    gyt = FixedMul(gy,viewcos);
    ny = gyt+gxt;

//
// calculate perspective ratio
//
    ob->transx = nx;
    ob->transy = ny;

    if (nx<MINDIST)                 // too close, don't overflow the divide
    {
        ob->viewheight = 0;
        return;
    }

    ob->viewx = (word)(centerx + ny*scale/nx);

//
// calculate height (heightnumerator/(nx>>8))
//
    ob->viewheight = (word)(heightnumerator/(nx>>8));
}

//==========================================================================

/*
========================
=
= TransformTile
=
= Takes paramaters:
=   tx,ty               : tile the object is centered in
=
= globals:
=   viewx,viewy         : point of view
=   viewcos,viewsin     : sin/cos of viewangle
=   scale               : conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
= Returns true if the tile is withing getting distance
=
========================
*/

boolean TransformTile (int tx, int ty, short *dispx, short *dispheight)
{
    fixed gx,gy,gxt,gyt,nx,ny;

//
// translate point to view centered coordinates
//
    gx = ((int32_t)tx<<TILESHIFT)+0x8000-viewx;
    gy = ((int32_t)ty<<TILESHIFT)+0x8000-viewy;

//
// calculate newx
//
    gxt = FixedMul(gx,viewcos);
    gyt = FixedMul(gy,viewsin);
    nx = gxt-gyt-0x2000;            // 0x2000 is size of object

//
// calculate newy
//
    gxt = FixedMul(gx,viewsin);
    gyt = FixedMul(gy,viewcos);
    ny = gyt+gxt;


//
// calculate height / perspective ratio
//
    if (nx<MINDIST)                 // too close, don't overflow the divide
        *dispheight = 0;
    else
    {
        *dispx = (short)(centerx + ny*scale/nx);
        *dispheight = (short)(heightnumerator/(nx>>8));
    }

//
// see if it should be grabbed
//
    if (nx<TILEGLOBAL && ny>-TILEGLOBAL/2 && ny<TILEGLOBAL/2)
        return true;
    else
        return false;
}

//==========================================================================

/*
====================
=
= CalcHeight
=
= Calculates the height of xintercept,yintercept from viewx,viewy
=
====================
*/

#ifdef MEGA65
// n / d straight from the math unit's divider, for drawing: its integer part
// can be one off (m65_hwdiv.c puts that right for the C operators; a texture
// step or texel does not need it), and it takes a few cycles.
static inline uint16_t DivApprox (uint32_t n, uint16_t d)
{
    *(volatile uint32_t *) 0xD770 = n;
    *(volatile uint32_t *) 0xD774 = d;
    while(*(volatile uint8_t *) 0xD70F & 0x80)
        ;
    return *(volatile uint16_t *) 0xD76C;
}
#endif

int CalcHeight()
{
    fixed z = FixedMul(xintercept - viewx, viewcos)
        - FixedMul(yintercept - viewy, viewsin);
    if(z < MINDIST) z = MINDIST;
#ifdef MEGA65
    // (drawing only, so the divider's raw quotient: see DivApprox)
    int height = (int) DivApprox(heightnumerator, (uint16_t) (z >> 8));
#else
    int height = heightnumerator / (z >> 8);
#endif
    if(height < min_wallheight) min_wallheight = height;
    return height;
}

//==========================================================================

/*
===================
=
= ScalePost
=
===================
*/

#ifdef MEGA65
// The texture column is in far memory (VSWAP in attic RAM).
farptr postsource;
#define POSTSOURCE_ADD(p, n) FAR_ADD(p, n)
#else
byte *postsource;
#define POSTSOURCE_ADD(p, n) ((p) + (n))
#endif
int postx;
int postwidth;

#ifdef MEGA65

//
// Far address of pixel (x, y) of the 3D view in screenBuffer.
//
// Offset of each 8-pixel strip in the tiled draw buffer: (x >> 3) * 1600,
// without a multiply (the whole buffer is 64000 bytes: 16 bits suffice).
#define STRIP(n) (uint16_t) ((unsigned) (n) * M65_CELLCOL_SIZE)
#define STRIPS8(n) STRIP(n), STRIP(n + 1), STRIP(n + 2), STRIP(n + 3), \
                   STRIP(n + 4), STRIP(n + 5), STRIP(n + 6), STRIP(n + 7)
static const uint16_t stripofs[M65_SCREEN_W / 8] =
    { STRIPS8(0), STRIPS8(8), STRIPS8(16), STRIPS8(24), STRIPS8(32) };

// Far address of view pixel (x, y) in screenBuffer (tiled: m65_video.h);
// surf_addr's result, inline and without its 32-bit multiply.
static inline uint32_t ViewAddr (int x, int y)
{
    x += viewscreenx;
    y += viewscreeny;
    return screenBuffer->farpixels
         + (uint16_t) (stripofs[x >> 3] + ((uint16_t) y << 3) + (x & 7));
}

//
// The same scaling as below, on row indices rather than row*pitch offsets
// (which overflow a 16-bit int for close walls): the texture column is
// fetched with one DMA job, the column is built in a near buffer, and the
// rows drawn are written to the screen with one DMA job (the framebuffer's
// columns are every 8th byte).
//
// The column buffer (mega65/m65_draw.s), for the sprites.
extern "C" byte m65_colbuf[];


//
// A wall column: one DMA copy that scales the texture column as it goes. The
// wall is 2*yd rows high (from viewheight/2 - yd), and its 64 texels run
// from top to bottom, so the source steps 32/yd texels a row (8.8 fixed
// point for the DMA controller). The first visible row's texel is worked
// out exactly; the rest follow the rounded step (under a texel off over a
// column). The original's error-accumulating loop picks texels a little
// differently: close, not identical.
//
void ScalePost()
{
    int yd, walltop, ytop, yend;
    uint16_t step;
    uint32_t first;

    yd = wallheight[postx] >> 3;
    if(yd <= 0) return;                 // (nothing to draw, as the original)

    walltop = viewheight / 2 - yd;
    ytop = walltop < 0 ? 0 : walltop;
    yend = viewheight / 2 + yd - 1;
    if(yend >= viewheight) yend = viewheight - 1;
    if(yend < ytop) return;

    step = DivApprox((uint32_t) TEXTURESIZE / 2 << 8, (uint16_t) yd);
    first = ytop == walltop ? 0      // (not clipped at the top: most columns)
          : DivApprox((uint32_t) (ytop - walltop) * (TEXTURESIZE / 2), (uint16_t) yd);
    m65_dma_scale(ViewAddr(postx, ytop), postsource.a + first,
                  yend - ytop + 1, step, M65_COLUMN_STEP);
}

#else

void ScalePost()
{
    int ywcount, yoffs, yw, yd, yendoffs;
    byte col;

#ifdef USE_SHADING
    byte *curshades = shadetable[GetShade(wallheight[postx])];
#endif

    ywcount = yd = wallheight[postx] >> 3;
    if(yd <= 0) yd = 100;

    yoffs = (viewheight / 2 - ywcount) * vbufPitch;
    if(yoffs < 0) yoffs = 0;
    yoffs += postx;

    yendoffs = viewheight / 2 + ywcount - 1;
    yw=TEXTURESIZE-1;

    while(yendoffs >= viewheight)
    {
        ywcount -= TEXTURESIZE/2;
        while(ywcount <= 0)
        {
            ywcount += yd;
            yw--;
        }
        yendoffs--;
    }
    if(yw < 0) return;

#ifdef USE_SHADING
    col = curshades[postsource[yw]];
#else
    col = postsource[yw];
#endif
    yendoffs = yendoffs * vbufPitch + postx;
    while(yoffs <= yendoffs)
    {
        vbuf[yendoffs] = col;
        ywcount -= TEXTURESIZE/2;
        if(ywcount <= 0)
        {
            do
            {
                ywcount += yd;
                yw--;
            }
            while(ywcount <= 0);
            if(yw < 0) break;
#ifdef USE_SHADING
            col = curshades[postsource[yw]];
#else
            col = postsource[yw];
#endif
        }
        yendoffs -= vbufPitch;
    }
}

void GlobalScalePost(byte *vidbuf, unsigned pitch)
{
    vbuf = vidbuf;
    vbufPitch = pitch;
    ScalePost();
}

#endif // MEGA65

/*
====================
=
= HitVertWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitVertWall (void)
{
    int wallpic;
    int texture;

    texture = ((yintercept+texdelta)>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;
    if (xtilestep == -1)
    {
        texture = TEXTUREMASK-texture;
        xintercept += TILEGLOBAL;
    }

    if(lastside==1 && lastintercept==xtile && lasttilehit==tilehit && !(lasttilehit & 0x40))
    {
        if((pixx&3) && texture == lasttexture)
        {
            ScalePost();
            postx = pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost();
        wallheight[pixx] = CalcHeight();
        postsource=POSTSOURCE_ADD(postsource,texture-lasttexture);
        postwidth=1;
        postx=pixx;
        lasttexture=texture;
        return;
    }

    if(lastside!=-1) ScalePost();

    lastside=1;
    lastintercept=xtile;
    lasttilehit=tilehit;
    lasttexture=texture;
    wallheight[pixx] = CalcHeight();
    postx = pixx;
    postwidth = 1;

    if (tilehit & 0x40)
    {                                                               // check for adjacent doors
        ytile = (short)(yintercept>>TILESHIFT);
        if ( tilemap[xtile-xtilestep][ytile]&0x80 )
            wallpic = DOORWALL+3;
        else
            wallpic = vertwall[tilehit & ~0x40];
    }
    else
        wallpic = vertwall[tilehit];

    postsource = POSTSOURCE_ADD(PM_GetTexture(wallpic), texture);
}


/*
====================
=
= HitHorizWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitHorizWall (void)
{
    int wallpic;
    int texture;

    texture = ((xintercept+texdelta)>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;
    if (ytilestep == -1)
        yintercept += TILEGLOBAL;
    else
        texture = TEXTUREMASK-texture;

    if(lastside==0 && lastintercept==ytile && lasttilehit==tilehit && !(lasttilehit & 0x40))
    {
        if((pixx&3) && texture == lasttexture)
        {
            ScalePost();
            postx=pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost();
        wallheight[pixx] = CalcHeight();
        postsource=POSTSOURCE_ADD(postsource,texture-lasttexture);
        postwidth=1;
        postx=pixx;
        lasttexture=texture;
        return;
    }

    if(lastside!=-1) ScalePost();

    lastside=0;
    lastintercept=ytile;
    lasttilehit=tilehit;
    lasttexture=texture;
    wallheight[pixx] = CalcHeight();
    postx = pixx;
    postwidth = 1;

    if (tilehit & 0x40)
    {                                                               // check for adjacent doors
        xtile = (short)(xintercept>>TILESHIFT);
        if ( tilemap[xtile][ytile-ytilestep]&0x80)
            wallpic = DOORWALL+2;
        else
            wallpic = horizwall[tilehit & ~0x40];
    }
    else
        wallpic = horizwall[tilehit];

    postsource = POSTSOURCE_ADD(PM_GetTexture(wallpic), texture);
}

//==========================================================================

/*
====================
=
= HitHorizDoor
=
====================
*/

void HitHorizDoor (void)
{
    int doorpage;
    int doornum;
    int texture;

    doornum = tilehit&0x7f;
    texture = ((xintercept-doorposition[doornum])>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;

    if(lasttilehit==tilehit)
    {
        if((pixx&3) && texture == lasttexture)
        {
            ScalePost();
            postx=pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost();
        wallheight[pixx] = CalcHeight();
        postsource=POSTSOURCE_ADD(postsource,texture-lasttexture);
        postwidth=1;
        postx=pixx;
        lasttexture=texture;
        return;
    }

    if(lastside!=-1) ScalePost();

    lastside=2;
    lasttilehit=tilehit;
    lasttexture=texture;
    wallheight[pixx] = CalcHeight();
    postx = pixx;
    postwidth = 1;

    switch(doorobjlist[doornum].lock)
    {
        case dr_normal:
            doorpage = DOORWALL;
            break;
        case dr_lock1:
        case dr_lock2:
        case dr_lock3:
        case dr_lock4:
            doorpage = DOORWALL+6;
            break;
        case dr_elevator:
            doorpage = DOORWALL+4;
            break;
    }

    postsource = POSTSOURCE_ADD(PM_GetTexture(doorpage), texture);
}

//==========================================================================

/*
====================
=
= HitVertDoor
=
====================
*/

void HitVertDoor (void)
{
    int doorpage;
    int doornum;
    int texture;

    doornum = tilehit&0x7f;
    texture = ((yintercept-doorposition[doornum])>>TEXTUREFROMFIXEDSHIFT)&TEXTUREMASK;

    if(lasttilehit==tilehit)
    {
        if((pixx&3) && texture == lasttexture)
        {
            ScalePost();
            postx=pixx;
            wallheight[pixx] = wallheight[pixx-1];
            return;
        }
        ScalePost();
        wallheight[pixx] = CalcHeight();
        postsource=POSTSOURCE_ADD(postsource,texture-lasttexture);
        postwidth=1;
        postx=pixx;
        lasttexture=texture;
        return;
    }

    if(lastside!=-1) ScalePost();

    lastside=2;
    lasttilehit=tilehit;
    lasttexture=texture;
    wallheight[pixx] = CalcHeight();
    postx = pixx;
    postwidth = 1;

    switch(doorobjlist[doornum].lock)
    {
        case dr_normal:
            doorpage = DOORWALL+1;
            break;
        case dr_lock1:
        case dr_lock2:
        case dr_lock3:
        case dr_lock4:
            doorpage = DOORWALL+7;
            break;
        case dr_elevator:
            doorpage = DOORWALL+5;
            break;
    }

    postsource = POSTSOURCE_ADD(PM_GetTexture(doorpage), texture);
}

//==========================================================================

#define HitHorizBorder HitHorizWall
#define HitVertBorder HitVertWall

//==========================================================================

byte vgaCeiling[]=
{
#ifndef SPEAR
 0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0xbf,
 0x4e,0x4e,0x4e,0x1d,0x8d,0x4e,0x1d,0x2d,0x1d,0x8d,
 0x1d,0x1d,0x1d,0x1d,0x1d,0x2d,0xdd,0x1d,0x1d,0x98,

 0x1d,0x9d,0x2d,0xdd,0xdd,0x9d,0x2d,0x4d,0x1d,0xdd,
 0x7d,0x1d,0x2d,0x2d,0xdd,0xd7,0x1d,0x1d,0x1d,0x2d,
 0x1d,0x1d,0x1d,0x1d,0xdd,0xdd,0x7d,0xdd,0xdd,0xdd
#else
 0x6f,0x4f,0x1d,0xde,0xdf,0x2e,0x7f,0x9e,0xae,0x7f,
 0x1d,0xde,0xdf,0xde,0xdf,0xde,0xe1,0xdc,0x2e,0x1d,0xdc
#endif
};

/*
=====================
=
= VGAClearScreen
=
=====================
*/

void VGAClearScreen (void)
{
    byte ceiling=vgaCeiling[gamestate.episode*10+mapon];

#ifdef MEGA65
    surf_fill_rect(screenBuffer, viewscreenx, viewscreeny, viewwidth, viewheight / 2, ceiling);
    surf_fill_rect(screenBuffer, viewscreenx, viewscreeny + viewheight / 2,
                   viewwidth, viewheight - viewheight / 2, 0x19);
#else
    int y;
    byte *ptr = vbuf;
#ifdef USE_SHADING
    for(y = 0; y < viewheight / 2; y++, ptr += vbufPitch)
        memset(ptr, shadetable[GetShade((viewheight / 2 - y) << 3)][ceiling], viewwidth);
    for(; y < viewheight; y++, ptr += vbufPitch)
        memset(ptr, shadetable[GetShade((y - viewheight / 2) << 3)][0x19], viewwidth);
#else
    for(y = 0; y < viewheight / 2; y++, ptr += vbufPitch)
        memset(ptr, ceiling, viewwidth);
    for(; y < viewheight; y++, ptr += vbufPitch)
        memset(ptr, 0x19, viewwidth);
#endif
#endif
}

//==========================================================================

/*
=====================
=
= CalcRotate
=
=====================
*/

int CalcRotate (objtype *ob)
{
    int angle, viewangle;

    // this isn't exactly correct, as it should vary by a trig value,
    // but it is close enough with only eight rotations

    viewangle = player->angle + (centerx - ob->viewx)/8;

    if (ob->obclass == rocketobj || ob->obclass == hrocketobj)
        angle = (viewangle-180) - ob->angle;
    else
        angle = (viewangle-180) - dirangle[ob->dir];

    angle+=ANGLES/16;
    while (angle>=ANGLES)
        angle-=ANGLES;
    while (angle<0)
        angle+=ANGLES;

    if (ob->state->rotate == 2)             // 2 rotation pain frame
        return 0;               // pain with shooting frame bugfix

    return angle/(ANGLES/8);
}

#ifdef MEGA65

//
// ScaleShape and SimpleScaleShape on the MEGA65: the sprite (a t_compshape:
// leftpix, rightpix, dataofs[], then posts and texels) is read in place from
// far memory. Each post (run of opaque texels) of a texture column is drawn
// with one DMA copy that scales it as it goes, per screen column the texture
// column covers; the original's spans, approximately. pixcnt is 32-bit,
// since i * pixheight overflows a 16-bit int for close sprites.
//
static void ScaleShapeFar (int xcenter, int shapenum, unsigned scale,
                           unsigned height, bool clipwalls)
{
    enum { MAXSEGS = 16 };
    uint8_t segtop[MAXSEGS], segcount[MAXSEGS], nsegs, k;
    uint32_t segsrc[MAXSEGS];
    farptr shape = PM_GetSprite(shapenum);
    farptr cmdptr, line;
    uint32_t pixheight;
    int32_t pixcnt;
    unsigned starty, endy, leftpix, rightpix;
    int actx, i, upperedge;
    int16_t newstart;
    int r0, r1, top, bot, lpix, rpix;
    uint16_t step;

    pixheight = (uint32_t) scale * SPRITESCALEFACTOR;
    actx = xcenter - scale;
    upperedge = viewheight / 2 - scale;
    // texels per row, 8.8 fixed point (a texel is pixheight/64 rows high)
    step = DivApprox((uint32_t) 64 << 8, (uint16_t) pixheight);

    leftpix = far_peekw(shape);
    rightpix = far_peekw(FAR_ADD(shape, 2));
    cmdptr = FAR_ADD(shape, 4);                     // dataofs[0]

    for(i=leftpix,pixcnt=(int32_t)i*pixheight,rpix=(pixcnt>>6)+actx;i<=(int)rightpix;i++,cmdptr=FAR_ADD(cmdptr,2))
    {
        lpix=rpix;
        if(lpix>=viewwidth) break;
        pixcnt+=pixheight;
        rpix=(pixcnt>>6)+actx;
        if(lpix!=rpix && rpix>0)
        {
            if(lpix<0) lpix=0;
            if(rpix>viewwidth) rpix=viewwidth,i=rightpix+1;

            //
            // The posts (runs of opaque texels) of this texture column: texel
            // j covers rows (j * pixheight >> 6) + upperedge up to the next
            // texel's, so a post is one run of rows, drawn with one scaled
            // DMA copy per screen column (below). The first visible row's
            // texel is exact; the rest follow the rounded step.
            //
            nsegs = 0;
            line = FAR_ADD(shape, far_peekw(cmdptr));
            while((endy = far_peekw(line)) != 0)
            {
                endy >>= 1;
                newstart = (int16_t) far_peekw(FAR_ADD(line, 2));
                starty = far_peekw(FAR_ADD(line, 4)) >> 1;
                line = FAR_ADD(line, 6);
                r0 = (int) (((int32_t) starty * pixheight) >> 6) + upperedge;
                r1 = (int) (((int32_t) endy * pixheight) >> 6) + upperedge;
                top = r0 < 0 ? 0 : r0;
                bot = r1 > viewheight ? viewheight : r1;
                if(top < bot && nsegs < MAXSEGS)
                {
                    segtop[nsegs] = (uint8_t) top;
                    segcount[nsegs] = (uint8_t) (bot - top);
                    segsrc[nsegs] = shape.a + (int32_t) newstart + starty;
                    if(top != r0)       // (clipped at the top)
                        segsrc[nsegs] += DivApprox((uint32_t) (top - r0) << 6, (uint16_t) pixheight);
                    nsegs++;
                }
            }

            for(; lpix<rpix; lpix++)
            {
                if(clipwalls && wallheight[lpix]>(int)height)
                    continue;
                for(k = 0; k < nsegs; k++)
                    m65_dma_scale(ViewAddr(lpix, segtop[k]), segsrc[k], segcount[k],
                                  step, M65_COLUMN_STEP);
            }
        }
    }
}

void ScaleShape (int xcenter, int shapenum, unsigned height, uint32_t flags)
{
    unsigned scale=height>>3;       // low three bits are fractional
    (void) flags;
    if(!scale) return;              // too close or far away
    ScaleShapeFar(xcenter, shapenum, scale, height, true);
}

void SimpleScaleShape (int xcenter, int shapenum, unsigned height)
{
    ScaleShapeFar(xcenter, shapenum, height>>1, height, false);
}

#else

void ScaleShape (int xcenter, int shapenum, unsigned height, uint32_t flags)
{
    t_compshape *shape;
    unsigned scale,pixheight;
    unsigned starty,endy;
    word *cmdptr;
    byte *cline;
    byte *line;
    byte *vmem;
    int actx,i,upperedge;
    short newstart;
    int scrstarty,screndy,lpix,rpix,pixcnt,ycnt;
    unsigned j;
    byte col;

#ifdef USE_SHADING
    byte *curshades;
    if(flags & FL_FULLBRIGHT)
        curshades = shadetable[0];
    else
        curshades = shadetable[GetShade(height)];
#endif

    shape = (t_compshape *) PM_GetSprite(shapenum);

    scale=height>>3;                 // low three bits are fractional
    if(!scale) return;   // too close or far away

    pixheight=scale*SPRITESCALEFACTOR;
    actx=xcenter-scale;
    upperedge=viewheight/2-scale;

    cmdptr=(word *) shape->dataofs;

    for(i=shape->leftpix,pixcnt=i*pixheight,rpix=(pixcnt>>6)+actx;i<=shape->rightpix;i++,cmdptr++)
    {
        lpix=rpix;
        if(lpix>=viewwidth) break;
        pixcnt+=pixheight;
        rpix=(pixcnt>>6)+actx;
        if(lpix!=rpix && rpix>0)
        {
            if(lpix<0) lpix=0;
            if(rpix>viewwidth) rpix=viewwidth,i=shape->rightpix+1;
            cline=(byte *)shape + *cmdptr;
            while(lpix<rpix)
            {
                if(wallheight[lpix]<=(int)height)
                {
                    line=cline;
                    while((endy = READWORD(line)) != 0)
                    {
                        endy >>= 1;
                        newstart = READWORD(line);
                        starty = READWORD(line) >> 1;
                        j=starty;
                        ycnt=j*pixheight;
                        screndy=(ycnt>>6)+upperedge;
                        if(screndy<0) vmem=vbuf+lpix;
                        else vmem=vbuf+screndy*vbufPitch+lpix;
                        for(;j<endy;j++)
                        {
                            scrstarty=screndy;
                            ycnt+=pixheight;
                            screndy=(ycnt>>6)+upperedge;
                            if(scrstarty!=screndy && screndy>0)
                            {
#ifdef USE_SHADING
                                col=curshades[((byte *)shape)[newstart+j]];
#else
                                col=((byte *)shape)[newstart+j];
#endif
                                if(scrstarty<0) scrstarty=0;
                                if(screndy>viewheight) screndy=viewheight,j=endy;

                                while(scrstarty<screndy)
                                {
                                    *vmem=col;
                                    vmem+=vbufPitch;
                                    scrstarty++;
                                }
                            }
                        }
                    }
                }
                lpix++;
            }
        }
    }
}

void SimpleScaleShape (int xcenter, int shapenum, unsigned height)
{
    t_compshape   *shape;
    unsigned scale,pixheight;
    unsigned starty,endy;
    word *cmdptr;
    byte *cline;
    byte *line;
    int actx,i,upperedge;
    short newstart;
    int scrstarty,screndy,lpix,rpix,pixcnt,ycnt;
    unsigned j;
    byte col;
    byte *vmem;

    shape = (t_compshape *) PM_GetSprite(shapenum);

    scale=height>>1;
    pixheight=scale*SPRITESCALEFACTOR;
    actx=xcenter-scale;
    upperedge=viewheight/2-scale;

    cmdptr=shape->dataofs;

    for(i=shape->leftpix,pixcnt=i*pixheight,rpix=(pixcnt>>6)+actx;i<=shape->rightpix;i++,cmdptr++)
    {
        lpix=rpix;
        if(lpix>=viewwidth) break;
        pixcnt+=pixheight;
        rpix=(pixcnt>>6)+actx;
        if(lpix!=rpix && rpix>0)
        {
            if(lpix<0) lpix=0;
            if(rpix>viewwidth) rpix=viewwidth,i=shape->rightpix+1;
            cline = (byte *)shape + *cmdptr;
            while(lpix<rpix)
            {
                line=cline;
                while((endy = READWORD(line)) != 0)
                {
                    endy >>= 1;
                    newstart = READWORD(line);
                    starty = READWORD(line) >> 1;
                    j=starty;
                    ycnt=j*pixheight;
                    screndy=(ycnt>>6)+upperedge;
                    if(screndy<0) vmem=vbuf+lpix;
                    else vmem=vbuf+screndy*vbufPitch+lpix;
                    for(;j<endy;j++)
                    {
                        scrstarty=screndy;
                        ycnt+=pixheight;
                        screndy=(ycnt>>6)+upperedge;
                        if(scrstarty!=screndy && screndy>0)
                        {
                            col=((byte *)shape)[newstart+j];
                            if(scrstarty<0) scrstarty=0;
                            if(screndy>viewheight) screndy=viewheight,j=endy;

                            while(scrstarty<screndy)
                            {
                                *vmem=col;
                                vmem+=vbufPitch;
                                scrstarty++;
                            }
                        }
                    }
                }
                lpix++;
            }
        }
    }
}

#endif // MEGA65

/*
=====================
=
= DrawScaleds
=
= Draws all objects that are visable
=
=====================
*/

#ifdef MEGA65
// Objects visible in one frame; DrawScaleds drops any beyond this (as the
// original does at 250). Saves 1KB of near memory.
#define MAXVISABLE 128
#else
#define MAXVISABLE 250
#endif

typedef struct
{
    short      viewx,
               viewheight,
               shapenum;
    short      flags;          // this must be changed to uint32_t, when you
                               // you need more than 16-flags for drawing
#ifdef USE_DIR3DSPR
    statobjptr transsprite;
#endif
} visobj_t;

visobj_t vislist[MAXVISABLE];
visobj_t *visptr,*visstep,*farthest;

void DrawScaleds (void)
{
    int      i,least,numvisable,height;
    spotvisptr visspot;
#ifdef MEGA65
    FarByteGrid<0, MAPSIZE>::Ptr tilespot;
#else
    byte     *tilespot;
#endif
    unsigned spotloc;

    statobjptr statptr;
    objtype   *obj;

    visptr = &vislist[0];

//
// place static objects
//
    for (statptr = &statobjlist[0] ; statptr !=laststatobj ; statptr++)
    {
        if ((visptr->shapenum = statptr->shapenum) == -1)
            continue;                                               // object has been deleted

        if (!*statptr->visspot)
            continue;                                               // not visable

        if (TransformTile (statptr->tilex,statptr->tiley,
            &visptr->viewx,&visptr->viewheight) && statptr->flags & FL_BONUS)
        {
            GetBonus (statptr);
            if(statptr->shapenum == -1)
                continue;                                           // object has been taken
        }

        if (!visptr->viewheight)
            continue;                                               // to close to the object

#ifdef USE_DIR3DSPR
        if(statptr->flags & FL_DIR_MASK)
            visptr->transsprite=statptr;
        else
            visptr->transsprite=NULL;
#endif

        if (visptr < &vislist[MAXVISABLE-1])    // don't let it overflow
        {
            visptr->flags = (short) statptr->flags;
            visptr++;
        }
    }

//
// place active objects
//
    for (obj = player->next;obj;obj=obj->next)
    {
        if ((visptr->shapenum = obj->state->shapenum)==0)
            continue;                                               // no shape

        spotloc = (obj->tilex<<mapshift)+obj->tiley;   // optimize: keep in struct?
        visspot = SPOTVIS_FLAT+spotloc;
        tilespot = TILEMAP_FLAT+spotloc;

        //
        // could be in any of the nine surrounding tiles
        //
        if (*visspot
            || ( *(visspot-1) && !*(tilespot-1) )
            || ( *(visspot+1) && !*(tilespot+1) )
            || ( *(visspot-65) && !*(tilespot-65) )
            || ( *(visspot-64) && !*(tilespot-64) )
            || ( *(visspot-63) && !*(tilespot-63) )
            || ( *(visspot+65) && !*(tilespot+65) )
            || ( *(visspot+64) && !*(tilespot+64) )
            || ( *(visspot+63) && !*(tilespot+63) ) )
        {
            obj->active = ac_yes;
            TransformActor (obj);
            if (!obj->viewheight)
                continue;                                               // too close or far away

            visptr->viewx = obj->viewx;
            visptr->viewheight = obj->viewheight;
            if (visptr->shapenum == -1)
                visptr->shapenum = obj->temp1;  // special shape

            if (obj->state->rotate)
                visptr->shapenum += CalcRotate (obj);

            if (visptr < &vislist[MAXVISABLE-1])    // don't let it overflow
            {
                visptr->flags = (short) obj->flags;
#ifdef USE_DIR3DSPR
                visptr->transsprite = NULL;
#endif
                visptr++;
            }
            obj->flags |= FL_VISABLE;
        }
        else
            obj->flags &= ~FL_VISABLE;
    }

//
// draw from back to front
//
    numvisable = (int) (visptr-&vislist[0]);

    if (!numvisable)
        return;                                                                 // no visable objects

    for (i = 0; i<numvisable; i++)
    {
        least = 32000;
        for (visstep=&vislist[0] ; visstep<visptr ; visstep++)
        {
            height = visstep->viewheight;
            if (height < least)
            {
                least = height;
                farthest = visstep;
            }
        }
        //
        // draw farthest
        //
#ifdef USE_DIR3DSPR
        if(farthest->transsprite)
            Scale3DShape(vbuf, vbufPitch, farthest->transsprite);
        else
#endif
            ScaleShape(farthest->viewx, farthest->shapenum, farthest->viewheight, farthest->flags);

        farthest->viewheight = 32000;
    }
}

//==========================================================================

/*
==============
=
= DrawPlayerWeapon
=
= Draw the player's hands
=
==============
*/

int weaponscale[NUMWEAPONS] = {SPR_KNIFEREADY, SPR_PISTOLREADY,
    SPR_MACHINEGUNREADY, SPR_CHAINREADY};

void DrawPlayerWeapon (void)
{
    int shapenum;

#ifndef SPEAR
    if (gamestate.victoryflag)
    {
#ifndef APOGEE_1_0
        if (player->state == &s_deathcam && (GetTimeCount()&32) )
            SimpleScaleShape(viewwidth/2,SPR_DEATHCAM,viewheight+1);
#endif
        return;
    }
#endif

    if (gamestate.weapon != -1)
    {
        shapenum = weaponscale[gamestate.weapon]+gamestate.weaponframe;
        SimpleScaleShape(viewwidth/2,shapenum,viewheight+1);
    }

    if (demorecord || demoplayback)
        SimpleScaleShape(viewwidth/2,SPR_DEMO,viewheight+1);
}


//==========================================================================


/*
=====================
=
= CalcTics
=
=====================
*/

void CalcTics (void)
{
//
// calculate tics since last refresh for adaptive timing
//
    if (lasttimecount > (int32_t) GetTimeCount())
        lasttimecount = GetTimeCount();    // if the game was paused a LONG time

    uint32_t curtime = SDL_GetTicks();
    tics = (curtime * 7) / 100 - lasttimecount;
    if(!tics)
    {
        // wait until end of current tic
        SDL_Delay(((lasttimecount + 1) * 100) / 7 - curtime);
        tics = 1;
    }

    lasttimecount += tics;

    if (tics>MAXTICS)
        tics = MAXTICS;
}


//==========================================================================

#ifndef M65_NOINLINE
#define M65_NOINLINE
#endif

// A ray meets the moving pushwall (tilehit 64) in AsmRefresh: draws it, or
// returns true if the ray passes (goto passvert there). Kept out of
// AsmRefresh so that it fits a code overlay on the MEGA65.
static M65_NOINLINE bool HitVertPushwall (int32_t xstep, int32_t ystep)
{
    if(pwalldir==di_west || pwalldir==di_east)
    {
        int32_t yintbuf;
        int pwallposnorm;
        int pwallposinv;
        if(pwalldir==di_west)
        {
            pwallposnorm = 64-pwallpos;
            pwallposinv = pwallpos;
        }
        else
        {
            pwallposnorm = pwallpos;
            pwallposinv = 64-pwallpos;
        }
        if(pwalldir == di_east && xtile==pwallx && ((uint32_t)yintercept>>16)==pwally
            || pwalldir == di_west && !(xtile==pwallx && ((uint32_t)yintercept>>16)==pwally))
        {
            yintbuf=yintercept+((ystep*pwallposnorm)>>6);
            if((yintbuf>>16)!=(yintercept>>16))
                return true;

            xintercept=((int32_t)xtile<<TILESHIFT)+TILEGLOBAL-((int32_t)pwallposinv<<10);
            yintercept=yintbuf;
            ytile = (short) (yintercept >> TILESHIFT);
            tilehit=pwalltile;
            HitVertWall();
        }
        else
        {
            yintbuf=yintercept+((ystep*pwallposinv)>>6);
            if((yintbuf>>16)!=(yintercept>>16))
                return true;

            xintercept=((int32_t)xtile<<TILESHIFT)-((int32_t)pwallposinv<<10);
            yintercept=yintbuf;
            ytile = (short) (yintercept >> TILESHIFT);
            tilehit=pwalltile;
            HitVertWall();
        }
    }
    else
    {
        int pwallposi = pwallpos;
        if(pwalldir==di_north) pwallposi = 64-pwallpos;
        if(pwalldir==di_south && (word)yintercept<((int32_t)pwallposi<<10)
            || pwalldir==di_north && (word)yintercept>((int32_t)pwallposi<<10))
        {
            if(((uint32_t)yintercept>>16)==pwally && xtile==pwallx)
            {
                if(pwalldir==di_south && (int32_t)((word)yintercept)+ystep<((int32_t)pwallposi<<10)
                        || pwalldir==di_north && (int32_t)((word)yintercept)+ystep>((int32_t)pwallposi<<10))
                    return true;

                if(pwalldir==di_south)
                    yintercept=(yintercept&0xffff0000)+((int32_t)pwallposi<<10);
                else
                    yintercept=(yintercept&0xffff0000)-TILEGLOBAL+((int32_t)pwallposi<<10);
                xintercept=xintercept-((xstep*(64-pwallpos))>>6);
                xtile = (short) (xintercept >> TILESHIFT);
                tilehit=pwalltile;
                HitHorizWall();
            }
            else
            {
                texdelta = -((int32_t)pwallposi<<10);
                xintercept=((int32_t)xtile<<TILESHIFT);
                ytile = (short) (yintercept >> TILESHIFT);
                tilehit=pwalltile;
                HitVertWall();
            }
        }
        else
        {
            if(((uint32_t)yintercept>>16)==pwally && xtile==pwallx)
            {
                texdelta = -((int32_t)pwallposi<<10);
                xintercept=((int32_t)xtile<<TILESHIFT);
                ytile = (short) (yintercept >> TILESHIFT);
                tilehit=pwalltile;
                HitVertWall();
            }
            else
            {
                if(pwalldir==di_south && (int32_t)((word)yintercept)+ystep>((int32_t)pwallposi<<10)
                        || pwalldir==di_north && (int32_t)((word)yintercept)+ystep<((int32_t)pwallposi<<10))
                    return true;

                if(pwalldir==di_south)
                    yintercept=(yintercept&0xffff0000)-((int32_t)(64-pwallpos)<<10);
                else
                    yintercept=(yintercept&0xffff0000)+((int32_t)(64-pwallpos)<<10);
                xintercept=xintercept-((xstep*pwallpos)>>6);
                xtile = (short) (xintercept >> TILESHIFT);
                tilehit=pwalltile;
                HitHorizWall();
            }
        }
    }
    return false;
}

// A ray meets the moving pushwall (tilehit 64) in AsmRefresh: draws it, or
// returns true if the ray passes (goto passhoriz there). Kept out of
// AsmRefresh so that it fits a code overlay on the MEGA65.
static M65_NOINLINE bool HitHorizPushwall (int32_t xstep, int32_t ystep)
{
    if(pwalldir==di_north || pwalldir==di_south)
    {
        int32_t xintbuf;
        int pwallposnorm;
        int pwallposinv;
        if(pwalldir==di_north)
        {
            pwallposnorm = 64-pwallpos;
            pwallposinv = pwallpos;
        }
        else
        {
            pwallposnorm = pwallpos;
            pwallposinv = 64-pwallpos;
        }
        if(pwalldir == di_south && ytile==pwally && ((uint32_t)xintercept>>16)==pwallx
            || pwalldir == di_north && !(ytile==pwally && ((uint32_t)xintercept>>16)==pwallx))
        {
            xintbuf=xintercept+((xstep*pwallposnorm)>>6);
            if((xintbuf>>16)!=(xintercept>>16))
                return true;

            yintercept=((int32_t)ytile<<TILESHIFT)+TILEGLOBAL-((int32_t)pwallposinv<<10);
            xintercept=xintbuf;
            xtile = (short) (xintercept >> TILESHIFT);
            tilehit=pwalltile;
            HitHorizWall();
        }
        else
        {
            xintbuf=xintercept+((xstep*pwallposinv)>>6);
            if((xintbuf>>16)!=(xintercept>>16))
                return true;

            yintercept=((int32_t)ytile<<TILESHIFT)-((int32_t)pwallposinv<<10);
            xintercept=xintbuf;
            xtile = (short) (xintercept >> TILESHIFT);
            tilehit=pwalltile;
            HitHorizWall();
        }
    }
    else
    {
        int pwallposi = pwallpos;
        if(pwalldir==di_west) pwallposi = 64-pwallpos;
        if(pwalldir==di_east && (word)xintercept<((int32_t)pwallposi<<10)
                || pwalldir==di_west && (word)xintercept>((int32_t)pwallposi<<10))
        {
            if(((uint32_t)xintercept>>16)==pwallx && ytile==pwally)
            {
                if(pwalldir==di_east && (int32_t)((word)xintercept)+xstep<((int32_t)pwallposi<<10)
                        || pwalldir==di_west && (int32_t)((word)xintercept)+xstep>((int32_t)pwallposi<<10))
                    return true;

                if(pwalldir==di_east)
                    xintercept=(xintercept&0xffff0000)+((int32_t)pwallposi<<10);
                else
                    xintercept=(xintercept&0xffff0000)-TILEGLOBAL+((int32_t)pwallposi<<10);
                yintercept=yintercept-((ystep*(64-pwallpos))>>6);
                ytile = (short) (yintercept >> TILESHIFT);
                tilehit=pwalltile;
                HitVertWall();
            }
            else
            {
                texdelta = -((int32_t)pwallposi<<10);
                yintercept=((int32_t)ytile<<TILESHIFT);
                xtile = (short) (xintercept >> TILESHIFT);
                tilehit=pwalltile;
                HitHorizWall();
            }
        }
        else
        {
            if(((uint32_t)xintercept>>16)==pwallx && ytile==pwally)
            {
                texdelta = -((int32_t)pwallposi<<10);
                yintercept=((int32_t)ytile<<TILESHIFT);
                xtile = (short) (xintercept >> TILESHIFT);
                tilehit=pwalltile;
                HitHorizWall();
            }
            else
            {
                if(pwalldir==di_east && (int32_t)((word)xintercept)+xstep>((int32_t)pwallposi<<10)
                        || pwalldir==di_west && (int32_t)((word)xintercept)+xstep<((int32_t)pwallposi<<10))
                    return true;

                if(pwalldir==di_east)
                    xintercept=(xintercept&0xffff0000)-((int32_t)(64-pwallpos)<<10);
                else
                    xintercept=(xintercept&0xffff0000)+((int32_t)(64-pwallpos)<<10);
                yintercept=yintercept-((ystep*pwallpos)>>6);
                ytile = (short) (yintercept >> TILESHIFT);
                tilehit=pwalltile;
                HitVertWall();
            }
        }
    }
    return false;
}

#ifdef MEGA65
extern "C" {
    uint8_t m65_trace (uint8_t entry);          // (m65_trace.c)
    extern int32_t m65_rxstep, m65_rystep;
    extern uint32_t m65_tm_base, m65_sv_base;
}
#endif

void AsmRefresh()
{
    int32_t xstep,ystep;
    longword xpartial,ypartial;
    boolean playerInPushwallBackTile = tilemap[focaltx][focalty] == 64;
#ifdef MEGA65
    m65_tm_base = decltype(tilemap)::base;
    m65_sv_base = decltype(spotvis)::base;
    if(((m65_tm_base | m65_sv_base) & 0xFF) || (m65_tm_base ^ m65_sv_base) >> 24)
        Quit("m65_trace: tilemap/spotvis must be 256-byte aligned");
#endif

    for(pixx=0;pixx<viewwidth;pixx++)
    {
        short angl=midangle+pixelangle[pixx];
        if(angl<0) angl+=FINEANGLES;
        if(angl>=3600) angl-=FINEANGLES;
        if(angl<900)
        {
            xtilestep=1;
            ytilestep=-1;
            xstep=finetangent[900-1-angl];
            ystep=-finetangent[angl];
            xpartial=xpartialup;
            ypartial=ypartialdown;
        }
        else if(angl<1800)
        {
            xtilestep=-1;
            ytilestep=-1;
            xstep=-finetangent[angl-900];
            ystep=-finetangent[1800-1-angl];
            xpartial=xpartialdown;
            ypartial=ypartialdown;
        }
        else if(angl<2700)
        {
            xtilestep=-1;
            ytilestep=1;
            xstep=-finetangent[2700-1-angl];
            ystep=finetangent[angl-1800];
            xpartial=xpartialdown;
            ypartial=ypartialup;
        }
        else if(angl<3600)
        {
            xtilestep=1;
            ytilestep=1;
            xstep=finetangent[angl-2700];
            ystep=finetangent[3600-1-angl];
            xpartial=xpartialup;
            ypartial=ypartialup;
        }
        yintercept=FixedMul(ystep,xpartial)+viewy;
        xtile=focaltx+xtilestep;
        xspot=(word)((xtile<<mapshift)+((uint32_t)yintercept>>16));
        xintercept=FixedMul(xstep,ypartial)+viewx;
        ytile=focalty+ytilestep;
        yspot=(word)((((uint32_t)xintercept>>16)<<mapshift)+ytile);
        texdelta=0;

        // Special treatment when player is in back tile of pushwall
        if(playerInPushwallBackTile)
        {
            if(    pwalldir == di_east && xtilestep ==  1
                || pwalldir == di_west && xtilestep == -1)
            {
                int32_t yintbuf = yintercept - ((ystep * (64 - pwallpos)) >> 6);
                if((yintbuf >> 16) == focalty)   // ray hits pushwall back?
                {
                    if(pwalldir == di_east)
                        xintercept = ((int32_t)focaltx<<TILESHIFT) + ((int32_t)pwallpos<<10);
                    else
                        xintercept = ((int32_t)focaltx<<TILESHIFT) - TILEGLOBAL + ((int32_t)(64 - pwallpos)<<10);
                    yintercept = yintbuf;
                    ytile = (short) (yintercept >> TILESHIFT);
                    tilehit = pwalltile;
                    HitVertWall();
                    continue;
                }
            }
            else if(pwalldir == di_south && ytilestep ==  1
                ||  pwalldir == di_north && ytilestep == -1)
            {
                int32_t xintbuf = xintercept - ((xstep * (64 - pwallpos)) >> 6);
                if((xintbuf >> 16) == focaltx)   // ray hits pushwall back?
                {
                    xintercept = xintbuf;
                    if(pwalldir == di_south)
                        yintercept = ((int32_t)focalty<<TILESHIFT) + ((int32_t)pwallpos<<10);
                    else
                        yintercept = ((int32_t)focalty<<TILESHIFT) - TILEGLOBAL + ((int32_t)(64 - pwallpos)<<10);
                    xtile = (short) (xintercept >> TILESHIFT);
                    tilehit = pwalltile;
                    HitHorizWall();
                    continue;
                }
            }
        }

#ifdef MEGA65
        //
        // The two stepping loops below, in assembly (m65_trace.c): it steps
        // the ray and hands back what needs the code here (the same code as
        // below: doors, pushwalls, walls, the map's edge).
        //
        m65_rxstep = xstep;
        m65_rystep = ystep;
        uint8_t entry = 0;
        for(;;)
        {
            uint8_t what = m65_trace(entry);
            if(what == 1)                           // a tile, vertical crossing
            {
                if(tilehit&0x80)
                {
                    int32_t yintbuf=yintercept+(ystep>>1);
                    if((yintbuf>>16)!=(yintercept>>16)
                            || (word)yintbuf<doorposition[tilehit&0x7f])
                    {
                        entry = 1;                  // passvert
                        continue;
                    }
                    yintercept=yintbuf;
                    xintercept=((int32_t)xtile<<TILESHIFT)|0x8000;
                    ytile = (short) (yintercept >> TILESHIFT);
                    HitVertDoor();
                }
                else if(tilehit==64)
                {
                    if(HitVertPushwall(xstep,ystep))
                    {
                        entry = 1;
                        continue;
                    }
                }
                else
                {
                    xintercept=((int32_t)xtile<<TILESHIFT);
                    ytile = (short) (yintercept >> TILESHIFT);
                    HitVertWall();
                }
            }
            else if(what == 2)                      // a tile, horizontal crossing
            {
                if(tilehit&0x80)
                {
                    int32_t xintbuf=xintercept+(xstep>>1);
                    if((xintbuf>>16)!=(xintercept>>16)
                            || (word)xintbuf<doorposition[tilehit&0x7f])
                    {
                        entry = 2;                  // passhoriz
                        continue;
                    }
                    xintercept=xintbuf;
                    yintercept=((int32_t)ytile<<TILESHIFT)+0x8000;
                    xtile = (short) (xintercept >> TILESHIFT);
                    HitHorizDoor();
                }
                else if(tilehit==64)
                {
                    if(HitHorizPushwall(xstep,ystep))
                    {
                        entry = 2;
                        continue;
                    }
                }
                else
                {
                    yintercept=((int32_t)ytile<<TILESHIFT);
                    xtile = (short) (xintercept >> TILESHIFT);
                    HitHorizWall();
                }
            }
            else if(what == 3)                      // the edge, vertical crossing
            {
                if(xtile<0) xintercept=0, xtile=0;
                else if(xtile>=mapwidth) xintercept=((int32_t)mapwidth<<TILESHIFT), xtile=mapwidth-1;
                else xtile=(short) (xintercept >> TILESHIFT);
                if(yintercept<0) yintercept=0, ytile=0;
                else if(yintercept>=((int32_t)mapheight<<TILESHIFT)) yintercept=((int32_t)mapheight<<TILESHIFT), ytile=mapheight-1;
                yspot=0xffff;
                tilehit=0;
                HitHorizBorder();
            }
            else if(what == 4)                      // the edge, horizontal crossing
            {
                if(ytile<0) yintercept=0, ytile=0;
                else if(ytile>=mapheight) yintercept=((int32_t)mapheight<<TILESHIFT), ytile=mapheight-1;
                else ytile=(short) (yintercept >> TILESHIFT);
                if(xintercept<0) xintercept=0, xtile=0;
                else if(xintercept>=((int32_t)mapwidth<<TILESHIFT)) xintercept=((int32_t)mapwidth<<TILESHIFT), xtile=mapwidth-1;
                xspot=0xffff;
                tilehit=0;
                HitVertBorder();
            }
            break;                                  // (5: beyond the map)
        }
#else
        do
        {
            if(ytilestep==-1 && (yintercept>>16)<=ytile) goto horizentry;
            if(ytilestep==1 && (yintercept>>16)>=ytile) goto horizentry;
vertentry:
            if((uint32_t)yintercept>mapheight*65536-1 || (word)xtile>=mapwidth)
            {
                if(xtile<0) xintercept=0, xtile=0;
                else if(xtile>=mapwidth) xintercept=((int32_t)mapwidth<<TILESHIFT), xtile=mapwidth-1;
                else xtile=(short) (xintercept >> TILESHIFT);
                if(yintercept<0) yintercept=0, ytile=0;
                else if(yintercept>=((int32_t)mapheight<<TILESHIFT)) yintercept=((int32_t)mapheight<<TILESHIFT), ytile=mapheight-1;
                yspot=0xffff;
                tilehit=0;
                HitHorizBorder();
                break;
            }
            if(xspot>=maparea) break;
            tilehit=TILEMAP_FLAT[xspot];
            if(tilehit)
            {
                if(tilehit&0x80)
                {
                    int32_t yintbuf=yintercept+(ystep>>1);
                    if((yintbuf>>16)!=(yintercept>>16))
                        goto passvert;
                    if((word)yintbuf<doorposition[tilehit&0x7f])
                        goto passvert;
                    yintercept=yintbuf;
                    xintercept=((int32_t)xtile<<TILESHIFT)|0x8000;
                    ytile = (short) (yintercept >> TILESHIFT);
                    HitVertDoor();
                }
                else
                {
                    if(tilehit==64)
                    {
                        if(HitVertPushwall(xstep,ystep))
                            goto passvert;
                    }
                    else
                    {
                        xintercept=((int32_t)xtile<<TILESHIFT);
                        ytile = (short) (yintercept >> TILESHIFT);
                        HitVertWall();
                    }
                }
                break;
            }
passvert:
            *(SPOTVIS_FLAT+xspot)=1;
            xtile+=xtilestep;
            yintercept+=ystep;
            xspot=(word)((xtile<<mapshift)+((uint32_t)yintercept>>16));
        }
        while(1);
        continue;

        do
        {
            if(xtilestep==-1 && (xintercept>>16)<=xtile) goto vertentry;
            if(xtilestep==1 && (xintercept>>16)>=xtile) goto vertentry;
horizentry:
            if((uint32_t)xintercept>mapwidth*65536-1 || (word)ytile>=mapheight)
            {
                if(ytile<0) yintercept=0, ytile=0;
                else if(ytile>=mapheight) yintercept=((int32_t)mapheight<<TILESHIFT), ytile=mapheight-1;
                else ytile=(short) (yintercept >> TILESHIFT);
                if(xintercept<0) xintercept=0, xtile=0;
                else if(xintercept>=((int32_t)mapwidth<<TILESHIFT)) xintercept=((int32_t)mapwidth<<TILESHIFT), xtile=mapwidth-1;
                xspot=0xffff;
                tilehit=0;
                HitVertBorder();
                break;
            }
            if(yspot>=maparea) break;
            tilehit=TILEMAP_FLAT[yspot];
            if(tilehit)
            {
                if(tilehit&0x80)
                {
                    int32_t xintbuf=xintercept+(xstep>>1);
                    if((xintbuf>>16)!=(xintercept>>16))
                        goto passhoriz;
                    if((word)xintbuf<doorposition[tilehit&0x7f])
                        goto passhoriz;
                    xintercept=xintbuf;
                    yintercept=((int32_t)ytile<<TILESHIFT)+0x8000;
                    xtile = (short) (xintercept >> TILESHIFT);
                    HitHorizDoor();
                }
                else
                {
                    if(tilehit==64)
                    {
                        if(HitHorizPushwall(xstep,ystep))
                            goto passhoriz;
                    }
                    else
                    {
                        yintercept=((int32_t)ytile<<TILESHIFT);
                        xtile = (short) (xintercept >> TILESHIFT);
                        HitHorizWall();
                    }
                }
                break;
            }
passhoriz:
            *(SPOTVIS_FLAT+yspot)=1;
            ytile+=ytilestep;
            xintercept+=xstep;
            yspot=(word)((((uint32_t)xintercept>>16)<<mapshift)+ytile);
        }
        while(1);
#endif
    }
}

/*
====================
=
= WallRefresh
=
====================
*/

void WallRefresh (void)
{
    xpartialdown = viewx&(TILEGLOBAL-1);
    xpartialup = TILEGLOBAL-xpartialdown;
    ypartialdown = viewy&(TILEGLOBAL-1);
    ypartialup = TILEGLOBAL-ypartialdown;

    min_wallheight = viewheight;
    lastside = -1;                  // the first pixel is on a new wall
    AsmRefresh ();
    ScalePost ();                   // no more optimization on last post
}

void CalcViewVariables()
{
    viewangle = player->angle;
    midangle = viewangle*(FINEANGLES/ANGLES);
    viewsin = sintable[viewangle];
    viewcos = costable[viewangle];
    viewx = player->x - FixedMul(focallength,viewcos);
    viewy = player->y + FixedMul(focallength,viewsin);

    focaltx = (short)(viewx>>TILESHIFT);
    focalty = (short)(viewy>>TILESHIFT);

    viewtx = (short)(player->x >> TILESHIFT);
    viewty = (short)(player->y >> TILESHIFT);
}

//==========================================================================

/*
========================
=
= ThreeDRefresh
=
========================
*/

void    ThreeDRefresh (void)
{
//
// clear out the traced array
//
#ifdef MEGA65
    spotvis.clear();
#else
    memset(spotvis,0,maparea);
#endif
    spotvis[player->tilex][player->tiley] = 1;       // Detect all sprites over player fix

#ifndef MEGA65       // (the MEGA65 renderer addresses screenBuffer directly)
    vbuf = VL_LockSurface(screenBuffer);
    if(vbuf == NULL) return;

    vbuf += screenofs;
    vbufPitch = bufferPitch;
#endif

    CalcViewVariables();

//
// follow the walls from there to the right, drawing as we go
//
    VGAClearScreen ();
#if defined(USE_FEATUREFLAGS) && defined(USE_STARSKY)
    if(GetFeatureFlags() & FF_STARSKY)
        DrawStarSky(vbuf, vbufPitch);
#endif

    WallRefresh ();

#if defined(USE_FEATUREFLAGS) && defined(USE_PARALLAX)
    if(GetFeatureFlags() & FF_PARALLAXSKY)
        DrawParallax(vbuf, vbufPitch);
#endif
#if defined(USE_FEATUREFLAGS) && defined(USE_CLOUDSKY)
    if(GetFeatureFlags() & FF_CLOUDSKY)
        DrawClouds(vbuf, vbufPitch, min_wallheight);
#endif
#ifdef USE_FLOORCEILINGTEX
    DrawFloorAndCeiling(vbuf, vbufPitch, min_wallheight);
#endif

//
// draw all the scaled images
//
    DrawScaleds();                  // draw scaled stuff

#if defined(USE_FEATUREFLAGS) && defined(USE_RAIN)
    if(GetFeatureFlags() & FF_RAIN)
        DrawRain(vbuf, vbufPitch);
#endif
#if defined(USE_FEATUREFLAGS) && defined(USE_SNOW)
    if(GetFeatureFlags() & FF_SNOW)
        DrawSnow(vbuf, vbufPitch);
#endif

    DrawPlayerWeapon ();    // draw player's hands

    if(Keyboard[sc_Tab] && viewsize == 21 && gamestate.weapon != -1)
        ShowActStatus();

#ifndef MEGA65
    VL_UnlockSurface(screenBuffer);
    vbuf = NULL;
#endif

#if defined(MEGA65) || defined(FRAMEDUMP)
    // Tests: the finished frame of a demo, before it is shown (mega65/: the
    // MEGA65 port's frames are compared with the original code's).
    if (demoplayback)
        FrameDumpHook();
#ifdef MEGA65
    else
        GameFrameHook();            // (a frame of play: tests, m65_test.c)
#endif
#endif

//
// show screen and time last cycle
//

    if (fizzlein)
    {
        FizzleFade(screenBuffer, 0, 0, screenWidth, screenHeight, 20, false);
        fizzlein = false;

        lasttimecount = GetTimeCount();          // don't make a big tic count
    }
    else
    {
#ifndef REMDEBUG
        if (fpscounter)
        {
            fontnumber = 0;
            SETFONTCOLOR(7,127);
            PrintX=4; PrintY=1;
            VWB_Bar(0,0,50,10,bordercol);
            US_PrintSigned(fps);
#ifdef MEGA65
            US_Print(" ms");            // (real time per frame: see below)
#else
            US_Print(" fps");
#endif
        }
#endif
        SDL_BlitSurface(screenBuffer, NULL, screen, NULL);
        SDL_Flip(screen);
    }

#ifdef MEGA65
    // The time per frame, in real milliseconds, averaged over 8 frames: the
    // counter below counts frames per 35 tics, but a frame is credited at
    // most MAXTICS tics, so it cannot show fewer than 6-8 frames per second.
    if (fpscounter)
    {
        static uint32_t fpsstart;
        if (++fps_frames == 8)
        {
            uint32_t now = SDL_GetTicks();
            fps = (int) ((now - fpsstart) / 8);
            fpsstart = now;
            fps_frames = 0;
        }
    }
#elif !defined(REMDEBUG)
    if (fpscounter)
    {
        fps_frames++;
        fps_time+=tics;

        if(fps_time>35)
        {
            fps_time-=35;
            fps=fps_frames<<1;
            fps_frames=0;
        }
    }
#endif
}
