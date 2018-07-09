/*

Memory mapped file mass storage benchmarks. (C)2018 IC Book Labs.
Designed as IPB/TPB/OPB communication sample,
IPB=Input Parameters Block, OPB=Output Parameters Block, TPB=Transit Parameters Block

run:

sudo ./mapfile [options]

options list:

path=<file path>  , target file path and name, default myfile.bin in the current directory
size=<block size> , default 1GB, default units=bytes (possible K/M/G), examples: 10240, 16K, 20M, 1G
wsync=<0|1>       , don't synchronize write(0), synchronize(1), default=synchronize(1)
wdelay=<value>    , delay from start to write, milliseconds
rdelay=<value>    , delay from write end to read, milliseconds
repeats=<value>   , number of times to repeat test, for measurement

examples (default and custom)

sudo ./mapfile
sudo ./mapfile path=aaa.bin size=100K wsync=0 wdelay=1000 rdelay=3000 repeats=3

*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/hdreg.h>
#include <linux/fs.h>

//--- Title string ---
#ifdef __x86_64__
#define TITLE "Memory-mapped files benchmark for Linux 64.\n(C)2018 IC Book Labs. v0.08"
#else
#define TITLE "Memory-mapped files benchmark for Linux 32.\n(C)2018 IC Book Labs. v0.08"
#endif

//--- Defaults definitions ---
#define FILE_PATH   "myfile.bin"       // default file path and name
#define FILE_SIZE   1024*1024*1024     // default file size, bytes
#define WSYNC_MODE  1                  // default write synchronization mode
#define WRITE_DELAY 100                // default delay from Start to Write in milliseconds, argument of Sleep()
#define READ_DELAY  100                // default delay from Write end to Read in milliseconds, argument of Sleep()
#define MEASURE_REPEATS 5              // default number of measurement repeats

//--- Limits definitions ---
#define FILE_SIZE_MIN  4096            // minimum file size 4096 bytes
#define FILE_SIZE_MAX  1536*1024*1024  // maximum file size 1.5 gigabytes
#define WSYNC_NO       0               // additional write synchronization don't used
#define WSYNC_YES      1               // additional write synchronization used
#define DELAY_MIN      0               // minimum delay value, 0 milliseconds
#define DELAY_MAX      100000          // maximum delay value, 100000 milliseconds = 100 seconds
#define REPEATS_MIN    0               // minimum number of measurement repeats
#define REPEATS_MAX    100             // maximum number of measurement repeats

//--- Memory allocation constants ---
#define BUFFER_SIZE 1024*1024          // buffer size for file create only
#define BUFFER_ALIGNMENT 4096          // alignment factor, 4KB is page size for x86/x64

//--- Timer constant ---
#define TIME_TO_SECONDS 0.000000001    // multiply by this to convert 1 nanosecond units to 1 second

//--- Page walk constant ---
#define PAGE_WALK_STEP 4096            // step for cause swapping, page=4096 bytes but sector=512 bytes

//--- Output tabulation options ---
#define IPB_TABS  18    // number of chars before "=" for tabulation, this used for start conditions (input parameters block)
#define OPB_TABS  8     // number of chars before "=" for tabulation, this used for results statistics (output/transit parm. block)

//--- Numeric data for storing command line options, with defaults assigned ---
static char    fileDefaultPath[] = FILE_PATH;   // constant string for references
static char*   filePath   = fileDefaultPath;    // pointer to file path string
static size_t  fileSize   = FILE_SIZE;          // file size, bytes
static int     wsyncMode  = WSYNC_YES;          // additional write synchronization option
static int     writeDelay = WRITE_DELAY;        // delay from start to write, milliseconds
static int     readDelay  = READ_DELAY;         // delay from write end to read, milliseconds
static int     repeats    = MEASURE_REPEATS;    // number of times to repeat test, for measurement precision

//--- Memory allocation and fill variables ---
static size_t bufAlign = BUFFER_ALIGNMENT;      // page alignment required
static size_t bufSize = 0;                      // size of buffer
static char* diskData = NULL;                   // pointer to buffer
static char setData = 0;                        // data pattern

//--- File creation and open variables, include parameters of "open" funcion  ---
static int fileHandle = 0;                      // file handle, result of open (create) file
static int createFlags = O_RDWR|O_DIRECT|O_DSYNC|O_CREAT;  // file create flags
static int openFlags = O_RDWR|O_DIRECT|O_DSYNC;            // file open flags

//--- File mapping variables, include parameters of "mmap" function ---
static void* mapPointer = NULL;   // pointer to virtual address
static void* mapInput = NULL;     // input settings for mapping
static size_t mapLength = 0;      // length of mapping address range
static int mapProtect = PROT_WRITE|PROT_READ;   // memory protection attributes
static int mapFlags = MAP_SHARED;               // sharing flags
static int mapOffset = 0;                       // offset for file addressing

//--- Numeric data for benchmarks results statistics ---
static double readLog[REPEATS_MAX];    // array of read results, megabytes per second
static double writeLog[REPEATS_MAX];   // array of write results, megabytes per second
static int logCount = 0;               // number of actual log entries
static double resultMedian = 0.0;      // median speed, megabytes per second
static double resultAverage = 0.0;     // average speed, megabytes per second
static double resultMinimum = 0.0;     // minimum detected speed, megabytes per second
static double resultMaximum = 0.0;     // maximum detected speed, megabytes per second

//--- Data for timings and benchmarks ---
struct timespec ts1, ts2;              // start and end moments
long long int sec = 0, ns = 0;         // transit variables for time
double seconds = 0.0;                  // time in seconds
double megabytes = 0.0;                // size in megabytes
double mbps = 0.0;                     // speed in megabytes per second

//--- Common status variable for API return ---
static int status = 0;

//--- Strings ---
static char sPath[]     = "path"     ,  // this for command line options names detect
            sSize[]     = "size"     ,
            sWsync[]    = "wsync"    ,
            sWdelay[]   = "wdelay"   ,
            sRdelay[]   = "rdelay"   ,
            sRepeats[]  = "repeats"  ,
            
            ssPath[]    = "file path"         ,    // this for start conditions visual
            ssSize[]    = "file size"         ,
            ssWsync[]   = "wait write sync"   ,
            ssWdelay[]  = "write delay (ms)"  ,
            ssRdelay[]  = "read delay (ms)"   ,
            ssRepeats[] = "repeat times"      ,
            
            sMedian[]   = "Median"   ,             // this for result statistics median
            sAverage[]  = "Average"  ,
            sMinimum[]  = "Minimum"  ,
            sMaximum[]  = "Maximum"  ;

//--- Control block for command line parse, build IPB = Input Parameters Block ---
typedef enum
    { NOOPT, INTPARM, MEMPARM, SELPARM, STRPARM } OPTION_TYPES;
typedef struct
    {
    char* name;             // pointer to parm. name string for recognition NAME=VALUE
    char** values;          // pointer to array of strings pointers, text opt.
    int n_values;           // number of strings for text option recognition
    void* data;             // pointer to updated option variable
    OPTION_TYPES routine;   // select handling method for this entry
    } OPTION_ENTRY;
    
//--- Entries for command line options, null-terminated list ---
static OPTION_ENTRY ipb_list[] =
    {
        { sPath    ,  NULL ,  0 ,  &filePath   ,  STRPARM },
        { sSize    ,  NULL ,  0 ,  &fileSize   ,  MEMPARM },
        { sWsync   ,  NULL ,  0 ,  &wsyncMode  ,  INTPARM },
        { sWdelay  ,  NULL ,  0 ,  &writeDelay ,  INTPARM },
        { sRdelay  ,  NULL ,  0 ,  &readDelay  ,  INTPARM },
        { sRepeats ,  NULL ,  0 ,  &repeats    ,  INTPARM },
        { NULL     ,  NULL ,  0 ,  NULL        ,  NOOPT   }
    };

//--- Control block for start conditions parameters visual, bulid TPB = Transit Parameters Block ---
typedef enum
    { NOPRN, VDOUBLE, VINTEGER, MEMSIZE, SELECTOR, POINTER, HEX64, MHZ, STRNG } PRINT_TYPES;
typedef struct
    {
    char* name;             // pointer to parameter name for visual NAME=VALUE 
    char** values;          // pointer to array of strings pointers, text opt.
    void* data;             // pointer to visualized option variable
    PRINT_TYPES routine;    // select handling method for this entry
    } PRINT_ENTRY;

//--- Entries for print, null-terminated list ---
static PRINT_ENTRY tpb_list[] = 
    {
        { ssPath    ,  NULL ,  &filePath   ,  STRNG    },
        { ssSize    ,  NULL ,  &fileSize   ,  MEMSIZE  },
        { ssWsync   ,  NULL ,  &wsyncMode  ,  VINTEGER },
        { ssWdelay  ,  NULL ,  &writeDelay ,  VINTEGER },
        { ssRdelay  ,  NULL ,  &readDelay  ,  VINTEGER },
        { ssRepeats ,  NULL ,  &repeats    ,  VINTEGER },
        { NULL      ,  NULL ,  0           ,  NOPRN    }
    }; 

//--- Control block for result parameters visual, build OPB = Output Parameters Block ---
//--- Entries for print, null-terminated list ---
static PRINT_ENTRY opb_list[] = 
    {
        { sMedian     , NULL    , &resultMedian     , VDOUBLE  },
        { sAverage    , NULL    , &resultAverage    , VDOUBLE  },
        { sMinimum    , NULL    , &resultMinimum    , VDOUBLE  },
        { sMaximum    , NULL    , &resultMaximum    , VDOUBLE  },
        { NULL        , NULL    , 0                 , NOPRN    }
    };


//--- Helper method, get and print Linux application resource usage statistics ---
void printResourceStatistics()
{
struct rusage usage;
int usageStatus = 0;
int why = RUSAGE_SELF;
usageStatus = getrusage ( why, &usage );
if ( usageStatus < 0 )
    {
    printf ( "Get resource usage failed ( %s )\n" , strerror(errno) );
    exit(2);
    }
printf ( "User space CPU time used: %ld sec %ld usec\n", 
         (long)(usage.ru_utime.tv_sec), (long)(usage.ru_utime.tv_usec) );
printf ( "System space CPU time used: %ld sec %ld usec\n", 
         (long)(usage.ru_stime.tv_sec), (long)(usage.ru_stime.tv_usec) );
printf ( "Maximum resident set size        = %ld KB\n", usage.ru_maxrss );
printf ( "Integral shared memory size      = %ld KB\n", usage.ru_ixrss );
printf ( "Integral unshared data size      = %ld KB\n", usage.ru_idrss );
printf ( "Integral unshared stack size     = %ld KB\n", usage.ru_isrss );
printf ( "Page reclaims (soft page faults) = %ld\n", usage.ru_minflt );
printf ( "Page faults (hard page faults)   = %ld\n", usage.ru_majflt );
printf ( "Swaps                            = %ld\n", usage.ru_nswap );
printf ( "Block input operations           = %ld\n", usage.ru_inblock );
printf ( "Block output operations          = %ld\n", usage.ru_oublock );
printf ( "IPC messages sent                = %ld\n", usage.ru_msgsnd );
printf ( "IPC messages received            = %ld\n", usage.ru_msgrcv );
printf ( "Signals received                 = %ld\n", usage.ru_nsignals );
printf ( "Voluntary context switches       = %ld\n", usage.ru_nvcsw );
printf ( "Involuntary context switches     = %ld\n", usage.ru_nivcsw );
}

//--- Helper method for print memory size: bytes/KB/MB/GB, to scratch string ---
// INPUT:   scratchPointer = pointer to destination string
//          memsize = memory size for visual, bytes
// OUTPUT:  number of chars write
//---
#define KILO 1024
#define MEGA 1024*1024
#define GIGA 1024*1024*1024
#define PRINT_LIMIT 20
int scratchMemorySize( char* scratchPointer, size_t memsize )
    {
    double xd = memsize;
    int nchars = 0;
    if ( memsize < KILO )
        {
        int xi = memsize;
        nchars = snprintf( scratchPointer, PRINT_LIMIT, "%d bytes", xi );
        }
    else if ( memsize < MEGA )
        {
        xd /= KILO;
        nchars = snprintf( scratchPointer, PRINT_LIMIT, "%.2lfK", xd );
        }
    else if ( memsize < GIGA )
        {
        xd /= MEGA;
        nchars = snprintf( scratchPointer, PRINT_LIMIT, "%.2lfM", xd );
        }
    else
        {
        xd /= GIGA;
        nchars = snprintf( scratchPointer, PRINT_LIMIT, "%.2lfG", xd );
        }
    return nchars;
    }

//--- Helper method for print memory size: bytes/KB/MB/GB, to console ---
// INPUT:   memsize = memory size for visual, bytes
// OUTPUT:  number of chars write
//---
int printMemorySize( size_t memsize )
    {
    double xd = memsize;
    int nchars = 0;
    if ( memsize < KILO )
        {
        int xi = memsize;
        nchars = printf( "%d bytes", xi );
        }
    else if ( memsize < MEGA )
        {
        xd /= KILO;
        nchars = printf( "%.2lfK", xd );
        }
    else if ( memsize < GIGA )
        {
        xd /= MEGA;
        nchars = printf( "%.2lfM", xd );
        }
    else
        {
        xd /= GIGA;
        nchars = printf( "%.2lfG", xd );
        }
    return nchars;
    }

//--- Helper method for print selected string from strings array ---
// INPUT: select = value for select string from strings array
//        names = strings array
//---
void printSelectedString( int select, char* names[] )
    {
    printf( "%s", names[select] );
    }

//--- Helper method for calculate median, average, minimum, maximum ---
// INPUT:   statArray[] = array of results
//          statCount = number of actual results in the array, can be smaller than array size
// OUTPUT:  update variables by input pointers:
//          statMedian, statAverage, statMin, statMax
//---
void calculateStatistics( double statArray[], int statCount,
                          double *statMedian, double *statAverage,
                          double *statMin, double *statMax )
    {
    double statSum = 0.0;
    double statTemp = 0.0;
    int flag = 0;
    int i = 0;
    //--- Minimum, Maximum, Average ---
    *statMin = statArray[0];
    *statMax = statArray[0];
    for ( i=0; i<statCount; i++ )
        {
        if ( *statMin > statArray[i] ) { *statMin = statArray[i]; }
        if ( *statMax < statArray[i] ) { *statMax = statArray[i]; }
        statSum += statArray[i];
        }
    *statAverage = statSum / statCount;
    //--- Median, first ordering ---
    flag = 1;
    while ( flag == 1 )
        {
        flag = 0;
        for ( i=0; i<(statCount-1); i++ )
            {
            if ( statArray[i] > statArray[i+1] )
                {
                statTemp = statArray[i];
                statArray[i] = statArray[i+1];
                statArray[i+1] = statTemp;
                flag = 1;
                }
            }
        }
    if ( ( statCount % 2 ) == 0 )
        {  // median if array length EVEN, average of middle pair
        i = statCount / 2;
        *statMedian = ( statArray[i-1] + statArray[i] ) / 2.0;
        }
    else
        {  // median if array length ODD, middle element
        i = statCount/2;
        *statMedian = statArray[i];
        }
    }

//--- Handler for Receive console input (command line, text file or GUI shell) data to IPB ---
// IPB = Input Parameters Block
// INPUT:   pCount = number of command line parameters 
//          pStrings = array of command line strings NAME=VALUE, program name included first
//          parse_control = control array for parsing formal description
// OUTPUT:  status, 0=parsed OK, otherwise parsing error, messages output to console
//---
int handlerInput( int pCount, char** pStrings, OPTION_ENTRY parse_control[] )
{
//--- Accept command line options ---
int i=0, j=0, k=0, k1=0, k2=0;  // miscellaneous counters and variables
int recognized = 0;             // result of strings comparision, 0=match 
OPTION_TYPES t = NOOPT;         // enumeration of parameters types for accept
char* pAll = NULL;              // pointer to option full string NAME=VALUE
char* pName = NULL;             // pointer to sub-string NAME
char* pValue = NULL;            // pointer to sub-string VALUE
char* pPattern = NULL;          // pointer to compared pattern string
char** pPatterns = NULL;        // pointer to pointer to pattern strings
int* pInt = NULL;               // pointer to integer (32b) for variable store
size_t* pSize = NULL;           // transit pointer to block size, parse control
size_t kSize = 0;               // transit value of block size, parse control
char c = 0;                     // transit storage for char
#define SMIN 3                  // minimum option string length, example a=b
#define SMAX 81                 // maximum option string length
char cmdName[SMAX];             // extracted NAME of option string
char cmdValue[SMAX];            // extracted VALUE of option string

for ( i=1; i<pCount; i++ )      // cycle for command line options
    {
    // initializing for parsing current string
    // because element [0] is application name, starts from [1]
    pAll = pStrings[i];
    for ( j=0; j<SMAX; j++ )  // clear buffers
        {
        cmdName[j]=0;
        cmdValue[j]=0;
        }
    // check option sub-string length
    k = strlen(pAll);                   // k = length of one option sub-string
    if ( k<SMIN )
        {
        printf( "ERROR, OPTION TOO SHORT: %s\n", pAll );
        return 1;
        }
    if ( k>SMAX )
        {
        printf( "ERROR, OPTION TOO LONG: %s\n", pAll );
        return 1;
        }
    // extract option name and option value substrings
    pName = cmdName;
    pValue = cmdValue;
    strcpy( pName, pAll );           // store option sub-string to pName
    strtok( pName, "=" );            // pName = pointer to fragment before "="
    pValue = strtok( NULL, " " );    // pValue = pointer to fragment after "="
    // check option name and option value substrings
    k1 = 0;
    k2 = 0;
    if ( pName  != NULL ) { k1 = strlen( pName );  }
    if ( pValue != NULL ) { k2 = strlen( pValue ); }
    if ( ( k1==0 )||( k2==0 ) )
        {
        printf( "ERROR, OPTION INVALID: %s\n", pAll );
        return 1;
        }

    // detect option by comparision from list, cycle for supported options
    for ( j=0; parse_control[j].name!=NULL; j++ )
        {
        pPattern = parse_control[j].name;
        recognized = strcmp ( pName, pPattern );
        if ( recognized==0 )
            {
            // option-type specific handling, run if name match
            t = parse_control[j].routine;
            switch(t)
                {
                case INTPARM:  // support integer parameters
                    {
                    k1 = strlen( pValue );
                    for ( k=0; k<k1; k++ )
                        {
                        if ( isdigit( pValue[k] ) == 0 )
                            {
                            printf( "ERROR, NOT A NUMBER: %s\n", pValue );
                            return 1;
                            }
                        }
                    k = atoi( pValue );   // convert string to integer
                    pInt = (int *) parse_control[j].data;
                    *pInt = k;
                    break;
                    }
                case MEMPARM:  // support memory block size parameters
                    {
                    k1 = 0;
                    k2 = strlen( pValue );
                    c = pValue[k2-1];
                    if ( isdigit(c) != 0 )
                        {
                        k1 = 1;             // no units kilo, mega, giga
                        }
                    else if ( c == 'K' )    // K means kilobytes
                        {
                        k2--;               // last char not a digit K/M/G
                        k1 = 1024;
                        }
                    else if ( c == 'M' )    // M means megabytes
                        {
                        k2--;
                        k1 = 1024*1024;
                        }
                    else if ( c == 'G' )    // G means gigabytes
                        {
                        k2--;
                        k1 = 1024*1024*1024;
                        }
                    for ( k=0; k<k2; k++ )
                        {
                        if ( isdigit( pValue[k] ) == 0 )
                            {
                            k1 = 0;
                            }
                        }
                    if ( k1==0 )
                        {
                        printf( "ERROR, NOT A BLOCK SIZE: %s\n", pValue );
                        return 1;
                        }
                    k = atoi( pValue );   // convert string to integer
                    kSize = k;
                    kSize *= k1;
                    pSize = (size_t *) parse_control[j].data;
                    *pSize = kSize;
                    break;
                    }
                case SELPARM:    // support parameters selected from text names
                    {
                    k1 = parse_control[j].n_values;
                    k2 = 0;
                    pPatterns = parse_control[j].values;
                    for ( k=0; k<k1; k++ )
                        {
                        pPattern = pPatterns[k];
                        k2 = strcmp ( pValue, pPattern );
                        if ( k2==0 )
                            {
                            pInt = (int *) parse_control[j].data;
                            *pInt = k;
                            break;
                            }
                        }
                    if ( k2 != 0 )
                        {
                        printf( "ERROR, VALUE INVALID: %s\n", pAll );
                        return 1;
                        }
                    break;
                    }
                case STRPARM:    // support parameter as text string
                    {
                    pPatterns = (char **) parse_control[j].data;
                    *pPatterns = pStrings[i] + k1 + 1;          // skip string before "=" and "=" char
                    break;
                    }
                }
            break;
            }
        }
    // check option name recognized or not
    if ( recognized != 0 )
        {
        printf( "ERROR, OPTION NOT RECOGNIZED: %s\n", pName );
        return 1;
        }
    }
return 0;
}

//--- Handler for Transmit TPB to console output (text screen, text file or GUI shell) ---
// TPB = Transit Parameters Block
//--- Handler for Transmit OPB to console output (text screen, text file or GUI shell) ---
// OPB = Output Parameters Block
// INPUT:   print_control = control array for output formal description
//          tabSize = number of tabulations before "="
// same handler for TPB and OPB: handlerOutput().
//---
void handlerOutput( PRINT_ENTRY print_control[] , int tabSize )
{
int i=0, j=0, k=0, k1=0, k2=0;   // miscellaneous counters and variables
PRINT_TYPES m = NOPRN;           // enumeration of parameters types for print
long long unsigned int n = 0;         // value for block size
long long unsigned int* np = NULL;    // pointer to block size value
double d = 0.0;                  // transit variable
double* dp = NULL;               // transit pointer to double
unsigned long long* lp = NULL;   // transit pointer to long long
int*  kp = NULL;                 // pointer to integer
char* cp = NULL;                 // pointer to char
char** ccp = NULL;               // pointer to array of pointers to strings
size_t* sizep = 0;               // pointer to block size variable
size_t size = 0;                 // block size variable
// cycle for print parameters strings
for ( i=0; print_control[i].name!=NULL; i++ )
    {
    k = tabSize - printf( "%s", print_control[i].name );
    for ( j=0; j<k; j++ )
        {
        printf(" ");
        }
    printf("= ");
    m = print_control[i].routine;
    switch(m)
        {
        case VDOUBLE:   // double parameter
            {
            dp = (double *) print_control[i].data;
            d = *dp;
            printf( "%.3f", d );
            break;
            }
        case VINTEGER:  // integer parameter
            {
            kp = (int *) print_control[i].data;
            k = *kp;
            printf( "%d", k );
            break;
            }
        case MEMSIZE:  // memory block size parameter
            {
            sizep = (size_t *) print_control[i].data;
            size = *sizep;
            printMemorySize( size );
            break;
            }
        case SELECTOR:  // pool of text names parameter
            {
            kp = (int *) print_control[i].data;
            k = *kp;
            ccp = print_control[i].values;
            printSelectedString( k, ccp );
            break;
            }
        case POINTER:  // memory pointer parameter
            {
            ccp = (char **) print_control[i].data;
            cp = *ccp;
            printf( "%ph", cp );
            break;
            }
        case HEX64:  // 64-bit hex number parameter
            {
            np = (long long unsigned int *) print_control[i].data;
            n = *np;
            printf( "0x%08llX", n );
            break;
            }
        case MHZ:  // frequency in MHz parameter
            {
            lp = (long long unsigned int *) print_control[i].data;
            d = *lp;  // with convert from unsigned long long to double
            d /= 1000000.0;
            printf( "%.1f MHz", d );
            break;
            }
        case STRNG:  // parameter as text string
            {
            ccp = (char **) print_control[i].data;
            cp = *ccp;
            printf( "%s", cp );
            break;
            }
        }
    printf("\n");
    }
}

//--- Handler for output current string at test progress ---
// INPUT:  char* stepName = name of step
//         int   stepNumber = number of step (pass)
//         double statArray[] = statistic array, stepNumber entries used
//---
void handlerProgress( char stepName[], int stepNumber, double statArray[] )
    {
    double currentMBPS = statArray[stepNumber];
    calculateStatistics( statArray, stepNumber + 1,
                        &resultMedian, &resultAverage,
                        &resultMinimum, &resultMaximum );
    printf( " %-6d%-11s%8.3f%11.3f%11.3f%11.3f%11.3f\n",
            stepNumber+1,
	    stepName,
	    currentMBPS,
	    resultMedian,
	    resultAverage,
	    resultMinimum,
	    resultMaximum
	  );
    }

//---------- Application entry point -------------------------------------------

int main( int argc, char** argv )
{
//--- Start message ---
printf( "\n%s\n\n", TITLE );

//--- Parse command line ---
if ( handlerInput( argc, argv, ipb_list ) != 0 ) return 1;

//--- Title string for test conditions ---
printf( "Start conditions:\n" );

//--- Print transit (config) parameters ---
handlerOutput( tpb_list, IPB_TABS );

//--- Check start parameters validity and compatibility ---
if ( ( fileSize < FILE_SIZE_MIN ) | ( fileSize > FILE_SIZE_MAX ) )
    {
    printf("\nBAD PARAMETER: file size must be from " );
    printMemorySize( FILE_SIZE_MIN );
    printf(" to ");
    printMemorySize( FILE_SIZE_MAX );
    printf( "\n" );
    return 1;
    }
if ( ( wsyncMode != WSYNC_NO ) & ( wsyncMode != WSYNC_YES ) )
    {
    printf("\nBAD PARAMETER: Write synchronization option must be %d or %d \n", WSYNC_NO, WSYNC_YES );
    return 1;
    }
if ( ( writeDelay < DELAY_MIN ) | ( writeDelay > DELAY_MAX ) )
    {
    printf("\nBAD PARAMETER: Write delay must be from %d to %d milliseconds\n", DELAY_MIN, DELAY_MAX );
    return 1;
    }
if ( ( readDelay < DELAY_MIN ) | ( readDelay > DELAY_MAX ) )
    {
    printf("\nBAD PARAMETER: Read delay must be from %d to %d milliseconds\n", DELAY_MIN, DELAY_MAX );
    return 1;
    }
if ( ( repeats < REPEATS_MIN ) | ( repeats > REPEATS_MAX ) )
    {
    printf("\nBAD PARAMETER: Repeats must be from %d to %d times\n", REPEATS_MIN, REPEATS_MAX );
    return 1;
    }

//--- Wait for key (Y/N) with list of start parameters ---
printf("\nStart? (Y/N)" );
int key = 0;
key = getchar();
key = tolower(key);
if ( key != 'y' )
    {
    printf( "Test skipped.\n" );
    return 3;
    }

//--- Blank log arrays ---
int rep = repeats;
for ( rep=0; rep<REPEATS_MAX; rep++ )
	{
	readLog[rep] = 0.0;
	writeLog[rep] = 0.0;
	}

//--- Cycle for measurement repeats ---
printf( "\nStart benchmarking.\n" );
printf( "Pass | Operation | MBPS     | Median   | Average  | Minimum  | Maximum\n" );
printf( "-------------------------------------------------------------------------\n\n" );

//--- Cycle for WRITE --------------------------------------------------

for ( rep=0; rep<repeats; rep++ )
    {
    //--- Start create temporary file, allocate memory ---
    bufSize = BUFFER_SIZE;
    diskData = memalign ( bufAlign, bufSize );
    if ( diskData<=0 )
        {
        printf( "%s ( %s )\n", "Memory allocation failed", strerror(errno) );
        return 3;
        }
    //--- Fill memory ---
    setData = '0';
    memset ( diskData, setData, bufSize );
    //--- Create file ---
    fileHandle = open ( filePath, createFlags );    // open (create) file
    if ( fileHandle <= 0 )
        {
        printf ( "\nFile create error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }
    //--- Write file from buffer ---
    ssize_t addSize = 0;
    ssize_t outSize = 0;
    ssize_t count = bufSize;
    while ( addSize < fileSize )
        {
        if ( ( fileSize - addSize ) < bufSize )
            {
            count = fileSize - addSize;
            }
        outSize = write( fileHandle, diskData, count );
        if ( outSize > 0 )
            {
            addSize += outSize;
            }
        else if ( outSize == 0 )
            {
            printf( "\nUnexpected zero size write error: %s", filePath );
            return 3;
            }
        else
            {
            printf ( "\nFile write error: %s ( %s )\n", filePath, strerror(errno) );
            return 3;
            }
        }
    //--- Close file ---
    status = close( fileHandle );
    if ( status < 0 )
        {
        printf ( "\nFile close error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }
    //--- Release memory ---
    free( diskData );

    //--- WRITE PHASE: Open file ---
    fileHandle = open ( filePath, openFlags );    // open file
    if ( fileHandle <= 0 )
        {
        printf ( "\nFile open error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }
    //--- WRITE PHASE: Map file to virtual address space ---
    mapLength = fileSize;
    mapPointer = mmap( mapInput, mapLength, mapProtect, mapFlags,  // map file 
                    fileHandle, mapOffset );
    if ( mapPointer == MAP_FAILED )
        {
        printf ( "\nFile mapping error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }
    //--- WRITE PHASE: Write delay ---
    status = usleep( writeDelay * 1000 );
    if ( status != 0 )
        {
        printf( "\nDelay error\n" );
        return 3;
        }
    //--- WRITE PHASE: Time measurement start point ---
    status = clock_gettime( CLOCK_REALTIME, &ts1 );
    if( status != 0 )
        {
        printf( "\nDelay error ( %s )\n", strerror(errno) );
        return 3;
        }
    //--- WRITE PHASE: Buffer page walk ---
    char* walkPointer = mapPointer;
    size_t walkStep = PAGE_WALK_STEP;
    size_t walkLength = 0;
    setData = '1';
    while ( walkLength < mapLength )
        {
        *walkPointer = setData;
        walkPointer += walkStep;
        walkLength += walkStep;
        }
    //--- WRITE PHASE: Flush memory to file ---
    if ( wsyncMode == 1 )
	{
        status = fsync( fileHandle );
        if ( status < 0 )
            {
            printf ( "\nFile flush error: %s ( %s )\n", filePath, strerror(errno) );
            return 3;
            }
         }
    //--- WRITE PHASE: Time measurement stop point ---
    status = clock_gettime( CLOCK_REALTIME, &ts2 );
    if( status != 0 )
        {
        printf( "\nGet time error ( %s )\n", strerror(errno) );
        return 3;
        }
    //--- WRITE PHASE: Calculate resut megabytes per second ---
    sec = ts2.tv_sec  - ts1.tv_sec;
    ns  = ts2.tv_nsec - ts1.tv_nsec;
    seconds = ns;
    seconds *= TIME_TO_SECONDS;       // convert from nanoseconds to seconds
    seconds += sec;
    megabytes = fileSize;
    megabytes /= 1048576.0;           // convert from bytes to megabytes
    mbps = megabytes / seconds;
    writeLog[rep] = mbps;
    handlerProgress( "write", rep, writeLog );
    //--- WRITE PHASE: Unmap file ---
    status = munmap( mapPointer, mapLength );
    if ( status < 0 )
        {
        printf ( "\nFile un-mapping error: %s ( %s )\n", filePath, strerror(errno) );
        return 1;
        }
    //--- WRITE PHASE: Close file ---
    status = close( fileHandle );
    if ( status < 0 )
        {
        printf ( "\nFile close error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }

    //--- WRITE PHASE: Delete file ---
    status = remove( filePath );
    if ( status < 0 )
        {
        printf ( "\nFile delete error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }
     }

//--- Cycle for READ ---------------------------------------------------

printf( "\n" );
for ( rep=0; rep<repeats; rep++ )
    {
    //--- Start create temporary file, allocate memory ---
    bufSize = BUFFER_SIZE;
    diskData = memalign ( bufAlign, bufSize );
    if ( diskData<=0 )
        {
        printf( "%s ( %s )\n", "Memory allocation failed", strerror(errno) );
        return 3;
        }
    //--- Fill memory ---
    setData = '0';
    memset ( diskData, setData, bufSize );
    //--- Create file ---
    fileHandle = open ( filePath, createFlags );    // open (create) file
    if ( fileHandle <= 0 )
        {
        printf ( "\nFile create error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }
    //--- Write file from buffer ---
    ssize_t addSize = 0;
    ssize_t outSize = 0;
    ssize_t count = bufSize;
    while ( addSize < fileSize )
        {
        if ( ( fileSize - addSize ) < bufSize )
            {
            count = fileSize - addSize;
            }
        outSize = write( fileHandle, diskData, count );
        if ( outSize > 0 )
            {
            addSize += outSize;
            }
        else if ( outSize == 0 )
            {
            printf( "\nUnexpected zero size write error: %s", filePath );
            return 3;
            }
        else
            {
            printf ( "\nFile write error: %s ( %s )\n", filePath, strerror(errno) );
            return 3;
            }
        }
    //--- Close file ---
    status = close( fileHandle );
    if ( status < 0 )
        {
        printf ( "\nFile close error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }
    //--- Release memory ---
    free( diskData );

    //--- READ PHASE: Open file ---
    fileHandle = open ( filePath, openFlags );    // open file
    if ( fileHandle <= 0 )
        {
        printf ( "\nFile open error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }
    //--- READ PHASE: Map file to virtual address space ---
    mapLength = fileSize;
    mapPointer = mmap( mapInput, mapLength, mapProtect, mapFlags,  // map file 
                    fileHandle, mapOffset );
    if ( mapPointer == MAP_FAILED )
        {
        printf ( "\nFile mapping error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }
    //--- READ PHASE: Read delay ---
    status = usleep( readDelay * 1000 );
    if ( status != 0 )
        {
        printf( "\nDelay error ( %s )\n", strerror(errno) );
        return 3;
        }
    //--- READ PHASE: Time measurement start point ---
    status = clock_gettime( CLOCK_REALTIME, &ts1 );
    if( status != 0 )
        {
        printf( "\nGet time error ( %s )\n", strerror(errno) );
        return 3;
        }
    //--- READ PHASE: Buffer page walk ---
    char* walkPointer = mapPointer;
    size_t walkStep = PAGE_WALK_STEP;
    size_t walkLength = 0;
    setData = 0;
    while ( walkLength < mapLength )
        {
        setData = *walkPointer;
        walkPointer += walkStep;
        walkLength += walkStep;
        }
    //--- READ PHASE: Time measurement stop point ---
    status = clock_gettime( CLOCK_REALTIME, &ts2 );
    if( status != 0 )
        {
        printf( "\nGet time error ( %s )\n", strerror(errno) );
        return 3;
        }
    //--- READ PHASE: Calculate resut megabytes per second ---
    sec = ts2.tv_sec  - ts1.tv_sec;
    ns  = ts2.tv_nsec - ts1.tv_nsec;
    seconds = ns;
    seconds *= TIME_TO_SECONDS;       // convert from nanoseconds to seconds
    seconds += sec;
    megabytes = fileSize;
    megabytes /= 1048576.0;           // convert from bytes to megabytes
    mbps = megabytes / seconds;
    readLog[rep] = mbps;
    handlerProgress( "read", rep, readLog );
    //--- READ PHASE: Unmap file ---
    status = munmap( mapPointer, mapLength );
    if ( status < 0 )
        {
        printf ( "\nFile un-mapping error: %s ( %s )\n", filePath, strerror(errno) );
        return 1;
        }
    //--- READ PHASE: Close file ---
    status = close( fileHandle );
    if ( status < 0 )
        {
        printf ( "\nFile close error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }

    //--- READ PHASE: Delete file ---
    status = remove( filePath );
    if ( status < 0 )
        {
        printf ( "\nFile delete error: %s ( %s )\n", filePath, strerror(errno) );
        return 3;
        }
    }

printf( "\n-------------------------------------------------------------------------\n" );


//--- Print summary info -----------------------------------------------

//--- Print output parameters, read results ---
printf( "\nWrite statistics (MBPS):\n" );
calculateStatistics(  writeLog, repeats,
                     &resultMedian, &resultAverage,
                     &resultMinimum, &resultMaximum );
handlerOutput( opb_list, OPB_TABS );

//--- Print output parameters, read results ---
printf( "\nRead statistics (MBPS):\n" );
calculateStatistics(  readLog, repeats,
                     &resultMedian, &resultAverage,
                     &resultMinimum, &resultMaximum );
handlerOutput( opb_list, OPB_TABS );

//--- Print application statistics by OS info ---
printf ( "\nLinux system resources usage statistics:\n" );
printResourceStatistics();
    
//--- Exit ---
printf( "\nDone.\n" );
return 0;

}


