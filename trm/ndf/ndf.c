//
// Python/C interface file for ndf files
//

#include <Python.h>
#include "numpy/arrayobject.h"

#include <stdio.h>
#include <string.h>

// NDF includes
#include "ndf.h"
#include "star/hds.h"
#include "sae_par.h"
#include "prm_par.h"

// Removes locators once they are no longer needed
static void PyDelLoc(void *ptr)
{
    HDSLoc* loc = (HDSLoc*)ptr;
    int status = SAI__OK;
    datAnnul(&loc, &status);
    printf("Inside PyDelLoc\n");
    return;
}

// Translates from Python's axis number into the one NDF expects
// Needed because of inversion of C and Fortran arrays.
// Requires knowing the number of dimensions.

static int tr_iaxis(int indf, int iaxis, int *status)
{
    if(iaxis == -1) return 0;

    // Get dimensions
    const int NDIMX = 10;
    int ndim, idim[NDIMX];
    ndfDim(indf, NDIMX, idim, &ndim, status);
    if(*status != SAI__OK) return -1;
    if(iaxis < -1 || iaxis > ndim-1){
	PyErr_SetString(PyExc_IOError, "tr_axis: axis number too out of range");
	*status = SAI__ERROR;
	return -1;
    }
    return ndim-iaxis;
}

// Now onto main routines

static PyObject* 
pydat_annul(PyObject *self, PyObject *args)
{
    PyObject* pobj;
    if(!PyArg_ParseTuple(args, "O:pydat_annul", &pobj))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(pobj);
    int status = SAI__OK;    
    datAnnul(&loc, &status);
    if(status != SAI__OK) return NULL;
    Py_RETURN_NONE;
};

static PyObject* 
pydat_cell(PyObject *self, PyObject *args)
{
    PyObject *pobj1, *osub;
    if(!PyArg_ParseTuple(args, "OO:pydat_cell", &pobj1, &osub))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc1 = (HDSLoc*)PyCObject_AsVoidPtr(pobj1);

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
    
    // Finally run the routine
    datCell(loc1, ndim, rdim, &loc2, &status);
    if(status != SAI__OK) goto fail;

    // PyCObject to pass pointer along to other wrappers
    PyObject *pobj2 = PyCObject_FromVoidPtr(loc2, PyDelLoc);
    Py_DECREF(sub);
    return Py_BuildValue("O", pobj2);

fail:    

    Py_XDECREF(sub);
    return NULL;
};

static PyObject* 
pydat_index(PyObject *self, PyObject *args)
{
    PyObject* pobj;
    int index;
    if(!PyArg_ParseTuple(args, "Oi:pydat_index", &pobj, &index))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc1 = (HDSLoc*)PyCObject_AsVoidPtr(pobj);
    HDSLoc* loc2 = NULL;

    int status = SAI__OK;    
    datIndex(loc1, index+1, &loc2, &status);
    if(status != SAI__OK) return NULL;

    // PyCObject to pass pointer along to other wrappers
    PyObject *pobj2 = PyCObject_FromVoidPtr(loc2, PyDelLoc);
    return Py_BuildValue("O", pobj2);

};

static PyObject* 
pydat_find(PyObject *self, PyObject *args)
{
    PyObject* pobj1;
    const char* name;
    if(!PyArg_ParseTuple(args, "Os:pydat_find", &pobj1, &name))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc1 = (HDSLoc*)PyCObject_AsVoidPtr(pobj1);
    HDSLoc* loc2 = NULL;

    int status = SAI__OK;    
    datFind(loc1, name, &loc2, &status);
    if(status != SAI__OK) return NULL;

    // PyCObject to pass pointer along to other wrappers
    PyObject *pobj2 = PyCObject_FromVoidPtr(loc2, PyDelLoc);
    return Py_BuildValue("O", pobj2);
};

static PyObject* 
pydat_get(PyObject *self, PyObject *args)
{
    PyObject* pobj;
    if(!PyArg_ParseTuple(args, "O:pydat_get", &pobj))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(pobj);

    // guard against structures
    int state, status = SAI__OK;
    datStruc(loc, &state, &status);
    if(status != SAI__OK) return NULL;
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
    if(status != SAI__OK) return NULL;

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
	if(status != SAI__OK) goto fail;

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

    Py_XDECREF(arr);
    return NULL;

};

static PyObject* 
pydat_name(PyObject *self, PyObject *args)
{
    PyObject* pobj;
    if(!PyArg_ParseTuple(args, "O:pydat_name", &pobj))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(pobj);

    char name_str[DAT__SZNAM+1];
    int status = SAI__OK;
    datName(loc, name_str, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("s", name_str);
};

static PyObject* 
pydat_ncomp(PyObject *self, PyObject *args)
{
    PyObject* pobj;
    if(!PyArg_ParseTuple(args, "O:pydat_ncomp", &pobj))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(pobj);

    int status = SAI__OK, ncomp;
    datNcomp(loc, &ncomp, &status);
    if(status != SAI__OK) return NULL;

    return Py_BuildValue("i", ncomp);
};

static PyObject* 
pydat_shape(PyObject *self, PyObject *args)
{
    PyObject* pobj;
    if(!PyArg_ParseTuple(args, "O:pydat_type", &pobj))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(pobj);

    const int NDIMX=7;
    int ndim;
    hdsdim tdim[NDIMX];
    int status = SAI__OK;
    datShape(loc, NDIMX, tdim, &ndim, &status);
    if(status != SAI__OK) return NULL;

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
pydat_state(PyObject *self, PyObject *args)
{
    PyObject* pobj;
    if(!PyArg_ParseTuple(args, "O:pydat_state", &pobj))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(pobj);

    int status = SAI__OK, state;
    datState(loc, &state, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("i", state);
};

static PyObject* 
pydat_struc(PyObject *self, PyObject *args)
{
    PyObject* pobj;
    if(!PyArg_ParseTuple(args, "O:pydat_get", &pobj))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(pobj);

    // guard against structures
    int state, status = SAI__OK;
    datStruc(loc, &state, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("i", state);
};

static PyObject* 
pydat_type(PyObject *self, PyObject *args)
{
    PyObject* pobj;
    if(!PyArg_ParseTuple(args, "O:pydat_type", &pobj))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(pobj);

    char typ_str[DAT__SZTYP+1];
    int status = SAI__OK;
    datType(loc, typ_str, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("s", typ_str);
};

static PyObject* 
pydat_valid(PyObject *self, PyObject *args)
{
    PyObject* pobj;
    if(!PyArg_ParseTuple(args, "O:pydat_valid", &pobj))
	return NULL; 

    // Recover C-pointer passed via Python
    HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(pobj);
    int state, status = SAI__OK;    
    datValid(loc, &state, &status);
    if(status != SAI__OK) return NULL;

    return Py_BuildValue("i", state);
};

static PyObject* 
pyndf_acget(PyObject *self, PyObject *args)
{
    const char *comp;
    int indf, iaxis;
    if(!PyArg_ParseTuple(args, "isi:pyndf_acget", &indf, &comp, &iaxis))
	return NULL;

    // Return None if component does not exist
    int state, status = SAI__OK;
    int naxis = tr_iaxis(indf, iaxis, &status);

    ndfAstat(indf, comp, naxis, &state, &status);
    if(status != SAI__OK) return NULL;
    if(!state)
	Py_RETURN_NONE;

    int clen;
    ndfAclen(indf, comp, naxis, &clen, &status);
    char value[clen+1];
    ndfAcget(indf, comp, naxis, value, clen+1, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("s", value);
};

static PyObject* 
pyndf_aform(PyObject *self, PyObject *args)
{
    const char *comp;
    int indf, iaxis;
    if(!PyArg_ParseTuple(args, "isi:pyndf_aform", &indf, &comp, &iaxis))
	return NULL;
    int status = SAI__OK;
    int naxis = tr_iaxis(indf, iaxis, &status);
    char value[30];
    ndfAform(indf, comp, naxis, value, 30, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("s", value);
};

static PyObject* 
pyndf_annul(PyObject *self, PyObject *args)
{
    int indf;
    if(!PyArg_ParseTuple(args, "i:pyndf_annul", &indf))
	return NULL;
    int status = SAI__OK;
    ndfAnnul(&indf, &status);
    if(status != SAI__OK) return NULL;
    Py_RETURN_NONE;
};

static PyObject* 
pyndf_anorm(PyObject *self, PyObject *args)
{
    int indf, iaxis;
    if(!PyArg_ParseTuple(args, "ii:pyndf_anorm", &indf, &iaxis))
	return NULL;
    int state, status = SAI__OK;
    int naxis = tr_iaxis(indf, iaxis, &status);
    ndfAnorm(indf, naxis, &state, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("i", state);
};

static PyObject* 
pyndf_aread(PyObject *self, PyObject *args)
{
    int indf, iaxis;
    const char *MMOD = "READ";
    const char *comp;
    if(!PyArg_ParseTuple(args, "isi:pyndf_aread", &indf, &comp, &iaxis))
	return NULL;

    int status = SAI__OK;
    int naxis = tr_iaxis(indf, iaxis, &status);

    // Return None if component does not exist
    int state;
    ndfAstat(indf, comp, naxis, &state, &status);
    if(status != SAI__OK) return NULL;
    if(!state) Py_RETURN_NONE;

    // Get dimensions
    const int NDIMX = 10;
    int idim[NDIMX], ndim;
    ndfDim(indf, NDIMX, idim, &ndim, &status);
    if(status != SAI__OK) return NULL;

    // get number for particular axis in question.
    int nelem = idim[naxis-1];

    // Determine the data type
    const int MXLEN=33;
    char type[MXLEN];
    ndfAtype(indf, comp, naxis, type, MXLEN, &status);
    if(status != SAI__OK) return NULL;

    // Create array of correct dimensions and type to save data to
    size_t nbyte;
    ndim = 1;
    npy_intp dim[1] = {nelem};
    PyArrayObject* arr = NULL;
    if(strcmp(type, "_REAL") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, dim, PyArray_FLOAT);
	nbyte = sizeof(float);
    }else if(strcmp(type, "_DOUBLE") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, dim, PyArray_DOUBLE);
	nbyte = sizeof(double);
    }else if(strcmp(type, "_INTEGER") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, dim, PyArray_INT);
	nbyte = sizeof(int);
    }else{
	PyErr_SetString(PyExc_IOError, "ndf_aread error: unrecognised data type");
	goto fail;
    }
    if(arr == NULL) goto fail;

    // map, store, unmap
    int nread;
    void *pntr[1];
    ndfAmap(indf, comp, naxis, type, MMOD, pntr, &nread, &status);
    if(status != SAI__OK) goto fail;
    if(nelem != nread){
	printf("nread = %d, nelem = %d, iaxis = %d, %d\n",nread,nelem,iaxis,naxis);
	PyErr_SetString(PyExc_IOError, "ndf_aread error: number of elements different from number expected");
	goto fail;
    }
    memcpy(arr->data, pntr[0], nelem*nbyte);
    ndfAunmp(indf, comp, naxis, &status);

    return Py_BuildValue("N", PyArray_Return(arr));

fail:
    
    Py_XDECREF(arr);
    return NULL;

};

static PyObject* 
pyndf_astat(PyObject *self, PyObject *args)
{
    const char *comp;
    int indf, iaxis;
    if(!PyArg_ParseTuple(args, "isi:pyndf_astat", &indf, &comp, &iaxis))
	return NULL;
    int state, status = SAI__OK;
    int naxis = tr_iaxis(indf, iaxis, &status);
    ndfAstat(indf, comp, naxis, &state, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("i", state);
};

static PyObject* 
pyndf_init(PyObject *self, PyObject *args)
{
    int argc = 0, status = SAI__OK;
    char **argv = NULL;
    ndfInit(argc, argv, &status);
    if(status != SAI__OK) return NULL;
    Py_RETURN_NONE;
};

static PyObject* 
pyndf_begin(PyObject *self, PyObject *args)
{
    ndfBegin();
    Py_RETURN_NONE;
};

static PyObject* 
pyndf_bound(PyObject *self, PyObject *args)
{
    int indf, i;
    if(!PyArg_ParseTuple(args, "i:pyndf_bound", &indf))
	return NULL;

    PyArrayObject* bound = NULL;
    int ndim;
    const int NDIMX=20;
    int *lbnd = (int *)malloc(NDIMX*sizeof(int));
    int *ubnd = (int *)malloc(NDIMX*sizeof(int));
    if(lbnd == NULL || ubnd == NULL)
	goto fail;

    int status = SAI__OK;
    ndfBound(indf, NDIMX, lbnd, ubnd, &ndim, &status ); 
    if(status != SAI__OK) goto fail;

    npy_intp odim[2];
    odim[0] = 2;
    odim[1] = ndim;
    bound   = (PyArrayObject*) PyArray_SimpleNew(2, odim, PyArray_INT);
    if(bound == NULL) goto fail;
    int *bptr = (int *)bound->data;
    for(i=0; i<ndim; i++){
	bptr[i]      = lbnd[ndim-i-1];
	bptr[i+ndim] = ubnd[ndim-i-1];
    }
    free(lbnd);
    free(ubnd);
    return Py_BuildValue("N", PyArray_Return(bound));

fail:
    
    if(lbnd != NULL) free(lbnd);
    if(ubnd != NULL) free(ubnd);
    Py_XDECREF(bound);
    return NULL;
};

static PyObject* 
pyndf_cget(PyObject *self, PyObject *args)
{
    const char *comp;
    int indf;
    if(!PyArg_ParseTuple(args, "is:pyndf_cget", &indf, &comp))
	return NULL;

    // Return None if component does not exist
    int state, status = SAI__OK;
    ndfState(indf, comp, &state, &status);
    if(status != SAI__OK) return NULL;
    if(!state)
	Py_RETURN_NONE;

    int clen;
    ndfClen(indf, comp, &clen, &status);
    if(status != SAI__OK) return NULL;
    char value[clen+1];
    ndfCget(indf, comp, value, clen+1, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("s", value);
};

static PyObject* 
pyndf_dim(PyObject *self, PyObject *args)
{
    int indf, i;
    if(!PyArg_ParseTuple(args, "i:pyndf_dim", &indf))
	return NULL;

    PyArrayObject* dim = NULL;
    int ndim;
    const int NDIMX=20;
    int *idim = (int *)malloc(NDIMX*sizeof(int));
    if(idim == NULL)
	goto fail;

    int status = SAI__OK;
    ndfDim(indf, NDIMX, idim, &ndim, &status ); 
    if(status != SAI__OK) goto fail;

    npy_intp odim[1];
    odim[0] = ndim;
    dim = (PyArrayObject*) PyArray_SimpleNew(1, odim, PyArray_INT);
    if(dim == NULL) goto fail;
    for(i=0; i<ndim; i++) 
	((int *)dim->data)[i] = idim[ndim-i-1];
    free(idim);

    return Py_BuildValue("N", PyArray_Return(dim));

fail:
    
    if(idim != NULL) free(idim);
    Py_XDECREF(dim);
    return NULL;
};

static PyObject* 
pyndf_end(PyObject *self, PyObject *args)
{
//    if(!PyArg_ParseTuple(args, "i:pyndf_end"))
//	return NULL;
    int status = SAI__OK;
    ndfEnd(&status);
    Py_RETURN_NONE;
};

// open an existing or new NDF file
static PyObject* 
pyndf_open(PyObject *self, PyObject *args)
{
    const char *name;
	const char *mode = "READ";
	const char *stat = "OLD";
    if(!PyArg_ParseTuple(args, "s|ss:pyndf_open", &name, &mode, &stat))
		return NULL;
	// check for allowed values of mode and stat
	if(strcmp(mode,"READ") != 0 && strcmp(mode,"WRITE") != 0 && strcmp(mode,"UPDATE") != 0)
		return NULL;
	if(strcmp(stat,"OLD") != 0 && strcmp(stat,"NEW") != 0 && strcmp(stat,"UNKNOWN") != 0)
		return NULL;
    int indf, place;
    int status = SAI__OK;
    ndfOpen( NULL, name, mode, stat, &indf, &place, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("ii", indf, place);
};

// create a new NDF (simple) structure
static PyObject*
pyndf_new(PyObject *self, PyObject *args)
{
	// use ultracam defaults
	const char *ftype = "_REAL";
	int ndim, indf, place;
	PyObject* lb;
	PyObject* ub;
	if(!PyArg_ParseTuple(args, "iisiOO:pyndf_new", &indf, &place, &ftype, &ndim, &lb, &ub))
		return NULL;
	if(ndim < 0 || ndim > 7)
		return NULL;
	// TODO: check for ftype here
	int status = SAI__OK;
	PyArrayObject* lower = (PyArrayObject*) PyArray_FROM_OTF(lb, NPY_UINT, NPY_IN_ARRAY | NPY_FORCECAST);
	PyArrayObject* upper = (PyArrayObject*) PyArray_FROM_OTF(ub, NPY_UINT, NPY_IN_ARRAY | NPY_FORCECAST);
	if (!lower || !upper)
		return NULL;
	if(PyArray_SIZE(lower) != ndim || PyArray_SIZE(upper) != ndim)
		return NULL;
	ndfNew(ftype,ndim,(int*)PyArray_DATA(lower),(int*)PyArray_DATA(upper),&place,&indf,&status); // placeholder annulled by this routine
	if(status != SAI__OK)
		return NULL;
	Py_DECREF(lower);
	Py_DECREF(upper);
	return Py_BuildValue("i",indf);
}

// this copies a block of memory from a numpy array to a memory address
static PyObject*
pyndf_numpytoptr(PyObject *self, PyObject *args)
{
	PyObject *npy, *ptrobj;
	PyArrayObject *npyarray;
	int el;
	size_t bytes;
	const char *ftype;
	if(!PyArg_ParseTuple(args, "OOis:pyndf_numpytoptr",&npy,&ptrobj,&el,&ftype))
		return NULL;
	void *ptr = PyCObject_AsVoidPtr(ptrobj);
	if (el <= 0 || ptr == NULL)
		return NULL;
	if(strcmp(ftype,"_INTEGER") == 0) {
		npyarray = (PyArrayObject*) PyArray_FROM_OTF(npy, NPY_INT, NPY_IN_ARRAY | NPY_FORCECAST);
		bytes = sizeof(int);
	} else if(strcmp(ftype,"_REAL") == 0) {
		npyarray = (PyArrayObject*) PyArray_FROM_OTF(npy, NPY_FLOAT, NPY_IN_ARRAY | NPY_FORCECAST);
		bytes = sizeof(float);
	} else if(strcmp(ftype,"_DOUBLE") == 0) {
		npyarray = (PyArrayObject*) PyArray_FROM_OTF(npy, NPY_DOUBLE, NPY_IN_ARRAY | NPY_FORCECAST);
		bytes = sizeof(double);
	} else if(strcmp(ftype,"_BYTE") == 0) {
		npyarray = (PyArrayObject*) PyArray_FROM_OTF(npy, NPY_BYTE, NPY_IN_ARRAY | NPY_FORCECAST);
		bytes = sizeof(char);
	} else if(strcmp(ftype,"_UBYTE") == 0) {
		npyarray = (PyArrayObject*) PyArray_FROM_OTF(npy, NPY_UBYTE, NPY_IN_ARRAY | NPY_FORCECAST);
		bytes = sizeof(char);
	} else {
		return NULL;
	}
	memcpy(ptr,PyArray_DATA(npyarray),el*bytes);
	Py_DECREF(npyarray);
	Py_RETURN_NONE;
}

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

// create a new NDF extension
static PyObject*
pyndf_xnew(PyObject *self, PyObject *args)
{
	int indf, ndim = 0;
	const char *xname, *type;
	PyObject *dim;
	if(!PyArg_ParseTuple(args, "iss|iO:pyndf_xnew", &indf, &xname, &type, &ndim, &dim))
		return NULL;
	int status = SAI__OK;
	HDSLoc *loc = NULL;
	// perform checks if we're not making an extension header
	if(ndim != 0) {
		// check for HDS types
		if (!checkHDStype(type))
			return NULL;
		// need dims if it's not an ext
		if(ndim < 1 || dim == NULL)
			return NULL;
		PyArrayObject *npydim = (PyArrayObject*) PyArray_FROM_OTF(dim,NPY_INT,NPY_IN_ARRAY|NPY_FORCECAST);
		if (PyArray_SIZE(npydim) != ndim)
			return NULL;
		ndfXnew(indf,xname,type,ndim,(int*)PyArray_DATA(npydim),&loc,&status);
		Py_DECREF(npydim);
	} else {
		// making an ext/struct
		ndfXnew(indf,xname,type,0,0,&loc,&status);
	}
	if(status != SAI__OK) {
		PyErr_SetString(PyExc_IOError,"status is not SAI__OK");
		return NULL;
	}
	PyObject* pobj = PyCObject_FromVoidPtr(loc, PyDelLoc);
	return Py_BuildValue("O",pobj);
}

static PyObject*
pyndf_getbadpixval(PyObject *self, PyObject *args)
{
	const char *type;
	if(!PyArg_ParseTuple(args, "s:pyndf_getpadpixval", &type))
		return NULL;
	if (strcmp(type,"_DOUBLE") == 0)
		return Py_BuildValue("f",VAL__BADD);
	else if (strcmp(type,"_REAL") == 0)
		return Py_BuildValue("f",VAL__BADR);
	else if (strcmp(type,"_INTEGER") == 0)
		return Py_BuildValue("i",VAL__BADI);
	else
		return NULL;
}

// make a new primitive
static PyObject*
pydat_new(PyObject *self, PyObject *args)
{
	PyObject *dimobj,*locobj;
	const char *type, *name;
	int ndim;
	if(!PyArg_ParseTuple(args, "OssiO:pydat_new", &locobj, &name, &type, &ndim, &dimobj))
		return NULL;
	HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(locobj);
	if(!checkHDStype(type))
		return NULL;
	int status = SAI__OK;
	if (ndim > 0) {
		PyArrayObject *npydim = (PyArrayObject*) PyArray_FROM_OTF(dimobj,NPY_INT,NPY_IN_ARRAY|NPY_FORCECAST);
		hdsdim *dims = (hdsdim*)PyArray_DATA(npydim);
		datNew(loc,name,type,ndim,dims,&status);
		Py_DECREF(npydim);
	} else {
		datNew(loc,name,type,0,0,&status);
	}
	if(status != SAI__OK)
		return NULL;
	Py_RETURN_NONE;
}


// write a primitive
static PyObject*
pydat_put(PyObject *self, PyObject *args)
{
	PyObject *value, *locobj, *dimobj;
	PyArrayObject *npyval;
	const char* type;
	int ndim;
	if(!PyArg_ParseTuple(args,"OsiOO:pydat_put",&locobj,&type,&ndim,&dimobj,&value))
		return NULL;
	if(!checkHDStype(type))
		return NULL;
	HDSLoc* loc = (HDSLoc*)PyCObject_AsVoidPtr(locobj);
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
	if(status != SAI__OK)
		return NULL;
	Py_DECREF(npyval);
	Py_RETURN_NONE;
}

static PyObject*
pydat_putc(PyObject *self, PyObject *args)
{
	PyObject *strobj,*locobj;
	int strlen;
	if(!PyArg_ParseTuple(args,"OOi:pydat_putc",&locobj,&strobj,&strlen))
		return NULL;
	HDSLoc *loc = (HDSLoc*)PyCObject_AsVoidPtr(locobj);
	PyArrayObject *npystr = (PyArrayObject*) PyArray_FROM_OTF(strobj,NPY_STRING,NPY_FORCECAST);
	char *strptr = PyArray_DATA(npystr);
	int status = SAI__OK;
	datPutC(loc,0,0,strptr,(size_t)strlen,&status);
	if(status != SAI__OK)
		return NULL;
	Py_DECREF(npystr);
	Py_RETURN_NONE;
}

// map access to array component
static PyObject*
pyndf_map(PyObject *self, PyObject* args)
{
	int indf, el;
	void* ptr;
	const char* comp;
	const char* type;
	const char* mmod;
	if(!PyArg_ParseTuple(args,"isss:pyndf_map",&indf,&comp,&type,&mmod))
		return NULL;
	int status = SAI__OK;
	if(indf < 0)
		return NULL;
	if(strcmp(comp,"DATA") != 0 && strcmp(comp,"QUALITY") != 0 &&
			strcmp(comp,"VARIANCE") != 0 && strcmp(comp,"ERROR") != 0)
		return NULL;
	if(strcmp(mmod,"READ") != 0 && strcmp(mmod,"UPDATE") != 0 &&
			strcmp(mmod,"WRITE") != 0)
		return NULL;
	if(!checkHDStype(type))
		return NULL;
	// can't use QUALITY with anything but a _UBYTE
	if(strcmp(comp,"QUALITY") == 0 && strcmp(type,"_UBYTE") != 0)
		return NULL;
	ndfMap(indf,comp,type,mmod,&ptr,&el,&status);
	if(status != SAI__OK)
		return NULL;
	PyObject* ptrobj = PyCObject_FromVoidPtr(ptr,NULL);
	return Py_BuildValue("Oi",ptrobj,el);
}

// unmap an NDF or mapped array
static PyObject*
pyndf_unmap(PyObject* self, PyObject* args)
{
	int indf;
	const char* comp;
	if(!PyArg_ParseTuple(args,"is:pyndf_unmap",&indf,&comp))
		return NULL;
	int status = SAI__OK;
	if(indf < 0)
		return NULL;
	if(strcmp(comp,"DATA") != 0 && strcmp(comp,"QUALITY") != 0 &&
			strcmp(comp,"VARIANCE") != 0 && strcmp(comp,"AXIS") != 0 &&
			strcmp(comp,"*") != 0)
		return NULL;
	ndfUnmap(indf,comp,&status);
	if(status != SAI__OK)
		return NULL;
	Py_RETURN_NONE;
}

// Reads an NDF into a numpy array
static PyObject* 
pyndf_read(PyObject *self, PyObject *args)
{
    int indf, i;
    const char *comp;
    if(!PyArg_ParseTuple(args, "is:pyndf_read", &indf, &comp))
	return NULL;

    // series of declarations in an attempt to avoid problem with
    // goto fail
    const int MXLEN=32;
    char type[MXLEN+1];
    size_t nbyte;
    int npix, nelem;

    // Return None if component does not exist
    int state, status = SAI__OK;
    ndfState(indf, comp, &state, &status);
    if(status != SAI__OK) return NULL;
    if(!state)
	Py_RETURN_NONE;

    PyArrayObject* arr = NULL;

    // Get dimensions, reverse order to account for C vs Fortran
    const int NDIMX = 10;
    int idim[NDIMX];
    npy_intp rdim[NDIMX];

    int ndim;
    ndfDim(indf, NDIMX, idim, &ndim, &status);
    if(status != SAI__OK) goto fail; 

    // Reverse order to account for C vs Fortran
    for(i=0; i<ndim; i++) rdim[i] = idim[ndim-i-1];

    // Determine the data type
    ndfType(indf, comp, type, MXLEN+1, &status);
    if(status != SAI__OK) goto fail;

    // Create array of correct dimensions and type to save data to
    if(strcmp(type, "_REAL") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, rdim, PyArray_FLOAT);
	nbyte = sizeof(float);
    }else if(strcmp(type, "_DOUBLE") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, rdim, PyArray_DOUBLE);
	nbyte = sizeof(double);
    }else if(strcmp(type, "_INTEGER") == 0){
	arr = (PyArrayObject*) PyArray_SimpleNew(ndim, rdim, PyArray_INT);
	nbyte = sizeof(int);
    }else{
	PyErr_SetString(PyExc_IOError, "ndf_read error: unrecognised data type");
	goto fail;
    }
    if(arr == NULL) goto fail;

    // get number of elements, allocate space, map, store

    ndfSize(indf, &npix, &status);
    if(status != SAI__OK) goto fail;
    void *pntr[1];
    ndfMap(indf, comp, type, "READ", pntr, &nelem, &status);
    if(status != SAI__OK) goto fail;
    if(nelem != npix){
	PyErr_SetString(PyExc_IOError, "ndf_read error: number of elements different from number expected");
	goto fail;
    }
    memcpy(arr->data, pntr[0], npix*nbyte);
    ndfUnmap(indf, comp, &status);
    if(status != SAI__OK) goto fail;

    return Py_BuildValue("N", PyArray_Return(arr));

fail:
    
    Py_XDECREF(arr);
    return NULL;
};


static PyObject* 
pyndf_state(PyObject *self, PyObject *args)
{
    const char *comp;
    int indf;
    if(!PyArg_ParseTuple(args, "is:pyndf_state", &indf, &comp))
	return NULL;
    int state, status = SAI__OK;
    ndfState(indf, comp, &state, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("i", state);
};

static PyObject* 
pyndf_xloc(PyObject *self, PyObject *args)
{
    const char *xname, *mode;
    int indf;
    if(!PyArg_ParseTuple(args, "iss:pyndf_xloc", &indf, &xname, &mode))
	return NULL;
    HDSLoc* loc = NULL;
    int status = SAI__OK;
    ndfXloc(indf, xname, mode, &loc, &status);
    if(status != SAI__OK) return NULL;

    // PyCObject to pass pointer along to other wrappers
    PyObject *pobj = PyCObject_FromVoidPtr(loc, PyDelLoc);
    return Py_BuildValue("O", pobj);
};

static PyObject* 
pyndf_xname(PyObject *self, PyObject *args)
{
    int indf, nex, nlen = 32;
    if(!PyArg_ParseTuple(args, "ii|i:pyndf_xname", &indf, &nex, &nlen))
	return NULL;

    char xname[nlen+1];
    int status = SAI__OK;
    ndfXname(indf, nex+1, xname, nlen+1, &status);
    if(status != SAI__OK) return NULL;
    return Py_BuildValue("s", xname);
};

static PyObject* 
pyndf_xnumb(PyObject *self, PyObject *args)
{
    int indf;
    if(!PyArg_ParseTuple(args, "i:pyndf_xnumb", &indf))
	return NULL;
    int status = SAI__OK, nextn;
    ndfXnumb(indf, &nextn, &status);
    if(status != SAI__OK) return NULL;

    return Py_BuildValue("i", nextn);
};

static PyObject* 
pyndf_xstat(PyObject *self, PyObject *args)
{
    const char *xname;
    int indf;
    if(!PyArg_ParseTuple(args, "isi:pyndf_xstat", &indf, &xname))
	return NULL;
    int state, status = SAI__OK;
    ndfXstat(indf, xname, &state, &status);
    if(status != SAI__OK) return NULL;

    return Py_BuildValue("i", state);
};

// The methods

static PyMethodDef NdfMethods[] = {

    {"dat_annul", pydat_annul, METH_VARARGS, 
     "dat_annul(loc) -- annuls the HDS locator."},

    {"dat_cell", pydat_cell, METH_VARARGS, 
     "loc2 = dat_cell(loc1, sub) -- returns locator of a cell of an array."},

    {"dat_index", pydat_index, METH_VARARGS, 
     "loc2 = dat_index(loc1, index) -- returns locator of index'th component (starts at 0)."},

    {"dat_find", pydat_find, METH_VARARGS, 
     "loc2 = dat_find(loc1, name) -- finds a named component, returns locator."},

    {"dat_get", pydat_get, METH_VARARGS, 
     "value = dat_get(loc) -- get data associated with locator regardless of type."},

    {"dat_name", pydat_name, METH_VARARGS, 
     "name_str = dat_name(loc) -- returns name of components."},

    {"dat_ncomp", pydat_ncomp, METH_VARARGS, 
     "ncomp = dat_ncomp(loc) -- return number of components."},

    {"dat_shape", pydat_shape, METH_VARARGS, 
     "dim = dat_shape(loc) -- returns shape of the component. dim=None for a scalar"},

    {"dat_state", pydat_state, METH_VARARGS, 
     "state = dat_state(loc) -- determine the state of an HDS component."},

    {"dat_struc", pydat_struc, METH_VARARGS, 
     "state = dat_struc(loc) -- is the component a structure."},

    {"dat_type", pydat_type, METH_VARARGS, 
     "typ_str = dat_type(loc) -- returns type of the component"},

    {"dat_valid", pydat_valid, METH_VARARGS, 
     "state = dat_valid(loc) -- is locator valid?"},

    {"ndf_acget", pyndf_acget, METH_VARARGS, 
     "value = ndf_acget(indf, comp, iaxis) -- returns character component comp of axis iaxis (starts at 0), None if comp does not exist."},

    {"ndf_aform", pyndf_aform, METH_VARARGS, 
     "value = ndf_aform(indf, comp, iaxis) -- returns storage form of an axis (iaxis starts at 0)."},

    {"ndf_annul", pyndf_annul, METH_VARARGS, 
     "ndf_annul(indf) -- annuls the NDF identifier."},

    {"ndf_anorm", pyndf_anorm, METH_VARARGS, 
     "state = ndf_anorm(indf, iaxis) -- determine axis normalisation flag (iaxis=-1 ORs all flags)."},

    {"ndf_aread", pyndf_aread, METH_VARARGS, 
     "arr = ndf_aread(indf,comp,iaxis) -- reads component comp of axis iaxis. Returns None if does not exist"},

    {"ndf_astat", pyndf_astat, METH_VARARGS, 
     "state = ndf_astat(indf, comp, iaxis) -- determine the state of an NDF axis component (iaxis starts at 0)."},

    {"ndf_init", pyndf_init, METH_VARARGS, 
     "ndf_init() -- initialises the C ndf system."},

    {"ndf_begin", pyndf_begin, METH_VARARGS, 
     "ndf_begin() -- starts a new NDF context."},

    {"ndf_bound", pyndf_bound, METH_VARARGS, 
     "bound = ndf_bound(indf) -- returns pixel bounds, (2,ndim) array."},

    {"ndf_cget", pyndf_cget, METH_VARARGS, 
     "value = ndf_cget(indf, comp) -- returns character component comp as a string, None if comp does not exist."},

    {"ndf_dim", pyndf_dim, METH_VARARGS, 
     "dim = ndf_dim(indf) -- returns dimensions as 1D array."},

    {"ndf_end", pyndf_end, METH_VARARGS, 
     "ndf_end() -- ends the current NDF context."},

    {"ndf_open", pyndf_open, METH_VARARGS, 
     "(indf,place) = ndf_open(name) -- opens an NDF file."},

    {"ndf_read", pyndf_read, METH_VARARGS, 
     "arr = ndf_read(indf,comp) -- reads component comp of an NDF (e.g. dat or var). Returns None if it does not exist."},

    {"ndf_state", pyndf_state, METH_VARARGS, 
     "state = ndf_state(indf, comp) -- determine the state of an NDF component."},

    {"ndf_xloc", pyndf_xloc, METH_VARARGS, 
     "loc = ndf_xloc(indf, xname, mode) -- return HDS locator."},

    {"ndf_xname", pyndf_xname, METH_VARARGS, 
     "xname = ndf_xname(indf, n) -- return name of extension n (starting from 0)."},

    {"ndf_xnumb", pyndf_xnumb, METH_VARARGS, 
     "nextn = ndf_xnumb(indf) -- return number of extensions."},

    {"ndf_xstat", pyndf_xstat, METH_VARARGS, 
     "state = ndf_xstat(indf, xname) -- determine whether extension xname exists."},

	{"ndf_new", pyndf_new, METH_VARARGS,
		"(place,indf) = ndf_new(ftype,ndim,lbnd,ubnd,place) -- create a new simple ndf structure."},

	{"ndf_xnew", pyndf_xnew, METH_VARARGS,
		"loc = ndf_xnew(indf,xname,type,ndim,dim) -- create a new ndf extension."},

	{"dat_put", pydat_put, METH_VARARGS,
		"status = dat_put(loc,type,ndim,dim,value) -- write a primitive inside an ndf."},

	{"dat_new", pydat_new, METH_VARARGS,
		"dat_new(loc,name,type,ndim,dim) -- create a primitive given a locator."},

	{"ndf_map", pyndf_map, METH_VARARGS,
		"(pointer,elements) = ndf_map(indf,comp,type,mmod) -- map access to array component."},

	{"ndf_unmap", pyndf_unmap, METH_VARARGS,
		"status = ndf_unmap(indf,comp) -- unmap an NDF or mapped NDF array."},

	{"ndf_numpytoptr", pyndf_numpytoptr, METH_VARARGS,
		"ndf_numpytoptr(array,pointer,elements,type) -- write numpy array to mapped pointer elements."},

	{"dat_putc", pydat_putc, METH_VARARGS,
		"ndf_putc(loc,string) -- write a character string to primitive at locator."},

	{"ndf_getbadpixval", pyndf_getbadpixval, METH_VARARGS,
		"ndf_getbadpixval(type) -- return a bad pixel value for given ndf data type."},

    {NULL, NULL, 0, NULL} /* Sentinel */
};

PyMODINIT_FUNC
init_ndf(void)
{

    (void) Py_InitModule("_ndf", NdfMethods);
    import_array();
}
