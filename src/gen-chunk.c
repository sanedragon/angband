/** \file gen-chunk.c 
    \brief Handling of chunks of cave
 
 *
 * Copyright (c) 2014 Nick McConnell
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "cave.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "obj-util.h"
#include "trap.h"

#define CHUNK_LIST_INCR 10
struct cave **chunk_list;
u16b chunk_list_max = 0;

/**
 * Write a chunk to memory and return a pointer to it.  Optionally write
 * monsters, objects and/or traps, and optionally delete those things from
 * the source chunk
 */
struct cave *chunk_write(int y0, int x0, int height, int width, bool monsters,
						 bool objects, bool traps, bool delete_old)
{
	int i;
	int x, y;

	struct cave *new = cave_new(height, width);

	/* Write the location stuff */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			int this_o_idx, next_o_idx, held;

			/* Terrain */
			new->feat[y][x] = cave->feat[y0 + y][x0 + x];
			sqinfo_copy(new->info[y][x], cave->info[y0 + y][x0 + x]);

			/* Dungeon objects */
			if (objects){
				if (square_object(cave, y0 + y, x0 + x)) {
					new->o_idx[y][x] = cave_object_count(new) + 1;
					for (this_o_idx = cave->o_idx[y0 + y][x0 + x]; this_o_idx;
						 this_o_idx = next_o_idx) {
						object_type *source_obj = cave_object(cave, this_o_idx);
						object_type *dest_obj = cave_object(new, ++new->obj_cnt);
						
						/* Copy over */
						object_copy(dest_obj, source_obj);
						
						/* Adjust stuff */
						dest_obj->iy = y;
						dest_obj->ix = x;
						next_o_idx = source_obj->next_o_idx;
						if (next_o_idx)
							dest_obj->next_o_idx = new->obj_cnt + 1;
						if (delete_old) delete_object_idx(this_o_idx);
					}
				}
			}

			/* Monsters and held objects */
			if (monsters){
				held = 0;
				if (cave->m_idx[y0 + y][x0 + x] > 0) {
					monster_type *source_mon = square_monster(cave, y0 + y, x0 + x);
					monster_type *dest_mon = NULL;

					/* Valid monster */
					if (!source_mon->race)
						continue;

					/* Copy over */
					new->m_idx[y][x] = ++new->mon_cnt;
					dest_mon = cave_monster(new, new->mon_cnt);
					memcpy(dest_mon, source_mon, sizeof(*source_mon));

					/* Adjust position */
					dest_mon->fy = y;
					dest_mon->fx = x;

					/* Held objects */
					if (objects && source_mon->hold_o_idx) {
						for (this_o_idx = source_mon->hold_o_idx; this_o_idx;
							 this_o_idx = next_o_idx) {
							object_type *source_obj = cave_object(cave, this_o_idx);
							object_type *dest_obj = cave_object(new, ++new->obj_cnt);
							
							/* Copy over */
							object_copy(dest_obj, source_obj);

							/* Adjust stuff */
							dest_obj->iy = y;
							dest_obj->ix = x;
							next_o_idx = source_obj->next_o_idx;
							if (next_o_idx)
								dest_obj->next_o_idx = cave_object_count(new) + 1;
							dest_obj->held_m_idx = cave_monster_count(new);
							if (!held)
								held = cave_object_count(new);
							if (delete_old) delete_object_idx(this_o_idx);
						}
					}
					dest_mon->hold_o_idx = held;
					if (delete_old) delete_monster(y0 + y, x0 + x);
				}
			}
		}
	}

	/* Traps */
	if (traps){
		for (i = 0; i < cave_trap_max(cave); i++) {
			/* Point to this trap */
			trap_type *t_ptr = cave_trap(cave, i);
			trap_type *u_ptr = cave_trap(new, cave_trap_max(new) + 1);
			int ty = t_ptr->fy;
			int tx = t_ptr->fx;

			if ((ty >= y0) && (ty < y0 + height) &&
				(tx >= x0) && (tx < x0 + width)) {
				/* Copy over */
				memcpy(u_ptr, t_ptr, sizeof(*t_ptr));

				/* Adjust stuff */
				new->trap_max++;
				u_ptr->fy = ty - y0;
				u_ptr->fx = tx - x0;
				if (delete_old)
					square_remove_trap(cave, t_ptr->fy, t_ptr->fx, FALSE, i);
			}
		}
	}

	return new;
}

/**
 * Add an entry to the chunk list - any problems with the length of this will
 * be more in the memory used by the chunks themselves rather than the list
 */
void chunk_list_add(struct cave *c)
{
	int newsize = (chunk_list_max + CHUNK_LIST_INCR) *	sizeof(struct cave *);

	/* Lengthen the list if necessary */
	if (chunk_list_max == 0)
		chunk_list = mem_zalloc(newsize);
	else if ((chunk_list_max % CHUNK_LIST_INCR) == 0)
		chunk_list = (struct cave **) mem_realloc(chunk_list, newsize);

	/* Add the new one */
	chunk_list[chunk_list_max++] = c;
}

/**
 * Remove an entry from the chunk list, return whether it was found
 */
bool chunk_list_remove(char *name)
{
	int i, j;
	int newsize = 0;

	for (i = 0; i < chunk_list_max; i++) {
		/* Find the match */
		if (!strcmp(name, chunk_list[i]->name)) {
			/* Copy all the succeeding ones back one */
			for (j = i + 1; j < chunk_list_max; j++)
				chunk_list[j - 1] = chunk_list[j];

			/* Destroy the last one, and shorten the list */
			if ((chunk_list_max % CHUNK_LIST_INCR) == 0)
				newsize = (chunk_list_max - CHUNK_LIST_INCR) *	
					sizeof(struct cave *);
			chunk_list_max--;
			chunk_list[chunk_list_max] = NULL;
			if (newsize)
				chunk_list = (struct cave **) mem_realloc(chunk_list, newsize);

			return TRUE;
		}
	}

	return FALSE;
}

/**
 * Find a chunk by name
 */
struct cave *chunk_find_name(char *name)
{
	int i;

	for (i = 0; i < chunk_list_max; i++)
		if (!strcmp(name, chunk_list[i]->name))
			return chunk_list[i];

	return NULL;
}

/**
 * Find a chunk by pointer
 */
bool chunk_find(struct cave *c)
{
	int i;

	for (i = 0; i < chunk_list_max; i++)
		if (c == chunk_list[i]) return TRUE;

	return FALSE;
}

/**
 * Transform y, x coordinates by rotation, reflection and translation
 * Stolen from PosChengband
 */
void symmetry_transform(int *y, int *x, int y0, int x0, int height, int width,
						int rotate, bool reflect)
{
	int i;

	/* Rotate (in multiples of 90 degrees clockwise) */
    for (i = 0; i < rotate % 4; i++)
    {
        int temp = *x;
        *x = height - 1 - (*y);
        *y = temp;
    }

	/* Reflect (horizontally) */
	if (reflect)
		*x = width - 1 - *x;

	/* Translate */
	*y += y0;
	*x += x0;
}

/**
 * Write a chunk, transformed, to a given offset in another chunk
 */
bool chunk_copy(struct cave *dest, struct cave *source, int y0, int x0,
				int rotate, bool reflect)
{
	int i;
	int y, x;
	int h = source->height, w = source->width;

	/* Check bounds */
	if (rotate % 1) {
		if ((w + y0 > dest->height) || (h + x0 > dest->width))
			return FALSE;
	} else {
		if ((h + y0 > dest->height) || (w + x0 > dest->width))
			return FALSE;
	}

	/* Write the location stuff */
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			int this_o_idx, next_o_idx, held;
			bool first_obj = TRUE;

			/* Work out where we're going */
			int dest_y = y;
			int dest_x = x;
			symmetry_transform(&dest_y, &dest_x, y0, x0, h, w, rotate, reflect);

			/* Terrain */
			dest->feat[dest_y][dest_x] = source->feat[y][x];
			sqinfo_copy(dest->info[dest_y][dest_x], source->info[y][x]);

			/* Dungeon objects */
			held = 0;
			if (source->o_idx[y][x]) {
				for (this_o_idx = source->o_idx[y][x]; this_o_idx;
					 this_o_idx = next_o_idx) {
					object_type *source_obj = cave_object(source, this_o_idx);
					object_type *dest_obj = NULL;
					int o_idx;

					/* Is this the first object on this square? */
					if (first_obj) {
						/* Make an object */
						o_idx = o_pop(dest);

						/* Hope this never happens */
						if (!o_idx)
							break;

						/* Mark this square as holding this object */
						dest->o_idx[dest_y][dest_x] = o_idx;

						first_obj = FALSE;
					}

					/* Copy over */
					dest_obj = cave_object(dest, o_idx);
					object_copy(dest_obj, source_obj);

					/* Adjust position */
					dest_obj->iy = dest_y;
					dest_obj->ix = dest_x;

					/* Tell the monster on this square what it's holding */
					if (source_obj->held_m_idx) {
						if (!held)
							held = o_idx;
					}

					/* Look at the next object, if there is one */
					next_o_idx = source_obj->next_o_idx;

					/* Make a slot for it if there is, and point to it */
					if (next_o_idx) {
						o_idx = o_pop(dest);
						if (!o_idx)
							break;
						dest_obj->next_o_idx = o_idx;
					}
				}
			}

			/* Monsters */
			if (source->m_idx[y][x] > 0) {
				monster_type *source_mon = square_monster(source, y, x);
				monster_type *dest_mon = NULL;
				int idx;

				/* Valid monster */
				if (!source_mon->race)
					continue;

				/* Make a monster */
				idx = mon_pop(dest);

				/* Hope this never happens */
				if (!idx)
					break;

				/* Copy over */
				dest_mon = cave_monster(dest, idx);
				dest->m_idx[dest_y][dest_x] = idx;
				memcpy(dest_mon, source_mon, sizeof(*source_mon));

				/* Adjust stuff */
				dest_mon->midx = idx;
				dest_mon->fy = dest_y;
				dest_mon->fx = dest_x;
				dest_mon->hold_o_idx = held;
				cave_object(dest, held)->held_m_idx = idx;
			}

			/* Player */
			if (source->m_idx[y][x] == -1) 
				dest->m_idx[dest_y][dest_x] = -1;
		}
	}

	/* Traps */
	for (i = 0; i < cave_trap_max(source); i++) {
		/* Point to this trap */
		trap_type *t_ptr = cave_trap(source, cave_trap_max(dest) + 1);
		trap_type *u_ptr = cave_trap(dest, i);

		/* Copy over */
		memcpy(u_ptr, t_ptr, sizeof(*t_ptr));

		/* Adjust stuff */
		dest->trap_max++;
		y = t_ptr->fy;
		x = t_ptr->fx;
		symmetry_transform(&y, &x, y0, x0, h, w, rotate, reflect);
		u_ptr->fy = y;
		u_ptr->fx = x;
	}

	/* Miscellany */
	for (i = 0; i < z_info->f_max + 1; i++)
		dest->feat_count[i] += source->feat_count[i];

	dest->obj_rating += source->obj_rating;
	dest->mon_rating += source->mon_rating;

	if (source->good_item)
		dest->good_item = TRUE;

	return TRUE;
}
