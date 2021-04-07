#include "pythonactor.h"
#include "pyzmsg.h"
#include <wchar.h>
#ifdef __UTYPE_OSX
#include "CoreFoundation/CoreFoundation.h"
#elif defined(__WINDOWS__)
#include <windows.h>
#endif

// https://stackoverflow.com/questions/2736753/how-to-remove-extension-from-file-name
static char *
s_remove_ext(const char* myStr) {
    char *retStr;
    char *lastExt;
    if (myStr == NULL) return NULL;
    if ((retStr = malloc (strlen (myStr) + 1)) == NULL) return NULL;
    strcpy (retStr, myStr);
    lastExt = strrchr (retStr, '.');
    if (lastExt != NULL)
        *lastExt = '\0';
    return retStr;
}

static char *
s_basename(char const *path)
{
    char *s = strrchr(path, '/');
    if (!s)
        return strdup(path);
    else
        return strdup(s + 1);
}

static bool
s_dir_exists(const wchar_t *pypath)
{
    // bleeh we need to convert to char as czmq only supports char paths
    char path[PATH_MAX];
    wcstombs(path, pypath, PATH_MAX);
    zfile_t *dir = zfile_new(NULL, path);
    assert(dir);
    bool ret = zfile_is_directory(dir);
    zfile_destroy(&dir);
    return ret;
}

static zosc_t *
s_py_zosc(PyObject *pAddress, PyObject *pData)
{
    assert( PyUnicode_Check(pAddress) );
    assert( PyList_Check(pData) );
    PyObject *stringbytes = PyUnicode_AsASCIIString(pAddress);
    zosc_t *ret = zosc_new( PyBytes_AsString( stringbytes) );
    // iterate
    for ( Py_ssize_t i=0;i<PyList_Size(pData);++i )
    {
        PyObject *item = PyList_GetItem(pData, i);
        assert(item);
        // determine type
        if ( PyLong_Check(item) )
        {
            long v = PyLong_AsLong(item);
            zosc_append(ret, "h", v);
        }
        else if (PyUnicode_Check(item))
        {
            PyObject* ascii = PyUnicode_AsASCIIString(item);
            assert(ascii);
            char* s = PyBytes_AsString(ascii);
            zosc_append(ret, "s", s);
            Py_DECREF(ascii);
        }
        else
            zsys_warning("unsupported python type");
    }
    return ret;
}

int python_init()
{
    Py_UnbufferedStdioFlag = 1;
    //  add internal wrapper to available modules
    int rc = PyImport_AppendInittab("sph", PyInit_PyZmsg);
    // TODO set the python home to our bundled python or system installed
    // We need to check whether the PYTHONHOME env var is provided,
    //   * if so let python handle it, we do nothing
    //   * else check if we have a bundled python dir and set pythonhome to it
    //   * otherwise do nothing hoping a system installed python is found?
    //   ref: https://github.com/blender/blender/blob/594f47ecd2d5367ca936cf6fc6ec8168c2b360d0/source/blender/python/generic/py_capi_utils.c#L894
    char *homepath = getenv("PYTHONHOME");
    if ( homepath == NULL ) // PYTHONHOME not set so check for bundled python
    {
        wchar_t pypath[PATH_MAX];
#ifdef __UTYPE_OSX
        char path[PATH_MAX];
        CFURLRef res = CFBundleCopyResourcesDirectoryURL(CFBundleGetMainBundle());
        CFURLGetFileSystemRepresentation(res, TRUE, (UInt8 *)path, PATH_MAX);
        CFRelease(res);
        // append python to our path
        swprintf(pypath, PATH_MAX, L"%hs/python", path);
#elif defined(__WINDOWS__)
        wchar_t path[PATH_MAX];
        GetModuleFileNameW(NULL, path, PATH_MAX);
        // remove program name by finding the last delimiter
        wchar_t *s = wcsrchr(path, L'\\');
        if (s) *s = 0;
        swprintf(pypath, PATH_MAX, L"%ls\\python", path);
#else   // linux, we could check for __UTYPE_LINUX
        size_t pathsize;
        char path[PATH_MAX];
        long result = readlink("/proc/self/exe", path, pathsize);
        assert(result > -1);
        if (result > 0)
        {
            path[result] = 0; /* add NULL */
        }
        // remove program name by finding the last /
        char *s = strrchr(path, '/');
        if (s) *s = 0;
        // append python to our path
        swprintf(pypath, PATH_MAX, L"%hs/python", path);
#endif
        if ( s_dir_exists(pypath) )
        {
            Py_SetPythonHome(pypath);
        }
        else
            zsys_warning("No python dir found in '%s' so hoping for a system installed", path);
    }
    //Py_IgnoreEnvironmentFlag = true;
    //Py_NoUserSiteDirectory = true;
    Py_Initialize();
    assert(rc == 0);
    //  add some paths for importing python files
    rc = PyRun_SimpleString(
                    "import sys\n"
                    "import os\n"
                    //"print(os.getcwd())\n"
                    "sys.path.append(os.getcwd())\n"
                    //"sys.path.append('/home/arnaud/src/czmq/bindings/python')\n"
                    "print(\"Python {0} initialized. Paths: {1}\".format(sys.version, sys.path))\n"
                       );
    //  import the internal wrapper module
    PyObject *imp = PyImport_ImportModule("sph");
    assert(imp);
    //  this is our last Python API call before threads jump in,
    //  thus we need to release the GIL
    PyEval_SaveThread();

    if ( rc != 0 )
    {
        PyErr_Print();
    }

    return rc;
}

const char * pythonactorcapabilities =
        "capabilities\n"
        "    pyfile\n"
        "        type = \"filename\"\n"
        "        value = \"\"\n"
        "        api_call = \"SET FILE\"\n"
        "        api_value = \"s\"\n"           // optional picture format used in zsock_send
        "        options = \"textedit,ext=*.py\"\n"
        "inputs\n"
        "    input\n"
        "        type = \"OSC\"\n"
        "outputs\n"
        "    output\n"
        //TODO: Perhaps add NatNet output type so we can filter the data multiple times...
        "        type = \"OSC\"\n";

// this is needed because we have to conform to returning a void * thus we need to wrap
// the pythonactor_new method, unless some know a better method?
void *
pythonactor_new_helper(void *args)
{
    return (void *)pythonactor_new();
}

pythonactor_t *
pythonactor_new()
{
    pythonactor_t *self = (pythonactor_t *) zmalloc (sizeof (pythonactor_t));
    assert (self);

    self->pyinstance = NULL;
    self->main_filename = NULL;

    return self;
}

void
pythonactor_destroy(pythonactor_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        pythonactor_t *self = *self_p;
        if (self->main_filename )
            zstr_free(&self->main_filename);
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

zmsg_t *
pythonactor_init(pythonactor_t *self, sphactor_event_t *ev)
{
    sphactor_actor_set_capability((sphactor_actor_t*)ev->actor, zconfig_str_load(pythonactorcapabilities));

    // if file loaded execute init!
    if ( ev->msg ) zmsg_destroy(&ev->msg);
    return NULL;
}

zmsg_t *
pythonactor_timer(pythonactor_t *self, sphactor_event_t *ev)
{
    return NULL;
}

zmsg_t *
pythonactor_api(pythonactor_t *self, sphactor_event_t *ev)
{
    char *cmd = zmsg_popstr(ev->msg);
    if ( streq(cmd, "SET FILE") )
    {
        char *filename = zmsg_popstr(ev->msg);
        // dispose the event msg we don't use it further
        zmsg_destroy(&ev->msg);

        if ( strlen(filename) )
        {
            if(self->main_filename)
                zstr_free(&self->main_filename);
            self->main_filename = filename;

            char *filebasename = s_basename(filename);
            char *pyname = s_remove_ext(filebasename);

            //  Acquire the GIL
            PyGILState_STATE gstate;
            gstate = PyGILState_Ensure();

            //  import our python file
            PyObject *pClass, *pModule, *pName;
            pName = PyUnicode_DecodeFSDefault( pyname );
            if ( pName == NULL )
            {
                if ( PyErr_Occurred() )
                    PyErr_Print();
                zsys_error("error loading %s", pyname);
                PyGILState_Release(gstate);
                zstr_free(&pyname);
                zstr_free(&filebasename);
                return NULL;
            }

            //  we loaded the file now import it
            pModule = PyImport_Import( pName );
            if ( pModule == NULL )
            {
                if ( PyErr_Occurred() )
                    PyErr_Print();
                zsys_error("error importing %s", filename);
                PyGILState_Release(gstate);
                zstr_free(&pyname);
                zstr_free(&filebasename);
                return NULL;
            }

            //  get the class with the same name as the filename
            pClass = PyObject_GetAttrString(pModule, pyname );
            if (pClass == NULL )
            {
                if (PyErr_Occurred())
                    PyErr_Print();
                zsys_error("pClass is NULL");
                PyGILState_Release(gstate);
                zstr_free(&pyname);
                zstr_free(&filebasename);
                return NULL;
            }

            // create an instance of the class
            self->pyinstance = PyObject_CallObject((PyObject *) pClass, NULL);
            if (self->pyinstance == NULL )
            {
                if (PyErr_Occurred())
                    PyErr_Print();
                zsys_error("pClassInstance is NULL");
                PyGILState_Release(gstate);
                zstr_free(&pyname);
                zstr_free(&filebasename);
                return NULL;
            }

            //  this is our last Python API call before threads jump in,
            //  thus we need to release the GIL
            // Release the GIL again as we are ready with Python
            zsys_info("Successfully loaded %s", filename);
            PyGILState_Release(gstate);
            zstr_free(&pyname);
            zstr_free(&filebasename);
            return NULL;
        }
        zstr_free(&filename);
    }

    if ( ev->msg ) zmsg_destroy(&ev->msg);
    return NULL;
}

zmsg_t *
pythonactor_socket(pythonactor_t *self, sphactor_event_t *ev)
{
    assert(self);
    assert(self->pyinstance);
    //zsys_info("Hello from py_class_actor wrapper: type=%s", ev->type);
    //  This is not compatible with subinterpreters!
    //  https://docs.python.org/3/c-api/init.html#c.PyGILState_Ensure
    //  Acquire the GIL
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    // pass the msg to our pyinstance
    // build evmsg for member argument
    PyZmsgObject *evmsg = (PyZmsgObject *)PyObject_CallObject((PyObject *)&PyZmsgType, NULL);
    assert(evmsg);
    // dispose the event message as we already copied it to a python object
    if (ev->msg)
    {
        zmsg_destroy(&evmsg->msg);
        evmsg->msg = ev->msg;
    }
    // call member 'handleMsg' with event arguments
    PyObject *pReturn = PyObject_CallMethod(self->pyinstance, "handleSocket", "Osss", evmsg, ev->type, ev->name, ev->uuid);
    Py_XINCREF(pReturn);  // increase refcount to prevent destroy
    if (!pReturn)
    {
        PyErr_Print();
        Py_XDECREF(pReturn);  // decrease refcount to trigger destroy
        zsys_error("pythonactor: error calling handleMsg");
    }
    else if (pReturn != Py_None)
    {
        if (PyBytes_Check(pReturn))
        {
            // handle python bytes
            zmsg_t *ret = NULL;
            Py_ssize_t size = PyBytes_Size(pReturn);
            if ( size > 0 )
            {
                ret = zmsg_new();
#ifdef _MSC_VER
                char *buf = (char *)_malloca( size );
                memcpy(buf, PyBytes_AsString(pReturn), size);
                zmsg_addmem(ret, buf, size);
                _freea(buf);
#else
                char buf[size];// = new char[size];
                memcpy(buf, PyBytes_AsString(pReturn), size);
                zmsg_addmem(ret, buf, size);
#endif
            }
            else
            {
                zsys_warning("PyBytes has zero size");
            }
            Py_XDECREF(pReturn);  // decrease refcount to trigger destroy
            // Release the GIL again as we are ready with Python
            PyGILState_Release(gstate);
            // already destroyed: zmsg_destroy(&ev->msg);
            return ret;
        }
        else if ( PyTuple_Check(pReturn) ) // we expect a tuple in the format ( address, [data])
        {
            zmsg_t *ret = NULL; // our return
            // convert the tuple to an osc message
            Py_ssize_t tuple_len = PyTuple_Size(pReturn);
            assert(tuple_len > 0 );
            // first item must be the address string
            PyObject *pAddress = PyTuple_GetItem(pReturn, 0);
            assert(pAddress);
            if ( ! PyUnicode_Check(pAddress) )
            {
                zsys_error("first item in the tuple is not a string, first item should be the address string");
            }
            else
            {
                PyObject *pData = PyTuple_GetItem(pReturn, 1);
                assert(pData);
                if ( PyList_Check(pData) )
                {
                    zosc_t *retosc = s_py_zosc(pAddress, pData);
                    assert(retosc);
                    ret = zmsg_new();
                    assert(ret);
                    zframe_t *data = zosc_packx(&retosc);
                    assert(data);
                    zmsg_append(ret, &data);
                }
            }
            Py_XDECREF(pReturn);  // decrease refcount to trigger destroy
            // Release the GIL again as we are ready with Python
            PyGILState_Release(gstate);
            // already destroyed: zmsg_destroy(&ev->msg);
            return ret;
        }
        else // we expect a zmsg type
        {
            // check whether we received our own message
            PyZmsgObject *c = (PyZmsgObject *)pReturn;
            assert(c->msg);
            if (c->msg == ev->msg)
            {
                zsys_info("we received our own message");
            }
            if ( zmsg_size(c->msg) > 0 )
            {
                //  It would be nicer if we could just return the zmsg in
                //  the python object. However how do we then prevent destruction
                //  by the garbage controller. For now just duplicate.
                zmsg_t *ret = zmsg_dup( c->msg );
                Py_XDECREF(pReturn);  // decrease refcount to trigger destroy
                // Release the GIL again as we are ready with Python
                PyGILState_Release(gstate);
                zmsg_destroy(&ev->msg);
                return ret;
            }
        }
        Py_XDECREF(pReturn);  // decrease refcount to trigger destroy
    }
    // Release the GIL again as we are ready with Python
    PyGILState_Release(gstate); // TODO: didn't we already do this?

    return NULL;
}

zmsg_t *
pythonactor_custom_socket(pythonactor_t *self, sphactor_event_t *ev)
{
    return NULL;
}

zmsg_t *
pythonactor_stop(pythonactor_t *self, sphactor_event_t *ev)
{
    assert(self);
    if (self->pyinstance)
    {
        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();
        PyObject *pReturn = PyObject_CallMethod(self->pyinstance, "handleStop", "Osss", Py_None, ev->type, ev->name, ev->uuid);
        Py_XINCREF(pReturn);  // increase refcount to prevent destroy
        if (!pReturn)
        {
            PyErr_Print();
            zsys_error("pythonactor: error calling handleStop method");
        }
        Py_XDECREF(pReturn);  // decrease refcount to trigger destroy
    }
    return NULL;
}

zmsg_t *
pythonactor_handler(sphactor_event_t *ev, void *args)
{
    assert(args);
    pythonactor_t *self = (pythonactor_t *)args;
    return pythonactor_handle_msg(self, ev);
}

zmsg_t *
pythonactor_handle_msg(pythonactor_t *self, sphactor_event_t *ev)
{
    assert(self);
    assert(ev);
    // these calls don't require a python instance
    if (streq(ev->type, "API"))
    {
        return pythonactor_api(self, ev);
    }
    else if (streq(ev->type, "DESTROY"))
    {
        pythonactor_destroy(&self);
        return NULL;
    }
    else if (streq(ev->type, "INIT"))
    {
        return pythonactor_init(self, ev);
    }
    else if (self->pyinstance == NULL)
    {
        zsys_warning("No valid python file has been loaded (yet)");
        return NULL;
    }
    // these calls require a pyinstance! (loaded python file)
    else if ( streq(ev->type, "TIME") )
    {
        return pythonactor_timer(self, ev);
    }
    else if ( streq(ev->type, "SOCK") )
    {
        return pythonactor_socket(self, ev);
    }
    else if ( streq(ev->type, "FDSOCK") )
    {
        zframe_t *frame = zmsg_pop(ev->msg);
        assert(frame);
        zmsg_destroy(&ev->msg);

        if (zframe_size(frame) == sizeof( void *) )
        {
            void *p = *( (void **)zframe_data(frame));
            zsock_t *sock = (zsock_t *)zsock_resolve(p);
            if ( sock )
            {
                zmsg_t *msg = zmsg_recv(sock);
                assert(msg);
                ev->msg = msg;
            }
            else
            {
                zsys_error("we received a FDSOCK event but we didn't get a socket pointer");
            }
        }
        zframe_destroy(&frame);

        return pythonactor_custom_socket(self, ev);
    }
    else if ( streq(ev->type, "STOP") )
    {
        return pythonactor_stop(self, ev);
    }
    else
        zsys_error("Unhandled sphactor event: %s", ev->type);

    return NULL;
}

