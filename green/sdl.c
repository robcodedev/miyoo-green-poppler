/* green - the PDF reader
 * Copyright (C) 2009 Florian Tobias Schandinat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "green.h"


#define FLAG_QUIT	0x0001
#define FLAG_RENDER	0x0002


typedef enum
{
	NORMAL, GOTO, SEARCH, FIT, ROTATE, MIRROR
	
}	RState;

typedef struct
{
	char	buff[64];
	unsigned char	used, cur;
	
}	IBuffer;


const Uint32	live_interval = 40;


void	GetInput( IBuffer *input, SDL_Event *event )
{
	char	c;
	int i;
	
	switch (event->key.keysym.sym)
	{
		case SDLK_LEFT:
			if (input->cur)
				input->cur--;
			
			break;
		case SDLK_RIGHT:
			if (input->cur < input->used)
				input->cur++;
			
			break;
		case SDLK_BACKSPACE:
			if (!input->cur)
				break;
			
			input->cur--;
		case SDLK_DELETE:
			if (input->cur == input->used)
				break;
			
			for (i = input->cur; i < input->used-1; i++)
				input->buff[i] = input->buff[i+1];
			
			input->used--;
			break;
		case 'a'...'z':
			if (event->key.keysym.mod & KMOD_SHIFT)
				event->key.keysym.sym += 'A' - 'a';
		case '0'...'9':
		case '+':
		case '-':
		case '*':
		case '/':
		case '=':
		case '_':
		case '.':
		case ':':
		case ',':
		case ';':
		case '!':
		case '?':
		case '(':
		case ')':
		case '@':
		case ' ':
		case '$':
		case '&':
		case '#':
		case '\\':
			if (input->used == sizeof( input->buff ) - 1)
				break;
			
			for (i = input->cur; i < input->used; i++)
				input->buff[i+1] = input->buff[i];
			
			c = event->key.keysym.sym;
			input->buff[input->cur] = c;
			input->cur++;
			input->used++;
			break;
		default:
			break;
	}
	
	return;
}

/* The Miyoo Mini's FB0 is physically rotated 180 degrees.  Keep Green's
 * page coordinates logical; rotate only output coordinates.  This avoids
 * an additional full-screen buffer copy on every page update.
 * GREEN_FB_ROTATE_180=0 can disable the workaround for diagnostics. */
static int Green_FbRotate180(void)
{
#ifdef GREEN_MIYOO_MINI
	const char *setting = getenv("GREEN_FB_ROTATE_180");
	return !setting || strcmp(setting, "0") != 0;
#else
	return 0;
#endif
}

static inline Uint8 *Green_FramePixel(SDL_Surface *display, int x, int y, int inverted)
{
	if (inverted)
	{
		x = display->w - 1 - x;
		y = display->h - 1 - y;
	}
	return (Uint8 *)display->pixels + y * display->pitch +
		x * display->format->BytesPerPixel;
}

/* Write one converted pixel without overrunning 16/24-bit SDL surfaces. */
static inline void Green_WritePixel( Uint8 *dst, Uint32 pixel, int bpp )
{
	switch (bpp)
	{
		case 2:
		{
			Uint16 value = (Uint16)pixel;
			memcpy( dst, &value, sizeof(value) );
			break;
		}
		case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			dst[0] = (Uint8)(pixel >> 16);
			dst[1] = (Uint8)(pixel >> 8);
			dst[2] = (Uint8)pixel;
#else
			dst[0] = (Uint8)pixel;
			dst[1] = (Uint8)(pixel >> 8);
			dst[2] = (Uint8)(pixel >> 16);
#endif
			break;
		case 4:
			memcpy( dst, &pixel, sizeof(pixel) );
			break;
	}
}


/* Memory bounded recent-page cache. Only pages already visited are cached.
 * Maximum 6 pages total and a 12 MiB default budget per open document.
 * Changing the environment affects the next launch, not the current process. */
#define GREEN_RECENT_SLOTS 5

static int Green_CacheSetting(const char *name, int defaultValue, int minValue, int maxValue)
{
	const char *env = getenv( name );
	char *end;
	long value;
	if (!env || !*env)
		return defaultValue;
	value = strtol( env, &end, 10 );
	if (*end != '\0' || value < minValue || value > maxValue)
		return defaultValue;
	return (int)value;
}

static size_t Green_SurfaceBytes( cairo_surface_t *surface )
{
	if (!surface || cairo_surface_get_type( surface ) != CAIRO_SURFACE_TYPE_IMAGE)
		return 0;
	return (size_t)cairo_image_surface_get_stride( surface ) *
		(size_t)cairo_image_surface_get_height( surface );
}

static void Green_ClearRecent(Green_PageBuffer *entry)
{
	if (entry->surface)
		cairo_surface_destroy( entry->surface );
	entry->surface = NULL;
	entry->page = -1;
	entry->tscale = 0;
}

static int Green_CacheHit(Green_Document *doc, double tscale)
{
	int i;
	if (doc->cache.surface && doc->cache.page == doc->page_cur &&
	    doc->cache.tscale == tscale)
		return 1;
	for (i = 0; i < GREEN_RECENT_SLOTS; ++i)
		if (doc->recent[i].surface && doc->recent[i].page == doc->page_cur &&
		    doc->recent[i].tscale == tscale)
			return 1;
	return 0;
}

static void Green_CacheLimit(Green_Document *doc)
{
	int i;
	/* Current page is never evicted, even when it exceeds budget. */
	const int maxRecent = Green_CacheSetting("GREEN_CACHE_PAGES", 5, 2, 6) - 1;
	const size_t budget = (size_t)Green_CacheSetting("GREEN_CACHE_MB", 12, 4, 24) * 1024u * 1024u;
	size_t used = Green_SurfaceBytes(doc->cache.surface);
	for (i = 0; i < GREEN_RECENT_SLOTS; ++i)
	{
		size_t bytes = Green_SurfaceBytes(doc->recent[i].surface);
		if (i >= maxRecent || bytes > budget || used > budget - bytes)
			Green_ClearRecent(&doc->recent[i]);
		else
			used += bytes;
	}
}

static void Green_PromoteRecent(Green_Document *doc, int hit)
{
	int i;
	Green_PageBuffer found = doc->recent[hit];
	for (i = hit; i > 0; --i)
		doc->recent[i] = doc->recent[i - 1];
	doc->recent[0] = doc->cache;
	doc->cache = found;
}

/* Small three-dot indication drawn before a slow uncached page is rendered.
 * It will be cleared by the normal full-frame update after rendering. */
static void Green_ShowBusy(SDL_Surface *display)
{
	SDL_Rect box, dot;
	Uint32 dark, light;
	int i, inverted = Green_FbRotate180();
	if (!display || display->w < 40 || display->h < 30)
		return;
	box.w = 29;
	box.h = 20;
	/* Physical top-left displays at bottom-right on inverted Miyoo FB0. */
	box.x = inverted ? 5 : display->w - box.w - 5;
	box.y = inverted ? 5 : display->h - box.h - 5;
	dark = SDL_MapRGB(display->format, 20, 20, 20);
	light = SDL_MapRGB(display->format, 235, 235, 235);
	SDL_FillRect(display, &box, dark);
	dot.w = dot.h = 4;
	dot.y = box.y + 8;
	for (i = 0; i < 3; ++i) {
		/* Reverse the 1-pixel asymmetry of the original dot layout. */
		dot.x = box.x + (inverted ? 4 : 5) + 8 * i;
		SDL_FillRect(display, &dot, light);
	}
	/* Flush the background and indicator together in one full-screen update. */
	SDL_UpdateRect(display, 0, 0, 0, 0);
}

void	RenderPage( Green_RTD *rtd, SDL_Rect dest, int xoff, int yoff, PopplerPage *page, double tscale )
{
	PopplerRectangle	*rect;
	Green_Document	*doc = rtd->docs[rtd->doc_cur];
	SDL_Surface	*display = SDL_GetVideoSurface();
	SDL_PixelFormat	fmt = *display->format;
	cairo_surface_t	*surface;
	cairo_t		*context;
	void	*pixels;
	unsigned short	ar, ag, ab, ia;
	gdouble	tmp_d;
	double	pwidth, pheight;
	guint	i, n;
	GList	*list = NULL;
	Uint32	*src;
	Uint8	*dst;
	int	x, y, rowstride, w, h, dir_x, dir_y;
	const int fb180 = Green_FbRotate180();
	const int pixel_step = (fb180 ? -1 : 1) * fmt.BytesPerPixel;

	if (doc->search_str)
		list = poppler_page_find_text( page, doc->search_str );

	if (doc->cache.surface && doc->cache.page == doc->page_cur && doc->cache.tscale == tscale)
		surface = doc->cache.surface;
	else
	{
		int hit = -1;
		for (i = 0; i < GREEN_RECENT_SLOTS; ++i)
			if (doc->recent[i].surface && doc->recent[i].page == doc->page_cur &&
			    doc->recent[i].tscale == tscale)
			{
				hit = (int)i;
				break;
			}
		if (hit >= 0)
		{
			Green_PromoteRecent(doc, hit);
			surface = doc->cache.surface;
		}
		else
		{
			Green_GetDimension( page, &w, &h, tscale, false );
			surface = cairo_image_surface_create( CAIRO_FORMAT_ARGB32, w, h );
			if (cairo_surface_status( surface ) != CAIRO_STATUS_SUCCESS)
			{
				cairo_surface_destroy( surface );
				if (list)
					g_list_free( list );
				return;
			}
			context = cairo_create( surface );
			cairo_save( context );
			cairo_scale( context, tscale, tscale );
			poppler_page_render( page, context );
			cairo_restore( context );
			cairo_set_operator( context, CAIRO_OPERATOR_DEST_OVER );
			cairo_set_source_rgb( context, 1., 1., 1. );
			cairo_paint( context );
			cairo_destroy( context );

			/* Push the old current page into the MRU end of history. */
			Green_ClearRecent(&doc->recent[GREEN_RECENT_SLOTS - 1]);
			for (int j = GREEN_RECENT_SLOTS - 1; j > 0; --j)
				doc->recent[j] = doc->recent[j - 1];
			doc->recent[0] = doc->cache;
			doc->cache.surface = surface;
			doc->cache.page = doc->page_cur;
			doc->cache.tscale = tscale;
		}
	}
	Green_CacheLimit(doc);

	if (doc->rotation == 1)
	{
		dir_x = -1;
		dir_y = 1;
	}
	else if (doc->rotation == 2)
	{
		dir_x = -1;
		dir_y = -1;
	}
	else if (doc->rotation == 3)
	{
		dir_x = 1;
		dir_y = -1;
	}
	else
		dir_x = dir_y = 1;

	if (doc->mirrored)
		dir_y *= -1;

	pixels = cairo_image_surface_get_data( surface );
	rowstride = cairo_image_surface_get_stride( surface );
	SDL_LockSurface( display );
	if (doc->rotation == 0 && !doc->mirrored &&
		fmt.BytesPerPixel == 4 &&
		fmt.Rmask == 0x00ff0000 && fmt.Gmask == 0x0000ff00 &&
		fmt.Bmask == 0x000000ff)
	{
		/* Cairo ARGB32 and this SDL format use identical RGB bits. */
		for (y = 0; y < dest.h; y++)
		{
			const Uint8 *row = (const Uint8 *)pixels +
				(yoff + y) * rowstride + xoff * 4;
			Uint8 *out = Green_FramePixel(display, dest.x, dest.y + y, fb180);
			if (!fb180)
				memcpy( out, row, (size_t)dest.w * 4 );
			else
			{
				/* Rotate while copying. No second framebuffer pass. */
				const Uint32 *in32 = (const Uint32 *)row;
				Uint32 *out32 = (Uint32 *)out;
				for (x = 0; x < dest.w; ++x)
					out32[-x] = in32[x];
			}
		}
	}
	else if (doc->rotation % 2)
	{
		for (y = 0; y < dest.h; y++)
		{
			src = pixels + (xoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * 4 + (yoff + (dir_x < 0 ? dest.w - 1 : 0)) * rowstride;
			dst = Green_FramePixel(display, dest.x, dest.y + y, fb180);
			for (x = 0; x < dest.w; x++)
			{
				Green_WritePixel( dst, ((((*src>>16)&0xFF)>>fmt.Rloss)<<fmt.Rshift)
					| ((((*src>>8)&0xFF)>>fmt.Gloss)<<fmt.Gshift)
					| (((*src&0xFF)>>fmt.Bloss)<<fmt.Bshift), fmt.BytesPerPixel );
				
				src = (void*)src + dir_x * rowstride;
				dst += pixel_step;
			}
		}
	}
	else
	{
		for (y = 0; y < dest.h; y++)
		{
			src = pixels + (yoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * rowstride + (xoff + (dir_x < 0 ? dest.w - 1 : 0)) * 4;
			dst = Green_FramePixel(display, dest.x, dest.y + y, fb180);
			for (x = 0; x < dest.w; x++)
			{
				Green_WritePixel( dst, ((((*src>>16)&0xFF)>>fmt.Rloss)<<fmt.Rshift)
					| ((((*src>>8)&0xFF)>>fmt.Gloss)<<fmt.Gshift)
					| (((*src&0xFF)>>fmt.Bloss)<<fmt.Bshift), fmt.BytesPerPixel );
				
				src += dir_x;
				dst += pixel_step;
			}
		}
	}
	
	if (list)
	{
		poppler_page_get_size( page, &pwidth, &pheight );
		ar = rtd->c_highlight.a * rtd->c_highlight.r;
		ag = rtd->c_highlight.a * rtd->c_highlight.g;
		ab = rtd->c_highlight.a * rtd->c_highlight.b;
		ia = 0xFF - rtd->c_highlight.a;
		n = g_list_length( list );
		for (i = 0; i < n; i++)
		{
			rect = g_list_nth_data( list, i );
			tmp_d = pheight - rect->y2;
			rect->y2 = pheight - rect->y1;
			rect->y1 = tmp_d;
			rect->x1 *= tscale;
			rect->y1 *= tscale;
			rect->x2 *= tscale;
			rect->y2 *= tscale;
			rect->x1 -= xoff;
			rect->y1 -= yoff;
			rect->x2 -= xoff;
			rect->y2 -= yoff;
			if (doc->rotation % 2)
			{
				tmp_d = rect->x1;
				rect->x1 = rect->y1;
				rect->y1 = tmp_d;
				tmp_d = rect->x2;
				rect->x2 = rect->y2;
				rect->y2 = tmp_d;
			}

			if (doc->mirrored)
			{
				tmp_d = rect->y1;
				rect->y1 = dest.h - rect->y2;
				rect->y2 = dest.h - tmp_d;
			}

			if (doc->rotation == 1)
			{
				tmp_d = rect->x1;
				rect->x1 = dest.w - rect->x2;
				rect->x2 = dest.w - tmp_d;
			}
			else if (doc->rotation == 2)
			{
				tmp_d = rect->x1;
				rect->x1 = dest.w - rect->x2;
				rect->x2 = dest.w - tmp_d;
				tmp_d = rect->y1;
				rect->y1 = dest.h - rect->y2;
				rect->y2 = dest.h - tmp_d;
			}
			else if (doc->rotation == 3)
			{
				tmp_d = rect->y1;
				rect->y1 = dest.h - rect->y2;
				rect->y2 = dest.h - tmp_d;
			}

			if (rect->x1 > dest.w)
				continue;
			else if (rect->x1 < 0)
				rect->x1 = 0;
			
			if (rect->x2 < 0)
				continue;
			else if (rect->x2 > dest.w)
				rect->x2 = dest.w;
			
			if (rect->y1 > dest.h)
				continue;
			else if (rect->y1 < 0)
				rect->y1 = 0;
			
			if (rect->y2 < 0)
				continue;
			else if (rect->y2 > dest.h)
				rect->y2 = dest.h;
			
			if (doc->rotation % 2)
			{
				for (y = rect->y1; y < (int)rect->y2; y++)
				{
					src = pixels + (xoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * 4 + (yoff + dir_x * (int)rect->x1 + (dir_x < 0 ? dest.w - 1 : 0)) * rowstride;
					dst = Green_FramePixel(display, dest.x + (int)rect->x1,
							 dest.y + y, fb180);
					for (x = rect->x1; x < (int)rect->x2; x++)
					{
						Green_WritePixel( dst, ((((((*src>>16)&0xFF) * ia + ar) / 256)>>fmt.Rloss)<<fmt.Rshift)
							| ((((((*src>>8)&0xFF) * ia + ag) / 256)>>fmt.Gloss)<<fmt.Gshift)
							| (((((*src&0xFF) * ia + ab) / 256)>>fmt.Bloss)<<fmt.Bshift), fmt.BytesPerPixel );
						
						src = (void*)src + dir_x * rowstride;
						dst += pixel_step;
					}
				}
			}
			else
			{
				for (y = rect->y1; y < (int)rect->y2; y++)
				{
					src = pixels + (yoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * rowstride + (xoff + dir_x * (int)rect->x1 + (dir_x < 0 ? dest.w - 1 : 0)) * 4;
					dst = Green_FramePixel(display, dest.x + (int)rect->x1,
							 dest.y + y, fb180);
					for (x = rect->x1; x < (int)rect->x2; x++)
					{
						Green_WritePixel( dst, ((((((*src>>16)&0xFF) * ia + ar) / 256)>>fmt.Rloss)<<fmt.Rshift)
							| ((((((*src>>8)&0xFF) * ia + ag) / 256)>>fmt.Gloss)<<fmt.Gshift)
							| (((((*src&0xFF) * ia + ab) / 256)>>fmt.Bloss)<<fmt.Bshift), fmt.BytesPerPixel );
						
						src += dir_x;
						dst += pixel_step;
					}
				}
			}
		}
		
		g_list_free( list );
	}
	
	SDL_UnlockSurface( display );
	return;
}

void	Render( Green_RTD *rtd )
{
	Green_Document	*doc;
	PopplerPage	*page = NULL;
	SDL_Surface	*display = SDL_GetVideoSurface();
	SDL_Rect	rect, screen;
	double	tscale;
	int	w, h;
	
	rect.x = rect.y = 0;
	rect.w = display->w;
	rect.h = display->h;
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
	{
		SDL_FillRect(display, &rect, SDL_MapRGB(display->format,
			rtd->c_background.r, rtd->c_background.g, rtd->c_background.b));
		SDL_UpdateRect( display, 0, 0, 0, 0 );
		return;
	}
	
	doc = rtd->docs[rtd->doc_cur];
	page = poppler_document_get_page( doc->doc, doc->page_cur );
	if (!page)
		return;
	tscale = Green_FitPage( doc, page, display->w, display->h ) * doc->finescale;
	Green_GetDimension( page, &w, &h, tscale, doc->rotation % 2 );
	rect.w = w > display->w ? display->w : w;
	rect.h = h > display->h ? display->h : h;
	rect.x = (display->w - rect.w) / 2;
	rect.y = (display->h - rect.h) / 2;
	if (!Green_CacheHit(doc, tscale))
	{
		/* Clear the previous app/page in the software surface before
		 * showing the indicator. Green_ShowBusy publishes both together. */
		SDL_FillRect(display, NULL, SDL_MapRGB(display->format,
			rtd->c_background.r, rtd->c_background.g, rtd->c_background.b));
		Green_ShowBusy(display);
	}
	screen.x = screen.y = 0;
	screen.w = display->w;
	screen.h = display->h;
	SDL_FillRect(display, &screen, SDL_MapRGB(display->format,
		rtd->c_background.r, rtd->c_background.g, rtd->c_background.b));
	RenderPage( rtd, rect, doc->xoffset, doc->yoffset, page, tscale );
	g_object_unref( G_OBJECT( page ) );
	SDL_UpdateRect( display, 0, 0, 0, 0 );
	return;
}

RState	NormalInput( Green_RTD *rtd, SDL_Event *event, unsigned short *flags )
{
	Green_Document	*doc = NULL;
	SDL_Surface	*display = SDL_GetVideoSurface();
	RState	state = NORMAL;
	int	f = 0;
	
	if (Green_IsDocValid( rtd, rtd->doc_cur ))
		doc = rtd->docs[rtd->doc_cur];
	
	switch (event->key.keysym.sym)
	{
		case 'q':
			*flags |= FLAG_QUIT;
			break;
		case 'c':
			Green_Close( rtd, rtd->doc_cur );
			*flags |= FLAG_RENDER;
			break;
		case 'g':
			state = GOTO;
			break;
		case SDLK_s:
			/* type s-SEARCHSTRING-<Enter> to search string */
			state = SEARCH;
			break;
		case 'n':
			if (!doc || !doc->search_str)
				break;
			
			doc->page_cur = Green_FindNext( doc, doc->page_cur + 1 );
			*flags |= FLAG_RENDER;
			break;
		case 'f':
			state = FIT;
			break;
		case SDLK_UP:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, 0, - display->h * rtd->step, display->w, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_k:
			if (!doc)
				break;
			Green_ScrollRelative( doc, 0, - display->h * rtd->step, display->w, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_DOWN:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, 0, display->h * rtd->step, display->w, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
                case SDLK_j:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, 0, display->h * rtd->step, display->w, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_LEFT:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, - display->w * rtd->step, 0, display->w, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_h:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, - display->w * rtd->step, 0, display->w, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_l:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, display->w * rtd->step, 0, display->w, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_RIGHT:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, display->w * rtd->step, 0, display->w, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_PAGEUP:
			if (!doc || !Green_GotoPage( doc, doc->page_cur - 1, true ))
				break;
			
			*flags |= FLAG_RENDER;
			break;
		case SDLK_PAGEDOWN:
			if (!doc || !Green_GotoPage( doc, doc->page_cur + 1, true ))
				break;
			
			*flags |= FLAG_RENDER;
			break;
		case SDLK_m:
			if (!doc)
				break;
			
			state = MIRROR;
			break;
		case SDLK_r:
			if (!doc)
				break;
			
			state = ROTATE;
			break;
		case '+':
			if (!doc)
				break;
			
			Green_Zoom( doc, display->w,display->h, doc->finescale * rtd->zoomstep );
			*flags |= FLAG_RENDER;
			break;
		case '-':
			if (!doc)
				break;
			
			Green_Zoom( doc, display->w,display->h, doc->finescale / rtd->zoomstep );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_F12:
			f++;
		case SDLK_F11:
			f++;
		case SDLK_F10:
			f++;
		case SDLK_F9:
			f++;
		case SDLK_F8:
			f++;
		case SDLK_F7:
			f++;
		case SDLK_F6:
			f++;
		case SDLK_F5:
			f++;
		case SDLK_F4:
			f++;
		case SDLK_F3:
			f++;
		case SDLK_F2:
			f++;
		case SDLK_F1:

			if (!Green_IsDocValid( rtd, f ))
				break;
			
			rtd->doc_cur = f;
			*flags |= FLAG_RENDER;
			break;
		case SDLK_TAB:
			Green_NextVaildDoc( rtd );
			*flags |= FLAG_RENDER;
			break;
		default:
			break;
	}
	
	return state;
}

#ifdef GREEN_MIYOO_MINI
/* Miyoo Mini/Plus SDL 1.2 stock key map. Leave upstream keyboard controls
 * unchanged when this build option is disabled. */
static int MiyooInput( Green_RTD *rtd, SDLKey key, unsigned short *flags )
{
	Green_Document *doc;
	SDL_Surface *display = SDL_GetVideoSurface();
	int target;

	/* MENU: return to the launcher, including when no document is open. */
	if (key == SDLK_ESCAPE)
	{
		*flags |= FLAG_QUIT;
		return 1;
	}
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
		return 0;
	doc = rtd->docs[rtd->doc_cur];

	switch (key)
	{
		case SDLK_SPACE:       /* A: fit to width */
			doc->fit_method = WIDTH;
			doc->finescale = 1;
			doc->xoffset = doc->yoffset = 0;
			break;
		case SDLK_LCTRL:       /* B: rotate clockwise */
			Green_RotateRight( doc );
			Green_ValidateOffset( doc, display->w, display->h );
			break;
		case SDLK_LSHIFT:      /* X: zoom in */
			Green_Zoom( doc, display->w, display->h,
				doc->finescale * rtd->zoomstep );
			break;
		case SDLK_LALT:        /* Y: zoom out */
			Green_Zoom( doc, display->w, display->h,
				doc->finescale / rtd->zoomstep );
			break;
		case SDLK_e:           /* L1: previous page */
		case SDLK_t:           /* R1: next page */
		case SDLK_TAB:         /* L2: back 10 pages */
		case SDLK_BACKSPACE:   /* R2: forward 10 pages */
			target = doc->page_cur;
			if (key == SDLK_e) target--;
			else if (key == SDLK_t) target++;
			else if (key == SDLK_TAB) target -= 10;
			else target += 10;
			if (target < 0) target = 0;
			if (target >= doc->page_count) target = doc->page_count - 1;
			if (!Green_GotoPage( doc, target, true ))
				return 1;
			break;
		case SDLK_RCTRL:       /* SELECT: vertical mirror */
			Green_MirrorV( doc );
			Green_ValidateOffset( doc, display->w, display->h );
			break;
		case SDLK_RETURN:      /* START: horizontal mirror */
			Green_MirrorH( doc );
			Green_ValidateOffset( doc, display->w, display->h );
			break;
		default:
			return 0; /* D-pad and keyboard: upstream handling */
	}
	*flags |= FLAG_RENDER;
	return 1;
}
#endif

Uint32	live_timer( Uint32 interval, void *param )
{
	SDL_Event	event;
	
	event.type = SDL_USEREVENT;
	SDL_PushEvent( &event );
	return interval;
}

int	Green_SDL_Main( Green_RTD *rtd )
{
	SDL_TimerID	timer = NULL;
	SDL_Surface	*display;
	SDL_Event	event;
	RState	state = NORMAL;
	IBuffer	input;
	Uint32	mouse_last = 0, mouse_cur;
	Uint16	left_x = 0, left_y = 0, right_x = 0, right_y = 0;
	unsigned short	flags = FLAG_RENDER;
	unsigned char	event_count;
	char	*str;
	long	tmp;
	int	x, y, width, height;
	
	if (SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER ))
	{
		fprintf( stderr, "SDL_Init failed: %s\n", SDL_GetError() );
		return 1;
	}
	
	SDL_WM_SetCaption( "green - the PDF reader", NULL );
	display = SDL_SetVideoMode( rtd->width, rtd->height, 0, SDL_SWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE | (rtd->flags&GREEN_FULLSCREEN ? SDL_FULLSCREEN : 0) );
	if (!display)
	{
		SDL_Quit();
		fprintf( stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError() );
		return 2;
	}
	
	if (display->format->palette)
	{
		SDL_Quit();
		fprintf( stderr, "Palettes are not supported!\n" );
                return 3;
	}

	/* Clear the previous application's framebuffer as soon as SDL video is ready. */
	SDL_FillRect(display, NULL, SDL_MapRGB(display->format,
		rtd->c_background.r, rtd->c_background.g, rtd->c_background.b));
	SDL_UpdateRect(display, 0, 0, 0, 0);
	
	/* On a controller-only device, do not wake up 25 times a second.
	 * Keep the timer for mouse-cursor timeout or edge scrolling. */
	if (rtd->mouse.visibility > 0 || rtd->mouse.border_size)
		timer = SDL_AddTimer( live_interval, live_timer, NULL );
	mouse_last = SDL_GetTicks();
	if (!rtd->mouse.visibility)
		SDL_ShowCursor( SDL_DISABLE );
	
	do
	{
		if (flags&FLAG_RENDER)
		{
			Render( rtd );
			flags ^= FLAG_RENDER;
		}
		
		event_count = 0;
		if (!SDL_WaitEvent( &event ))
		{
			SDL_Quit();
			return -1;
		}
		
		do
		{
			switch (event.type)
			{
				case SDL_QUIT:
					flags |= FLAG_QUIT;
					break;
				case SDL_KEYDOWN:
#ifdef GREEN_MIYOO_MINI
					if (MiyooInput( rtd, event.key.keysym.sym, &flags ))
					{
						state = NORMAL;
						break;
					}
#endif
					if (event.key.keysym.sym == SDLK_ESCAPE)
						state = NORMAL;
					else if (event.key.keysym.sym == SDLK_RETURN)
					{
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						input.buff[input.used] = 0;
						switch (state)
						{
							case GOTO:
								tmp = strtol( input.buff, &str, 10 );
								if (*str || tmp <=0 || tmp > rtd->docs[rtd->doc_cur]->page_count )
									break;
								
								rtd->docs[rtd->doc_cur]->page_cur = tmp - 1;
								rtd->docs[rtd->doc_cur]->xoffset = 0;
								rtd->docs[rtd->doc_cur]->yoffset = 0;
								flags |= FLAG_RENDER;
								break;
							case SEARCH:
								free( rtd->docs[rtd->doc_cur]->search_str );
								rtd->docs[rtd->doc_cur]->search_str = NULL;
								if (!strlen( input.buff ))
									break;
								
								rtd->docs[rtd->doc_cur]->search_str = strdup( input.buff );
								tmp = Green_FindNext( rtd->docs[rtd->doc_cur], rtd->docs[rtd->doc_cur]->page_cur );
								if (tmp < 0)
								{
									free( rtd->docs[rtd->doc_cur]->search_str );
									rtd->docs[rtd->doc_cur]->search_str = NULL;
									break;
								}
								
								rtd->docs[rtd->doc_cur]->page_cur = tmp;
								flags |= FLAG_RENDER;
								break;
							default:
								break;
						}
						
						state = NORMAL;
					}
					else if (state == NORMAL)
					{
						state = NormalInput( rtd, &event, &flags );
						if (state != NORMAL)
						{
							input.used = 0;
							input.cur = 0;
						}
					}
					else if (state == GOTO || state == SEARCH)
						GetInput( &input, &event );
					else if (state == FIT)
					{
						state = NORMAL;
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						if (event.key.keysym.sym == 'n')
							rtd->docs[rtd->doc_cur]->fit_method = NATURAL;
						else if (event.key.keysym.sym == 'w')
							rtd->docs[rtd->doc_cur]->fit_method = WIDTH;
						else if (event.key.keysym.sym == 'h')
							rtd->docs[rtd->doc_cur]->fit_method = HEIGHT;
						else if (event.key.keysym.sym == 'p')
							rtd->docs[rtd->doc_cur]->fit_method = PAGE;
						
						if (event.key.keysym.sym == 'n'
							|| event.key.keysym.sym == 'w'
							|| event.key.keysym.sym == 'h'
							|| event.key.keysym.sym == 'p')
						{
							rtd->docs[rtd->doc_cur]->finescale = 1;
							rtd->docs[rtd->doc_cur]->xoffset = 0;
							rtd->docs[rtd->doc_cur]->yoffset = 0;
							flags |= FLAG_RENDER;
						}
					}
					else if (state == MIRROR)
					{
						state = NORMAL;
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						if (event.key.keysym.sym == 'h')
						{
							Green_MirrorH( rtd->docs[rtd->doc_cur] );
							flags |= FLAG_RENDER;
						}
						else if (event.key.keysym.sym == 'v')
						{
							Green_MirrorV( rtd->docs[rtd->doc_cur] );
							flags |= FLAG_RENDER;
						}
					}
					else if (state == ROTATE)
					{
						state = NORMAL;
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						if (event.key.keysym.sym == 'l')
						{
							Green_RotateLeft( rtd->docs[rtd->doc_cur] );
							Green_ValidateOffset( rtd->docs[rtd->doc_cur], display->w, display->h );
							flags |= FLAG_RENDER;
						}
						else if (event.key.keysym.sym == 'r')
						{
							Green_RotateRight( rtd->docs[rtd->doc_cur] );
							Green_ValidateOffset( rtd->docs[rtd->doc_cur], display->w, display->h );
							flags |= FLAG_RENDER;
						}
					}
					
					break;
				case SDL_VIDEORESIZE:
					display = SDL_SetVideoMode( event.resize.w, event.resize.h, 0, SDL_HWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE );
					if (!display)
					{
						SDL_Quit();
						fprintf( stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError() );
						return -5;
					}
					
					if (Green_IsDocValid( rtd, rtd->doc_cur ))
						Green_ValidateOffset( rtd->docs[rtd->doc_cur], display->w, display->h );
					
					flags |= FLAG_RENDER;
					break;
				case SDL_MOUSEMOTION:
					if (rtd->mouse.visibility > 0)
					{
						mouse_last = SDL_GetTicks();
						SDL_ShowCursor( SDL_ENABLE );
					}
					
					break;
				case SDL_MOUSEBUTTONDOWN:
					if (rtd->mouse.visibility > 0)
					{
						mouse_last = SDL_GetTicks();
						SDL_ShowCursor( SDL_ENABLE );
					}
					
					if (!(rtd->mouse.flags&0x01) || !Green_IsDocValid( rtd, rtd->doc_cur ))
						break;
					
					switch (event.button.button)
					{
						case SDL_BUTTON_LEFT:
							left_x = event.button.x;
							left_y = event.button.y;
							break;
						case SDL_BUTTON_RIGHT:
							right_x = event.button.x;
							right_y = event.button.y;
							break;
						case SDL_BUTTON_WHEELDOWN:
							Green_Zoom( rtd->docs[rtd->doc_cur], display->w, display->h, rtd->docs[rtd->doc_cur]->finescale * rtd->zoomstep );
							Render( rtd );
							break;
						case SDL_BUTTON_WHEELUP:
							Green_Zoom( rtd->docs[rtd->doc_cur], display->w, display->h, rtd->docs[rtd->doc_cur]->finescale / rtd->zoomstep );
							Render( rtd );
							break;
					}
					
					break;
				case SDL_MOUSEBUTTONUP:
					if (rtd->mouse.visibility)
					{
						mouse_last = SDL_GetTicks();
						SDL_ShowCursor( SDL_ENABLE );
					}
					
					if (!(rtd->mouse.flags&0x01) || !Green_IsDocValid( rtd, rtd->doc_cur ))
						break;
					
					if (event.button.button == SDL_BUTTON_RIGHT)
					{
						Green_ScrollRelative( rtd->docs[rtd->doc_cur], right_x - event.button.x, right_y - event.button.y, display->w, display->h, 1 );
						flags |= FLAG_RENDER;
					}
					
					break;
				case SDL_USEREVENT:
					if (rtd->mouse.visibility > 0)
					{
						mouse_cur = SDL_GetTicks();
						if ((Uint32)(mouse_cur - mouse_last) > rtd->mouse.visibility)
							SDL_ShowCursor( SDL_DISABLE );
					}
					
					SDL_GetMouseState( &x, &y );
					if (rtd->mouse.border_size && Green_IsDocValid( rtd, rtd->doc_cur ) && x >= 0 && y >= 0 && x <= display->w && y <= display->h)
					{
						width = display->w * rtd->mouse.border_size / 100;
						height = display->h * rtd->mouse.border_size / 100;
						
						if (x < width)
							width = -((width - x) * display->w / width * rtd->mouse.border_speed * live_interval / 1000);
						else if (x > display->w - width)
							width = (x + width - display->w) * display->w / width * rtd->mouse.border_speed * live_interval / 1000;
						else
							width = 0;
						
						if (y < height)
							height = -((height - y) * display->h / height * rtd->mouse.border_speed * live_interval / 1000);
						else if (y > display->h - height)
							height = (y + height - display->h) * display->h / height * rtd->mouse.border_speed * live_interval / 1000;
						else
							height = 0;
						
						if (width || height)
						{
							Green_ScrollRelative( rtd->docs[rtd->doc_cur], width, height, display->w, display->h, 0 );
							flags |= FLAG_RENDER;
						}
					}
					
					break;
			}
			
			event_count++;
			
		}	while (event_count && SDL_PollEvent( &event ));
		
	}	while (!(flags&FLAG_QUIT));
	
	if (timer)
		SDL_RemoveTimer( timer );
	SDL_Quit();
	return 0;
}
