/**
 * \file mon-list.c
 * \brief Monster list UI.
 *
 * Copyright (c) 1997-2007 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2013 Ben Semmler
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

#include "game-world.h"
#include "mon-desc.h"
#include "mon-list.h"
#include "mon-predicate.h"
#include "project.h"

/**
 * Allocate a new monster list based on the size of the current cave's monster
 * array.
 */
monster_list_t *monster_list_new(void)
{
	monster_list_t *list = mem_zalloc(sizeof(monster_list_t));
	size_t size = cave_monster_max(cave);

	if (list == NULL)
		return NULL;

	list->entries = mem_zalloc(size * sizeof(monster_list_entry_t));

	if (list->entries == NULL) {
		mem_free(list);
		return NULL;
	}

	list->entries_size = size;

	return list;
}

/**
 * Free a monster list.
 */
void monster_list_free(monster_list_t *list)
{
	if (list == NULL)
		return;

	if (list->entries != NULL) {
		mem_free(list->entries);
		list->entries = NULL;
	}

	mem_free(list);
	list = NULL;
}

/**
 * Shared monster list instance.
 */
static monster_list_t *monster_list_subwindow = NULL;

/**
 * Initialize the monster list module.
 */
void monster_list_init(void)
{
	monster_list_subwindow = NULL;
}

/**
 * Tear down the monster list module.
 */
void monster_list_finalize(void)
{
	monster_list_free(monster_list_subwindow);
}

/**
 * Return a common monster list instance.
 */
monster_list_t *monster_list_shared_instance(void)
{
	if (monster_list_subwindow == NULL) {
		monster_list_subwindow = monster_list_new();
	}

	return monster_list_subwindow;
}

/**
 * Return true if there is nothing preventing the list from being updated. This
 * should be for structural sanity checks and not gameplay checks.
 */
static bool monster_list_can_update(const monster_list_t *list)
{
	if (list == NULL || list->entries == NULL)
		return false;

	return (int)list->entries_size >= cave_monster_max(cave);
}

/**
 * Zero out the contents of a monster list. If needed, this function will
 * reallocate the entry list if the number of monsters has changed.
 */
void monster_list_reset(monster_list_t *list)
{
	if (list == NULL || list->entries == NULL)
		return;

	if ((int)list->entries_size < cave_monster_max(cave)) {
		list->entries = mem_realloc(list->entries, sizeof(list->entries[0])
									* cave_monster_max(cave));
		list->entries_size = cave_monster_max(cave);
	}

	memset(list->entries, 0, list->entries_size * sizeof(monster_list_entry_t));
	memset(list->total_entries, 0, MONSTER_LIST_SECTION_MAX * sizeof(u16b));
	memset(list->total_monsters, 0, MONSTER_LIST_SECTION_MAX * sizeof(u16b));
	list->distinct_entries = 0;
	list->creation_turn = 0;
	list->sorted = false;
}

/**
 * Collect monster information from the current cave's monster list.
 */
void monster_list_collect(monster_list_t *list)
{
	int i;

	if (list == NULL || list->entries == NULL)
		return;

	if (!monster_list_can_update(list))
		return;

	/* Use cave_monster_max() here in case the monster list isn't compacted. */
	for (i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);
		monster_list_entry_t *entry = NULL;
		int j, field;
		bool los = false;

		/* Only consider visible, known monsters */
		if (!monster_is_visible(mon) ||	monster_is_camouflaged(mon))
			continue;

		/* Find or add a list entry. */
		for (j = 0; j < (int)list->entries_size; j++) {
			if (list->entries[j].race == NULL) {
				/* We found an empty slot, so add this race here. */
				entry = &list->entries[j];
				memset(entry, 0, sizeof(monster_list_entry_t));
				entry->race = mon->race;
				break;
			}
			else if (list->entries[j].race == mon->race) {
				/* We found a matching race and we'll use that. */
				entry = &list->entries[j];
				break;
			}
		}

		if (entry == NULL)
			continue;

		/* Always collect the latest monster attribute so that flicker
		 * animation works. If this is 0, it needs to be replaced by 
		 * the standard glyph in the UI */
		entry->attr = mon->attr;

		/*
		 * Check for LOS
		 * Hack - we should use (mon->mflag & (MFLAG_VIEW)) here,
		 * but this does not catch monsters detected by ESP which are
		 * targetable, so we cheat and use projectable() instead
		 */
		los = projectable(cave, player->grid, mon->grid, PROJECT_NONE);
		field = (los) ? MONSTER_LIST_SECTION_LOS : MONSTER_LIST_SECTION_ESP;

		if (mon->m_timed[MON_TMD_SLEEP] > 0)
			entry->asleep[field]++;

		/* Store the closest location offset from the player */
		const int dx = mon->grid.x - player->grid.x;
		const int dy = mon->grid.y - player->grid.y;

		/* if a new entry, just set the location */
		if (entry->count[field] == 0) {
			entry->dx[field] = dx;
			entry->dy[field] = dy;
		} else {
			/* Use the closest monster */
			const int distance = dx > dy ? dx : dy;
			const int old_distance =
					entry->dx[field] > entry->dy[field] ? entry->dx[field]
														: entry->dy[field];

			if (distance < old_distance) {
				entry->dx[field] = dx;
				entry->dy[field] = dy;
			}
		}
		entry->count[field]++;
	}

	/* Collect totals for easier calculations of the list. */
	for (i = 0; i < (int)list->entries_size; i++) {
		if (list->entries[i].race == NULL)
			continue;

		if (list->entries[i].count[MONSTER_LIST_SECTION_LOS] > 0)
			list->total_entries[MONSTER_LIST_SECTION_LOS]++;

		if (list->entries[i].count[MONSTER_LIST_SECTION_ESP] > 0)
			list->total_entries[MONSTER_LIST_SECTION_ESP]++;

		list->total_monsters[MONSTER_LIST_SECTION_LOS] +=
			list->entries[i].count[MONSTER_LIST_SECTION_LOS];
		list->total_monsters[MONSTER_LIST_SECTION_ESP] +=
			list->entries[i].count[MONSTER_LIST_SECTION_ESP];
		list->distinct_entries++;
	}

	list->creation_turn = turn;
	list->sorted = false;
}

/**
 * Standard comparison function for the monster list:
 * Sort by distance, then by level.
 */
int monster_list_standard_compare(const void *a, const void *b)
{
	const monster_list_entry_t *ea = (monster_list_entry_t *) a;
	const monster_list_entry_t *eb = (monster_list_entry_t *) b;

	int a_dx, a_dy, b_dx, b_dy;

	/* This is a bit of a hack to avoid a refactor of the entries into separate
	 * LOS and ESP lists. If an entry has any monsters in LOS, use the LOS
	 * distance. Otherwise, use ESP distance. */
	if (ea->count[MONSTER_LIST_SECTION_LOS] > 0) {
		a_dx = abs(ea->dx[MONSTER_LIST_SECTION_LOS]);
		a_dy = abs(ea->dy[MONSTER_LIST_SECTION_LOS]);
	} else {
		a_dx = abs(ea->dx[MONSTER_LIST_SECTION_ESP]);
		a_dy = abs(ea->dy[MONSTER_LIST_SECTION_ESP]);
	}

	if (eb->count[MONSTER_LIST_SECTION_LOS] > 0) {
		b_dx = abs(eb->dx[MONSTER_LIST_SECTION_LOS]);
		b_dy = abs(eb->dy[MONSTER_LIST_SECTION_LOS]);
	} else {
		b_dx = abs(eb->dx[MONSTER_LIST_SECTION_ESP]);
		b_dy = abs(eb->dy[MONSTER_LIST_SECTION_ESP]);
	}

	/* distance is the number of moves (including diagonals) it would take to
	 * get to that grid, equal to the larger of the two diff coordinates
	 * minor is the other diff coordinate, likely we will encounter the
	 * monster with a smealler value first given tunnels in cardinal directions
	 */

	const int a_distance = a_dx > a_dy ? a_dx : a_dy;
	const int a_minor = a_dx > a_dy ? a_dy : a_dx;
	const int b_distance = b_dx > b_dy ? b_dx : b_dy;
	const int b_minor = b_dx > b_dy ? b_dy : b_dx;

	if (a_distance < b_distance)
		return -1;

	if (b_distance < a_distance)
		return 1;

	if (a_minor < b_minor)
		return -1;

	if (b_minor < a_minor)
		return 1;

	/* If this happens, something might be wrong in the collect function. */
	if (ea->race == NULL || eb->race == NULL)
		return 0;

	/* If they are the same distance, order by monster level */
	if (ea->race->level > eb->race->level)
		return -1;

	if (ea->race->level < eb->race->level)
		return 1;

	return 0;
}

/**
 * Sort the monster list with the given sort function.
 */
void monster_list_sort(monster_list_t *list,
					   int (*compare)(const void *, const void *))
{
	size_t elements;

	if (list == NULL || list->entries == NULL)
		return;

	if (list->sorted)
		return;

	elements = list->distinct_entries;

	if (elements <= 1)
		return;

	sort(list->entries, MIN(elements, list->entries_size), sizeof(list->entries[0]), compare);
	list->sorted = true;
}

/**
 * Return an color to display a particular list entry with.
 *
 * \param entry is the monster list entry to display.
 * \return a color for the monster entry.
 */
byte monster_list_entry_line_color(const monster_list_entry_t *entry)
{
	/* Display uniques in a special colour */
	if (rf_has(entry->race->flags, RF_UNIQUE))
		return COLOUR_VIOLET;
	else if (entry->race->level > player->depth)
		return COLOUR_RED;
	else
		return COLOUR_WHITE;
}

