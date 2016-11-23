
[The Hoard Memory Allocator](http://www.hoard.org)
--------------------------

Copyright (C) 1998-2015 by [Emery Berger](http://www.emeryberger.org)

The Hoard memory allocator is a fast, scalable, and memory-efficient
memory allocator that works on a range of platforms,
including Linux, Solaris, Mac OS X, and Windows.

Hoard is a drop-in replacement for malloc that can dramatically
improve application performance, especially for multithreaded programs
running on multiprocessors and multicore CPUs. No source code changes
necessary: just link it in or set one environment variable (see
[Building Hoard](#building-hoard-unixmac), below).

Press
-----

*   "If you'll be running on multiprocessor machines, ... use Emery
Berger's excellent Hoard multiprocessor memory management code. It's a
drop-in replacement for the C and C++ memory routines and is very fast
on multiprocessor machines."

    * [Debugging Applications for Microsoft .NET and Microsoft Windows, Microsoft Press](http://www.microsoft.com/mspress/books/5822.aspx)

*   "(To improve scalability), consider an open source alternative such as
the Hoard Memory Manager..."

    * [Windows System Programming, Addison-Wesley](http://www.amazon.com/Windows-Programming-Addison-Wesley-Microsoft-Technology/dp/0321657748/)

*   "Hoard dramatically improves program performance through its more
efficient use of memory. Moreover, Hoard has provably bounded memory
blowup and low synchronization costs."

    * [Principles of Parallel Programming, Addison-Wesley](http://www.amazon.com/Principles-Parallel-Programming-Calvin-Lin/dp/0321487907/)

Users
-----

Companies using Hoard in their products and servers include AOL,
British Telecom, Blue Vector, Business Objects (formerly Crystal
Decisions), Cisco, Credit Suisse, Entrust, InfoVista, Kamakura,
Novell, Oktal SE, OpenText, OpenWave Systems (for their Typhoon and
Twister servers), Pervasive Software, Plath GmbH, Quest Software,
Reuters, Royal Bank of Canada, SAP, Sonus Networks, Tata
Communications, and Verite Group.

Open source projects using Hoard include the Asterisk Open Source
Telephony Project, Bayonne GNU telephony server, the Cilk parallel
programming language, the GNU Common C++ system, the OpenFOAM
computational fluid dynamics toolkit, and the SafeSquid web proxy.

Hoard is now a standard compiler option for the Standard Performance
Evaluation Corporation's CPU2006 benchmark suite for the Intel and
Open64 compilers.

Licensing
---------

Hoard is distributed under the GPL (v2.0), and can also be licensed
for commercial use.

Because of the restrictions imposed by the GPL license (you must make
your code open-source), commercial users of Hoard can purchase non-GPL
licenses through the University of Texas at Austin. Please consult the
[current Hoard pricing
information](http://www.cs.umass.edu/~emery/hoard/Hoard%20Pricing%202-05-2009.pdf)
(updated 2/5/2009), which lists a number of options for purchasing
licenses, as well as [software license terms and
conditions](http://www.cs.umass.edu/~emery/hoard/SLA%20Terms%20and%20Conditions%209.22.2006.pdf),
and the [software license
agreement](http://www.cs.umass.edu/~emery/hoard/SLA%20Short%20Form%209-26-2006.pdf):
note that the [main UT-Austin licensing
page](http://www.otc.utexas.edu/IndustryForms.jsp) always contains the
most up-to-date documents.

To obtain a license, please contact Jitendra Jain directly
(jjain@otc.utexas.edu) and copy Emery Berger (emery@cs.umass.edu).

Jitendra Jain's full contact information follows:

Jitendra Jain  
The University of Texas at Austin  
Office of Technology Commercialization  
MCC Building, Suite 1.9A  
3925 West Braker Lane  
Austin, Texas 78759  
(512) 471-9055, (512) 475-6894 (fax)  


Why Hoard?
----------

There are a number of problems with existing memory allocators that
make Hoard a better choice.

### Contention ###


Multithreaded programs often do not scale because the heap is a
bottleneck. When multiple threads simultaneously allocate or
deallocate memory from the allocator, the allocator will serialize
them. Programs making intensive use of the allocator actually slow
down as the number of processors increases. Your program may be
allocation-intensive without you realizing it, for instance, if your
program makes many calls to the C++ Standard Template Library (STL). Hoard eliminates this bottleneck.

### False Sharing ###

System-provided memory allocators can cause insidious problems for multithreaded code. They can
lead to a phenomenon known as "false sharing": threads on different CPUs
can end up with memory in the same cache line, or chunk of
memory. Accessing these falsely-shared cache lines is hundreds of
times slower than accessing unshared cache lines. Hoard is designed to prevent false sharing.

### Blowup ###

Multithreaded programs can also lead the allocator to blowup memory
consumption. This effect can multiply the amount of memory needed to
run your application by the number of CPUs on your machine: four CPUs
could mean that you need four times as much memory. Hoard is guaranteed (provably!) to bound memory consumption.


Building Hoard (Unix/Mac)
-------------------------

**NOTE: Make sure to invoke git as follows:**

	% git clone --recursive https://github.com/emeryberger/Hoard.git

To build Hoard on non-Windows platforms, change into the `src/`
directory and run `make` followed by the appropriate target. If you
type `make help`, it will present a list of available targets. These
include `linux-gcc-x86`, `solaris-sunw-sparc`, `macos`, `windows`, and
more.

	% make linux-gcc-x86_64

You can then use Hoard by linking it with your executable, or
by setting the `LD_PRELOAD` environment variable, as in

	% export LD_PRELOAD=/path/to/libhoard.so

in Solaris:

	% make solaris-sunw-sparc
	% export LD_PRELOAD="/path/to/libhoard_32.so:/usr/lib/libCrun.so.1"

  (32-bit version)

	% export LD_PRELOAD="/path/to/libhoard_64.so:/usr/lib/64/libCrun.so.1"
  (64-bit version)

or, in Mac OS X:

	% make macos
	% export DYLD_INSERT_LIBRARIES=/path/to/libhoard.dylib

Building Hoard (Windows)
------------------------

Change into the `src` directory and build the Windows version:

	C:\hoard\src> nmake

To use Hoard, link your executable with `source\uselibhoard.cpp` and `libhoard.lib`.
You *must* use the `/MD` flag.

Example:

	C:\hoard\src> cl /Ox /MD yourapp.cpp source\uselibhoard.cpp libhoard.lib

To run `yourapp.exe`, you will need to have `libhoard.dll` in your path.


Benchmarks
----------

The directory `benchmarks/` contains a number of benchmarks used to
evaluate and tune Hoard.


Technical Information
---------------------

Hoard has changed quite a bit over the years, but for technical details of the first version of Hoard, read [Hoard: A
Scalable Memory Allocator for Multithreaded Applications](http://dl.acm.org/citation.cfm?id=379232),
by Emery D. Berger, Kathryn S. McKinley, Robert D. Blumofe, and Paul
R. Wilson. The Ninth International Conference on Architectural Support
for Programming Languages and Operating Systems
(ASPLOS-IX). Cambridge, MA, November 2000.

