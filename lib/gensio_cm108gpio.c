/*
 *  gensio - A library for abstracting stream I/O
 *  Copyright (C) 2018  Corey Minyard <minyard@acm.org>
 *
 *  SPDX-License-Identifier: LGPL-2.1-only
 */


#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include <gensio/gensio.h>
#include <gensio/gensio_os_funcs.h>
#include <gensio/gensio_class.h>
#include <gensio/argvutils.h>
#include <gensio/gensio_circbuf.h>

#if HAVE_UDEV == 1
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libudev.h>

#define fdtype int
#define INVALID_FD -1

static const char *
my_strrstr(const char *haystack, const char *needle)
{
    const char *n = NULL, *c = haystack;

    while (true) {
	c = strstr(c, needle);
	if (!c)
	    return n;
	n = c;
	c++;
    }
}

static int
find_hid_device(struct gensio_pparm_info *p, struct gensio_os_funcs *o,
		const char *idnum, char **devpath)
{
    struct udev *udev;
    struct udev_enumerate *e = NULL;
    struct udev_list_entry *devices, *l;
    struct udev_device *d, *pd, *f = NULL, *sounddev = NULL;
    const char *path = NULL, *endp;
    char *basepath = NULL, *tmps;
    size_t basepath_len = 0;
    int err = GE_NOTFOUND;

    if (strlen(idnum) == 0) {
	gensio_pparm_log(p, "You must provide an id or number to compare\n");
	return GE_INVAL;
    }

    udev = udev_new();
    if (!udev) {
	gensio_pparm_log(p, "Error opening udev()\n");
	return GE_NOTFOUND;
    }

    e = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(e, "sound");
    udev_enumerate_scan_devices(e);
    devices = udev_enumerate_get_list_entry(e);
    udev_list_entry_foreach(l, devices) {
	const char *devnode;

	path = udev_list_entry_get_name(l);
	d = udev_device_new_from_syspath(udev, path);
	devnode = udev_device_get_devnode(d);
	if (!devnode) {
	    const char *id = udev_device_get_sysattr_value(d, "id");
	    const char *num = udev_device_get_sysattr_value(d, "number");

#if 0
	    printf("  path = %s\n", path);
	    if (id)
		printf("  id = %s\n", id);
	    if (num)
		printf("  number = %s\n", num);
#endif

	    f = NULL;

	    if (id && strcmp(id, idnum) == 0)
		f = d;
	    else if (num && strcmp(num, idnum) == 0)
		f = d;
	    continue;
	}

	if (!f)
	    continue;

	pd = udev_device_get_parent_with_subsystem_devtype(d, "usb",
							   "usb_device");
	if (pd) {
#if 0
	    const char *p;
	    printf("  path = %s\n", path);
	    p = udev_device_get_sysattr_value(pd, "idVendor");
	    if (p)
		printf("    idVendor = %s\n", p);
	    p = udev_device_get_sysattr_value(pd, "idProduct");
	    if (p)
		printf("    idProduct = %s\n", p);
#endif
	    sounddev = f;
	    break;
	}
    }

    if (!sounddev) {
	gensio_pparm_log(p, "Unable to find matching sound device\n");
	err = GE_IOERR;
	goto out_err;
    }

    endp = my_strrstr(path, "/sound/");
    if (!endp) {
	gensio_pparm_log(p, "No /sound/ in device path: %s\n", path);
	err = GE_IOERR;
	goto out_err;
    }
    basepath = strndup(path, endp - path);
    udev_enumerate_unref(e);
    e = NULL;
    tmps = strrchr(basepath, ':');
    if (!tmps) {
	gensio_pparm_log(p, "No valid ':' in device path: %s\n", basepath);
	err = GE_IOERR;
	goto out_err;
    }
    basepath_len = tmps - basepath;
    *tmps = '\0';
    
    e = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(e, "hidraw");
    udev_enumerate_scan_devices(e);
    devices = udev_enumerate_get_list_entry(e);
    udev_list_entry_foreach(l, devices) {
	path = udev_list_entry_get_name(l);

#if 0
	const char *devnode;
	d = udev_device_new_from_syspath(udev, path);
	devnode = udev_device_get_devnode(d);
	if (!devnode) {
	    printf("  id = %s\n",
		   udev_device_get_sysattr_value(d, "id"));
	    printf("  number = %s\n",
		   udev_device_get_sysattr_value(d, "number"));
	    continue;
	}

	pd = udev_device_get_parent_with_subsystem_devtype(d, "usb",
							   "usb_device");
	if (pd) {
	    const char *p;

	    p = udev_device_get_sysattr_value(pd, "idVendor");
	    if (p)
		printf("    idVendor = %s\n", p);
	    p = udev_device_get_sysattr_value(pd, "idProduct");
	    if (p)
		printf("    idProduct = %s\n", p);
	}
#endif

	if (strncmp(path, basepath, basepath_len) == 0) {
	    char *n = strrchr(path, '/');

	    if (!n) {
		gensio_pparm_log(p, "No '/'' in path: %s\n", path);
		goto out_err;
	    }
	    n = gensio_alloc_sprintf(o, "/dev%s", n);
	    if (!n) {
		err = GE_NOMEM;
		goto out_err;
	    }
	    *devpath = n;
	    err = 0;
	    break;
	}
    }

 out_err:
    if (basepath)
	free(basepath);
    if (e)
	udev_enumerate_unref(e);
    udev_unref(udev);
    return err;
}

static int
hid_write(struct gensio_os_funcs *o, int fd,
	  unsigned char *io, unsigned int len)
{
    int rv, err = 0;

    rv = write(fd, io, len);
    if (rv != len)
	err = gensio_os_err_to_err(o, errno);

    return err;
}

static int
hid_open(struct gensio_os_funcs *o, const char *path, int *newfd)
{
    int fd = open(path, O_WRONLY);
    int err = 0;

    if (fd == -1)
	err = gensio_os_err_to_err(o, errno);
    else
	*newfd = fd;
    return err;
}

static void
hid_close(int fd)
{
    close(fd);
}

#elif defined(_WIN32)

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>

static const GUID InterfaceClassGuid = {0x4d1e55b2, 0xf16f, 0x11cf,
					{0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30} };

static struct {
    unsigned int vendor_id;
    unsigned int product_id;
    /* unsigned int ngpios; No good way to get these. */
} cmedia_ids[] = {
    /* CMedia Devices */
    { 0x0d8c, 0x0008 }, /* Range for CM108, CM109, CM119 (no following letters) */
    { 0x0d8c, 0x0009 }, /* Range for CM108, CM109, CM119 (no following letters) */
    { 0x0d8c, 0x000a }, /* Range for CM108, CM109, CM119 (no following letters) */
    { 0x0d8c, 0x000b }, /* Range for CM108, CM109, CM119 (no following letters) */
    { 0x0d8c, 0x000c }, /* Range for CM108, CM109, CM119 (no following letters) */
    { 0x0d8c, 0x000d }, /* Range for CM108, CM109, CM119 (no following letters) */
    { 0x0d8c, 0x000e }, /* Range for CM108, CM109, CM119 (no following letters) */
    { 0x0d8c, 0x000f }, /* Range for CM108, CM109, CM119 (no following letters) */
    { 0x0d8c, 0x0012 }, /* CM108B */
    { 0x0d8c, 0x0013 }, /* CM119B */
    { 0x0d8c, 0x0016 }, /* CM118B */
    { 0x0d8c, 0x0139 }, /* CM108AH */
    { 0x0d8c, 0x013A }, /* CM119A */
    { 0x0d8c, 0x013c }, /* CM108AH (alternate?), HS100 */

    /* SSS Devices */
    { 0x0c76, 0x1605 },
    { 0x0c76, 0x1607 },
    { 0x0c76, 0x160B },
    { 0, 0 }
};

struct win_hid_fd {
    struct gensio_os_funcs *o;
    HANDLE h;
    unsigned int min_write;
    unsigned char *write_buffer;
};

#define fdtype struct win_hid_fd *
#define INVALID_FD NULL

static int
find_hid_device(struct gensio_pparm_info *p, struct gensio_os_funcs *o,
		const char *idnum, char **devpath)
{
    HDEVINFO devinfo = INVALID_HANDLE_VALUE;
    unsigned int i;
    int devidx, err = 0;
    unsigned int matchcount, counter = 0;
    char *mypath = NULL;
    char *end;

    if (!idnum || strlen(idnum) == 0) {
	matchcount = 0;
    } else {
	matchcount = strtoul(idnum, &end, 0);
	if (*end != '\0') {
	    gensio_pparm_log(p, "Invalid number to match against.");
	    return GE_INVAL;
	}
    }

    devinfo = SetupDiGetClassDevsA(NULL, NULL, NULL,
				   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE | DIGCF_ALLCLASSES);
    for (devidx = 0; !err; devidx++) {
	HANDLE h = INVALID_HANDLE_VALUE;
	DWORD size = 0;
	SP_DEVINFO_DATA dev_data;
	SP_DEVICE_INTERFACE_DATA dev_if_data;
	SP_DEVICE_INTERFACE_DETAIL_DATA_A *dev_if_detail = NULL;
	HIDD_ATTRIBUTES attrib;

	memset(&dev_data, 0x0, sizeof(dev_data));
	dev_data.cbSize = sizeof(SP_DEVINFO_DATA);
	dev_if_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	if (!SetupDiEnumDeviceInterfaces(devinfo,
					 NULL,
					 &InterfaceClassGuid,
					 devidx,
					 &dev_if_data))
	    break;

	/* Fetch the size first and allocate the data. */
	SetupDiGetDeviceInterfaceDetailA(devinfo,
					 &dev_if_data,
					 NULL,
					 0,
					 &size,
					 NULL);

	dev_if_detail = malloc(size);
	dev_if_detail->cbSize = sizeof(*dev_if_detail);

	if (!SetupDiGetDeviceInterfaceDetailA(devinfo,
					      &dev_if_data,
					      dev_if_detail,
					      size,
					      NULL,
					      NULL))
	    goto next;

	/*The device must have class "HIDClass". */
	for (i = 0; ; i++) {
	    char class_name[256], driver_name[256];

	    if (!SetupDiEnumDeviceInfo(devinfo, i, &dev_data))
		goto next;

	    if (!SetupDiGetDeviceRegistryPropertyA(devinfo, &dev_data,
						SPDRP_CLASS, NULL,
						(PBYTE)class_name, sizeof(class_name),
						NULL))
		goto next;

	    if (strcmp(class_name, "HIDClass") == 0) {
		/* Make sure there's a driver bound. */
		if (SetupDiGetDeviceRegistryPropertyA(devinfo, &dev_data,
						      SPDRP_DRIVER, NULL,
						      (PBYTE)driver_name, sizeof(driver_name),
						      NULL))
		    break;
	    }
	}

	h = CreateFileA(dev_if_detail->DevicePath,
			0,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			0,
			0);
	if (h == INVALID_HANDLE_VALUE)
	    goto next;

	attrib.Size = sizeof(HIDD_ATTRIBUTES);
	HidD_GetAttributes(h, &attrib);
	for (i = 0; cmedia_ids[i].vendor_id; i++) {
	    if (attrib.VendorID == cmedia_ids[i].vendor_id &&
			attrib.ProductID == cmedia_ids[i].product_id)
		break;
	}
	if (cmedia_ids[i].vendor_id) {
	    counter++;
	    if (!mypath && (matchcount == 0 || counter == matchcount)) {
		mypath = gensio_strdup(o, dev_if_detail->DevicePath);
		if (!mypath)
		    err = GE_NOMEM;
	    }
	}

    next:
	if (h != INVALID_HANDLE_VALUE)
	    CloseHandle(h);

	free(dev_if_detail);
    }

    SetupDiDestroyDeviceInfoList(devinfo);

    if (err)
	return err;

#if 0 /* Maybe there will be a way to match against sound devices someday? */
    UINT ndevs;
    MMRESULT mres;
    WAVEOUTCAPS ocaps;

    ndevs = waveOutGetNumDevs();
    gensio_pparm_log(p, "Wave out devices: %d\n", ndevs);
    for (i = 0; i < ndevs; i++) {
	mres = waveOutGetDevCaps(i, &ocaps, sizeof(ocaps));
	if (mres != MMSYSERR_NOERROR)
	    continue;
	gensio_pparm_log(p, "  %d(%x,%x):%s\n", i, ocaps.wMid, ocaps.wPid, ocaps.szPname);
    }
#endif

    if (!mypath)
	return GE_NOTFOUND;

    if (matchcount == 0 && counter > 1) {
	gensio_pparm_log(p, "Found more than one cm108 device, you must enter which one to use.");
	o->free(o, mypath);
	return GE_INVAL;
    }

    *devpath = mypath;

    return 0;
}

static int
hid_write(struct gensio_os_funcs *o, struct win_hid_fd *fd,
	  unsigned char *io, unsigned int len)
{
    if (len < fd->min_write) {
	memset(fd->write_buffer, 0, fd->min_write);
	memcpy(fd->write_buffer, io, len);
	len = fd->min_write;
	io = fd->write_buffer;
    }

    if (!WriteFile(fd->h, io, len, NULL, NULL))
	return gensio_os_err_to_err(fd->o, GetLastError());

    return 0;
}

static int
hid_open(struct gensio_os_funcs *o, const char *path, struct win_hid_fd **newfd)
{
    struct win_hid_fd *fd;
    HANDLE h;
    HIDP_CAPS caps;
    PHIDP_PREPARSED_DATA pp_data;

    fd = o->zalloc(o, sizeof(*fd));
    if (!fd)
	return GE_NOMEM;

    h = CreateFileA(path,
		    GENERIC_WRITE | GENERIC_READ,
		    FILE_SHARE_READ | FILE_SHARE_WRITE,
		    NULL,
		    OPEN_EXISTING,
		    0,
		    0);
    if (h == INVALID_HANDLE_VALUE) {
	o->free(o, fd);
	return gensio_os_err_to_err(o, GetLastError());
    }
    fd->o = o;
    fd->h = h;

    /* Set the Input Report buffer size to 64 reports. */
    if (!HidD_SetNumInputBuffers(h, 64)) {
	o->free(o, fd);
	return gensio_os_err_to_err(o, GetLastError());
    }

    /* Get the Input Report length for the device. */
    if (!HidD_GetPreparsedData(h, &pp_data)) {
	o->free(o, fd);
	return gensio_os_err_to_err(o, GetLastError());
    }

    if (HidP_GetCaps(pp_data, &caps) != HIDP_STATUS_SUCCESS) {
	HidD_FreePreparsedData(pp_data);
	o->free(o, fd);
	return gensio_os_err_to_err(o, GetLastError());
    }
    fd->min_write = caps.OutputReportByteLength;
    HidD_FreePreparsedData(pp_data);

    if (fd->min_write > 0) {
	fd->write_buffer = o->zalloc(o, fd->min_write);
	if (!fd->write_buffer) {
	    o->free(o, fd);
	    return GE_NOMEM;
	}
    }

    *newfd = fd;
    return 0;
}

static void
hid_close(struct win_hid_fd *fd)
{
    CloseHandle(fd->h);
    fd->o->free(fd->o, fd);
}

#else
#error "cm108gpio can only be compiled on Linux or Windows"
#endif

enum cm108gpio_state {
    CM108GPIO_CLOSED,
    CM108GPIO_IN_OPEN,
    CM108GPIO_OPEN,
    CM108GPIO_IN_OPEN_CLOSE,
    CM108GPIO_IN_CLOSE,
};

struct cm108gpio_data {
    struct gensio_os_funcs *o;
    struct gensio_lock *lock;

    unsigned int refcount;
    enum cm108gpio_state state;

    struct gensio *io;

    char *devpath;
    fdtype fd;
    char *idnum;
    unsigned int bit;

    bool xmit_enabled;

    gensio_done_err open_done;
    void *open_data;

    gensio_done close_done;
    void *close_data;

    /*
     * Used to run read callbacks from the selector to avoid running
     * it directly from user calls.
     */
    bool deferred_op_pending;
    struct gensio_runner *deferred_op_runner;
};

static void
cm108gpio_finish_free(struct cm108gpio_data *ndata)
{
    struct gensio_os_funcs *o = ndata->o;

    if (ndata->io)
	gensio_data_free(ndata->io);
    if (ndata->idnum)
	o->free(o, ndata->idnum);
    if (ndata->devpath)
	o->free(o, ndata->devpath);
    if (ndata->lock)
	o->free_lock(ndata->lock);
    if (ndata->deferred_op_runner)
	o->free_runner(ndata->deferred_op_runner);
    o->free(o, ndata);
}

static void
cm108gpio_lock(struct cm108gpio_data *ndata)
{
    ndata->o->lock(ndata->lock);
}

static void
cm108gpio_unlock(struct cm108gpio_data *ndata)
{
    ndata->o->unlock(ndata->lock);
}

static void
cm108gpio_ref(struct cm108gpio_data *ndata)
{
    assert(ndata->refcount > 0);
    ndata->refcount++;
}

static void
cm108gpio_unlock_and_deref(struct cm108gpio_data *ndata)
{
    assert(ndata->refcount > 0);
    if (ndata->refcount == 1) {
	cm108gpio_unlock(ndata);
	cm108gpio_finish_free(ndata);
    } else {
	ndata->refcount--;
	cm108gpio_unlock(ndata);
    }
}

static void
cm108gpio_deferred_op(struct gensio_runner *runner, void *cb_data)
{
    struct cm108gpio_data *ndata = cb_data;
    int err = 0;

    cm108gpio_lock(ndata);
 restart:
    if (ndata->state == CM108GPIO_IN_OPEN || ndata->state == CM108GPIO_IN_OPEN_CLOSE) {
	if (ndata->state == CM108GPIO_IN_OPEN_CLOSE) {
	    ndata->state = CM108GPIO_IN_CLOSE;
	    err = GE_LOCALCLOSED;
	} else {
	    ndata->state = CM108GPIO_OPEN;
	}
	if (ndata->open_done) {
	    cm108gpio_unlock(ndata);
	    ndata->open_done(ndata->io, err, ndata->open_data);
	    cm108gpio_lock(ndata);
	}
    }

    while (ndata->state == CM108GPIO_OPEN && ndata->xmit_enabled) {
	cm108gpio_unlock(ndata);
	err = gensio_cb(ndata->io, GENSIO_EVENT_WRITE_READY, 0,
			NULL, NULL, NULL);
	cm108gpio_lock(ndata);
	if (err)
	    break;
    }

    if (ndata->state == CM108GPIO_IN_CLOSE) {
	hid_close(ndata->fd);
	ndata->fd = INVALID_FD;
	ndata->state = CM108GPIO_CLOSED;
	if (ndata->close_done) {
	    cm108gpio_unlock(ndata);
	    ndata->close_done(ndata->io, ndata->close_data);
	    cm108gpio_unlock(ndata);
	}

	if (ndata->state != CM108GPIO_CLOSED)
	    goto restart;
    }

    ndata->deferred_op_pending = false;

    cm108gpio_unlock_and_deref(ndata);
}

static void
cm108gpio_start_deferred_op(struct cm108gpio_data *ndata)
{
    if (!ndata->deferred_op_pending) {
	/* Call the read from the selector to avoid lock nesting issues. */
	ndata->deferred_op_pending = true;
	ndata->o->run(ndata->deferred_op_runner);
	cm108gpio_ref(ndata);
    }
}

static void
cm108gpio_set_write_callback_enable(struct gensio *io, bool enabled)
{
    struct cm108gpio_data *ndata = gensio_get_gensio_data(io);

    cm108gpio_lock(ndata);
    ndata->xmit_enabled = enabled;
    if (enabled && ndata->state == CM108GPIO_OPEN)
	cm108gpio_start_deferred_op(ndata);
    cm108gpio_unlock(ndata);
}

static int
cm108gpio_hid_set(struct cm108gpio_data *ndata, int set)
{
    unsigned char io[5];

    io[0] = 0;
    io[1] = 0;
    io[2] = set << (ndata->bit - 1);
    io[3] = 1 << (ndata->bit - 1);
    io[4] = 0;

    return hid_write(ndata->o, ndata->fd, io, 5);
}

static int
cm108gpio_write(struct gensio *io, gensiods *rcount,
		 const struct gensio_sg *sg, gensiods sglen)
{
    struct cm108gpio_data *ndata = gensio_get_gensio_data(io);
    gensiods i, j, count = 0;
    int set = 0, err = 0;

    for (i = 0; i < sglen; i++) {
	const char *s = sg[i].buf;

	for (j = 0; j < sg[i].buflen; j++) {
	    if (s[j] == '1')
		set = 1;
	    else if (s[j] == '0')
		set = -1;
	    count++;
	}
    }

    cm108gpio_lock(ndata);
    if (ndata->state != CM108GPIO_OPEN) {
	cm108gpio_unlock(ndata);
	return GE_NOTREADY;
    }
    if (set != 0) {
	if (set < 0)
	    set = 0;
	err = cm108gpio_hid_set(ndata, set);
    }
    cm108gpio_unlock(ndata);
    if (rcount)
	*rcount = count;
    return err;
}

static int
cm108gpio_open(struct gensio *io, gensio_done_err open_done, void *open_data)
{
    struct cm108gpio_data *ndata = gensio_get_gensio_data(io);
    int err = 0;

    cm108gpio_lock(ndata);
    if (ndata->state != CM108GPIO_CLOSED) {
	err = GE_NOTREADY;
	goto out_unlock;
    }
    err = hid_open(ndata->o, ndata->devpath, &ndata->fd);
    if (!err) {
	ndata->state = CM108GPIO_IN_OPEN;
	ndata->open_done = open_done;
	ndata->open_data = open_data;
	cm108gpio_start_deferred_op(ndata);
    }
 out_unlock:
    cm108gpio_unlock(ndata);

    return err;
}

static int
cm108gpio_close(struct gensio *io, gensio_done close_done, void *close_data)
{
    struct cm108gpio_data *ndata = gensio_get_gensio_data(io);
    int err = 0;

    cm108gpio_lock(ndata);
    if (ndata->state != CM108GPIO_OPEN && ndata->state != CM108GPIO_IN_OPEN) {
	err = GE_NOTREADY;
	goto out_unlock;
    }
    if (ndata->state == CM108GPIO_IN_OPEN)
	ndata->state = CM108GPIO_IN_OPEN_CLOSE;
    else
	ndata->state = CM108GPIO_IN_CLOSE;
    ndata->close_done = close_done;
    ndata->close_data = close_data;
    cm108gpio_start_deferred_op(ndata);
 out_unlock:
    cm108gpio_unlock(ndata);

    return err;
}

static void
cm108gpio_free(struct gensio *io)
{
    struct cm108gpio_data *ndata = gensio_get_gensio_data(io);

    cm108gpio_lock(ndata);
    ndata->state = CM108GPIO_CLOSED;
    cm108gpio_unlock_and_deref(ndata);
}

static int
cm108gpio_disable(struct gensio *io)
{
    struct cm108gpio_data *ndata = gensio_get_gensio_data(io);

    cm108gpio_lock(ndata);
    ndata->state = CM108GPIO_CLOSED;
    cm108gpio_unlock(ndata);

    return 0;
}

static int
cm108gpio_control(struct gensio *io, bool get, int option, char *data,
		   gensiods *datalen)
{
    struct cm108gpio_data *ndata = gensio_get_gensio_data(io);

    if (option != GENSIO_CONTROL_RADDR)
	return GE_NOTSUP;
    if (!get)
	return GE_NOTSUP;
    if (strtoul(data, NULL, 0) > 0)
	return GE_NOTFOUND;
    *datalen = gensio_pos_snprintf(data, *datalen, NULL, "cm108gpio,%s,%u",
				   ndata->idnum, ndata->bit);
    return 0;
}

static int
gensio_cm108gpio_func(struct gensio *io, int func, gensiods *count,
		       const void *cbuf, gensiods buflen, void *buf,
		       const char *const *auxdata)
{
    switch (func) {
    case GENSIO_FUNC_WRITE_SG:
	return cm108gpio_write(io, count, cbuf, buflen);

    case GENSIO_FUNC_OPEN:
	return cm108gpio_open(io, (void *) cbuf, buf);

    case GENSIO_FUNC_CLOSE:
	return cm108gpio_close(io, (void *) cbuf, buf);

    case GENSIO_FUNC_FREE:
	cm108gpio_free(io);
	return 0;

    case GENSIO_FUNC_SET_READ_CALLBACK:
	return 0;

    case GENSIO_FUNC_SET_WRITE_CALLBACK:
	cm108gpio_set_write_callback_enable(io, buflen);
	return 0;

    case GENSIO_FUNC_DISABLE:
	return cm108gpio_disable(io);

    case GENSIO_FUNC_CONTROL:
	return cm108gpio_control(io, *((bool *) cbuf), buflen, buf, count);

    default:
	return GE_NOTSUP;
    }
}

static int
cm108gpio_ndata_setup(struct gensio_pparm_info *p, struct gensio_os_funcs *o,
		      const char *idnum, unsigned int bit,
		      struct cm108gpio_data **new_ndata)
{
    struct cm108gpio_data *ndata;
    int err = GE_NOMEM;

    ndata = o->zalloc(o, sizeof(*ndata));
    if (!ndata)
	return GE_NOMEM;
    ndata->o = o;
    ndata->refcount = 1;
    ndata->fd = INVALID_FD;

    if (!idnum)
	idnum = "";
    ndata->idnum = gensio_strdup(o, idnum);
    if (!ndata->idnum)
	goto out_err;

    ndata->bit = bit;

    ndata->lock = o->alloc_lock(o);
    if (!ndata->lock)
	goto out_err;

    ndata->deferred_op_runner = o->alloc_runner(o, cm108gpio_deferred_op,
						ndata);
    if (!ndata->deferred_op_runner)
	goto out_err;

    err = find_hid_device(p, o, idnum, &ndata->devpath);
    if (err)
	goto out_err;

    *new_ndata = ndata;

    return 0;

 out_err:
    cm108gpio_finish_free(ndata);

    return err;
}

static int
cm108gpio_gensio_alloc(const void *gdata,
		       const char * const args[],
		       struct gensio_os_funcs *o,
		       gensio_event cb, void *user_data,
		       struct gensio **new_gensio)
{
    int err;
    struct cm108gpio_data *ndata = NULL;
    int i;
    GENSIO_DECLARE_PPGENSIO(p, o, cb, "cm108gpio", user_data);
    unsigned int bit = 3;

    for (i = 0; args && args[i]; i++) {
	if (gensio_pparm_uint(&p, args[i], "bit", &bit) > 0)
	    continue;
	gensio_pparm_unknown_parm(&p, args[i]);
	return GE_INVAL;
    }

    if (bit < 1 || bit > 8) {
	gensio_pparm_log(&p, "Bit value must be from 1-8, it was %u\n", bit);
	return GE_INVAL;
    }

    err = cm108gpio_ndata_setup(&p, o, gdata, bit, &ndata);
    if (err)
	return err;

    ndata->io = gensio_data_alloc(ndata->o, cb, user_data,
				  gensio_cm108gpio_func, NULL, "cm108gpio",
				  ndata);
    if (!ndata->io)
	goto out_nomem;

    *new_gensio = ndata->io;

    return 0;

 out_nomem:
    cm108gpio_finish_free(ndata);
    return GE_NOMEM;
}

static int
str_to_cm108gpio_gensio(const char *str, const char * const args[],
			struct gensio_os_funcs *o,
			gensio_event cb, void *user_data,
			struct gensio **new_gensio)
{
    return cm108gpio_gensio_alloc(str, args, o, cb, user_data, new_gensio);
}

int
gensio_init_cm108gpio(struct gensio_os_funcs *o)
{
    int rv;

    rv = register_gensio(o, "cm108gpio",
			 str_to_cm108gpio_gensio, cm108gpio_gensio_alloc);
    if (rv)
	return rv;
    return 0;
}
