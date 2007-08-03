#include <tclstuff.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <malloc.h>

/*
 * THIS IS NOT THREAD SAFE
 *
 * AT ALL
 */


static int				g_queue;
static Tcl_HashTable	g_paths;

static int list2mask(interp, list, mask) //<<<
	Tcl_Interp		*interp;
	Tcl_Obj			*list;
	uint32_t		*mask;
{
	int				objc, i, index;
	Tcl_Obj			**objv;
	uint32_t		build = 0;
	static CONST char *mask_bits[] = {
		"IN_ACCESS",
		"IN_MODIFY",
		"IN_ATTRIB",
		"IN_CLOSE_WRITE",
		"IN_CLOSE_NOWRITE",
		"IN_OPEN",
		"IN_MOVED_FROM",
		"IN_MOVED_TO",
		"IN_CREATE",
		"IN_DELETE",
		"IN_DELETE_SELF",
		"IN_MOVE_SELF",
		"IN_CLOSE",				// IN_CLOSE_WRITE | IN_CLOSE_NOWRITE
		"IN_MOVE",				// IN_MOVED_FROM | IN_MOVED_TO
		"IN_ONLYDIR",
		"IN_DONT_FOLLOW",
		"IN_MASK_ADD",
		"IN_ISDIR",
		"IN_ONESHOT",
		"IN_ALL_EVENTS",
		(char *)NULL
	};
	uint32_t map[] = {
		IN_ACCESS,
		IN_MODIFY,
		IN_ATTRIB,
		IN_CLOSE_WRITE,
		IN_CLOSE_NOWRITE,
		IN_OPEN,
		IN_MOVED_FROM,
		IN_MOVED_TO,
		IN_CREATE,
		IN_DELETE,
		IN_DELETE_SELF,
		IN_MOVE_SELF,
		IN_CLOSE,
		IN_MOVE,
		IN_ONLYDIR,
		IN_DONT_FOLLOW,
		IN_MASK_ADD,
		IN_ISDIR,
		IN_ONESHOT,
		IN_ALL_EVENTS
	};

	TEST_OK(Tcl_ListObjGetElements(interp, list, &objc, &objv));

	for (i=0; i<objc; i++) {
		TEST_OK(Tcl_GetIndexFromObj(interp, objv[i], mask_bits, "mask bit",
					TCL_EXACT, &index));
		build |= map[index];
	}

	*mask = build;

	return TCL_OK;
}

//>>>
static int glue_add_watch(cdata, interp, objc, objv) //<<<
	ClientData		cdata;
	Tcl_Interp		*interp;
	int				objc;
	Tcl_Obj *CONST	objv[];
{
	int				wd, is_new;
	uint32_t		mask;
	const char		*path;
	Tcl_HashEntry	*entry;

	CHECK_ARGS(2, "path mask");

	path = Tcl_GetString(objv[1]);
	TEST_OK(list2mask(interp, objv[2], &mask));

	wd = inotify_add_watch(g_queue, path, mask);

	entry = Tcl_CreateHashEntry(&g_paths, path, &is_new);
	if (is_new) {
		// This was a new watch
	} else {
		// This was an update on an existing watch
	}

	Tcl_SetHashValue(entry, (ClientData)wd);

	return TCL_OK;
}

//>>>
static int glue_rm_watch(cdata, interp, objc, objv) //<<<
	ClientData		cdata;
	Tcl_Interp		*interp;
	int				objc;
	Tcl_Obj *CONST	objv[];
{
	int				wd, ret;
	const char		*path;
	Tcl_HashEntry	*entry;

	CHECK_ARGS(1, "path");

	path = Tcl_GetString(objv[1]);

	entry = Tcl_FindHashEntry(&g_paths, path);

	if (entry == NULL) {
		Tcl_SetErrorCode(interp, "no_watches_on_path", path, NULL);
		THROW_ERROR("Path is not watched: ", path);
	}

	wd = (int)Tcl_GetHashValue(entry);

	ret = inotify_rm_watch(g_queue, wd);

	Tcl_DeleteHashEntry(entry);

	Tcl_SetObjResult(interp, Tcl_NewIntObj(ret));

	return TCL_OK;
}

//>>>
static int glue_slurp_queue(cdata, interp, objc, objv) //<<<
	ClientData		cdata;
	Tcl_Interp		*interp;
	int				objc;
	Tcl_Obj *CONST	objv[];
{
	unsigned int			waiting, offset, eventsize;
	int						res;
	struct inotify_event	*event;
	unsigned char			*buf;
	size_t					got;
	Tcl_Obj					*result;

	CHECK_ARGS(0, "");

	waiting = 0;
	res = ioctl(g_queue, FIONREAD, (char *)&waiting);

	if (res == -1)
		THROW_ERROR("Problem checking queue length");

	offset = 0;
	buf = (unsigned char *)malloc(waiting);
	event = (struct inotify_event *)(buf + offset);

	got = read(g_queue, buf, waiting);

	result = Tcl_NewListObj(0, NULL);
	while (got > 0) {
		eventsize = sizeof(struct inotify_event) + event->len;

		if (Tcl_ListObjAppendElement(interp, result,
					Tcl_NewStringObj(event->name, -1)) != TCL_OK) goto wobbly;
		if (Tcl_ListObjAppendElement(interp, result,
					Tcl_NewIntObj(event->mask)) != TCL_OK) goto wobbly;

		got -= eventsize;
		offset += eventsize;
		event = (struct inotify_event *)(buf + offset);
	}

	free(buf);

	Tcl_SetObjResult(interp, result);

	return TCL_OK;

wobbly:
	free(buf);
	return TCL_ERROR;
}

//>>>
int Inotify_Init(Tcl_Interp *interp) //<<<
{
	if (Tcl_InitStubs(interp, "8.1", 0) == NULL)
		return TCL_ERROR;

	if (sizeof(ClientData) < sizeof(int))
		THROW_ERROR("On this platform ints are bigger than pointers.  That freaks us out, sorry");

	g_queue = inotify_init();

	Tcl_InitHashTable(&g_paths, TCL_STRING_KEYS);

	NEW_CMD("inotify::add_watch", glue_add_watch);
	NEW_CMD("inotify::rm_watch", glue_rm_watch);
	NEW_CMD("inotify::slurp_queue", glue_slurp_queue);

	return TCL_OK;
}

//>>>
// vim: foldmethod=marker foldmarker=<<<,>>>
