/*
 Linux block devices benchmark.

 Options list (can be extended later):

 device = block device path, default "/dev/sda"
 operation = block operation, values: read, write, copy
 addressing = sequental, pseudo-random, hardware pseudo-random
 data = zero, pseudo-random, hardware pseudo-random
 threads = select number of execution threads: numeric value
 start = starts from this value: numeric value + K/M/G
 stop = stops at this value: numeric value + K/M/G
 block = block size, numeric value + K/M/G
 sector = device sector size
 direct = disable skip OS read buferring
 wsync = disable OS writeback caching
 precision = time or precision priority, values: fast, slow
 machinereadable = make output machine readable (hex data), values: 0 or 1

 BUGS AND NOTES.
 - all delta time visual, for all 4 timers
 - multi thread
 - carefully variables declaration
 - see program options
 + exit codes: 0=ok, 1=command line parse, 2=platform detect
 - parameter-specific verification
 - irregularity after some samples mixed
 - macroblocks required for visual, blocks is too small, too much strings
 - visualization ergonomics

 See BUGS AND ROADMAP at BlockBench3.
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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/hdreg.h>
#include <linux/fs.h>

//--- Title string ---
#define TITLE "Linux block devices benchmark v0.45. Variant 1. (C)2018 IC Book Labs."

//--- Defaults definitions ---
#define SMIN 3              // minimum option string length, example a=b
#define SMAX 81             // maximum option string length
#define PATH pathString     // default block device name
#define OPERATION 0         // operation default is read
#define ADDRESSING 0        // addressing default is sequental
#define DATA 0              // data pattern default is zero-fill
#define THREADS 1           // number of threads default is 1
#define START 0             // start address default is 0
#define STOP 1048576*11     // end address default, must check device
#define BLOCK 1048576       // bytes per request default, must check device
#define SECTOR 512          // bytes per sector default
#define DIRECT 1            // cache skip mode default ON
#define WSYNC 1             // sync write mode default ON (no writeback)
#define PRECISION 0         // default fast test, not a precision test
#define MACHINEREADABLE 0   // machine readable output disabled by default
#define BUFALIGN 4096       // alignment factor, 4KB is page size for x86/x64

#define OPERATION_PER_LINE 1048576*100  // size per line output

//--- Text data for interpreting command line options ---
#define n_op 3
static char* operations[] = 
    { "read", "write", "copy" };
#define n_adr 3
static char* addrmodes[] = 
    { "sequental", "pseudo-random", "pseudo-random hw" };
#define n_dat 3
static char* datamodes[] =
    { "zero-fill", "pseudo-random", "pseudo-random hw" };
#define n_pr 2
static char* precisions[] = 
    { "fast", "slow" };
char pathString[] = "/dev/sda";

//--- Numeric data for storing command line options, with defaults assigned ---
static char pathBuffer[SMAX];
static char* path = pathBuffer;
static int operation = OPERATION;
static int addressing = ADDRESSING;
static int data = DATA;
static int threads = THREADS;
static size_t start = START;
static size_t stop = STOP;
static size_t block = BLOCK;
static size_t sector = SECTOR;
static int direct = DIRECT;
static int wsync = WSYNC;
static int precision = PRECISION;
static int machinereadable = MACHINEREADABLE;

//--- Numeric data for storing scan configuration results ---
static size_t bufalign = BUFALIGN;
static size_t bufsize = 0;
static char* diskData = NULL;

//--- Message strings for disk drive parameters names ---
static const char msgDrive[]       = "Drive model       : ";
static const char msgSerial[]      = "Serial number     : ";
static const char msgFirmware[]    = "Firmware Revision : ";
static const char msgCylinders[]   = "Cylinders=";
static const char msgHeads[]       = "Heads=";
static const char msgSectors[]     = "Sectors=";
static const char msgStartSector[] = "Start sector=";

//--- Message strings for IOCTL requests names ---
static const char msgIdentify[]    = "HDIO_GET_IDENTIFY:";
static const char msgGetGeo[]      = "HDIO_GETGEO:";
static const char msgGetSize[]     = "BLKGETSIZE:";
static const char msgBlkSectGet[]  = "BLKSETGET:";

//--- List of Linux timers IDs and its names ---
#define TCNT 4
static int clk_ids[] =
    { 
    CLOCK_REALTIME ,
    CLOCK_MONOTONIC ,
    CLOCK_PROCESS_CPUTIME_ID ,  // this timer not counts when sleep
    CLOCK_THREAD_CPUTIME_ID     // this also
    };
static char nameT0[] = "CLOCK_REALTIME          ";
static char nameT1[] = "CLOCK_MONOTONIC         ";
static char nameT2[] = "CLOCK_PROCESS_CPUTIME_ID";
static char nameT3[] = "CLOCK_THREAD_CPUTIME_ID ";
static char* namesT[] = { nameT0, nameT1, nameT2, nameT3 };

//--- Variables for OS timers support ---
static struct timespec ts[TCNT];              // reports of timers parameters
static struct timespec ts1[TCNT], ts2[TCNT];  // start and end moments

//--- Variables for IOCTL requests to block devices ---
int fd = 0;                     // file descriptor, open device as file
typedef union                   // data region for IDENTIFY_DEVICE
    {
    struct hd_driveid hdstr;    // alias 1 = formatted structure
    char hdraw[512];            // alias 2 = raw buffer (RSRV. FOR DUMP)
    } HD_UNION;
HD_UNION hd;                    // data region for IDENTIFY_DEVICE
struct hd_geometry hdg;         // data region for GET GEOMETRY request

//--- Control block for command line parse ---
typedef enum
    { INTPARM, MEMPARM, SELPARM, STRPARM } OPTION_TYPES;
typedef struct
    {
    char* name;             // pointer to parm. name for recognition NAME=VALUE
    char** values;          // pointer to array of strings pointers, text opt.
    int n_values;           // number of strings for text option recognition
    void* data;             // pointer to updated option variable
    OPTION_TYPES routine;   // select handling method for this entry
    } OPTION_ENTRY;
    
#define OPTION_COUNT 13     // number of entries for command line options
static OPTION_ENTRY option_list[] =
    {
        { "path"            , NULL       , 0     , &path            , STRPARM },
        { "operation"       , operations , n_op  , &operation       , SELPARM },
        { "addressing"      , addrmodes  , n_adr , &addressing      , SELPARM },
        { "data"            , datamodes  , n_dat , &data            , SELPARM },
        { "threads"         , NULL       , 0     , &threads         , INTPARM },
        { "start"           , NULL       , 0     , &start           , MEMPARM },
        { "stop"            , NULL       , 0     , &stop            , MEMPARM },
        { "block"           , NULL       , 0     , &block           , MEMPARM },
        { "sector"          , NULL       , 0     , &sector          , MEMPARM },
        { "direct"          , NULL       , 0     , &direct          , INTPARM },
        { "sync"            , NULL       , 0     , &wsync           , INTPARM },
        { "precision"       , precisions , n_pr  , &precision       , SELPARM },
        { "machinereadable" , NULL       , 0     , &machinereadable , INTPARM }
    };

//--- Control block for start conditions parameters visual ---
typedef enum
    { INTEGER, MEMSIZE, SELECTOR, POINTER, HEX64, MHZ, STRNG } PRINT_TYPES;
typedef struct
    {
    char* name;             // pointer to parameter name for visual NAME=VALUE 
    char** values;          // pointer to array of strings pointers, text opt.
    void* data;             // pointer to visualized option variable
    PRINT_TYPES routine;    // select handling method for this entry
    } PRINT_ENTRY;

#define PRINT_COUNT 16    // number of entries for print
#define PRINT_NAME  20    // number of chars before "=" for tabulation
static PRINT_ENTRY print_list[] = 
    {
        { "Block device path"   , NULL       , &path            , STRNG    },
        { "Disk operation"      , operations , &operation       , SELECTOR },
        { "Address mode"        , addrmodes  , &addressing      , SELECTOR },
        { "Data mode"           , datamodes  , &data            , SELECTOR },
        { "Threads count"       , NULL       , &threads         , INTEGER  },
        { "Start position"      , NULL       , &start           , MEMSIZE  },
        { "End position"        , NULL       , &stop            , MEMSIZE  },
        { "Bytes per request"   , NULL       , &block           , MEMSIZE  },
        { "Bytes per sector"    , NULL       , &sector          , MEMSIZE  },
        { "Direct mode"         , NULL       , &direct          , INTEGER  },
        { "Synchronous mode"    , NULL       , &wsync           , INTEGER  },
        { "Precision option"    , precisions , &precision       , SELECTOR },
        { "Machine readable"    , NULL       , &machinereadable , INTEGER  },
        { "Buffer pointer"      , NULL       , &diskData        , POINTER  },
        { "Buffer size"         , NULL       , &bufsize         , MEMSIZE  },
        { "Buffer alignment"    , NULL       , &bufalign        , MEMSIZE  },
    }; 

//--- Helper method for print memory size: bytes/KB/MB/GB, overloaded ---
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

//--- Helper method for print memory size: bytes/KB/MB/GB, overloaded ---
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
void printSelectedString( int select, char* names[] )
    {
    printf( "%s", names[select] );
    }

//--- Detect, store and print OS timers configuration ---
void detectAndPrintTimers()
{
int i = 0;
int clockStatus = 0;
unsigned long long int seconds = 0, nanoseconds = 0;
for ( i=0; i<TCNT; i++ )
    {
    clockStatus = clock_getres(clk_ids[i], &ts[i] );
    if( clockStatus==0 )
        {
        seconds      = ts[i].tv_sec;
        nanoseconds  = ts[i].tv_nsec;
        double xs = seconds;
        double xn = nanoseconds;
        printf( "%s  %.lf s %.lf ns\n", namesT[i], xs, xn );
        }
    else
        {
        ts[i].tv_sec = -1;
        ts[i].tv_nsec = -1;
        printf( "%s  N/A ( %s )\n", namesT[i], strerror(errno) );
        }
    }
}

//--- Called at start of measured interval ---
void startTimeDelta()
{
int i = 0;
int timerStatus = 0;
for ( i=0; i<TCNT; i++ )
    {
    if ( ts[i].tv_sec >= 0 )  // validation from get units
        {  // get current time, return status, it checked
        timerStatus = clock_gettime(clk_ids[i], &ts1[i] );
        }
    else
        {  // set error condition if previous get time units failed
        timerStatus = -1;
        }
    if( timerStatus!=0 )  // validation from get time result
        {
        ts1[i].tv_sec = -1;   // force invalid result
        ts1[i].tv_nsec = -1;
        }
    }
}

//--- Called at end of measured interval ---
void stopTimeDelta()
{
int i = 0;
int timerStatus = 0;
for ( i=0; i<TCNT; i++ )
    {
    if ( ts1[i].tv_sec >= 0 )  // validation from get start time
        {  // get current time, return status, it checked
        timerStatus = clock_gettime(clk_ids[i], &ts2[i] );
        }
    else
        {  // set error condition if previous get start time failed
        timerStatus = -1;
        }
    if( timerStatus!=0 )      // validation from get time result
        {
        ts2[i].tv_sec = -1;   // force invalid result
        ts2[i].tv_nsec = -1;
        }
    }
}

//--- Get and print Linux application statistics ---
void printStatistics()
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

//--- Names of tests ---
static char* testsNames[] = 
    { "Read blocks", "Write blocks", "Copy blocks" };

//--- Values of bytes per instruction for convert instructions to megabytes ---
static int bytesPerInstruction[] = 
    { 16, 16, 16 };

//---------- Application entry point -------------------------------------------

int main( int argc, char** argv )
{
//--- Start message ---
printf ( "\n%s\n\n", TITLE );

//--- Initializing pseudo constant ---
strcpy ( pathBuffer, PATH );

//--- Accept command line options ---
int i=0, j=0, k=0, k1=0, k2=0;  // miscellaneous counters and variables
int recognized = 0;             // result of strings comparision, 0=match 
OPTION_TYPES t = 0;             // enumeration of parameters types for accept
char* pAll = NULL;              // pointer to option full string NAME=VALUE
char* pName = NULL;             // pointer to sub-string NAME
char* pValue = NULL;            // pointer to sub-string VALUE
char* pPattern = NULL;          // pointer to compared pattern string
char** pPatterns = NULL;        // pointer to array of pointers to pat. strings
int* pInt = NULL;               // pointer to integer (32b) for variable store
long long* pLong = NULL;        // pointer to long (64b) for variable store
long long k64;                  // transit variable for memory block size
char c = 0;                     // transit storage for char
char cmdName[SMAX];             // extracted NAME of option string
char cmdValue[SMAX];            // extracted VALUE of option string

for ( i=1; i<argc; i++ )        // cycle for command line options
    {
    // initializing for parsing current string
    // because element [0] is application name, starts from [1]
    pAll = argv[i];
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
        exit(1);
        }
    if ( k>SMAX )
        {
        printf( "ERROR, OPTION TOO LONG: %s\n", pAll );
        exit(1);
        }
    // extract option name and option value substrings
    pName = cmdName;
    pValue = cmdValue;
    strcpy( pName, pAll );           // store option sub-string to pName
    strtok( pName, "=" );            // pName = pointer to fragment before "="
    pValue = strtok( NULL, "?" );    // pValue = pointer to fragment after "="
    // check option name and option value substrings
    k1 = 0;
    k2 = 0;
    if ( pName  != NULL ) { k1 = strlen( pName );  }
    if ( pValue != NULL ) { k2 = strlen( pValue ); }
    if ( ( k1==0 )||( k2==0 ) )
        {
        printf( "ERROR, OPTION INVALID: %s\n", pAll );
        exit(1);
        }
    // detect option by comparision from list, cycle for supported options
    for ( j=0; j<OPTION_COUNT; j++ )
        {
        pPattern = option_list[j].name;
        recognized = strcmp ( pName, pPattern );
        if ( recognized==0 )
            {
            // option-type specific handling, run if name match
            t = option_list[j].routine;
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
                            exit(1);
                            }
                        }
                    k = atoi( pValue );   // convert string to integer
                    pInt = option_list[j].data;
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
                        exit(1);
                        }
                    k = atoi( pValue );   // convert string to integer
                    k64 = k;
                    k64 *= k1;
                    pLong = option_list[j].data;
                    *pLong = k64;
                    break;
                    }
                case SELPARM:    // support parameters selected from text names
                    {
                    k1 = option_list[j].n_values;
                    k2 = 0;
                    pPatterns = option_list[j].values;
                    for ( k=0; k<k1; k++ )
                        {
                        pPattern = pPatterns[k];
                        k2 = strcmp ( pValue, pPattern );
                        if ( k2==0 )
                            {
                            pInt = option_list[j].data;
                            *pInt = k;
                            break;
                            }
                        }
                    if ( k2 != 0 )
                        {
                        printf( "ERROR, VALUE INVALID: %s\n", pAll );
                        exit(1);
                        }
                    break;
                    }
                case STRPARM:    // support parameter as text string
                    {
                    // pPatterns = path = pointer to pathBuffer
                    // pValue = pointer to source temp parsing buffer
                    pPatterns = option_list[j].data;
                    // *pPatterns = pValue;
                    strcpy ( *pPatterns, pValue );
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
        exit(1);
        }
    }
    
//--- Detect OS timers, print results ---
printf( "OS timers list with resolutions:\n" );
detectAndPrintTimers();

//--- Detect OS block device, print results ---
printf( "\nDetect block device...\n" );
if ((fd = open( path, O_RDONLY|O_DIRECT|O_SYNC|O_DSYNC )) < 0)  // changed
    {
    printf( "\n%s: %s ( %s )\n", 
            "ERROR OPEN DEVICE", path, strerror(errno) );
    exit(1);
    }

//--- IOCTL request: HDIO_GET_IDENTIFY ---
int xc = 0, xh = 0, xs = 0;
long xt = 0;
if (!ioctl(fd, HDIO_GET_IDENTITY, &hd))   // make IOCTL request 
    {                                     // output parameters if no errors
    printf( "%s\n" , msgIdentify );
    printf( "%s%.40s\n" , msgDrive    , hd.hdstr.model );
    printf( "%s%.20s\n" , msgSerial   , hd.hdstr.serial_no );
    printf( "%s%.8s\n"  , msgFirmware , hd.hdstr.fw_rev );
    xc = hd.hdstr.cyls & 0xFFFF;       // cylinders
    xh = hd.hdstr.heads & 0xFFFF;      // heads
    xs = hd.hdstr.sectors & 0xFFFF;    // sectors
    printf( "%s%d , %s%d , %s%d\n",
            msgCylinders, xc, msgHeads, xh, msgSectors, xs );
    }
else if (errno == -ENOMSG)     // special error handling 
    {                          // error = no message of desired type
    printf( "%s ( %s )\n", "IDENTIFICATION NOT AVAILABLE", strerror(errno) );
    exit(1);
    }
else                           // other errors handling
    {
    printf( "%s ( %s )\n", "IDENTIFICATION FAILED", strerror(errno) );
    exit(1);
    }

//--- IOCTL request: HDIO_GETGEO ---
if (!ioctl(fd, HDIO_GETGEO, &hdg))    // make IOCTL request
    {
    printf( "%s\n" , msgGetGeo );      // output parameters if no errors
    xc = hdg.cylinders & 0xFFFF;        
    xh = hdg.heads & 0xFF;        
    xs = hdg.sectors & 0xFF;        
    xt = hdg.start;
    printf( "%s%d , %s%d , %s%d , %s%ld\n",
            msgCylinders, xc, msgHeads, xh, msgSectors, xs,
            msgStartSector, xt );
    } 
else if (errno == -ENOMSG)            // special error handling
    {                                 // error = no message of desired type
    printf( "%s ( %s )\n", "GEOMETRY NOT AVAILABLE", strerror(errno) );
    } 
else                                  // other errors handling
    {
    printf( "%s ( %s )\n", "GET GEOMETRY FAILED", strerror(errno) );
    }

//--- IOCTL request BLKGETSIZE ---
int sizeSect = 0;
double sizeMB = 0.0;
if ( !ioctl(fd, BLKGETSIZE, &sizeSect) )
    {
    sizeMB = sizeSect;
    sizeMB = sizeMB / 1048576.0 * sector;
    printf( "%s\n%d sectors , means %.1lf MB\n",
            msgGetSize, sizeSect, sizeMB );
    }
else
    {
    printf( "%s ( %s )\n", "GET DRIVE SIZE FAILED", strerror(errno) );
    exit(1);
    }

//--- IOCTL request BLKSECTGET, number of sectors per request ---
sizeSect = 0;
if ( !ioctl(fd, BLKSECTGET, &sizeSect) )
    {
    bufsize = sizeSect * sector;
    block = bufsize;
    sizeMB = bufsize;
    sizeMB = sizeMB / 1048576.0;
    printf( "%s\nmaximum %d sectors per request , means %.1lf MB\n",
            msgBlkSectGet, sizeSect, sizeMB );
    }
else
    {
    printf( "%s ( %s )\n", "GET I/O REQUEST SIZE FAILED", strerror(errno) );
    exit(1);
    }

//--- Allocate memory ---
printf ( "\nAllocate memory...\n" );
diskData = memalign ( bufalign, bufsize );
if ( diskData<=0 )
    {
    printf( "%s ( %s )\n", "Memory allocation failed", strerror(errno) );
    exit(1);
    }
    
//--- Clear memory, page faults better outside measured interval ---
// This code must be near to benchmarking because cache effects
for ( i=0; i<bufsize; i++ )
    {
    diskData[i] = 0;
    }

//--- Print start conditions of test ---
printf ( "\nStart conditions:\n" );
// correct this: declarations irregular
PRINT_TYPES m = 0;            // enumeration of parameters types for print
long long unsigned int n = 0;         // value for block size
long long unsigned int* np = NULL;    // pointer to block size value
double d = 0.0;                  // transit variable
unsigned long long* dp = NULL;   // transit pointer to double
int*  kp = NULL;                 // pointer to integer
char* cp = NULL;                 // pointer to char
char** ccp = NULL;               // pointer to array of pointers to strings
size_t* sizep = 0;               // pointer to block size variable
size_t size = 0;                 // block size variable
// correct this: declarations irregular
for ( i=0; i<PRINT_COUNT; i++ )
    {
    k = PRINT_NAME - printf( "%s", print_list[i].name );
    for ( j=0; j<k; j++ )
        {
        printf(" ");
        }
    printf("= ");
    m = print_list[i].routine;
    switch(m)
        {
        case INTEGER:  // integer parameter
            {
            kp = print_list[i].data;
            k = *kp;
            printf( "%d", k );
            break;
            }
        case MEMSIZE:  // memory block size parameter
            {
            sizep = print_list[i].data;
            size = *sizep;
            printMemorySize( size );
            break;
            }
        case SELECTOR:  // pool of text names parameter
            {
            kp = print_list[i].data;
            k = *kp;
            ccp = print_list[i].values;
            printSelectedString( k, ccp );
            break;
            }
        case POINTER:  // memory pointer parameter
            {
            ccp = print_list[i].data;
            cp = *ccp;
            printf( "%p", cp );
            break;
            }
        case HEX64:  // 64-bit hex number parameter
            {
            np = print_list[i].data;
            n = *np;
            printf( "0x%08llX", n );
            break;
            }
        case MHZ:  // frequency in MHz parameter
            {
            dp = print_list[i].data;
            d = *dp;  // with convert from unsigned long long to double
            d /= 1000000.0;
            printf( "%.1f MHz", d );
            break;
            }
        case STRNG:  // parameter as text string
            {
            ccp = print_list[i].data;
            cp = *ccp;
            printf( "%s", cp );
            break;
            }
        }
    printf("\n");
    }

//--- Check start parameters validity and compatibility ---

if ( operation != 0 )
    {
    printf("\nBAD PARAMETER: Read only supported yet.\n");
    exit(1);
    }

if ( addressing != 0 )
    {
    printf("\nBAD PARAMETER: non-sequental access not supported yet.\n");
    exit(1);
    }

if ( data != 0 )
    {
    printf("\nBAD PARAMETER: data randomization not supported yet.\n");
    exit(1);
    }

if ( threads != 1 )
    {
    printf("\nBAD PARAMETER: multi-thread not supported yet.\n");
    exit(1);
    }
    
long long limitmax = 1024*1024*1024*10LL;
long long limitmin = 0;
long long limblk = 4096;
if ( ( start > limitmax )||( stop > limitmax )||( block > limitmax )||
   ( start < limitmin )||( stop < limitmin )||( block < limblk ) )
    {
    printf("\nBAD PARAMETER: start, stop, block must be 0, 10GB, 4096.\n");
    exit(1);
    }

if ( sector != 512 )
    {
    printf("\nBAD PARAMETER: Sector size control not supported yet.\n");
    exit(1);
    }

if ( direct != 1 )
    {
    printf("\nBAD PARAMETER: Direct mode disable not supported yet.\n");
    exit(1);
    }

if ( wsync != 1 )
    {
    printf("\nBAD PARAMETER: Sync mode disable not supported yet.\n");
    exit(1);
    }

if ( precision != 0 )
    {
    printf("\nBAD PARAMETER: precision control not supported yet.\n");
    exit(1);
    }
    
if ( machinereadable != 0 )
    {
    printf("\nBAD PARAMETER: machine readable output not supported yet.\n");
    exit(1);
    }

//--- Wait for key (Y/N) with list of start parameters ---
printf("\nStart? (Y/N)" );
int key = 0;
key = getchar();
key = tolower(key);
if ( key != 'y' )
    {
    printf( "Test skipped.\n" );
    printf( "Release memory...\n" );
    free( diskData );
    exit(3);
    }

//--- Target benchmark operation with time measurement, print results ---
printf( "\nBenchmarking (%s)...\n" , testsNames[operation] );
printf( "\n Offset      Size         MBPS          Utilization" );
printf( "\n---------------------------------------------------------\n" );

//--- Variables for Block I/O benchmarks ---
size_t varOffset = 0;                    // offset, modified in cycle
size_t varSize = 0;                      // size of operation per output line
size_t status = 0;                       // API functions status or size return
size_t accum = 0;                        // read size accumulation
double megabytes = 0.0;                  // block size calculation
double seconds = 0.0, nanoseconds = 0.0;     // time calculation
double mbps = 0.0;                           // megabytes per second
double timeTotal = 0.0, timeUtilized = 0.0;  // total and utilized time
double utilization = 0.0;                    // processor utilization

//--- Variables for statistics ---
int statCount = 0;                           // counter used for statistics
// DEBUG, DYNAMICAL ALLOCATION REQUIRED
double statArray[100000];
for ( i=0; i<100; i++ )
    {
    statArray[i] = 0.0;
    }
// DEBUG

//--- Text string format parameters ---
#define MAXLINE  200            // maximum line size, chars, include last zero
#define MAXENTRY 20             // maximum sub-line size for parameter
char  scratchLine[MAXLINE];     // scratch buffer for line
char* scratchPointer = NULL;    // pointer for scratch buffer addressing
size_t spaces = 0;              // calculated for tabulations

//--- Cycle for required zone of block device ---
for ( varOffset = start; varOffset < stop; varOffset += varSize )
    {
    // blank scratch line, initialize pointer
    for ( i=0; i<MAXLINE; i++ ) { scratchLine[i] = 0; }
    scratchPointer = scratchLine;
    // print block offset to transit string
    spaces = snprintf( scratchPointer, 2, " " ); 
    scratchPointer += spaces;
    spaces = scratchMemorySize( scratchPointer, varOffset ); 
    scratchPointer += spaces;
    spaces = 12 - spaces;
    for (i=0; i<spaces; i++ )
        { scratchPointer += snprintf( scratchPointer, 2, " " ); }
    // print block size to transit string
    varSize = OPERATION_PER_LINE;
    status = stop - varOffset;
    if ( varSize > status ) { varSize = status; }
    spaces = scratchMemorySize( scratchPointer, varSize ); 
    scratchPointer += spaces;
    
    // read requested block with benchmarking
    // get start time
    startTimeDelta();
    //read
    accum = 0;
    while ( accum < varSize )
        {
        status = read( fd, diskData, block );
        if ( status < 0 )
            {
            printf( "%s ( %s )\n", "BLOCK READ ERROR", strerror(errno) );
            exit(1);
            }
        if ( status == 0 )
            {
            printf( "%s ( %s )\n", "UNEXPECTED ZERO LENGTH", strerror(errno) );
            exit(1);
            }
        accum += status;
        }
    // get stop time
    stopTimeDelta();
    
    // calculate megabytes per second
    seconds = ts2[0].tv_sec - ts1[0].tv_sec;
    nanoseconds = ts2[0].tv_nsec - ts1[0].tv_nsec;
    seconds += nanoseconds / 1000000000.0;        // add nanoseconds
    megabytes = varSize;
    megabytes /= 1048576.0;
    mbps = megabytes / seconds;
    // calculate CPU utilization
    timeTotal = seconds;
    seconds = ts2[2].tv_sec - ts1[2].tv_sec;
    nanoseconds = ts2[2].tv_nsec - ts1[2].tv_nsec;
    seconds += nanoseconds / 1000000000.0;        // add nanoseconds
    timeUtilized = seconds;
    utilization = timeUtilized / timeTotal;
    // print megabytes per second
    spaces = 13 - spaces;
    for (i=0; i<spaces; i++ )
        { scratchPointer += snprintf( scratchPointer, 2, " " ); }
    spaces = snprintf( scratchPointer, MAXENTRY, "%.2f", mbps );
    scratchPointer += spaces;
    // print CPU utilization
    spaces = 14 - spaces;
    for (i=0; i<spaces; i++ ) 
        { scratchPointer += snprintf( scratchPointer, 2, " " ); }
    snprintf( scratchPointer, MAXENTRY, "%.3f", utilization ); 
    // output one current line to console
    // better one string built with all previous entries 
    printf( "%s\n", scratchLine );
    
    // statistics support at cycle
    statArray[statCount] = mbps;
    statCount++;
    
    }

printf( "---------------------------------------------------------\n" );

//--- Release allocated memory ---
printf ( "\nRelease memory...\n" );
free( diskData );

//--- Calculate and print benchmarks statistics: min, max, average, median ---
printf ( "\nBenchmarks statistics (MBPS):\n" );
double statSum = 0.0;
double statAverage = 0.0;
double statMedian = 0.0;
double statMin = 0.0;
double statMax = 0.0;
double statTemp = 0.0;
int flag = 0;

//--- Minimum, Maximum, Average ---
statMin = statArray[0];
statMax = statArray[0];
for ( i=0; i<statCount; i++ )
    {
    if ( statMin > statArray[i] ) { statMin = statArray[i]; }
    if ( statMax < statArray[i] ) { statMax = statArray[i]; }
    statSum += statArray[i];
    }
statAverage = statSum / statCount;

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
    statMedian = ( statArray[i-1] + statArray[i] ) / 2.0;
    }
else
    {  // median if array length ODD, middle element, even if length=1
    i = statCount/2;
    statMedian = statArray[i];
    }

//--- Output benchmarks statistics ---
printf( "Median=%.2f , Average=%.2f , Min=%.2f , Max=%.2f\n" ,
        statMedian, statAverage , statMin , statMax );

//--- Print application statistics by OS info ---
printf ( "\nApplication statistics:\n" );
printStatistics();

//--- Done ---
// printf("\n");
exit(0);
}
