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
	fprintf(stderr, "Mask is: 0x%08x, from (%s)\n", *mask, Tcl_GetString(list));

	return TCL_OK;
}

//>>>
static int glue_create_queue(cdata, interp, objc, objv) //<<<
	ClientData		cdata;
	Tcl_Interp		*interp;
	int				objc;
	Tcl_Obj *CONST	objv[];
{
	const char	*channel_name;
	Tcl_Channel	channel;
	int			queue_fd;

	CHECK_ARGS(0, "");

	queue_fd = inotify_init();
	channel = Tcl_MakeFileChannel((ClientData)queue_fd, TCL_READABLE);
	Tcl_RegisterChannel(interp, channel);
	channel_name = Tcl_GetChannelName(channel);
	fprintf(stderr, "queue fd is: %d, channel name %s\n", queue_fd, channel_name);

	Tcl_SetObjResult(interp, Tcl_NewStringObj(channel_name, -1));

	return TCL_OK;
}

//>>>
static int get_queue_fd_from_chan(interp, handle, queue_fd) //<<<
	Tcl_Interp		*interp;
	Tcl_Obj			*handle;
	int				*queue_fd;
{
	Tcl_Channel		channel;
	int				chan_mode;

	channel = Tcl_GetChannel(interp, Tcl_GetString(handle), &chan_mode);
	if (channel == NULL)
		THROW_ERROR("Invalid queue handle: ", Tcl_GetString(handle));

	if (chan_mode & TCL_READABLE != TCL_READABLE)
		THROW_ERROR("Queue exists, but is not readable.  Wierd, man");

	if (Tcl_GetChannelHandle(channel, TCL_READABLE, (ClientData *)queue_fd) != TCL_OK) {
		THROW_ERROR("Couldn't retrieve queue fd from channel");
	}

	return TCL_OK;
}

//>>>
static int glue_add_watch(cdata, interp, objc, objv) //<<<
	ClientData		cdata;
	Tcl_Interp		*interp;
	int				objc;
	Tcl_Obj *CONST	objv[];
{
	int				queue_fd, wd, is_new;
	uint32_t		mask;
	const char		*path;
	Tcl_HashEntry	*entry;

	CHECK_ARGS(3, "queue path mask");

	TEST_OK(get_queue_fd_from_chan(interp, objv[1], &queue_fd));
	path = Tcl_GetString(objv[2]);
	TEST_OK(list2mask(interp, objv[3], &mask));

	wd = inotify_add_watch(queue_fd, path, mask);
	if (wd == -1)
		THROW_ERROR("Problem creating watch on ", path);

	entry = Tcl_CreateHashEntry(&g_paths, path, &is_new);
	if (is_new) {
		// This was a new watch
	} else {
		// This was an update on an existing watch
	}

	Tcl_SetHashValue(entry, (ClientData)wd);

	Tcl_SetObjResult(interp, Tcl_NewIntObj(wd));

	return TCL_OK;
}

//>>>
static int glue_rm_watch(cdata, interp, objc, objv) //<<<
	ClientData		cdata;
	Tcl_Interp		*interp;
	int				objc;
	Tcl_Obj *CONST	objv[];
{
	int				queue_fd, wd, ret;
	const char		*path;
	Tcl_HashEntry	*entry;

	CHECK_ARGS(2, "queue path");

	TEST_OK(get_queue_fd_from_chan(interp, objv[1], &queue_fd));
	path = Tcl_GetString(objv[2]);

	entry = Tcl_FindHashEntry(&g_paths, path);

	if (entry == NULL) {
		Tcl_SetErrorCode(interp, "no_watches_on_path", path, NULL);
		THROW_ERROR("Path is not watched: ", path);
	}

	wd = (int)Tcl_GetHashValue(entry);

	ret = inotify_rm_watch(queue_fd, wd);

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
	int						queue_fd, res, block;
	struct inotify_event	*event;
	unsigned char			*buf;
	size_t					got;
	Tcl_Obj					*result;
	int						tmp = 0;

	if (objc < 2 && objc > 3) {
		CHECK_ARGS(2, "queue ?block?");
	}

	if (objc == 3) {
		TEST_OK(Tcl_GetBooleanFromObj(interp, objv[2], &block));
	} else {
		block = 0;
	}

	TEST_OK(get_queue_fd_from_chan(interp, objv[1], &queue_fd));

	waiting = 0;
	res = ioctl(queue_fd, FIONREAD, (char *)&waiting);
	fprintf(stderr, "%d bytes report waiting\n", waiting);

	if (res == -1)
		THROW_ERROR("Problem checking queue length");

	result = Tcl_NewListObj(0, NULL);
	if (!block && waiting == 0) {
		Tcl_SetObjResult(interp, result);
		return TCL_OK;
	}

	offset = 0;
	if (waiting < sizeof(struct inotify_event) + 256) {
		// Force a bigger buffer, and sit waiting for an event
		waiting = sizeof(struct inotify_event) + 256;
	}
	buf = (unsigned char *)malloc(waiting);
	event = (struct inotify_event *)(buf + offset);

	got = read(queue_fd, buf, waiting);
	fprintf(stderr, "Got %d bytes\n", got);

	while (got > 0) {
		tmp++;
		eventsize = sizeof(struct inotify_event) + event->len;
		got -= eventsize;
		offset += eventsize;

		fprintf(stderr, "Event #%d size: %d bytes, %d remain\n", tmp, eventsize, got);

		if (Tcl_ListObjAppendElement(interp, result,
					Tcl_NewStringObj(event->name, -1)) != TCL_OK) goto wobbly;
		if (Tcl_ListObjAppendElement(interp, result,
					Tcl_NewIntObj(event->mask)) != TCL_OK) goto wobbly;

		event = (struct inotify_event *)(buf + offset);

		if (tmp > 50) {
			fprintf(stderr, "Patience exceeded\n");
			Tcl_SetObjResult(interp, Tcl_NewStringObj("Patience exceeded", -1));
			goto wobbly;
		}
	}

	free(buf);

	Tcl_SetObjResult(interp, result);

	return TCL_OK;

wobbly:
	free(buf);
	return TCL_ERROR;
}

//>>>
static int glue_watched_paths(cdata, interp, objc, objv) //<<<
	ClientData		cdata;
	Tcl_Interp		*interp;
	int				objc;
	Tcl_Obj *CONST	objv[];
{
	Tcl_HashEntry	*entry;
	Tcl_HashSearch	searchPtr;
	Tcl_Obj			*result;

	result = Tcl_NewListObj(0, NULL);

	entry = Tcl_FirstHashEntry(&g_paths, &searchPtr);
	while (entry != NULL) {
		TEST_OK(Tcl_ListObjAppendElement(interp, result,
					Tcl_NewStringObj(Tcl_GetHashKey(&g_paths, entry), -1)));
		entry = Tcl_NextHashEntry(&searchPtr);
	}

	Tcl_SetObjResult(interp, result);

	return TCL_OK;
}

//>>>
int Inotify_Init(Tcl_Interp *interp) //<<<
{
	if (Tcl_InitStubs(interp, "8.1", 0) == NULL)
		return TCL_ERROR;

	if (sizeof(ClientData) < sizeof(int))
		THROW_ERROR("On this platform ints are bigger than pointers.  That freaks us out, sorry");

	Tcl_InitHashTable(&g_paths, TCL_STRING_KEYS);

	NEW_CMD("inotify::create_queue", glue_create_queue);
	NEW_CMD("inotify::add_watch", glue_add_watch);
	NEW_CMD("inotify::rm_watch", glue_rm_watch);
	NEW_CMD("inotify::slurp_queue", glue_slurp_queue);
	NEW_CMD("inotify::watched_paths", glue_watched_paths);

	return TCL_OK;
}

//>>>
// vim: foldmethod=marker foldmarker=<<<,>>>
