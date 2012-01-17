/*
   Copyright 2012 David Malcolm <dmalcolm@redhat.com>
   Copyright 2012 Red Hat, Inc.

   This is free software: you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

/*
  Low-level wrapper support, with integration with GCC's garbage
  collector (aka "GGC")
*/

#include <Python.h>
#include "gcc-python.h"
#include "gcc-python-wrappers.h"
#include "gcc-python-compat.h"

/* Debugging, for use by selftest routine */
static int debug_gcc_python_wrapper = 0;

/* Maintain a circular linked list of PyGccWrapper instances: */
static struct PyGccWrapper sentinel = {
    PyObject_HEAD_INIT(NULL)
    &sentinel,
    &sentinel,
};

PyGccWrapper *
_PyGccWrapper_New(PyGccWrapperTypeObject *typeobj)
{
    PyGccWrapper *obj;
    assert(typeobj);

    obj = PyObject_New(PyGccWrapper,
                       (PyTypeObject*)typeobj);
    if (!obj) {
        return NULL;
    }

    /* Add to list of live PyGccWrapper objects: */
    gcc_python_wrapper_track(obj);

    return obj;
}

extern void
gcc_python_wrapper_track(struct PyGccWrapper *obj)
{
    assert(obj);
    assert(sentinel.wr_next);
    assert(sentinel.wr_prev);
    /* obj is uninitialized, apart from ob_type and ob_refcnt */

    if (debug_gcc_python_wrapper) {
        printf("  gcc_python_wrapper_track: %s\n",
               Py_TYPE(obj)->tp_name);
    }

    /*
      obj's type must use correct delloc, so that obj removes itself from the
      list.  obj->ob_type->tp_dealloc should be either
      gcc_python_wrapper_dealloc or subtype_dealloc
     */

    /* Add to end of list, immediately before sentinel: */
    assert(sentinel.wr_prev->wr_next == &sentinel);
    sentinel.wr_prev->wr_next = obj;
    obj->wr_prev = sentinel.wr_prev;
    obj->wr_next = &sentinel;
    sentinel.wr_prev = obj;

    assert(obj->wr_prev);
    assert(obj->wr_next);
}

void
gcc_python_wrapper_untrack(struct PyGccWrapper *obj)
{
    if (debug_gcc_python_wrapper) {
        printf("    gcc_python_wrapper_untrack: %s\n", Py_TYPE(obj)->tp_name);
    }

    assert(obj);
    assert(Py_REFCNT(obj) == 0);
    assert(sentinel.wr_next);
    assert(sentinel.wr_prev);
    assert(obj->wr_prev);
    assert(obj->wr_next);

    /* Remove from linked list: */
    obj->wr_prev->wr_next = obj->wr_next;
    obj->wr_next->wr_prev = obj->wr_prev;
    obj->wr_prev = NULL;
    obj->wr_next = NULL;
}

void
gcc_python_wrapper_dealloc(PyObject *obj)
{
    assert(obj);
    assert(Py_REFCNT(obj) == 0);
    if (debug_gcc_python_wrapper) {
        printf("  gcc_python_wrapper_dealloc: %s\n",
               Py_TYPE(obj)->tp_name);
    }
    gcc_python_wrapper_untrack((struct PyGccWrapper*)obj);
    Py_TYPE(obj)->tp_free(obj);
}

static void
my_walker(void *arg ATTRIBUTE_UNUSED)
{
    /*
      Callback for use by GCC's garbage collector when marking

      Walk all the PyGccWrapper objects here and if they reference
      GCC GC objects, mark the underlying GCC objects so that they
      don't get swept
    */
    struct PyGccWrapper *iter;

    if (debug_gcc_python_wrapper) {
        printf("  walking the live PyGccWrapper objects\n");
    }
    for (iter = sentinel.wr_next; iter != &sentinel; iter = iter->wr_next) {
        wrtp_marker wrtp_mark;
        if (debug_gcc_python_wrapper) {
            printf("    marking inner object for: ");
            PyObject_Print((PyObject*)iter, stdout, 0);
            printf("\n");
        }
        wrtp_mark = ((PyGccWrapperTypeObject*)Py_TYPE(iter))->wrtp_mark;
        assert(wrtp_mark);
        wrtp_mark(iter);
    }
    if (debug_gcc_python_wrapper) {
        printf("  finished walking the live PyGccWrapper objects\n");
    }
}

static struct ggc_root_tab myroot = { "", 1, 1, my_walker, NULL };

void
gcc_python_wrapper_init(void)
{
    /* Register our GC root-walking callback: */
    ggc_register_root_tab(&myroot);
}

static void
force_gcc_gc(void)
{
    bool stored = ggc_force_collect;

    ggc_force_collect = true;
    ggc_collect();
    ggc_force_collect = stored;
}

PyObject *
gcc_python__force_garbage_collection(PyObject *self, PyObject *args)
{
    force_gcc_gc();
    Py_RETURN_NONE;
}

#define MY_ASSERT(condition) \
    if (!(condition)) { \
         PyErr_SetString(PyExc_AssertionError, #condition); \
         return NULL; \
    }

PyObject *
gcc_python__gc_selftest(PyObject *self, PyObject *args)
{
    tree tree_intcst;
    PyObject *wrapper_intcst;

    tree tree_str;
    PyObject *wrapper_str;

    printf("gcc._gc_selftest() starting\n");

    /* Enable verbose logging: */
    debug_gcc_python_wrapper = 1;

    /*
      Approach: construct various GCC objects here, unreferenced by anything.
      Wrap them in Python objects, then force the GCC GC, then verify that the
      underlying objects were marked, and weren't collected.

      See http://gcc.gnu.org/onlinedocs/gccint/Type-Information.html for some
      notes on GCC's garbage collector.
    */

    printf("creating test GCC objects\n");

    /* We're called from PLUGIN_FINISH, so integer_types[] should be ready: */
    tree_intcst = build_int_cst(integer_types[itk_int], 42);
    wrapper_intcst = gcc_python_make_wrapper_tree_unique(tree_intcst);
    MY_ASSERT(wrapper_intcst);

    #define MY_TEST_STRING "I am only referenced via a python wrapper"
    tree_str = build_string(strlen(MY_TEST_STRING), MY_TEST_STRING);

    /* We should now have a newly-allocated tree node; it should not
       have been marked yet by the GC: */
    MY_ASSERT(tree_str);
    //MY_ASSERT(!ggc_marked_p(str)); // this fails, why? why is it being marked?

    /* Wrap it with a Python object: */
    wrapper_str = gcc_python_make_wrapper_tree_unique(tree_str);
    MY_ASSERT(wrapper_str);

    /* Force a garbage collection: */
    printf("forcing a garbage collection:\n");
    force_gcc_gc();
    printf("completed the forced garbage collection\n");

    /*
      Verify that the various wrapped objects were marked (via the Python
      wrapper).
      If it was not marked, then we're accessing freed memory,
      and this will likely fail with an Internal Compiler Error
     */
    printf("verifying that the underlying GCC objects were marked\n");
    MY_ASSERT(ggc_marked_p(tree_intcst));
    MY_ASSERT(ggc_marked_p(tree_str));
    printf("all of the underlying GCC objects were indeed marked\n");

    /* Clean up the wrapper objects: */
    printf("invoking DECREF on Python wrapper objects\n");
    Py_DECREF(wrapper_intcst);
    Py_DECREF(wrapper_str);

    /* FIXME: need to do this for all of our types (base classes, at least) */

    printf("gcc._gc_selftest() complete\n");

    /* Turn off verbose logging: */
    debug_gcc_python_wrapper = 0;

    Py_RETURN_NONE;
}

/*
  PEP-7
Local variables:
c-basic-offset: 4
indent-tabs-mode: nil
End:
*/