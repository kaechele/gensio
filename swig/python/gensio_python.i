/*
 *  gensio - A library for abstracting stream I/O
 *  Copyright (C) 2018  Corey Minyard <minyard@acm.org>
 *
 *  SPDX-License-Identifier: LGPL-2.1-only
 */

%typemap(in) swig_cb * {
    if ($input == Py_None)
	$1 = NULL;
    else
	$1 = $input;
}

%exception {
    $action
    if (PyErr_Occurred())
	SWIG_fail;
}

%typemap(in, numinputs=0) char **rbuffer (char *temp = NULL),
                          size_t *rbuffer_len (size_t temp = 0) {
    $1 = &temp;
}

%typemap(argout) (char **rbuffer, size_t *rbuffer_len) {
    PyObject *r;

    if (*$1) {
	r = PyBytes_FromStringAndSize(*$1, *$2);
	free(*$1);
    } else {
	Py_INCREF(Py_None);
	r = Py_None;
    }

    $result = add_python_result($result, r);
}

%typemap(in, numinputs=0) char **rstr (char *temp = NULL),
                          size_t *rstr_len (size_t temp = 0) {
    $1 = &temp;
}

%typemap(argout) (char **rstr, size_t *rstr_len) {
    PyObject *r;

    if (*$1) {
	r = OI_PI_FromStringAndSize(*$1, *$2);
	free(*$1);
    } else {
	Py_INCREF(Py_None);
	r = Py_None;
    }

    $result = add_python_result($result, r);
}

%typemap(in, numinputs=0) long *r_int (long temp = 0) {
    $1 = &temp;
}

%typemap(argout) (long *r_int) {
    PyObject *r = PyInt_FromLong(*$1);

    $result = add_python_result($result, r);
}

%typemap(in, numinputs=0) struct gensio **io (struct gensio *temp = NULL) {
    $1 = &temp;
}

%typemap(argout) (struct gensio **io) {
    PyObject *r;

    if (*$1) {
	r = SWIG_NewPointerObj((void *) *$1, SWIGTYPE_p_gensio,
			       SWIG_POINTER_OWN);
    } else {
	Py_INCREF(Py_None);
	r = Py_None;
    }

    /*
     * This is kind of a hack, gensio must be the first return item.
     * We can't use add_python_result because *io may be Py_None.
     */
    Py_XDECREF($result);
    $result = PyTuple_New(1);
    PyTuple_SetItem($result, 0, r);
}

%typemap(in) (char *bytestr, my_ssize_t len) {
    if ($input == Py_None) {
	$1 = NULL;
	$2 = 0;
    } else if (OI_PI_BytesCheck($input)) {
	OI_PI_AsBytesAndSize($input, &$1, &$2);
    } else if (PyByteArray_Check($input)) {
	$1 = PyByteArray_AsString($input);
	$2 = PyByteArray_Size($input);
    } else {
        PyErr_SetString(PyExc_TypeError, "Must be a byte string or array");
        SWIG_fail;
    }
}

%typemap(in) const char ** {
    unsigned int i;
    unsigned int len;
    char **temp = NULL;

    if ($input == Py_None)
	goto null_auxdata;
    if (!PySequence_Check($input)) {
	PyErr_SetString(PyExc_TypeError, "Expecting a sequence");
	SWIG_fail;
    }
    len = PyObject_Length($input);
    if (len == 0)
	goto null_auxdata;

    temp = malloc(sizeof(char *) * (len + 1));
    if (!temp) {
	PyErr_SetString(PyExc_ValueError, "Out of memory");
	SWIG_fail;
    }
    memset(temp, 0, sizeof(char *) * (len + 1));
    for (i = 0; i < len; i++) {
	PyObject *o = PySequence_GetItem($input, i);

	if (!OI_PI_StringCheck(o)) {
	    Py_XDECREF(o);
	    PyErr_SetString(PyExc_ValueError,
			    "Expecting a sequence of strings");
	    for (; i > 0; i--)
		Py_XDECREF(temp[i - 1]);
	    free(temp);
	    SWIG_fail;
	}
	temp[i] = (char *) OI_PI_AsString(o);
	Py_DECREF(o);
    }
 null_auxdata:
    $1 = temp;
}

%typemap(freearg) const char ** {
    if ($1) {
	free($1);
    }
};

%typemap(ret) const char * {
    /* const char * means we don't have to free the string. */
}

%typemap(ret) char * {
    if ($1) {
	free($1);
    }
}
