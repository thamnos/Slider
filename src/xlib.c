/*****************************************************\
* XLIB.C
* By: Jesse McClure (c) 2012-2014
* See slider.c or COPYING for license information
\*****************************************************/

#include "slider.h"
#include "xlib-actions.h"

static Atom COM_ATOM;

static void (*handler[LASTEvent])(XEvent *) = {
	[ButtonPress]		= buttonpress,
	[Expose]				= expose,
	[KeyPress]			= keypress,
	[PropertyNotify]	= propertynotify,
};

int xlib_init(Show *show) {
	if (!(dpy=XOpenDisplay(NULL))) die("Failed to open display\n");
	scr = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);
	vis = DefaultVisual(dpy, scr);
	dep = DefaultDepth(dpy, scr);
	/* get screen information: */
	int nmon, i;
	XineramaScreenInfo *geom = XineramaQueryScreens(dpy, &nmon);
	if (conf.mon == -1 || conf.mon >= nmon) conf.mon = nmon - 1;
	sw = geom[conf.mon].width;
	sh = geom[conf.mon].height;
	show->w = sw; show->h = sh;
	show->x = geom[conf.mon].x_org;
	show->y = geom[conf.mon].y_org;
	/* init EWMH atoms */
	NET_WM_STATE = XInternAtom(dpy,
			"_NET_WM_STATE", False);
	NET_WM_STATE_FULLSCREEN = XInternAtom(dpy,
			"_NET_WM_STATE_FULLSCREEN", False);
	NET_ACTIVE_WINDOW = XInternAtom(dpy,
			"_NET_ACTIVE_WINDOW", False);
	/* create main window: */
	wshow = XCreateSimpleWindow(dpy, root, show->x, show->y,
			sw, sh, 0, 0, 0);
	XStoreName(dpy, wshow, "Slider");
	XClassHint *hint = XAllocClassHint();
	hint->res_name = "Slider";
	hint->res_class = "presentation";
	XSetClassHint(dpy, wshow, hint);
	hint->res_class = "notes";
	XSetWindowAttributes wa;
	wa.event_mask = ExposureMask | KeyPressMask | PropertyChangeMask |
			ButtonPressMask | StructureNotifyMask;
	XChangeWindowAttributes(dpy,wshow,CWEventMask,&wa);
	XSizeHints *size = XAllocSizeHints();
	size->base_width = size->width = show->w / 2;
	size->base_height = size->height = show->h / 2;
	size->flags = USSize | PBaseSize;
	XSetWMNormalHints(dpy, wshow, size);
	XMapWindow(dpy, wshow);
	XFlush(dpy);
	XFree(size);
	XMoveResizeWindow(dpy, wshow, show->x, show->y, show->w, show->h);
	XChangeProperty(dpy, wshow, NET_WM_STATE, XA_ATOM, 32,
		PropModeReplace, (unsigned char *)&NET_WM_STATE_FULLSCREEN, 1);
	/* create targets */
	if (nmon < 2) show->ntargets = 1;
	else show->ntargets = conf.nviews + 1;
	show->target = malloc(show->ntargets * sizeof(Target));
	Target *trg;
	View *vw;
	cairo_surface_t *t;
	trg = show->target;
	trg->win = wshow;
	trg->w = sw;
	trg->h = sh;
	trg->offset = 0;
	trg->show = show;
	t = cairo_xlib_surface_create(dpy, trg->win, vis, trg->w, trg->h);
	trg->ctx = cairo_create(t);
	cairo_surface_destroy(t);
	for (i = 1; i < show->ntargets; i++) {
		trg = &show->target[i];
		vw = &conf.view[i-1];
		trg->win = XCreateSimpleWindow(dpy, root, vw->x, vw->y,
				(trg->w = vw->w), (trg->h = vw->h), 0, 0, 0);
		XSetClassHint(dpy, trg->win, hint);
		XChangeWindowAttributes(dpy,trg->win,CWEventMask,&wa);
		XMapWindow(dpy, trg->win);
		trg->offset = vw->offset;
		t = cairo_xlib_surface_create(dpy, trg->win, vis, trg->w, trg->h);
		trg->ctx = cairo_create(t);
		cairo_surface_destroy(t);
	}
	XFree(hint);
	/* create cursors */
	XColor color;
	char c_data = 0;
	Pixmap curs_map = XCreateBitmapFromData(dpy, wshow, &c_data, 1,	1);
	invisible_cursor = XCreatePixmapCursor(dpy, curs_map, curs_map,
			&color, &color, 0, 0);
	crosshair_cursor = XCreateFontCursor(dpy, XC_crosshair);
	XFreePixmap(dpy, curs_map);
	XDefineCursor(dpy, wshow, invisible_cursor);
	//XSetInputFocus(dpy, wshow, RevertToPointerRoot, CurrentTime);
	running = True;
	COM_ATOM = XInternAtom(dpy, "Command", False);
	return 0;
}

int xlib_free() {
	Target *trg;
	int i;
	for (i = 0; i < show->ntargets; i++) {
		trg = &show->target[i];
		cairo_destroy(trg->ctx);
		XDestroyWindow(dpy, trg->win);
	}
	free(show->target);
	XFreeCursor(dpy, invisible_cursor);
	XFreeCursor(dpy, crosshair_cursor);
	XCloseDisplay(dpy);
	return 0;
}

#define MAX_LINE 256
int xlib_main_loop() {
	XFlush(dpy);
	draw(None);

	char line[MAX_LINE], *c;
	fd_set fds;
	int xfd = ConnectionNumber(dpy);
	int sfd = fileno(stdin);
	XEvent ev;
	while (running) {
		FD_ZERO(&fds);
		FD_SET(xfd, &fds);
		FD_SET(sfd, &fds);
		select(xfd+1, &fds, 0, 0, NULL);
		if (FD_ISSET(sfd, &fds)) {
			fgets(line, MAX_LINE, stdin);
			command(line);
		}
		if (FD_ISSET(xfd, &fds)) while (XPending(dpy)) {
			XNextEvent(dpy, &ev);
			if (ev.type < LASTEvent && handler[ev.type])
			handler[ev.type](&ev);
		}
	}
}

void buttonpress(XEvent *ev) {
	XButtonEvent *e = &ev->xbutton;
	XSetInputFocus(dpy, wshow, RevertToPointerRoot, CurrentTime);
	if (e->button < NBUTTON + 1) command(conf.button[e->button - 1]);
}

void expose(XEvent *ev) {
	draw(ev->xexpose.window);
}

void keypress(XEvent *ev) {
	int i;
	XKeyEvent *e = &ev->xkey;
	KeySym sym = XkbKeycodeToKeysym(dpy, e->keycode, 0, 0);
	for (i = 0; i < conf.nkeys; i++) {
		if( (sym==conf.key[i].keysym) && (conf.key[i].arg) &&
				conf.key[i].mod == ((e->state&~Mod2Mask)&~LockMask) ) {
			command(conf.key[i].arg);
		}
	}
}

static const char *_prop_clear = NULL;
void propertynotify(XEvent *ev) {
	XPropertyEvent *e = &ev->xproperty;
	if (e->window != wshow) return;
	if (e->atom != COM_ATOM) return;
	char **strs = NULL;
	int n;
	XTextProperty text;
	XGetTextProperty(dpy, wshow, &text, COM_ATOM);
	if (!text.nitems) return;
	if (text.encoding == XA_STRING) command( (char *) text.value);
	else if (XmbTextPropertyToTextList(dpy, &text, &strs, &n) >=
			Success) {
		command( (char *) *strs);
		XFreeStringList(strs);
	}
	XFree(text.value);
	XStringListToTextProperty((char **)&_prop_clear, 1, &text);
	XSetTextProperty(dpy, wshow, &text, COM_ATOM);
	XFlush(dpy);
	XFree(text.value);
}

void draw(Window win) {
	int i, j, n;
	Target *trg;
	for (i = 0; i < show->ntargets; i++) {
		trg = &show->target[i];
		if (!(win == None || trg->win == win)) continue;
		n = show->cur + trg->offset;
		if (n < 0) n = 0;
		else if (n >= trg->show->nslides) n = trg->show->nslides - 1;
		cairo_set_source_surface(trg->ctx, trg->show->slide[n], 0, 0);
		if (trg->win == wshow && win != wshow) for (j = conf.fade; j; j--) {
			cairo_paint_with_alpha(trg->ctx, 1/(float)j);
			XFlush(dpy);
			usleep(5000);
		}
		cairo_paint(trg->ctx);
	}
	XFlush(dpy);
}

