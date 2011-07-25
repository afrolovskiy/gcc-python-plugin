#   Copyright 2011 David Malcolm <dmalcolm@redhat.com>
#   Copyright 2011 Red Hat, Inc.
#
#   This is free software: you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see
#   <http://www.gnu.org/licenses/>.

GCC=gcc

PLUGIN_SOURCE_FILES= \
  gcc-python.c \
  gcc-python-cfg.c \
  gcc-python-closure.c \
  gcc-python-function.c \
  gcc-python-gimple.c \
  gcc-python-location.c \
  gcc-python-option.c \
  gcc-python-parameter.c \
  gcc-python-pass.c \
  gcc-python-pretty-printer.c \
  gcc-python-tree.c \
  gcc-python-variable.c \
  autogenerated-cfg.c \
  autogenerated-option.c \
  autogenerated-function.c \
  autogenerated-gimple.c \
  autogenerated-location.c \
  autogenerated-parameter.c \
  autogenerated-pass.c \
  autogenerated-pretty-printer.c \
  autogenerated-tree.c \
  autogenerated-variable.c

PLUGIN_OBJECT_FILES= $(patsubst %.c,%.o,$(PLUGIN_SOURCE_FILES))
GCCPLUGINS_DIR:= $(shell $(GCC) --print-file-name=plugin)

# The plugin supports both Python 2 and Python 3
#
# In theory we could have arbitrary combinations of python versions for each
# of:
#   - python version used when running scripts during the build (e.g. to
#     generate code)
#   - python version we compile and link the plugin against
#   - when running the plugin with the cpychecker script, the python version
#     that the code is being compiled against
#
# However, to keep things simple, let's assume for now that all of these are
# the same version: we're building the plugin using the same version of Python
# as we're linking against, and that the cpychecker will be testing that same
# version of Python
#
# By default, build against "python", using "python-config" to query for
# compilation options.  You can override this by passing other values for
# PYTHON and PYTHON_CONFIG when invoking "make" (or by simply hacking up this
# file): e.g.
#    make  PYTHON=python3  PYTHON_CONFIG=python3-config  all

# The python interpreter to use:
PYTHON=python
# The python-config executable to use:
PYTHON_CONFIG=python-config

#PYTHON=python3
#PYTHON_CONFIG=python3-config

#PYTHON=python-debug
#PYTHON_CONFIG=python-debug-config

#PYTHON=python3-debug
#PYTHON_CONFIG=python3-debug-config

PYTHON_CFLAGS=$(shell $(PYTHON_CONFIG) --cflags)
PYTHON_LDFLAGS=$(shell $(PYTHON_CONFIG) --ldflags)

CFLAGS+= -I$(GCCPLUGINS_DIR)/include -fPIC -O2 -Wall -Werror -g $(PYTHON_CFLAGS) $(PYTHON_LDFLAGS)

all: testcpybuilder test-suite testcpychecker

plugin: python.so

python.so: $(PLUGIN_OBJECT_FILES)
	$(GCC) $(CFLAGS) -shared $^ -o $@

clean:
	rm -f *.so *.o
	rm -f autogenerated*
	rm -rf docs/_build

autogenerated-gimple-types.txt: gimple-types.txt.in
	cpp $(CFLAGS) $^ -o $@

autogenerated-tree-types.txt: tree-types.txt.in
	cpp $(CFLAGS) $^ -o $@

autogenerated-cfg.c: cpybuilder.py generate-cfg-c.py
	$(PYTHON) generate-cfg-c.py > $@

autogenerated-function.c: cpybuilder.py generate-function-c.py
	$(PYTHON) generate-function-c.py > $@

autogenerated-gimple.c: cpybuilder.py generate-gimple-c.py autogenerated-gimple-types.txt maketreetypes.py
	$(PYTHON) generate-gimple-c.py > $@

autogenerated-location.c: cpybuilder.py generate-location-c.py
	$(PYTHON) generate-location-c.py > $@

autogenerated-option.c: cpybuilder.py generate-option-c.py
	$(PYTHON) generate-option-c.py > $@

autogenerated-parameter.c: cpybuilder.py generate-parameter-c.py
	$(PYTHON) generate-parameter-c.py > $@

autogenerated-pass.c: cpybuilder.py generate-pass-c.py
	$(PYTHON) generate-pass-c.py > $@

autogenerated-pretty-printer.c: cpybuilder.py generate-pretty-printer-c.py
	$(PYTHON) generate-pretty-printer-c.py > $@

autogenerated-tree.c: cpybuilder.py generate-tree-c.py autogenerated-tree-types.txt maketreetypes.py
	$(PYTHON) generate-tree-c.py > $@

autogenerated-variable.c: cpybuilder.py generate-variable-c.py autogenerated-gimple-types.txt maketreetypes.py
	$(PYTHON) generate-variable-c.py > $@

# Hint for debugging: add -v to the gcc options 
# to get a command line for invoking individual subprocesses
# Doing so seems to require that paths be absolute, rather than relative
# to this directory
TEST_CFLAGS= \
  -fplugin=$(shell pwd)/python.so \
  -fplugin-arg-python-script=test.py

# A catch-all test for quick experimentation with the API:
test: plugin
	gcc -v $(TEST_CFLAGS) $(shell pwd)/test.c

# Selftest for the cpychecker.py code:
testcpychecker: plugin
	$(PYTHON) testcpychecker.py -v

# Selftest for the cpybuilder code:
testcpybuilder:
	$(PYTHON) testcpybuilder.py -v

dump_gimple:
	gcc -fdump-tree-gimple $(shell pwd)/test.c

debug: plugin
	gcc -v $(TEST_CFLAGS) $(shell pwd)/test.c

# A simple demo, to make it easy to demonstrate the cpychecker:
demo: plugin
	gcc -fplugin=$(shell pwd)/python.so -fplugin-arg-python-script=cpychecker.py $(shell python-config --cflags) demo.c

test-suite: plugin
	$(PYTHON) run-test-suite.py

show-ssa: plugin
	./gcc-with-python show-ssa.py test.c

html: docs/tables-of-passes.rst docs/passes.svg
	cd docs && $(MAKE) html

# We commit this generated file to SCM to allow the docs to be built without
# needing to build the plugin:
docs/tables-of-passes.rst: plugin generate-tables-of-passes-rst.py
	./gcc-with-python generate-tables-of-passes-rst.py test.c > $@

# Likewise for this generated file:
docs/passes.svg: plugin generate-passes-svg.py
	./gcc-with-python generate-passes-svg.py test.c
