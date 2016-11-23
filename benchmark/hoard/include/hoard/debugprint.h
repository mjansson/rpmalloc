#if !defined(NDEBUG)
#define DEBUG_PRINT(format)			\
  {						\
    char buf[255];				\
    sprintf (buf, format);			\
    fprintf (stderr, buf);			\
  }

#define DEBUG_PRINT1(format,arg)		\
  {						\
    char buf[255];				\
    sprintf (buf, format, arg);			\
    fprintf (stderr, buf);			\
  }

#define DEBUG_PRINT2(format,arg1,arg2)		\
  {						\
    char buf[255];				\
    sprintf (buf, format, arg1,arg2);		\
    fprintf (stderr, buf);			\
  }

#define DEBUG_PRINT3(format,arg1,arg2,arg3)	\
  {						\
    char buf[255];				\
    sprintf (buf, format, arg1,arg2,arg3);	\
    fprintf (stderr, buf);			\
  }
#else
#define DEBUG_PRINT(f)
#define DEBUG_PRINT1(f,a)
#define DEBUG_PRINT2(f,a,b)
#define DEBUG_PRINT3(f,a,b,c)
#endif
