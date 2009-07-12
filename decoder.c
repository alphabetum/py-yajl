/*
 * Copyright 2009, R. Tyler Ballance <tyler@slide.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 * 
 *  3. Neither the name of R. Tyler Ballancenor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */ 

#include <string.h>

#include <Python.h>

#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#include "py_yajl.h"

enum { failure, success };

static void Push(PyObject *stack, PyObject *element)
{
    if (!stack)
        return;
    PyList_Append(stack, element);
}
static PyObject *Pop(PyObject *stack)
{
    return PyObject_CallMethodObjArgs(stack, PyString_FromString("pop"), NULL);
}
static unsigned int Length(PyObject *stack)
{
    if (!stack) 
        return 0;
    return (unsigned int)(PyList_Size(stack));
}

static PyObject *LastElement(PyObject *stack)
{
    unsigned int length = 0;

    if (!stack)
        return NULL;

    length = Length(stack);
    if (!length)
        return NULL;

    return PyList_GetItem(stack, length - 1);
}

int _PlaceObject(_YajlDecoder *self, PyObject *parent, PyObject *child)
{
    if ( (!self) || (!child) || (!parent) )
        return failure;

    if (PyList_Check(parent)) {
        PyList_Append(parent, child);
        return success;
    }
    if (PyDict_Check(parent)) {
        PyDict_SetItem(parent, Pop(self->keys), child);
        return success;
    }
    return failure;
}

int PlaceObject(_YajlDecoder *self, PyObject *object)
{
    unsigned int length;

    if (self->elements == NULL)
        return failure;

    length = Length(self->elements);

    if (length == 0) {
        /*
         * When the length is zero, and we're entering this code path
         * we should only be handling "primitive types" i.e. strings and
         * numbers, not dict/list.
         */
        self->root = object;
        return success;
    }
    return _PlaceObject(self, LastElement(self->elements), object);
}


int handle_null(void *ctx)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    PyObject *object = Py_None;
    int rc = -1;

    Py_INCREF(Py_None);

    rc = PlaceObject(self, object);

    if (rc == success)
        return success;
    Py_DECREF(Py_None);
    return failure;
}

int handle_int(void *ctx, long value)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    PyObject *object = PyInt_FromLong(value);

    return PlaceObject(self, object);
}

int handle_bool(void *ctx, int value)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    PyObject *object = PyBool_FromLong((long)(value));

    return PlaceObject(self, object);
}

int handle_double(void *ctx, double value)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    PyObject *object = PyFloat_FromDouble(value);

    return PlaceObject(self, object);
}

int handle_number(void *ctx, const char *value, unsigned int length)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    char *number = (char *)(malloc(sizeof(char) * (length + 1)));
    PyObject *string, *object;

    number = strncpy(number, value, length);
    number[length] = '\0';
    string = PyString_FromStringAndSize(number, length);

    object = PyFloat_FromString(string, NULL);
    
    return PlaceObject(self, object);
}

int handle_string(void *ctx, const unsigned char *value, unsigned int length)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    PyObject *object = PyString_FromStringAndSize((char *)(value), length);

    return PlaceObject(self, object);
}

int handle_start_dict(void *ctx)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    PyObject *object = PyDict_New();

    if (!object)
        return failure;

    Push(self->elements, object);
    return success;;
}

int handle_dict_key(void *ctx, const unsigned char *value, unsigned int length)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    PyObject *object = NULL;
    char *key = (char *)(malloc(sizeof(char) * (length + 1)));

    key = strncpy(key, (char *)(value), length);
    key[length] = '\0';

    object = PyString_FromStringAndSize(key, length);
    if (!object)
        return failure;

    Push(self->keys, object);
    return success;
}

int handle_end_dict(void *ctx)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    PyObject *last, *popped;
    unsigned int length;

    if (!self->elements)
        return failure;

    length = Length(self->elements);
    if (length == 1) {
        /* 
         * If this is the last element in the stack
         * then it's "root" and we should finish up
         */
        self->root = Pop(self->elements);
        return success;
    }
    
    /*
     * If not, then we should properly add this dict
     * to it's appropriate parent
     */
    popped = Pop(self->elements);
    if (!popped) 
        return failure;

    last = LastElement(self->elements);

    return _PlaceObject(self, last, popped);
}

int handle_start_list(void *ctx)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    PyObject *object = PyList_New(0);

    if (!object)
        return failure;

    Push(self->elements, object);
    return success;
}

int handle_end_list(void *ctx)
{
    _YajlDecoder *self = (_YajlDecoder *)(ctx);
    PyObject *last, *popped;
    unsigned int length;

    if (!self->elements)
        return failure;

    length = Length(self->elements);
    if (length == 1) {
        self->root = Pop(self->elements);
        return success;
    }
    
    popped = Pop(self->elements);
    if (!popped)
        return failure;
    
    last = LastElement(self->elements);

    return _PlaceObject(self, last, popped);
}

static yajl_callbacks decode_callbacks = {
    handle_null,
    handle_bool, 
    handle_int, 
    handle_double,
    handle_number,
    handle_string,
    handle_start_dict,
    handle_dict_key,
    handle_end_dict,
    handle_start_list, 
    handle_end_list
};

PyObject *py_yajldecoder_decode(PYARGS)
{
    _YajlDecoder *decoder = (_YajlDecoder *)(self);
    char *buffer = NULL;
    unsigned int buflen = 0;
    yajl_handle parser = NULL;
    yajl_status yrc;
    yajl_parser_config config = { 1, 1 };

    if (!PyArg_ParseTuple(args, "z#", &buffer, &buflen))
        return NULL;

    if (!buflen) {
        /* Raise some sort of exception? */
        fprintf(stderr, "`buflen` == NULL\n");
        return NULL;
    }

    if (decoder->elements) {
        Py_XDECREF(decoder->elements);
        decoder->elements = PyList_New(0);
    }
    if (decoder->keys) {
        Py_XDECREF(decoder->keys);
        decoder->keys = PyList_New(0);
    }

    /* callbacks, config, allocfuncs */
    parser = yajl_alloc(&decode_callbacks, &config, NULL, (void *)(self));

    yrc = yajl_parse(parser, (const unsigned char *)(buffer), buflen);
    yajl_parse_complete(parser);

    /*
     * Poor man's debuggery
    PyObject *s = PyObject_Str(decoder->root);
    fprintf(stderr, "FOOOOO: %s\n", PyString_AsString(s));
    */
    

    if (yrc != yajl_status_ok) {
        /* Raise some sort of exception */
        fprintf(stderr, "FAIL : %s\n", yajl_status_to_string(yrc));
        return NULL;
    }

    if (decoder->root == NULL) {
        fprintf(stderr, "FAIL : %s\n", yajl_status_to_string(yrc));
        return NULL;
    }
    
    return decoder->root;
}

PyObject *py_yajldecoder_raw_decode(PYARGS)
{
    return NULL;
}

int yajldecoder_init(PYARGS)
{
    _YajlDecoder *me = (_YajlDecoder *)(self);

    /* Set to NULL so the parser callbacks will function properly */
    me->elements = PyList_New(0);
    me->keys = PyList_New(0);
    me->root = NULL;

    return 0;
}
