Getting Started
===============

Note: this README page is also the Doxygen main page, the Github readme page, 
and the Docs main page.
You can view it by running :code:`make docs` in the root directory, then opening 
:code:`docs/doxygen/html/index.html` or :code:`docs/build/html/index.html` in a web browser. 

Compiling Alamo
---------------

This section describes how to compile and install Alamo and its dependencies.

**Installing Eigen**: You need to have the Eigen3 library installed. You can do this in one of two ways:

1.  Install using your package manager. In Ubuntu, you can install using

    .. code-block::

        sudo apt install libeigen3-dev

2. Download `eigen3` from the website http://eigen.tuxfamily.org.
   Store it in a directory (e.g. /home/myusername/eigen3/).

**Cloning**: Clone the repository using the command

.. code-block::

    git clone https://github.com/solidsuccs/alamo.git

**Configuring**: Navigate to the :code:`alamo` directory, and run the configure script:

.. code-block::

    ./configure --build-amrex

This will download and configure the AMReX repository for use by Alamo.
If you are compiling in 2D, add the argument :code:`--dim 2` to the command.
For a full list of options run :code:`./configure --help`.

.. NOTE:: 
    If you used option (2) to obtain Eigen, you need to add 
    :code:`--eigen /path/to/eigen` to your configure command:

**Making**: To build the code:
.. code-block::

    make

This will automatically build the appropriate version of AMReX as well as Alamo.
To build in parallel, add the :code:`-jN` argument where :code:`N` is the number of processors.

.. WARNING::
    There is an issue with GNU Make that can cause I/O errors during parallel builds.
    You may get the following error:

    .. code-block::

        make[1]: write error: stdout

    To continue the build, just issue the :code:`make` command again and it should continue normally.
    You can also add the :code:`--output-sync=target` option which may help eliminate the issue.

**Python Interface** See :ref:`building-python`

Testing
-------

Upon successful compilation, run tests by typing

.. code-block::

    ./bin/test-3d-debug-g++

The output will indicate whether the tests pass or fail.
If you are committing changes, you should always make sure the tests pass in 2 and 3 dimensions before committing.

Common Error Messages
---------------------

The following are some common error messages and problems encountered:

* :code:`MLLinOp: grids not coarsenable between AMR levels`
  This is a conflict in the **multigrid solver** because the grid size is not a power of 2.
  Solve by changing the domain dimensions (`amr.n_cell`) so that they are powers of two.

* :code:`static_cast<long>(i) < this->size() failed`
  One common reason this happens is if Dirichlet/Neumann
  boundaries are specified but no boundary values are provided.

Generating this documentation
-----------------------------

Generating documentation requires the following packages:

* Doxygen (on Ubuntu: :code:`sudo apt install doxygen`)
* Sphinx (on Ubuntu: :code:`sudo apt install python3-sphinx`)
* Breathe (on Ubuntu: :code:`sudo apt install python3-breathe`)
* M2R (on Ubuntu: :code:`python3 -m pip install m2r`)
* RTD theme (on Ubuntu: :code:`python3 -m pip install sphinx_rtd_theme`)
* GraphViz (on Ubuntu: :code:`sudo apt install graphviz`)

To generate the documentation, type

.. code-block::

    make docs

(You do not need to run :code:`./configure` before generating documentation.)
Documentation will be generated in `docs/build/html` and can be viewed using a browser.

Compiling on STAMPEDE2
----------------------

To compile on STAMPEDE2 you must first load the following modules:

.. code-block:: 

    module load python3
    module load mvapich2


