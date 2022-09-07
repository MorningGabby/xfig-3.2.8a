/*
 * FIG : Facility for Interactive Generation of figures
 * Copyright (c) 1985-1988 by Supoj Sutanthavibul
 * Parts Copyright (c) 1989-2015 by Brian V. Smith
 * Parts Copyright (c) 1991 by Paul King
 * Parts Copyright (c) 2016-2020 by Thomas Loimer
 *
 * Parts Copyright (c) 1995 by C. Blanc and C. Schlick
 *
 * Any party obtaining a copy of these files is granted, free of charge, a
 * full and unrestricted irrevocable, world-wide, paid up, royalty-free,
 * nonexclusive right and license to deal in this software and documentation
 * files (the "Software"), including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense and/or sell copies of
 * the Software, and to permit persons who receive copies from any such
 * party to do so, with the only requirement being that the above copyright
 * and this permission notice remain intact.
 *
 */

/**************** IMPORTS ****************/

#include "fig.h"
#include "resources.h"
#include "mode.h"
#include "object.h"
#include "paintop.h"
#include "e_addpt.h"
#include "e_arrow.h"
#include "e_convert.h"
#include "u_draw.h"
#include "u_elastic.h"
#include "u_free.h"
#include "u_list.h"
#include "u_redraw.h"
#include "u_undo.h"
#include "w_canvas.h"
#include "w_drawprim.h"
#include "w_file.h"
#include "w_layers.h"
#include "w_msgpanel.h"
#include "w_setup.h"

#include "e_deletept.h"
#include "e_scale.h"
#include "f_read.h"
#include "u_bound.h"
#include "u_create.h"

#include "u_markers.h"
#include "u_translate.h"
#include "w_cmdpanel.h"
#include "w_indpanel.h"
#include "w_color.h"

extern void	swap_depths(void);	/* w_layers.c */
extern void	swap_counts(void);	/* w_layers.c */

/*************** EXPORTS *****************/

/*
 * Object_tails *usually* points to the last object in each linked list in
 * objects.  The exceptions occur when multiple objects are added to a figure
 * (e.g. file read, break compound, undo delete region).  In these cases,
 * the added objects are appended to the object lists (and saved_objects is
 * set up to point to the new objects) but object_tails is not changed.
 * This speeds up a subsequent undo operation which need only set
 * all the "next" fields of objects pointed to by object_tails to NULL.
 */

F_compound	saved_objects = {0, 0, {0, 0}, {0, 0}, NULL, NULL, NULL, NULL,
				NULL, NULL, NULL, NULL, False, NULL, NULL};
F_compound	object_tails = {0, 0, {0, 0}, {0, 0}, NULL, NULL, NULL, NULL,
				NULL, NULL, NULL, NULL, False, NULL, NULL};
F_arrow		*saved_for_arrow = (F_arrow *) NULL;
F_arrow		*saved_back_arrow = (F_arrow *) NULL;
F_line		*latest_line;		/* for undo_join (line) */
F_spline	*latest_spline;		/* for undo_join (spline) */

int		last_action = F_NULL;

/*************** LOCAL *****************/

static int	last_object;
static F_pos	last_position, new_position;
static int	last_arcpointnum;
static F_point *last_prev_point, *last_selected_point, *last_next_point;
static F_sfactor  *last_selected_sfactor;
static F_linkinfo *last_links;
static F_arrow    *last_for_arrow, *last_back_arrow;
static int	last_linkmode;
static double	last_origin_tension, last_extremity_tension;
static Boolean freeze_redo_cleanup = False;
static Boolean freeze_undo_additions = False;
static F_history *undo_stack;
static F_history *redo_stack;


void undo_add (F_history **stack);
void undo_delete (F_history **stack);
void undo_move (F_history **stack);
void undo_change (F_history **stack);
void undo_glue (F_history **stack);
void undo_break (F_history **stack);
void undo_load (F_history **stack);
void undo_scale (F_history **stack);
void undo_addpoint (F_history **stack);
void undo_deletepoint (F_history **stack);
void undo_add_arrowhead (F_history **stack);
void undo_delete_arrowhead (F_history **stack);
void undo_convert (F_history **stack);
void undo_open_close (F_history **stack);
void undo_join_split (F_history **stack);
void set_action_object (int action, int object);
void swap_newp_lastp (F_history **stack);
void pop_stack(F_history **stack);
void swap_stack(F_history **stack);

void
undo(void)
{
    /* turn off Compose key LED */
    setCompLED(0);

	if(undo_stack == NULL)
	{
		put_msg("nothing to undo");
		return;
	}
    switch (undo_stack->last_action) {
      case F_ADD:
		  fprintf(stdout, "ADD\n");
	undo_add(&undo_stack);
	break;
      case F_DELETE:
	undo_delete(&undo_stack);
	break;
      case F_MOVE:
	undo_move(&undo_stack);
	break;
      case F_EDIT:
	undo_change(&undo_stack);
	break;
      case F_GLUE:
	undo_glue(&undo_stack);
	break;
      case F_BREAK:
	undo_break(&undo_stack);
	break;
      case F_LOAD:
	undo_load(&undo_stack);
	break;
      case F_SCALE:
		undo_scale(&undo_stack);
	break;
      case F_ADD_POINT:
	undo_addpoint(&undo_stack);
	break;
      case F_DELETE_POINT:
	undo_deletepoint(&undo_stack);
	break;
      case F_ADD_ARROW_HEAD:
	undo_add_arrowhead(&undo_stack);
	break;
      case F_DELETE_ARROW_HEAD:
	undo_delete_arrowhead(&undo_stack);
	break;
      case F_CONVERT:
	undo_convert(&undo_stack);
	break;
      case F_OPEN_CLOSE:
	undo_open_close(&undo_stack);
	break;
      case F_JOIN:
      case F_SPLIT:
	undo_join_split(&undo_stack);
	break;
    default:
	put_msg("Nothing to UNDO");
	return;
    }
    put_msg("Undo complete");
}

void
redo(void)
{
    /* turn off Compose key LED */
    setCompLED(0);

	if(redo_stack == NULL)
	{
		put_msg("Nothing to REDO");
		return;
	}
	fprintf(stdout, "last_action: %d\n", redo_stack->last_action);
    switch (redo_stack->last_action) {
      case F_ADD:
		  fprintf(stdout, "ADD\n");
	undo_add(&redo_stack);
	break;
      case F_DELETE:
	undo_delete(&redo_stack);
	break;
      case F_MOVE:
	undo_move(&redo_stack);
	break;
      case F_EDIT:
	undo_change(&redo_stack);
	break;
      case F_GLUE:
	undo_glue(&redo_stack);
	break;
      case F_BREAK:
	undo_break(&redo_stack);
	break;
      case F_LOAD:
	undo_load(&redo_stack);
	break;
      case F_SCALE:
		undo_scale(&redo_stack);
	break;
      case F_ADD_POINT:
	undo_addpoint(&redo_stack);
	break;
      case F_DELETE_POINT:
	undo_deletepoint(&redo_stack);
	break;
      case F_ADD_ARROW_HEAD:
	undo_add_arrowhead(&redo_stack);
	break;
      case F_DELETE_ARROW_HEAD:
	undo_delete_arrowhead(&redo_stack);
	break;
      case F_CONVERT:
	undo_convert(&redo_stack);
	break;
      case F_OPEN_CLOSE:
	undo_open_close(&redo_stack);
	break;
      case F_JOIN:
      case F_SPLIT:
	undo_join_split(&redo_stack);
	break;
    default:
	put_msg("Nothing to REDO");
	return;
    }
    put_msg("Redo complete");
}

void undo_join_split(F_history **stack)
{
    F_line	    swp_l;
    F_spline	    swp_s;
    if (last_object == O_POLYLINE) {
	new_l = (*stack)->saved_objects->lines;		/* the original */
	old_l = (*stack)->latest_line;			/* the changed object */
	/* swap old with new */
	memcpy(&swp_l, old_l, sizeof(F_line));
	memcpy(old_l, new_l, sizeof(F_line));
	memcpy(new_l, &swp_l, sizeof(F_line));
	/* this assumes that the object are at the end of the objects list */
	/* correct the depth counts if necessary */
	if (!new_l->next && old_l->next){ /* join undo */
          add_depth(O_POLYLINE, old_l->next->depth);
        }
	else if (new_l->next && !old_l->next){ /* split undo */
          remove_depth(O_POLYLINE, new_l->next->depth);
        }
	//set_action_object(F_JOIN, O_POLYLINE);
	(*stack)->last_action = F_JOIN;
	(*stack)->last_object = O_POLYLINE;
	redisplay_lines(new_l, old_l);
    } else {
	new_s = (*stack)->saved_objects->splines;		/* the original */
	old_s = (*stack)->latest_spline;			/* the changed object */
	/* swap old with new */
	memcpy(&swp_s, old_s, sizeof(F_spline));
	memcpy(old_s, new_s, sizeof(F_spline));
	memcpy(new_s, &swp_s, sizeof(F_spline));
	/* this assumes that the object are at the end of the objects list */
	/* correct the depth counts if necessary */
	if (!new_s->next && old_s->next){ /* join undo */
          add_depth(O_SPLINE, old_s->next->depth);
        }
	else if (new_s->next && !old_s->next){ /* split undo */
          remove_depth(O_SPLINE, new_s->next->depth);
        }
	(*stack)->last_action = F_JOIN;
	(*stack)->last_object = O_SPLINE;
	redisplay_splines(new_s, old_s);
    }

	swap_stack(stack);
} 

void undo_addpoint(F_history **stack)
{

	freeze_redo_cleanup = True;
    if ((*stack)->last_object == O_POLYLINE)
	{
	linepoint_deleting((*stack)->saved_objects->lines, (*stack)->last_prev_point,
			   (*stack)->last_selected_point);
	}
    else
	{
	splinepoint_deleting((*stack)->saved_objects->splines, (*stack)->last_prev_point,
			     (*stack)->last_selected_point);
	}

	//manually remove new undo stack element, with order dependant on which stack is passed
	if(*stack == undo_stack)
	{
		swap_stack(stack);
		pop_stack(stack);
	}
	else
	{
		swap_stack(stack);
		pop_stack(&undo_stack);

	}
	freeze_redo_cleanup = False;

}

//Everything done in this function is for the same reason as undo_addpoint()
void undo_deletepoint(F_history **stack)
{

	freeze_redo_cleanup = True;
    if ((*stack)->last_object == O_POLYLINE) {
	linepoint_adding((*stack)->saved_objects->lines, (*stack)->last_prev_point,
			 (*stack)->last_selected_point);
	/* turn back on all relevant markers */
	update_markers(new_objmask);

    } else {	/* last_object is a spline */
	splinepoint_adding((*stack)->saved_objects->splines, (*stack)->last_prev_point,
			 (*stack)->last_selected_point, (*stack)->last_next_point,
			 (*stack)->last_selected_sfactor->s);
    }

	//Realistically, this should free the point. Since this may have caused problems, it was set to NULL instead.
    (*stack)->last_next_point = NULL;

	if(*stack == undo_stack)
	{
		swap_stack(stack);
		pop_stack(stack);
	}
	else
	{
		swap_stack(stack);
		pop_stack(&undo_stack);

	}
	freeze_redo_cleanup = False;

}

//This mostly works, with the exception that occasionally an object is added into both the compound and &objects
//This is likely due to the order the objects are in inside of the &objects list
void undo_break(F_history **stack)
{
	
	list_delete_objects(&objects, (*stack)->saved_objects->compounds);
    /* remove the depths from this compound because they'll be added in right after */
    list_add_compound(&objects.compounds, (*stack)->saved_objects->compounds);
    (*stack)->last_action = F_GLUE;
    toggle_markers_in_compound((*stack)->saved_objects->compounds);
    mask_toggle_compoundmarker((*stack)->saved_objects->compounds);
	swap_stack(stack);
}

void undo_glue(F_history **stack)
{
	//remove compound
    list_delete_compound(&objects.compounds, (*stack)->saved_objects->compounds);
    tail(&objects, &object_tails);
	//add objects from deleted compound to &objects
    append_objects(&objects, (*stack)->saved_objects->compounds, &object_tails);
    /* add the depths from this compound because they weren't added by the append_objects() */
    add_compound_depth((*stack)->saved_objects->compounds);
	(*stack)->last_action = F_BREAK;

	//do mask logic so markers don't appear in compound
    mask_toggle_compoundmarker((*stack)->saved_objects->compounds);
    toggle_markers_in_compound((*stack)->saved_objects->compounds);
    if (cur_mode != F_GLUE && cur_mode != F_BREAK)
	{
		set_tags((*stack)->saved_objects->compounds, 0);
	}
	swap_stack(stack);
}
void undo_convert(F_history **stack)
{

    switch ((*stack)->last_object) {
      case O_POLYLINE:
	if ((*stack)->saved_objects->lines->type == T_BOX ||
	    (*stack)->saved_objects->lines->type == T_ARCBOX)
	{
		box_2_box((*stack)->latest_line);
	}
	else
	{
		spline_line((*stack)->saved_objects->splines);
	}
	break;
      case O_SPLINE:
	line_spline((*stack)->saved_objects->lines, (*stack)->next->saved_objects->splines->type);
	break;
    }


	if(*stack == undo_stack)
	{
	swap_stack(stack);
	pop_stack(&undo_stack);
	pop_stack(&undo_stack);
	redo_stack->saved_objects->lines = undo_stack->saved_objects->lines;
	}
	else
	{
	pop_stack(&undo_stack);
	pop_stack(&undo_stack);
	redo_stack->saved_objects->lines = undo_stack->saved_objects->lines;


	}
}

void undo_add_arrowhead(F_history **stack)
{

	freeze_redo_cleanup = True;
    switch ((*stack)->last_object) {
      case O_POLYLINE:
	delete_linearrow((*stack)->saved_objects->lines,
			 (*stack)->last_prev_point, (*stack)->last_selected_point);
	break;
      case O_SPLINE:
	delete_splinearrow((*stack)->saved_objects->splines,
			   (*stack)->last_prev_point, (*stack)->last_selected_point);
	break;
      case O_ARC:
	delete_arcarrow((*stack)->saved_objects->arcs, (*stack)->last_arcpointnum);
	break;
      default:
	return;
    }

	freeze_redo_cleanup = False;

	//get information from new arrows added
	F_arrow * tmp_for_arrow = (*stack)->saved_for_arrow;
	F_arrow * tmp_back_arrow = (*stack)->saved_back_arrow;
	
	pop_stack(stack); 
	
	//set saved arrow info
    (*stack)->last_action = F_DELETE_ARROW_HEAD;
	(*stack)->saved_for_arrow = tmp_for_arrow;
	(*stack)->saved_back_arrow = tmp_back_arrow;
	swap_stack(stack);
}

void undo_delete_arrowhead(F_history **stack)
{
    switch ((*stack)->last_object) {
      case O_POLYLINE:
	if ((*stack)->saved_for_arrow)
	    (*stack)->saved_objects->lines->for_arrow = (*stack)->saved_for_arrow;
	if ((*stack)->saved_back_arrow)
	    (*stack)->saved_objects->lines->back_arrow = (*stack)->saved_back_arrow;
	redisplay_line((*stack)->saved_objects->lines);
	break;
      case O_SPLINE:
	if ((*stack)->saved_for_arrow)
	    (*stack)->saved_objects->splines->for_arrow = (*stack)->saved_for_arrow;
	if ((*stack)->saved_back_arrow)
	    (*stack)->saved_objects->splines->back_arrow = (*stack)->saved_back_arrow;
	redisplay_spline((*stack)->saved_objects->splines);
	break;
      case O_ARC:
	if ((*stack)->saved_for_arrow)
	    (*stack)->saved_objects->arcs->for_arrow = (*stack)->saved_for_arrow;
	if ((*stack)->saved_back_arrow)
	    (*stack)->saved_objects->arcs->back_arrow = (*stack)->saved_back_arrow;
	redisplay_arc((*stack)->saved_objects->arcs);
	break;
      default:
	return;
    }
    (*stack)->last_action = F_ADD_ARROW_HEAD;
	swap_stack(stack);
}

void undo_change(F_history **stack)
{
    char	   *swp_comm;
    F_compound	    swp_c;
    F_line	    swp_l;
    F_spline	    swp_s;
    F_ellipse	    swp_e;
    F_arc	    swp_a;
    F_text	    swp_t;

    switch ((*stack)->last_object) {
      case O_POLYLINE:
	new_l = (*stack)->saved_objects->lines;		/* the original */
	old_l = (*stack)->saved_objects->lines->next;	/* the changed object */
	/* account for depths */
	remove_depth(O_POLYLINE, old_l->depth);
	add_depth(O_POLYLINE, new_l->depth);
	/* swap old with new */
	memcpy(&swp_l, old_l, sizeof(F_line));
	memcpy(old_l, new_l, sizeof(F_line));
	memcpy(new_l, &swp_l, sizeof(F_line));
	/* but keep the next pointers unchanged */
	swp_l.next = old_l->next;
	old_l->next = new_l->next;
	new_l->next = swp_l.next;

	redisplay_lines(new_l, old_l);
	break;
      case O_ELLIPSE:
	new_e = (*stack)->saved_objects->ellipses;
	old_e = (*stack)->saved_objects->ellipses->next;
	/* account for depths */
	remove_depth(O_ELLIPSE, old_e->depth);
	add_depth(O_ELLIPSE, new_e->depth);
	/* swap old with new  */
	memcpy(&swp_e, old_e, sizeof(F_ellipse));
	memcpy(old_e, new_e, sizeof(F_ellipse));
	memcpy(new_e, &swp_e, sizeof(F_ellipse));
	/* but keep the next pointers unchanged */
	swp_e.next = old_e->next;
	old_e->next = new_e->next;
	new_e->next = swp_e.next;

	redisplay_ellipses(new_e, old_e);
	break;
      case O_TXT:
	new_t = (*stack)->saved_objects->texts;
	old_t = (*stack)->saved_objects->texts->next;
	/* account for depths */
	remove_depth(O_TXT, old_t->depth);
	add_depth(O_TXT, new_t->depth);
	/* swap old with new */
	memcpy(&swp_t, old_t, sizeof(F_text));
	memcpy(old_t, new_t, sizeof(F_text));
	memcpy(new_t, &swp_t, sizeof(F_text));
	/* but keep the next pointers unchanged */
	swp_t.next = old_t->next;
	old_t->next = new_t->next;
	new_t->next = swp_t.next;

	redisplay_texts(new_t, old_t);
	break;
      case O_SPLINE:
	new_s = (*stack)->saved_objects->splines;
	old_s = (*stack)->saved_objects->splines->next;
	/* account for depths */
	remove_depth(O_SPLINE, old_s->depth);
	add_depth(O_SPLINE, new_s->depth);
	/* swap old with new */
	memcpy(&swp_s, old_s, sizeof(F_spline));
	memcpy(old_s, new_s, sizeof(F_spline));
	memcpy(new_s, &swp_s, sizeof(F_spline));
	/* but keep the next pointers unchanged */
	swp_s.next = old_s->next;
	old_s->next = new_s->next;
	new_s->next = swp_s.next;

	redisplay_splines(new_s, old_s);
	break;
      case O_ARC:
	new_a = (*stack)->saved_objects->arcs;
	old_a = (*stack)->saved_objects->arcs->next;
	/* account for depths */
	remove_depth(O_ARC, old_a->depth);
	add_depth(O_ARC, new_a->depth);
	/* swap old with new */
	memcpy(&swp_a, old_a, sizeof(F_arc));
	memcpy(old_a, new_a, sizeof(F_arc));
	memcpy(new_a, &swp_a, sizeof(F_arc));
	/* but keep the next pointers unchanged */
	swp_a.next = old_a->next;
	old_a->next = new_a->next;
	new_a->next = swp_a.next;

	redisplay_arcs(new_a, old_a);
	break;
      case O_COMPOUND:
	new_c = (*stack)->saved_objects->compounds;
	old_c = (*stack)->saved_objects->compounds->next;
	/* account for depths */
	remove_compound_depth(old_c);
	add_compound_depth(new_c);
	/* swap old with new */
	memcpy(&swp_c, old_c, sizeof(F_compound));
	memcpy(old_c, new_c, sizeof(F_compound));
	memcpy(new_c, &swp_c, sizeof(F_compound));
	/* but keep the next pointers unchanged */
	swp_c.next = old_c->next;
	old_c->next = new_c->next;
	new_c->next = swp_c.next;

	redisplay_compounds(new_c, old_c);
	break;
      case O_FIGURE:
	/* swap saved figure comments with current */
	swp_comm = objects.comments;
	objects.comments = (*stack)->saved_objects->comments;
	(*stack)->saved_objects->comments = swp_comm;

	break;
      case O_ALL_OBJECT:
	swp_c = objects;
	objects = *((*stack)->saved_objects);
	*((*stack)->saved_objects) = swp_c;
	new_c = &objects;
	old_c = (*stack)->saved_objects;
	/* account for depths */
	remove_compound_depth(old_c);
	add_compound_depth(new_c);

	set_modifiedflag();
	redisplay_zoomed_region(0, 0, BACKX(CANVAS_WD), BACKY(CANVAS_HT));
	break;
    }
	swap_stack(stack);
}

/*
 * When a single object is created, it is appended to the appropriate list
 * in objects.	It is also placed in the appropriate list in saved_objects.
 *
 * When a number of objects are created (usually by reading them in from
 * a file or undoing a remove-all action), they are appended to the lists in
 * objects and also saved in saved_objects.  The pointers in object_tails
 * will be set to point to the last members of the lists in objects prior to
 * the appending.
 *
 * Note: The read operation will set the pointers in object_tails while the
 * remove-all operation will zero pointers in objects.
 */

void undo_add(F_history **stack)
{
    int		    xmin, ymin, xmax, ymax;

    switch ((*stack)->last_object) {
      case O_POLYLINE:
	list_delete_line(&objects.lines, (*stack)->saved_objects->lines);
	redisplay_line((*stack)->saved_objects->lines);
	break;
      case O_ELLIPSE:
	list_delete_ellipse(&objects.ellipses, (*stack)->saved_objects->ellipses);
	redisplay_ellipse((*stack)->saved_objects->ellipses);
	break;
      case O_TXT:
	list_delete_text(&objects.texts, (*stack)->saved_objects->texts);
	redisplay_text((*stack)->saved_objects->texts);
	break;
      case O_SPLINE:
	list_delete_spline(&objects.splines, (*stack)->saved_objects->splines);
	redisplay_spline((*stack)->saved_objects->splines);
	break;
      case O_ARC:
	list_delete_arc(&objects.arcs, (*stack)->saved_objects->arcs);
	redisplay_arc((*stack)->saved_objects->arcs);
	break;
      case O_COMPOUND:
	list_delete_compound(&objects.compounds, (*stack)->saved_objects->compounds);
	redisplay_compound((*stack)->saved_objects->compounds);
	break;
      case O_ALL_OBJECT:
	cut_objects(&objects, &object_tails);
	compound_bound((*stack)->saved_objects, &xmin, &ymin, &xmax, &ymax);
	redisplay_zoomed_region(xmin, ymin, xmax, ymax);
	break;
    }
    (*stack)->last_action= F_DELETE;
	swap_stack(stack);
}

//Simply add saved object
void undo_delete(F_history **stack)
{
    char	   *swp_comm;
    int		    xmin, ymin, xmax, ymax;

    switch ((*stack)->last_object) {
      case O_POLYLINE:
	list_add_line(&objects.lines, (*stack)->saved_objects->lines);
	redisplay_line((*stack)->saved_objects->lines);
	break;
      case O_ELLIPSE:
	list_add_ellipse(&objects.ellipses, (*stack)->saved_objects->ellipses);
	redisplay_ellipse((*stack)->saved_objects->ellipses);
	break;
      case O_TXT:
	list_add_text(&objects.texts, (*stack)->saved_objects->texts);
	redisplay_text((*stack)->saved_objects->texts);
	break;
      case O_SPLINE:
	list_add_spline(&objects.splines, (*stack)->saved_objects->splines);
	redisplay_spline((*stack)->saved_objects->splines);
	break;
      case O_ARC:
	list_add_arc(&objects.arcs, (*stack)->saved_objects->arcs);
	redisplay_arc((*stack)->saved_objects->arcs);
	break;
      case O_COMPOUND:
	list_add_compound(&objects.compounds, (*stack)->saved_objects->compounds);
	redisplay_compound((*stack)->saved_objects->compounds);
	break;
      case O_FIGURE:
        /* swap saved figure comments with current */
        swp_comm = objects.comments;
        objects.comments = (*stack)->saved_objects->comments;
        (*stack)->saved_objects->comments = swp_comm;
        /* swap colors*/
        swap_colors();
        /* restore objects*/
        saved_objects.next = NULL;
        compound_bound(&saved_objects, &xmin, &ymin, &xmax, &ymax);
        tail(&objects, &object_tails);
        append_objects(&objects, (*stack)->saved_objects, &object_tails);
        redisplay_zoomed_region(xmin, ymin, xmax, ymax);
        break;
      case O_ALL_OBJECT:
	(*stack)->saved_objects->next = NULL;
	compound_bound((*stack)->saved_objects, &xmin, &ymin, &xmax, &ymax);
	tail(&objects, &object_tails);
	append_objects(&objects, (*stack)->saved_objects, &object_tails);
	redisplay_zoomed_region(xmin, ymin, xmax, ymax);
    }
    (*stack)->last_action = F_ADD;
	swap_stack(stack);
}

//With recorded last position, calculate difference between old and new coordinates, and use built-in translate functions
void undo_move(F_history **stack)
{
    int		    dx, dy;
    int		    xmin1, ymin1, xmax1, ymax1;
    int		    xmin2, ymin2, xmax2, ymax2;
    int		    dum;

    dx = (*stack)->last_x - (*stack)->new_x;
    dy = (*stack)->last_y - (*stack)->new_y;
    switch ((*stack)->last_object) {
      case O_POLYLINE:
	line_bound((*stack)->saved_objects->lines, &xmin1, &ymin1, &xmax1, &ymax1);
	translate_line((*stack)->saved_objects->lines, dx, dy);
	line_bound((*stack)->saved_objects->lines, &xmin2, &ymin2, &xmax2, &ymax2);
	adjust_links(last_linkmode, last_links, dx, dy, 0, 0, 1.0, 1.0, False);
	redisplay_regions(xmin1, ymin1, xmax1, ymax1,
			  xmin2, ymin2, xmax2, ymax2);
	break;
      case O_ELLIPSE:
	ellipse_bound((*stack)->saved_objects->ellipses, &xmin1, &ymin1, &xmax1, &ymax1);
	translate_ellipse((*stack)->saved_objects->ellipses, dx, dy);
	ellipse_bound((*stack)->saved_objects->ellipses, &xmin2, &ymin2, &xmax2, &ymax2);
	redisplay_regions(xmin1, ymin1, xmax1, ymax1,
			  xmin2, ymin2, xmax2, ymax2);
	break;
      case O_TXT:
	text_bound((*stack)->saved_objects->texts, &xmin1, &ymin1, &xmax1, &ymax1,
		&dum,&dum,&dum,&dum,&dum,&dum,&dum,&dum);
	translate_text((*stack)->saved_objects->texts, dx, dy);
	text_bound(saved_objects.texts, &xmin2, &ymin2, &xmax2, &ymax2,
		&dum,&dum,&dum,&dum,&dum,&dum,&dum,&dum);
	redisplay_regions(xmin1, ymin1, xmax1, ymax1,
			  xmin2, ymin2, xmax2, ymax2);
	break;
      case O_SPLINE:
	spline_bound((*stack)->saved_objects->splines, &xmin1, &ymin1, &xmax1, &ymax1);
	translate_spline((*stack)->saved_objects->splines, dx, dy);
	spline_bound((*stack)->saved_objects->splines, &xmin2, &ymin2, &xmax2, &ymax2);
	list_delete_spline(&objects.splines, (*stack)->saved_objects->splines);
	list_add_spline(&objects.splines, (*stack)->saved_objects->splines);
	redisplay_regions(xmin1, ymin1, xmax1, ymax1,
			  xmin2, ymin2, xmax2, ymax2);
	break;
      case O_ARC:
	arc_bound((*stack)->saved_objects->arcs, &xmin1, &ymin1, &xmax1, &ymax1);
	translate_arc((*stack)->saved_objects->arcs, dx, dy);
	arc_bound((*stack)->saved_objects->arcs, &xmin2, &ymin2, &xmax2, &ymax2);
	redisplay_regions(xmin1, ymin1, xmax1, ymax1,
			  xmin2, ymin2, xmax2, ymax2);
	break;
      case O_COMPOUND:
	compound_bound((*stack)->saved_objects->compounds, &xmin1, &ymin1, &xmax1, &ymax1);
	translate_compound((*stack)->saved_objects->compounds, dx, dy);
	compound_bound((*stack)->saved_objects->compounds, &xmin2, &ymin2, &xmax2, &ymax2);
	adjust_links(last_linkmode, last_links, dx, dy, 0, 0, 1.0, 1.0, False);
	redisplay_regions(xmin1, ymin1, xmax1, ymax1,
			  xmin2, ymin2, xmax2, ymax2);
	break;
    }
    swap_newp_lastp(stack);
	swap_stack(stack);
}

void undo_load(F_history **stack)
{
    F_compound	    temp;
    char	    ctemp[PATH_MAX];

    /* swap objects in current figure/figure we're restoring */
    temp = objects;
    objects = *(*stack)->saved_objects;
    *(*stack)->saved_objects = temp;
    /* swap filenames */
    strcpy(ctemp, cur_filename);
    update_cur_filename(save_filename);
    strcpy(save_filename, ctemp);
    /* restore colors for the figure we are restoring */
    swap_depths();
    swap_counts();
    swap_colors();
    colors_are_swapped = False;
    /* in case current figure doesn't have the colors shown in the fill/pen colors */
    current_memory = -1;
    show_pencolor();
    show_fillcolor();
    /* redisply that figure */
    redisplay_canvas();
    (*stack)->last_action = F_LOAD;
	swap_stack(stack);
}

void undo_scale(F_history **stack) 
{
    char	   *swp_comm;
    F_compound	    swp_c;
    F_line	    swp_l;
    F_spline	    swp_s;
    F_ellipse	    swp_e;
    F_arc	    swp_a;
    F_text	    swp_t;

    switch ((*stack)->last_object) {
      case O_POLYLINE:
	new_l = (*stack)->saved_objects->lines;
	old_l = (*stack)->saved_objects->lines->next;
	/* account for depths */
	remove_depth(O_POLYLINE, old_l->depth);
	add_depth(O_POLYLINE, new_l->depth);

	list_delete_line(&objects.lines, old_l);
	list_add_line(&objects.lines, new_l);

	(*stack)->saved_objects->lines = old_l;
	new_l->next = old_l->next;
	(*stack)->saved_objects->lines->next = new_l;

	redisplay_lines(new_l, old_l);
	break;
      case O_ELLIPSE:
	new_e = (*stack)->saved_objects->ellipses;
	old_e = (*stack)->saved_objects->ellipses->next;
	/* account for depths */
	remove_depth(O_ELLIPSE, old_e->depth);
	add_depth(O_ELLIPSE, new_e->depth);

	list_delete_ellipse(&objects.ellipses, old_e);
	list_add_ellipse(&objects.ellipses, new_e);

	(*stack)->saved_objects->ellipses = old_e;
	new_e->next = old_e->next;
	(*stack)->saved_objects->ellipses->next = new_e;

	redisplay_ellipses(new_e, old_e);
	break;
      case O_TXT:



	new_t = (*stack)->saved_objects->texts;
	old_t = (*stack)->saved_objects->texts->next;
	/* account for depths */
	remove_depth(O_TXT, old_t->depth);
	add_depth(O_TXT, new_t->depth);

	list_delete_text(&objects.texts, old_t);
	list_add_text(&objects.texts, new_t);

	(*stack)->saved_objects->texts = old_t;
	new_t->next = old_t->next;
	(*stack)->saved_objects->texts->next = new_t;

	redisplay_texts(new_t, old_t);
	break;
      case O_SPLINE:
	new_s = (*stack)->saved_objects->splines;
	old_s = (*stack)->saved_objects->splines->next;
	/* account for depths */
	remove_depth(O_SPLINE, old_s->depth);
	add_depth(O_SPLINE, new_s->depth);
	/* swap old with new */

	list_delete_spline(&objects.splines, old_s);
	list_add_spline(&objects.splines, new_s);

	(*stack)->saved_objects->splines = old_s;
	new_s->next = old_s->next;
	(*stack)->saved_objects->splines->next = new_s;

	//set_action_object(F_EDIT, O_SPLINE);
	redisplay_splines(new_s, old_s);
	break;
      case O_ARC:
	new_a = (*stack)->saved_objects->arcs;
	old_a = (*stack)->saved_objects->arcs->next;
	/* account for depths */
	remove_depth(O_ARC, old_a->depth);
	add_depth(O_ARC, new_a->depth);
	/* swap old with new */
	
	list_delete_arc(&objects.arcs, old_a);
	list_add_arc(&objects.arcs, new_a);

	(*stack)->saved_objects->arcs = old_a;
	new_a->next = old_a->next;
	(*stack)->saved_objects->arcs->next = new_a;
	
	//set_action_object(F_EDIT, O_ARC);
	redisplay_arcs(new_a, old_a);
	break;
      case O_COMPOUND:
	new_c = (*stack)->saved_objects->compounds;
	old_c = (*stack)->saved_objects->compounds->next;
	/* account for depths */
	remove_compound_depth(old_c);
	add_compound_depth(new_c);
	/* swap old with new */
	
	list_delete_compound(&objects.compounds, old_c);
	list_add_compound(&objects.compounds, new_c);

	(*stack)->saved_objects->compounds = old_c;
	new_c->next = old_c->next;
	(*stack)->saved_objects->compounds->next = new_c;
	
	//set_action_object(F_EDIT, O_COMPOUND);
	redisplay_compounds(new_c, old_c);
	break;
      case O_FIGURE:
	/* swap saved figure comments with current */
	swp_comm = objects.comments;
	objects.comments = (*stack)->saved_objects->comments;
	(*stack)->saved_objects->comments = swp_comm;
	break;
      case O_ALL_OBJECT:
	swp_c = objects;
	objects = *((*stack)->saved_objects);
	*((*stack)->saved_objects) = swp_c;
	new_c = &objects;
	old_c = (*stack)->saved_objects;
	/* account for depths */
	remove_compound_depth(old_c);
	add_compound_depth(new_c);
	set_modifiedflag();
	redisplay_zoomed_region(0, 0, BACKX(CANVAS_WD), BACKY(CANVAS_HT));
	break;
    }
	swap_stack(stack);
}

void undo_open_close(F_history **stack)
{
  switch ((*stack)->last_object) {
  case O_POLYLINE:
    if ((*stack)->saved_objects->lines->type == T_POLYGON) {
	(*stack)->saved_objects->lines->for_arrow = (*stack)->last_for_arrow;
	(*stack)->saved_objects->lines->back_arrow = (*stack)->last_back_arrow;
	(*stack)->last_for_arrow = (*stack)->last_back_arrow = NULL;
    }
    toggle_polyline_polygon((*stack)->saved_objects->lines, (*stack)->last_prev_point,
			    (*stack)->last_selected_point);
    break;
  case O_SPLINE:
    if ((*stack)->saved_objects->splines->type == T_OPEN_XSPLINE) {
	F_sfactor *c_tmp;

	draw_spline((*stack)->saved_objects->splines, ERASE);
	(*stack)->saved_objects->splines->sfactors->s = (*stack)->last_origin_tension;
	for (c_tmp=(*stack)->saved_objects->splines->sfactors ; c_tmp->next != NULL ;
	    c_tmp=c_tmp->next)
		;
	c_tmp->s = (*stack)->last_extremity_tension;
	(*stack)->saved_objects->splines->type = T_CLOSED_XSPLINE;
	draw_spline((*stack)->saved_objects->splines, PAINT);
    } else {
	if (closed_spline((*stack)->saved_objects->splines)) {
	    (*stack)->saved_objects->splines->for_arrow = (*stack)->last_for_arrow;
	    (*stack)->saved_objects->splines->back_arrow = (*stack)->last_back_arrow;
	    (*stack)->last_for_arrow = (*stack)->last_back_arrow = NULL;
	  }
	toggle_open_closed_spline((*stack)->saved_objects->splines, (*stack)->last_prev_point,
				  (*stack)->last_selected_point);
    }
    break;
  }
  swap_stack(stack);
}

void swap_newp_lastp(F_history **stack)
{
    int		    t;		/* swap new_position and last_position	*/

    t = (*stack)->new_x;
    (*stack)->new_x = (*stack)->last_x;
    (*stack)->last_x = t;
    t = (*stack)->new_y;
    (*stack)->new_y = (*stack)->last_y;
    (*stack)->last_y = t;
}


void clean_up(void)
{
	return;
}

void set_freeze_undo_additions(Boolean val)
{
	freeze_undo_additions = val;
}

void set_latest_line_var(F_line *line)
{
	latest_line = line;
}

void set_latest_spline_var(F_spline *spline)
{
	latest_spline = spline;
}

void set_latestarc(F_arc *arc)
{
    saved_objects.arcs = arc;
}

void set_latestobjects(F_compound *objects)
{
    saved_objects = *objects;
}

void set_latestcompound(F_compound *compound)
{
    saved_objects.compounds = compound;
}

void set_latestellipse(F_ellipse *ellipse)
{
    saved_objects.ellipses = ellipse;
}

void set_latestline(F_line *line)
{
    saved_objects.lines = line;
}

void set_latestspline(F_spline *spline)
{
    saved_objects.splines = spline;
}

void set_latesttext(F_text *text)
{
    saved_objects.texts = text;
}

void set_last_prevpoint(F_point *prev_point)
{
    last_prev_point = prev_point;
}

void set_last_selectedpoint(F_point *selected_point)
{
    last_selected_point = selected_point;
}

void set_last_selectedsfactor(F_sfactor *selected_sfactor)
{
  last_selected_sfactor = selected_sfactor;
}

void set_last_nextpoint(F_point *next_point)
{
    last_next_point = next_point;
}

void set_last_arcpointnum(int num)
{
    last_arcpointnum = num;
}

void set_lastposition(int x, int y)
{
    last_position.x = x;
    last_position.y = y;
}

void set_newposition(int x, int y)
{
    new_position.x = x;
    new_position.y = y;
}

void set_action(int action)
{
    last_action = action;
}

void set_action_object(int action, int object)
{
    last_action = action;
    last_object = object;
}

void set_lastlinkinfo(int mode, F_linkinfo *links)
{
    last_linkmode = mode;
    last_links = links;
}

void set_last_tension(double origin, double extremity)
{
  last_origin_tension = origin;
  last_extremity_tension = extremity;
}

void set_last_arrows(F_arrow *forward, F_arrow *backward)
{
      last_for_arrow = forward;
      last_back_arrow = backward;
}

//This is how an action is added to the undo history
void undo_update_history()
{
	if(freeze_undo_additions)
		return;

	//free anything in redo_stack to avoid memory leaks. See u_free.c.
	if(!freeze_redo_cleanup)
		free_history(&redo_stack);

	//add element to undo_stack
	F_history *cur_history = undo_stack;
	undo_stack = create_history();
	undo_stack->next = cur_history;

	undo_stack->saved_objects = create_compound();

	//copy pointers over
	undo_stack->saved_objects->arcs = saved_objects.arcs;
	undo_stack->saved_objects->ellipses = saved_objects.ellipses;
	undo_stack->saved_objects->lines = saved_objects.lines;
	undo_stack->saved_objects->splines = saved_objects.splines;
	undo_stack->saved_objects->comments = saved_objects.comments;
	undo_stack->saved_objects->texts = saved_objects.texts;
	undo_stack->saved_objects->compounds = saved_objects.compounds;

	//clean local saved_objects. This NEEDS to be done, or else freeing redo_stack will free the last object of each type.
	saved_objects.arcs = NULL;
	saved_objects.ellipses = NULL;
	saved_objects.lines = NULL;
	saved_objects.splines = NULL;
	saved_objects.comments = NULL;
	saved_objects.texts = NULL;
	saved_objects.compounds = NULL;

	//copy local variables
	undo_stack->last_action = last_action;
	undo_stack->last_object = last_object;

	undo_stack->last_prev_point = last_prev_point;
	undo_stack->last_next_point = last_next_point;
	undo_stack->last_selected_point = last_selected_point;

	undo_stack->saved_for_arrow = saved_for_arrow;
	undo_stack->saved_back_arrow = saved_back_arrow;

	//clear problematic local variables
	saved_for_arrow = NULL;
	saved_back_arrow = NULL;

	//handle some local variables in groups for ease of access
	undo_stack->latest_line = latest_line;
	latest_line = NULL;
	undo_stack->latest_spline = latest_spline;
	latest_spline = NULL;

	undo_stack->last_selected_sfactor = last_selected_sfactor;
	last_selected_sfactor = NULL;

	undo_stack->new_x = new_position.x;
	undo_stack->new_y = new_position.y;
	undo_stack->last_x = last_position.x;
	undo_stack->last_y = last_position.y;

}

void pop_stack(F_history **stack)
{
	if(*stack != NULL)
		*stack = (*stack)->next;

}

void swap_stack(F_history **stack)
{
	if(*stack == NULL)
		return;
	F_history **other_stack;
	if(*stack == undo_stack)
		other_stack = &redo_stack;
	else if(*stack == redo_stack)
		other_stack = &undo_stack;
	else
		return;

	//remember element
	F_history * element = (*stack);

	//remove element from stack
	pop_stack(stack);

	//add element to other stack
	element->next = *other_stack;
	*other_stack = element;
	
}
