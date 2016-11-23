[Heap Layers: An Extensible Memory Allocation Infrastructure](http://www.heaplayers.org)
-----------------------------------------------------------

Copyright (C) 2000 - 2012 by [Emery Berger](http://www.cs.umass.edu/~emery)

Please read [COPYING](heaplayers/COPYING) for licensing information.

Heap Layers is distributed under the terms of the GNU General Public License.

**Commercial licenses are also available.**

Please contact Emery Berger (emery@cs.umass.edu) for more details.

## Introduction ##

Heap Layers provides a flexible infrastructure for composing
high-performance memory allocators out of C++ _layers_. Heap Layers
makes it easy to write high-quality custom and general-purpose
memory allocators. Heap Layers has been used successfully to build
a number of high-performance allocators, including [Hoard](http://www.hoard.org) and [DieHard](http://www.diehard-software.org/).

## Using Heap Layers ##

For an introduction to Heap Layers, read the article [Policy-Based
Memory Allocation](http://www.drdobbs.com/184402039), by Andrei
Alexandrescu and Emery Berger. It's a good overview.

Heap Layers contains a number of ready-made heap components that can
be plugged together with minimal effort, and the result is often
faster than hand-built allocators. The PLDI 2001 paper [Composing
High-Performance Memory
Allocators](http://www.cs.umass.edu/~emery/pubs/berger-pldi2001.pdf)
has plenty of examples.

## Performance ##

Heap Layers can substantially outperform its only real competitor,
the Vmalloc package from AT&T. The OOPSLA 2002 paper [Reconsidering
Custom Memory
Allocation](http://www.cs.umass.edu/~emery/pubs/berger-oopsla2002.pdf)
paper has more details.

Not only is Heap Layers much higher level and simpler to use, but
its use of templates also improves performance. Heap Layers both
eliminates the function call overhead imposed by Vmalloc layers and
yields higher quality code by exposing more optimization
opportunities.

