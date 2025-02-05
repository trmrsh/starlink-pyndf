//
// Python/C interface file for ndf files

/*
    Copyright 2009-2011 Tom Marsh
    Copyright 2011 Richard Hickman
    Copyright 2011 Tim Jenness
    All Rights Reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */
//

#include <Python.h>
#include "structmember.h"
#include "numpy/arrayobject.h"

// Wrap the PyCObject -> PyCapsule transition to allow
// this to build with python2.
#include "../ndf/npy_3kcompat.h"

#include <stdio.h>
#include <string.h>

// NDF includes
#include "ndf.h"
#include "mers.h"
#include "star/hds.h"
#include "sae_par.h"
#include "prm_par.h"

static PyObject * StarlinkHDSError = NULL;

#if PY_VERSION_HEX >= 0x03000000
# define USE_PY3K
#endif

// Define an HDS object

typedef struct {
    PyObject_HEAD
    PyObject * _locator;
} HDSObject;

// Prototypes

static PyObject *
HDS_create_object( HDSLoc * loc );
static HDSLoc *
HDS_retrieve_locator( HDSObject * self );
static PyObject*
pydat_transfer(PyObject *self, PyObject *args);

// Deallocator. Need to see how this interacts with the PyCapsule deallocator

static void
HDS_dealloc(HDSObject * self)
{
    Py_XDECREF(self->_locator);
    PyObject_Del(self);
}

// Allocator of an HDS object

static PyObject *
HDS_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    HDSObject *self;

    self = (HDSObject *) _PyObject_New( type );
    if (self != NULL) {
      self->_locator = Py_None;
      if (self->_locator == NULL) {
        Py_DECREF(self);
        return NULL;
      }
    }

    return (PyObject *)self;
}

// __init__ method

static int
HDS_init(HDSObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *_locator = NULL;
    static char *kwlist[] = {"_locator", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist,
                                      &_locator ))
        return -1;

    if (_locator) {
      PyObject * tmp = self->_locator;
      Py_INCREF(_locator);
      self->_locator = _locator;
      Py_XDECREF(tmp);
    }

    return 0;
}


// Removes locators once they are no longer needed

static void PyDelLoc_ptr(void *ptr)
{
    HDSLoc* loc = (HDSLoc*)ptr;
    int status = SAI__OK;
    datAnnul(&loc, &status);
    printf("Inside PyDelLoc\n");
    return;
}

// Need a PyCapsule version for Python3

#ifdef USE_PY3K
static void PyDelLoc( PyObject *cap )
{
  PyDelLoc_ptr( PyCapsule_GetPointer( cap, NULL ));
}
#else
static void PyDelLoc( void * ptr )
{
  PyDelLoc_ptr(ptr);
}
#endif


// Extracts the contexts of the EMS error stack and raises an
// exception. Returns true if an exception was raised else
// false. Can be called as:
//   if (raiseHDSException(&status)) return NULL;
// The purpose of this routine is to flush errors and close
// the error context with an errEnd(). errBegin has be called
// in the code that is about to call Starlink routines.

#include "dat_err.h"
#include "ndf_err.h"

static int
raiseHDSException( int *status )
{
  char param[ERR__SZPAR+1];
  char opstr[ERR__SZMSG+1];
  int parlen = 0;
  int oplen = 0;
  size_t stringlen = 1;
  PyObject * thisexc = NULL;
  char * errstring = NULL;

  if (*status == SAI__OK) return 0;

  // We can translate some internal errors into standard python exceptions
  switch (*status) {
  case DAT__FILNF:
    thisexc = PyExc_IOError;
    break;
  default:
    thisexc = StarlinkHDSError;
  }

  // Start with a nul terminated buffer
  errstring = malloc( stringlen );
  if (!errstring) PyErr_NoMemory();
  errstring[0] = '\0';

  // Build up a string with the full error message
  while (*status != SAI__OK && errstring) {
    errLoad( param, sizeof(param), &parlen, opstr, sizeof(opstr), &oplen, status );
    if (*status != SAI__OK) {
      char *newstring;
      stringlen += oplen + 1;
      newstring = realloc( errstring, stringlen );
      if (newstring) {
        errstring = newstring;
        strcat( errstring, opstr );
        strcat( errstring, "\n" );
     } else {
        if (errstring) free(errstring);
        PyErr_NoMemory();
      }
    }
  }

  if (errstring) {
    PyErr_SetString( thisexc, errstring );
    free(errstring);
  }

  errEnd(status);
  return 1;
}

// Now onto main routines

// Destructor. Needs thought.

static PyObject* 
pydat_annul(HDSObject *self)
{
    // Recover C-pointer passed via Python
    HDSLoc* loc = HDS_retrieve_locator(self);
    int status = SAI__OK;
    errBegin(&status);
    datAnnul(&loc, &status);
    if(raiseHDSException(&status)) return NULL;
    Py_RETURN_NONE;
};

static PyObject* 
pydat_cell(HDSObject *self, PyObject *args)
{
    PyObject *pobj1, *osub;
    if(!PyArg_ParseTuple(args, "O:pydat_cell", &osub))
	return NULL;

    // Recover C-pointer passed via Python
    HDSLoc* loc1 = HDS_retrieve_locator(self);

    // Attempt to convert the input to something useable
    PyArrayObject *sub = (PyArrayObject *) PyArray_ContiguousFromAny(osub, NPY_INT, 1, 1);
    if(!sub) return NULL;
    
    // Convert Python-like --> Fortran-like
    int ndim = PyArray_SIZE(sub);
    int i, rdim[ndim];
    int *sdata = (int*)PyArray_DATA(sub);
    for(i=0; i<ndim; i++) rdim[i] = sdata[ndim-i-1]+1;

    HDSLoc* loc2 = NULL;
    int status = SAI__OK;
    errBegin(&status);
    // Finally run the routine
    datCell(loc1, ndim, rdim, &loc2, &status);
    if(status != SAI__OK) goto fail;

    // PyCObject to pass pointer along to other wrappers
    Py_DECREF(sub);
    return HDS_create_object(loc2);

fail:    
    raiseHDSException(&status);
    Py_XDECREF(sub);
    return NULL;
};

static PyObject* 
pydat_index(HDSObject *self, PyObject *args)
{
    PyObject* pobj;
    int index;
    if(!PyArg_ParseTuple(args, "i:pydat_index", &index))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc1 = HDS_retrieve_locator(self);
    HDSLoc* loc2 = NULL;

    int status = SAI__OK;    
    errBegin(&status);
    datIndex(loc1, index+1, &loc2, &status);
    if(raiseHDSException(&status)) return NULL;
    return HDS_create_object(loc2);
};

static PyObject* 
pydat_find(HDSObject *self, PyObject *args)
{
    PyObject* pobj1;
    const char* name;
    if(!PyArg_ParseTuple(args, "s:pydat_find", &name))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc1 = HDS_retrieve_locator( self );
    HDSLoc* loc2 = NULL;

    int status = SAI__OK;    
    errBegin(&status);
    datFind(loc1, name, &loc2, &status);
    if (raiseHDSException(&status)) return NULL;

    // PyCObject to pass pointer along to other wrappers
    return HDS_create_object(loc2);
};

static PyObject* 
pydat_get(HDSObject *self)
{
    // Recover C-pointer passed via Python
    HDSLoc* loc = HDS_retrieve_locator(self);

    // guard against structures
    int state, status = SAI__OK;
    errBegin(&status);
    datStruc(loc, &state, &status);
    if (raiseHDSException(&status)) return NULL;
    if(state){
	PyErr_SetString(PyExc_IOError, "dat_get error: cannot use on structures");
	return NULL;
    }

    // get type
    char typ_str[DAT__SZTYP+1];
    datType(loc, typ_str, &status);

    // get shape
    const int NDIMX=7;
    int ndim;
    hdsdim tdim[NDIMX];
    datShape(loc, NDIMX, tdim, &ndim, &status);
    if (raiseHDSException(&status)) return NULL;

    PyArrayObject* arr = NULL;

    // Either return values as a single scalar or a numpy array

    // Reverse order of dimensions
    npy_intp rdim[NDIMX];
    int i;
    for(i=0; i<ndim; i++) rdim[i] = tdim[ndim-i-1];

    if(strcmp(typ_str, "_INTEGER") == 0 || strcmp(typ_str, "_LOGICAL") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, rdim, NPY_INT);
    }else if(strcmp(typ_str, "_REAL") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, rdim, NPY_FLOAT);
    }else if(strcmp(typ_str, "_DOUBLE") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, rdim, NPY_DOUBLE);
    }else if(strncmp(typ_str, "_CHAR", 5) == 0){

	// work out the number of bytes
	size_t nbytes;
	datLen(loc, &nbytes, &status);
	if (status != SAI__OK) goto fail;

	int ncdim = 1+ndim;
	int cdim[ncdim];
	cdim[0] = nbytes+1;
	for(i=0; i<ndim; i++) cdim[i+1] = rdim[i];

	PyArray_Descr *descr = PyArray_DescrNewFromType(NPY_STRING);
	descr->elsize = nbytes;
	arr = (PyArrayObject*) PyArray_NewFromDescr(&PyArray_Type, descr, ndim, rdim, 
						    NULL, NULL, 0, NULL); 

    }else if(strcmp(typ_str, "_WORD") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, rdim, NPY_SHORT);
    }else if(strcmp(typ_str, "_UWORD") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, rdim, NPY_USHORT);
    }else if(strcmp(typ_str, "_BYTE") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, rdim, NPY_BYTE);
    }else if(strcmp(typ_str, "_UBYTE") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, rdim, NPY_UBYTE);
    }else{
	PyErr_SetString(PyExc_IOError, "dat_get: encountered an unimplemented type");
	return NULL;
    }
    if(arr == NULL) goto fail;
    datGet(loc, typ_str, ndim, tdim, arr->data, &status);
    if(status != SAI__OK) goto fail;
    return PyArray_Return(arr);

fail:    
    raiseHDSException(&status);
    Py_XDECREF(arr);
    return NULL;

};

static PyObject* 
pydat_name(HDSObject *self)
{
    // Recover C-pointer passed via Python
    HDSLoc* loc = HDS_retrieve_locator(self);

    char name_str[DAT__SZNAM+1];
    int status = SAI__OK;
    errBegin(&status);
    datName(loc, name_str, &status);
    if (raiseHDSException(&status)) return NULL;
    return Py_BuildValue("s", name_str);
};

static PyObject* 
pydat_ncomp(HDSObject *self)
{
    // Recover C-pointer passed via Python
    HDSLoc* loc = HDS_retrieve_locator(self);

    int status = SAI__OK, ncomp;
    errBegin(&status);
    datNcomp(loc, &ncomp, &status);
    if (raiseHDSException(&status)) return NULL;

    return Py_BuildValue("i", ncomp);
};

static PyObject* 
pydat_shape(HDSObject *self)
{
    // Recover C-pointer passed via Python
    HDSLoc* loc = HDS_retrieve_locator(self);

    const int NDIMX=7;
    int ndim;
    hdsdim tdim[NDIMX];
    int status = SAI__OK;
    errBegin(&status);
    datShape(loc, NDIMX, tdim, &ndim, &status);
    if (raiseHDSException(&status)) return NULL;

    // Return None in this case
    if(ndim == 0) Py_RETURN_NONE;

    // Create array of correct dimensions to save data to
    PyArrayObject* dim = NULL;
    npy_intp sdim[1];
    int i;
    sdim[0] = ndim;
    dim = (PyArrayObject*) PyArray_SimpleNew(1, sdim, PyArray_INT);
    if(dim == NULL) goto fail;

    // Reverse order Fortran --> C convention
    int* sdata = (int*)dim->data;
    for(i=0; i<ndim; i++) sdata[i] = tdim[ndim-i-1];
    return Py_BuildValue("N", PyArray_Return(dim));

fail:
    Py_XDECREF(dim);
    return NULL;
};

static PyObject* 
pydat_state(HDSObject *self, PyObject *args)
{
    // Recover C-pointer passed via Python
    HDSLoc* loc = HDS_retrieve_locator(self);

    int status = SAI__OK, state;
    errBegin(&status);
    datState(loc, &state, &status);
    if (raiseHDSException(&status)) return NULL;
    return Py_BuildValue("i", state);
};

static PyObject* 
pydat_struc(HDSObject *self)
{
    // Recover C-pointer passed via Python
    HDSLoc* loc = HDS_retrieve_locator(self);

    // guard against structures
    int state, status = SAI__OK;
    errBegin(&status);
    datStruc(loc, &state, &status);
    if (raiseHDSException(&status)) return NULL;
    return Py_BuildValue("i", state);
};

static PyObject* 
pydat_type(HDSObject *self)
{
    // Recover C-pointer passed via Python
    HDSLoc* loc = HDS_retrieve_locator(self);

    char typ_str[DAT__SZTYP+1];
    int status = SAI__OK;
    errBegin(&status);
    datType(loc, typ_str, &status);
    if (raiseHDSException(&status)) return NULL;
    return Py_BuildValue("s", typ_str);
};

static PyObject* 
pydat_valid(HDSObject *self)
{
    // Recover C-pointer passed via Python
    HDSLoc* loc = HDS_retrieve_locator(self);

    int state, status = SAI__OK;    
    errBegin(&status);
    datValid(loc, &state, &status);
    if (raiseHDSException(&status)) return NULL;

    return Py_BuildValue("i", state);
};

// check an HDS type
inline int checkHDStype(const char *type)
{
	if(strcmp(type,"_INTEGER") != 0 && strcmp(type,"_REAL") != 0 && strcmp(type,"_DOUBLE") != 0 &&
			strcmp(type,"_LOGICAL") != 0 && strcmp(type,"_WORD") != 0 && strcmp(type,"UWORD") != 0 &&
			strcmp(type,"_BYTE") != 0 && strcmp(type,"_UBYTE") != 0 && strcmp(type,"_CHAR") != 0 &&
			strncmp(type,"_CHAR*",6) != 0)
		return 0;
	else
		return 1;
}


// make a new primitive
static PyObject*
pydat_new(HDSObject *self, PyObject *args)
{
	PyObject *dimobj;
	const char *type, *name;
	int ndim;
	if(!PyArg_ParseTuple(args, "ssiO:pydat_new", &name, &type, &ndim, &dimobj))
		return NULL;
	HDSLoc* loc = HDS_retrieve_locator(self);
	if(!checkHDStype(type))
		return NULL;
	int status = SAI__OK;
        errBegin(&status);
	if (ndim > 0) {
		PyArrayObject *npydim = (PyArrayObject*) PyArray_FROM_OTF(dimobj,NPY_INT,NPY_IN_ARRAY|NPY_FORCECAST);
		hdsdim *dims = (hdsdim*)PyArray_DATA(npydim);
		datNew(loc,name,type,ndim,dims,&status);
		Py_DECREF(npydim);
	} else {
		datNew(loc,name,type,0,0,&status);
	}
	if (raiseHDSException(&status))
		return NULL;
	Py_RETURN_NONE;
}


// write a primitive
static PyObject*
pydat_put(HDSObject *self, PyObject *args)
{
	PyObject *value, *dimobj;
	PyArrayObject *npyval;
	const char* type;
	int ndim;
	if(!PyArg_ParseTuple(args,"siOO:pydat_put",&type,&ndim,&dimobj,&value))
		return NULL;
	if(!checkHDStype(type))
		return NULL;
	HDSLoc* loc = HDS_retrieve_locator(self);
	// create a pointer to an array of the appropriate data type
	if(strcmp(type,"_INTEGER") == 0) {
		npyval = (PyArrayObject*) PyArray_FROM_OTF(value, NPY_INT, NPY_IN_ARRAY | NPY_FORCECAST);
	} else if(strcmp(type,"_REAL") == 0) {
		npyval = (PyArrayObject*) PyArray_FROM_OTF(value, NPY_FLOAT, NPY_IN_ARRAY | NPY_FORCECAST);
	} else if(strcmp(type,"_DOUBLE") == 0) {
		npyval = (PyArrayObject*) PyArray_FROM_OTF(value, NPY_DOUBLE, NPY_IN_ARRAY | NPY_FORCECAST);
	} else if(strcmp(type,"_BYTE") == 0) {
		npyval = (PyArrayObject*) PyArray_FROM_OTF(value, NPY_BYTE, NPY_IN_ARRAY | NPY_FORCECAST);
	} else if(strcmp(type,"_UBYTE") == 0) {
		npyval = (PyArrayObject*) PyArray_FROM_OTF(value, NPY_UBYTE, NPY_IN_ARRAY | NPY_FORCECAST);
	} else if(strncmp(type,"_CHAR*",6) == 0) {
		npyval = (PyArrayObject*) PyArray_FROM_OT(value, NPY_STRING);
	} else {
		return NULL;
	}
	void *valptr = PyArray_DATA(npyval);
	int status = SAI__OK;
        errBegin(&status);
	if (ndim > 0) {
		// npydim is 1-D array stating the size of each dimension ie. npydim = numpy.array([1072 1072])
		// these are stored in an hdsdim type (note these are declared as signed)
		PyArrayObject *npydim = (PyArrayObject*) PyArray_FROM_OTF(dimobj,NPY_INT,NPY_IN_ARRAY|NPY_FORCECAST);
		hdsdim *dims = (hdsdim*)PyArray_DATA(npydim);
		datPut(loc,type,ndim,dims,valptr,&status);
		Py_DECREF(npydim);
	} else {
		datPut(loc,type,0,0,valptr,&status);
	}
	if (raiseHDSException(&status))
		return NULL;
	Py_DECREF(npyval);
	Py_RETURN_NONE;
}

static PyObject*
pydat_putc(HDSObject *self, PyObject *args)
{
	PyObject *strobj,*locobj;
	int strlen;
	if(!PyArg_ParseTuple(args,"Oi:pydat_putc",&strobj,&strlen))
		return NULL;
	HDSLoc *loc = HDS_retrieve_locator(self);
	PyArrayObject *npystr = (PyArrayObject*) PyArray_FROM_OTF(strobj,NPY_STRING,NPY_FORCECAST);
	char *strptr = PyArray_DATA(npystr);
	int status = SAI__OK;
        errBegin(&status);
	datPutC(loc,0,0,strptr,(size_t)strlen,&status);
	if (raiseHDSException(&status))
		return NULL;
	Py_DECREF(npystr);
	Py_RETURN_NONE;
}

//
//
//  END OF METHODS - NOW DEFINE ATTRIBUTES AND MODULES

static PyMemberDef HDS_members[] = {
  {"_locator", T_OBJECT_EX, offsetof(HDSObject, _locator), 0,
   "HDS Locator"},
  {NULL} /* Sentinel */
};

// The methods

static PyMethodDef HDS_methods[] = {

  {"annul", (PyCFunction)pydat_annul, METH_NOARGS,
   "hdsloc.annul() -- annuls the HDS locator."},

  {"_transfer", (PyCFunction)pydat_transfer, METH_VARARGS,
   "starlink.hds.api.transfer(xloc) -- transfer HDS locator from NDF."},

  {"cell", (PyCFunction)pydat_cell, METH_VARARGS,
   "loc2 = hdsloc1.cell(sub) -- returns locator of a cell of an array."},

  {"index", (PyCFunction)pydat_index, METH_VARARGS,
   "loc2 = hdsloc1.index(index) -- returns locator of index'th component (starts at 0)."},

  {"find", (PyCFunction)pydat_find, METH_VARARGS,
   "loc2 = hdsloc1.find(name) -- finds a named component, returns locator."},

  {"get", (PyCFunction)pydat_get, METH_NOARGS,
   "value = hdsloc.get() -- get data associated with locator regardless of type."},

  {"name", (PyCFunction)pydat_name, METH_NOARGS,
   "name_str = hdsloc.name() -- returns name of components."},

  {"ncomp", (PyCFunction)pydat_ncomp, METH_NOARGS,
   "ncomp = hdsloc.ncomp() -- return number of components."},

  {"shape", (PyCFunction)pydat_shape, METH_NOARGS,
   "dim = loc.shape() -- returns shape of the component. dim=None for a scalar"},

  {"state", (PyCFunction)pydat_state, METH_NOARGS,
   "state = hdsloc.state() -- determine the state of an HDS component."},

  {"struc", (PyCFunction)pydat_struc, METH_NOARGS,
   "state = hdsloc.struc() -- is the component a structure."},

  {"type", (PyCFunction)pydat_type, METH_NOARGS,
   "typ_str = hdsloc.type() -- returns type of the component"},

  {"valid", (PyCFunction)pydat_valid, METH_NOARGS,
   "state = hdsloc.valid() -- is locator valid?"},

  {"put", (PyCFunction)pydat_put, METH_VARARGS,
   "status = hdsloc.put(type,ndim,dim,value) -- write a primitive inside an hds item."},

  {"new", (PyCFunction)pydat_new, METH_VARARGS,
   "hdsloc.new(name,type,ndim,dim) -- create a primitive given a locator."},

  {"putc", (PyCFunction)pydat_putc, METH_VARARGS,
   "hdsloc.putc(string) -- write a character string to primitive at locator."},

  {NULL, NULL, 0, NULL} /* Sentinel */
};

static PyTypeObject HDSType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "starlink.hds.api",             /* tp_name */
    sizeof(HDSObject),             /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)HDS_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT |
        Py_TPFLAGS_BASETYPE,   /* tp_flags */
    "Raw API for HDS access",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    HDS_methods,             /* tp_methods */
    HDS_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)HDS_init,      /* tp_init */
    0,                         /* tp_alloc */
    HDS_new,                 /* tp_new */
};

// Helper to create an object with an HDS locator

static PyObject *
HDS_create_object( HDSLoc * locator )
{
  PyObject * pobj;
  HDSObject * self = (HDSObject*)HDS_new( &HDSType, NULL, NULL );
  pobj = NpyCapsule_FromVoidPtr( locator, PyDelLoc );
  HDS_init( self, Py_BuildValue("O", pobj ), NULL);

  return (PyObject*)self;
}

static HDSLoc *
HDS_retrieve_locator( HDSObject *self)
{
  if (self) {
    return (HDSLoc*)NpyCapsule_AsVoidPtr(self->_locator);
  } else {
    return NULL;
  }
}

static PyObject*
pydat_transfer(PyObject *self, PyObject *args)
{
  HDSObject * newself = (HDSObject*)HDS_new( &HDSType, NULL, NULL );
  if (!newself) return NULL;
  HDS_init( newself, args, NULL);
  return (PyObject*)newself;
}

#ifdef USE_PY3K

#define RETVAL m

static struct PyModuleDef moduledef = {
  PyModuleDef_HEAD_INIT,
  "api",
  "Raw HDS API",
  -1,
  HDS_methods,
  NULL,
  NULL,
  NULL,
  NULL
};

PyObject *PyInit_api(void)
#else

#define RETVAL

PyMODINIT_FUNC
initapi(void)
#endif
{
    PyObject *m = NULL;

    if (PyType_Ready(&HDSType) < 0)
        return RETVAL;

#ifdef USE_PY3K
    m = PyModule_Create(&moduledef);
#else
    m = Py_InitModule3("api", HDS_methods,
                      "Raw HDS API");
#endif
    import_array();

    Py_INCREF(&HDSType);
    PyModule_AddObject(m, "api", (PyObject *)&HDSType);

    StarlinkHDSError = PyErr_NewException("starlink.hds.error", NULL, NULL);
    Py_INCREF(StarlinkHDSError);
    PyModule_AddObject(m, "error", StarlinkHDSError);

    return RETVAL;
}
