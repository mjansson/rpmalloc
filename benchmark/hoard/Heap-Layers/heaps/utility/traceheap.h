// -*- C++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2012 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef HL_TRACEHEAP_H
#define HL_TRACEHEAP_H

#include <cstdlib>

#ifdef WIN32
#include <io.h>
#endif

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <cstdio>

// An object that manages direct writing to a file.

namespace HL {

  class FileObject {
  public:

    FileObject()
      : _isOpen (false),
	_file (0)
    {}
 
    FileObject (const FileObject& f)
      : _isOpen (f._isOpen),
	_file (f._file)
    {}

    FileObject& operator=(const FileObject& f) {
      _isOpen = f._isOpen;
      _file   = f._file;
      return *this;
    }

    ~FileObject() {
      close();
    }
 
    void open (char * fname)
    {
#if 1
      _file = fopen(fname, "w+");
#else
#ifdef WIN32
      _file = _open(fname, _O_WRONLY | _O_CREAT, _S_IREAD | _S_IWRITE);
#else
      _file = ::open(fname, O_WRONLY | O_CREAT, S_IREAD | S_IWRITE);
#endif
#endif
    }
 
    bool is_open() {
      return _isOpen;
    }
 
    void close()
    {
      if (_isOpen) {
#if 1
	fclose (_file);
#else
#ifdef WIN32
	_close (_file);
#else
	::close (_file);
#endif
#endif
	_isOpen = false;
      }
    }
 
 
    friend FileObject& operator<<(FileObject& f, int n) {
      char buf[255];
      sprintf (buf, "%d", n);
      f.writeString (buf);
      return f;
    }
 
    friend FileObject& operator<<(FileObject& f, char * s) {
      char buf[255];
      sprintf (buf, "%s", s);
      f.writeString (buf);
      return f;
    }
 
    friend FileObject& operator<<(FileObject& f, void * s) {
      char buf[255];
      sprintf (buf, "%p", s);
      f.writeString (buf);
      return f;
    }

  private:

    void writeString (char * buf) {
#if 1
      fprintf (_file, "%s", buf);
#else
#ifdef WIN32
      _write (_file, buf, strlen(buf));
#else
      write (_file, buf, strlen(buf));
#endif
#endif
    }

    bool _isOpen;

#if 1
    FILE * _file;
#else
    int _file;
#endif

  };

  // The TraceHeap itself.
  // Traces malloc & free calls.
 
  template <class Super, int Number>
  class TraceHeap : public Super {
  public:

    TraceHeap()
    {
      if (!theFile().is_open()) {
	char fname[255];
	sprintf (fname, "trace-%d", Number);
	theFile().open (fname);
	printf ("OPEN %s\n", fname);
      }
      getRefs()++;
    }
 
    ~TraceHeap()
    {
      //                      theFile() << ::std::flush;
      --getRefs();
      if (getRefs() == 0) {
	if (theFile().is_open())
	  theFile().close();
      }
    }
 
    inline void * malloc (size_t sz) {
      void * ptr = Super::malloc (sz);
      theFile() << "M " << Number << "\t" << sz << "\t" << ptr << "\n";
      return ptr;
    }
  
    inline void free (void * ptr) {
      theFile() << "F " << Number << "\t" << ptr << "\n";
      Super::free (ptr);
    }
 
  private:
 
    FileObject& theFile() {
      static FileObject f;
      return f;
    }
 
    int& getRefs() {
      static int refs = 0;
      return refs;
    }
 
  };
 
}
 
#endif

#if 0
/* -*- C++ -*- */
#ifndef _TRACEHEAP_H_
#define _TRACEHEAP_H_
// #include <iostream.h>
//#include <fstream.h>
#include <fstream>
#include <map>

template <class Super, int Number>
class TraceHeap : public Super {
public:
  
  TraceHeap()
  {
    if (!theFile().is_open()) {
      printf ("OPEN\n");
      theFile().open ("trace");
    }
    getRefs()++;
  }
  ~TraceHeap()
  {
    theFile() << ::std::flush;
    --getRefs();
    if (getRefs() == 0) {
      if (theFile().is_open())
	theFile().close();
    }
  }
  inline void * malloc (size_t sz) {
    void * ptr = Super::malloc (sz);
    theFile() << "M\t" << sz << "\t" << ptr << "\n";
    return ptr;
  }
  
  inline void free (void * ptr) {
    theFile() << "F\t" << ptr << "\n";
    Super::free (ptr);
  }
private:
  std::ofstream& theFile() {
    static std::ofstream f;
    return f;
  }
  int& getRefs() {
    static int refs = 0;
    return refs;
  }
};

#endif
#endif
