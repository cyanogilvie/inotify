#include <tclstuff.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <malloc.h>


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
static Tcl_Obj *mask2list(uint32_t mask) //<<<
{
	Tcl_Obj *result = Tcl_NewListObj(0, NULL);

#define CHECK_BIT(bit, str) \
	if ((mask & bit) == bit) \
		Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj(str, -1));

	CHECK_BIT(IN_ACCESS,		"IN_ACCESS");
	CHECK_BIT(IN_ATTRIB,		"IN_ATTRIB");
	CHECK_BIT(IN_CLOSE_WRITE,	"IN_CLOSE_WRITE");
	CHECK_BIT(IN_CLOSE_NOWRITE,	"IN_CLOSE_NOWRITE");
	CHECK_BIT(IN_CREATE,		"IN_CREATE");
	CHECK_BIT(IN_DELETE,		"IN_DELETE");
	CHECK_BIT(IN_DELETE_SELF,	"IN_DELETE_SELF");
	CHECK_BIT(IN_MODIFY,		"IN_MODIFY");
	CHECK_BIT(IN_MOVE_SELF,		"IN_MOVE_SELF");
	CHECK_BIT(IN_MOVED_FROM,	"IN_MOVED_FROM");
	CHECK_BIT(IN_MOVED_TO,		"IN_MOVED_TO");
	CHECK_BIT(IN_OPEN,			"IN_OPEN");
	CHECK_BIT(IN_IGNORED,		"IN_IGNORED");
	CHECK_BIT(IN_ISDIR,			"IN_ISDIR");
	CHECK_BIT(IN_Q_OVERFLOW,	"IN_Q_OVERFLOW");
	CHECK_BIT(IN_UNMOUNT,		"IN_UNMOUNT");

	return result;
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

	if ((chan_mode & TCL_READABLE) != TCL_READABLE)
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
	int				queue_fd, wd;
	uint32_t		mask;
	const char		*path;

	CHECK_ARGS(3, "queue path mask");

	TEST_OK(get_queue_fd_from_chan(interp, objv[1], &queue_fd));
	path = Tcl_GetString(objv[2]);
	TEST_OK(list2mask(interp, objv[3], &mask));

	wd = inotify_add_watch(queue_fd, path, mask);
	if (wd == -1)
		THROW_ERROR("Problem creating watch on ", path);

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

	CHECK_ARGS(2, "queue wd");

	TEST_OK(get_queue_fd_from_chan(interp, objv[1], &queue_fd));
	TEST_OK(Tcl_GetIntFromObj(interp, objv[2], &wd));

	ret = inotify_rm_watch(queue_fd, wd);

	Tcl_SetObjResult(interp, Tcl_NewIntObj(ret));

	return TCL_OK;
}

//>>>
static int glue_decode_events(cdata, interp, objc, objv) //<<<
	ClientData		cdata;
	Tcl_Interp		*interp;
	int				objc;
	Tcl_Obj *CONST	objv[];
{
	unsigned char			*raw;
	int						raw_len, remaining, eventsize, offset;
	struct inotify_event	*event;
	Tcl_Obj					*result;

	CHECK_ARGS(1, "raw_event_data");

	raw = Tcl_GetByteArrayFromObj(objv[1], &raw_len);

	offset = 0;
	remaining = raw_len;
	event = (struct inotify_event *)(raw + offset);

	result = Tcl_NewListObj(0, NULL);

	while (remaining > 0) {
		Tcl_Obj		*evdata = Tcl_NewListObj(0, NULL);

		eventsize = sizeof(struct inotify_event) + event->len;
		remaining -= eventsize;
		offset += eventsize;

#define ADD_ELEM(elem) \
		TEST_OK(Tcl_ListObjAppendElement(interp, evdata, elem));

		ADD_ELEM(Tcl_NewIntObj(event->wd));
		ADD_ELEM(mask2list(event->mask));
		ADD_ELEM(Tcl_NewIntObj(event->cookie));
		//ADD_ELEM(Tcl_NewIntObj(event->len));
		if (event->len > 0) {
			// Why not give event->len as the length?  len includes null padding
			ADD_ELEM(Tcl_NewStringObj(event->name, -1));
		} else {
			ADD_ELEM(Tcl_NewStringObj("", 0));
		}

		TEST_OK(Tcl_ListObjAppendElement(interp, result, evdata));

		event = (struct inotify_event *)(raw + offset);
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

	NEW_CMD("inotify::create_queue", glue_create_queue);
	NEW_CMD("inotify::add_watch", glue_add_watch);
	NEW_CMD("inotify::rm_watch", glue_rm_watch);
	NEW_CMD("inotify::decode_events", glue_decode_events);

	return TCL_OK;
}

//>>>
// vim: foldmethod=marker foldmarker=<<<,>>>
