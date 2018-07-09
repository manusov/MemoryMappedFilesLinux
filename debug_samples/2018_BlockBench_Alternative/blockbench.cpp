//------------------------------------------------------------------------------
// Linux block device benchmark.
// This version with extra debug messages.
// Usage:  sudo ./blockbench x1 x2 x3
// x1 = device name
// x2 = start sector (512 bytes per sector)
// x3 = number of sectors
// Example:  sudo ./blockbench /dev/sda 0 1000
//------------------------------------------------------------------------------

// FileBench3 supports compiling by makefile without NetBeans IDE.

// BUGS AND ROADMAP:
// 1) Required BLKGETSIZE64 support.
// 2)+ Detail errors messages, use errno value.
// 3) Sector size fixed = 512 bytes, must be variable.
// 4) Some strings defined in the code, better in the header.
// 5)+ Unify fragments from some sources.
// 6)+ Selection return/exit.
// 7)+ Variable size1 declared inside.
// 8) Direct use argv[1].
// 9)+ Yet one sector read.
// 10) Wrong MBPS by timers 2 and 3.
// 11) Required Use O_DIRECT, no effect when open, still cacheable.
// 12) Required 64-bit support geometry, function atoi() for integer only ?
// 13) Remove duplicated variables.
// 14)+ Timings accuracy, remove some ops (create) from measured interval.
// 15) Bug with N/1048576=0 at block size calculations.
// 16) Use correct special types, for example size_t, off_t, see die.net.

// see BBtasks (Block Benchmark Tasks separate debug),
// for this problems solutions as separate fragments.
// See also BlockBench, BlockBench1 projects for alternate solutions,
// this project BlockBench2.

// After Block Devices and Files, can test Memory and Persistent Memory.

//---------- Definitions -------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hdreg.h>
#include <linux/fs.h>
#include <cctype>

#define SECTOR 512            // Sector size yet fixed

using namespace std;

//---------- Title message strings ---------------------------------------------
static const char msgRun[] =
    "Linux block devices simple benchmark. Variant 2.";
static const char msgAbout[] =
    "(C)2018 IC Book Labs. v0.45 with extra debug messages.";

//---------- Messages strings for steps and sub-steps sequence -----------------
static const char msgCommandParms[] =
    "Command line parameters:";
static const char msgReqDevice[] =
    "Device for test = ";
static const char msgReqStart[] =
    " , start sector = ";
static const char msgReqCount[] =
    " , sectos count = ";
static const char msgDeviceParms[] =
    "Selected block device parameters:";
static const char msgTimersList[] =
    "Timers list with time units:";
static const char msgMemoryAllocate[] =
    "Memory allocation for aligned buffer:";
static const char msgSeek[] =
    "Seek to required offset:";
static const char msgTimerStart[] =
    "Timer start...";
static const char msgReadDisk[] =
    "Read disk...";
static const char msgTimerStop[] =
    "Timer stop...";
static const char msgCalculate[] =
    "Calculate results:";
static const char msgSeconds[] =
    "seconds";
static const char msgMBPS[] =
    "megabytes per second";
static const char msgUtilization[] =
    "processor utilization ratio";
static const char msgPrintStatistics[] =
    "Linux application statistics:";
static const char msgDone[] =
    "Done.";

//---------- Message strings for errors reporting ------------------------------
static const char msgError[] =
    "ERROR: ";
static const char msgNumParms[] =
    "wrong number of parameters.";
static const char msgUsage[] = 
    "USAGE:   sudo ./blockbench device startsector sectorscount";
static const char msgExample[] = 
    "EXAMPLE: sudo ./blockbench /dev/sda 0 1000";
static const char msgParm[] = 
    "bad parameter.";
static const char msgErrorOpen[] = 
    "Cannot open device";
static const char msgErrorID[]   = 
    "No hard disk identification information available";
static const char msgFailedID[] = 
    "Disk identification failed";
static const char msgErrorGeom[] = 
    "No hard disk geometry information available";
static const char msgFailedGeom[] = 
    "Disk get geometry failed";
static const char msgFailedSize[] =
    "Disk get size failed";
static const char msgFailedRequestSize[] =
    "Disk get maximum request size failed";
static const char msgFailedMemAlloc[] =
    "request failed";
static const char msgFailedSeek[] =
    "request failed";
static const char msgFailedRead[] =
    "data read failed";
static const char msgZeroRead[] =
    "data read unexpected zero length";

//---------- Message strings for disk drive parameters names -------------------
static const char msgDrive[]       = "Drive model       : ";
static const char msgSerial[]      = "Serial number     : ";
static const char msgFirmware[]    = "Firmware Revision : ";
static const char msgCylinders[]   = "Cylinders=";
static const char msgHeads[]       = "Heads=";
static const char msgSectors[]     = "Sectors=";
static const char msgStartSector[] = "Start sector=";

//---------- Message strings for IOCTL requests names --------------------------
static const char msgIdentify[]    = "HDIO_GET_IDENTIFY:";
static const char msgGetGeo[]      = "HDIO_GETGEO:";
static const char msgGetSize[]     = "BLKGETSIZE:";
static const char msgBlkSectGet[]  = "BLKSETGET:";

//---------- List of Linux timers IDs and its names ----------------------------
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

//---------- Helpers functions declaration -------------------------------------
// get and print Linux application statistics
void printStatistics();

//---------- Application entry point -------------------------------------------
int main(int argc, char** argv)    // argc = number of command line arguments, 
    {                              //        include program name
                                   // argv = array of strings
//---------- Create dynamical memory variables ---------------------------------    
    int i = 0;                          // counter for some cycles
    int status = 0;                     // called API functions status
    long xc=0, xh=0, xs=0, xt=0;        // data values for temporary masking
    double sizeMB = 0.0;                // size for visual as floating point
    char* reqDevice = 0;                // pointer to required device name
    unsigned long int reqStart = -1;    // required start sector
    unsigned long int reqCount = -1;    // required number of sectors
    int size = 0;                       // size for block devices
    int sectperreq = 0;                 // sectors per request
    // variables for IOCTL requests
    int fd = 0;                         // file descriptor, open device as file
    union hd_union                      // data region for IDENTIFY_DEVICE
        {
        struct hd_driveid hdstr;        // alias 1 = formatted structure
        char hdraw[512];                // alias 2 = raw buffer (RSRV. FOR DUMP)
        };
    hd_union hd;                        // data region for IDENTIFY_DEVICE
    struct hd_geometry hdg;             // data region for GET GEOMETRY request
    // variables for OS timers support
    unsigned long int sec = 0, ns = 0;     // transit variables for time
    struct timespec ts[TCNT];              // reports of timers parameters
    struct timespec ts1[TCNT], ts2[TCNT];  // start and end moments
    // variables for memory control
    void* dataBuffer = 0;                  // pointer to i/o buffer
    size_t bytesTotal = 0;                 // total bytes read per test
    off_t offsetRequest = 0;               // offset of start position
    size_t bytesPerRequest = 0;            // bytes per i/o request=buffer
    // variables for speed calculation
    double fsec = 0.0, fns = 0.0;          // transit variables for time
    double megabytes = 0.0;                // megabytes transferred
    double mbps = 0.0;                     // megabytes per second calculated
    // variables for utilization factor calculation
    double timeTotal = 0.0;                // total time per operation
    double timeUtilized = 0.0;             // CPU utilized time per operation
    double timeRatio = 0.0;                // ratio = utilized / total

//---------- Console output first title message --------------------------------    
    printf("\n%s\n%s\n", msgRun, msgAbout);
    
//---------- Get command line parameters, console output for check accept ------
    printf( "\n%s", msgCommandParms );
    printf( "\nargc = %d" , argc );
    for(i=0; i<argc; i++)
        {
        printf( "\nargv[%d] = %s" , i , argv[i] );
        }
    if (argc!=4)  // check number of command line arguments
        {
        printf ( "\n%s%s\n%s\n%s\n",
                 msgError, msgNumParms, msgUsage, msgExample );
        exit(1);
        }
    reqDevice = argv[1];                 // set pointer to device name
    if ( (argv[2]) && (argv[3]) )
        {
        if ( (isdigit(argv[2][0])) && (isdigit(argv[3][0])) )
            {
            reqStart = atoi( argv[2] );  // convert strings to numbers
            reqCount = atoi( argv[3] );
            }
        }
    if ( ( reqStart<0) | (reqCount<=0) )  // check parameters validity
        {                                 // bug fixed at v0.22
        printf ( "\n%s%s\n%s\n%s\n",
                 msgError, msgParm, msgUsage, msgExample );
        exit(1);
        }
    printf( "\n%s%s%s%ld%s%ld\n", 
            msgReqDevice , reqDevice,
            msgReqStart  , reqStart,
            msgReqCount  , reqCount );
    
//---------- Get block device parameters, console output device info -----------
    printf( "\n%s", msgDeviceParms );
    // note possible 4K-alignment issues in the O_DIRECT mode
    // if ((fd = open( reqDevice, O_RDONLY|O_NONBLOCK|O_DIRECT )) < 0)
    if ((fd = open( reqDevice, O_RDONLY|O_DIRECT )) < 0)  // changed at v0.22
        {
        printf( "\n%s%s %s ( %s )\n", 
                msgError, msgErrorOpen, reqDevice, strerror(errno) );
        exit(1);
        }
    // IOCTL request: HDIO_GET_IDENTIFY
    if (!ioctl(fd, HDIO_GET_IDENTITY, &hd))   // make IOCTL request 
        {                                     // output parameters if no errors
        printf( "\n%s\n" , msgIdentify );
        printf( "%s%.40s\n" , msgDrive    , hd.hdstr.model );
        printf( "%s%.20s\n" , msgSerial   , hd.hdstr.serial_no );
        printf( "%s%.8s\n"  , msgFirmware , hd.hdstr.fw_rev );
        xc = hd.hdstr.cyls & 0xFFFF;       // cylinders
        xh = hd.hdstr.heads & 0xFFFF;      // heads
        xs = hd.hdstr.sectors & 0xFFFF;    // sectors
        printf( "%s%ld , %s%ld , %s%ld\n",
                msgCylinders, xc, msgHeads, xs, msgSectors, xs );
        }
        else if (errno == -ENOMSG)     // special error handling 
            {                          // error = no message of desired type
            printf( "%s ( %s )\n", msgErrorID, strerror(errno) );
            exit(1);
            }
        else                           // other errors handling
            {
            printf( "%s ( %s )\n", msgFailedID, strerror(errno) );
            exit(1);
            }
    // IOCTL request: HDIO_GETGEO
    if (!ioctl(fd, HDIO_GETGEO, &hdg))    // make IOCTL request
        {
        printf( "%s " , msgGetGeo );    // output parameters if no errors
        xc = hdg.cylinders & 0xFFFF;        
        xh = hdg.heads & 0xFF;        
        xs = hdg.sectors & 0xFF;        
        xt = hdg.start;
        printf( "%s%ld , %s%ld , %s%ld , %s%ld\n",
          msgCylinders, xc, msgHeads, xs, msgSectors, xs, msgStartSector, xt );
        } 
    else if (errno == -ENOMSG)            // special error handling
        {                                 // error = no message of desired type
        printf( "%s ( %s )\n", msgErrorGeom, strerror(errno) );
        } 
    else                                  // other errors handling
        {
        printf( "%s ( %s )\n", msgFailedGeom, strerror(errno) );
        }
    // IOCTL request BLKGETSIZE
    if ( !ioctl(fd, BLKGETSIZE, &size) )
        {
        sizeMB = size;
        sizeMB = sizeMB / 1048576 * SECTOR;
        printf( "%s %d sectors , means %.1lf MB\n",
                msgGetSize, size, sizeMB );
        }
    else
        {
        printf( "%s ( %s )\n", msgFailedSize, strerror(errno) );
        exit(1);
        }
   
//---------- Get system timers parameters, console output timers info ----------
    printf( "\n%s\n", msgTimersList );
    for ( i=0; i<TCNT; i++ )
        {
        status = clock_getres(clk_ids[i], &ts[i] );
        if(status==0)
            {
            sec = ts[i].tv_sec;
            ns  = ts[i].tv_nsec;
            printf( "%s  %ld s %ld ns\n", namesT[i], sec, ns );
            }
        else
            {
            ts[i].tv_sec = -1;
            ts[i].tv_nsec = -1;
            printf( "%s  N/A ( %s )\n", namesT[i], strerror(errno) );
            }
        }

//---------- Allocate aligned memory buffer, console output buffer info --------
    printf( "\n%s", msgMemoryAllocate );

    // IOCTL request BLKSECTGET, number of sectors per request
    // moved to this point at v0.22
    if ( !ioctl(fd, BLKSECTGET, &sectperreq) )
        {
        bytesPerRequest = sectperreq * SECTOR;
        sizeMB = bytesPerRequest;
        sizeMB = sizeMB / 1048576;
        printf( "\n%s maximum %d sectors per request , means %.1lf MB",
                msgBlkSectGet, sectperreq, sizeMB );
        }
    else
        {
        printf( "%s ( %s )\n", msgFailedRequestSize, strerror(errno) );
        exit(1);
        }

    // allocate aligned memory region for buffer
    dataBuffer = memalign ( 4096, bytesPerRequest );
    if ( dataBuffer > 0 )
        {
        printf(" , base = %p\n" , dataBuffer );
        }
    else
        {
        printf( "%s ( %s )\n", msgFailedMemAlloc, strerror(errno) );
        exit(1);
        }
    
//---------- Seek to accepted start position, console output seek result -------
    printf( "%s", msgSeek );
    offsetRequest = reqStart * SECTOR;
    bytesTotal = reqCount * SECTOR;
    sizeMB = offsetRequest;
    sizeMB = sizeMB / 1048576;
    if ( ( lseek ( fd, offsetRequest, SEEK_SET ) ) == offsetRequest )
        {
        printf(" offset = %.1lf MB\n" , sizeMB );
        }
    else
        {
        printf( "%s ( %s )\n", msgFailedSeek, strerror(errno) );
        exit(1);
        }
//---------- Get time point for operation start, console output checkpoint -----
    printf( "\n%s\n", msgTimerStart );
    for ( i=0; i<TCNT; i++ )
        {
        if ( ts[i].tv_sec >= 0 )  // validation from get units
            {  // get current time, return status, it checked
            status = clock_gettime(clk_ids[i], &ts1[i] );
            }
        else
            {  // set error condition if previous get time units failed
            status = -1;
            }
        if(status!=0)  // validation from get time result
            {
            ts1[i].tv_sec = -1;   // force invalid result
            ts1[i].tv_nsec = -1;
            }
        }
    
//---------- Target operation, read from device, console output checkpoint -----
// Required termination by "\n" otherwise can be delayed by console output
    printf( "%s\n", msgReadDisk );
    // temporary variables
    ssize_t tmpsize = 0;
    ssize_t tmpadd = 0;
    ssize_t tmptotal = bytesTotal;
    ssize_t tmprequest = bytesPerRequest;
    // data read cycle
    while ( tmpadd < tmptotal )
        {
        tmpsize = read( fd, dataBuffer, tmprequest );
        if ( tmpsize < 0 )
            {
            printf( "%s ( %s )\n", msgFailedRead, strerror(errno) );
            exit(1);
            }
        if ( tmpsize == 0 )
            {
            printf( "%s ( %s )\n", msgZeroRead, strerror(errno) );
            exit(1);
            }
        tmpadd += tmpsize;
        }
    
//---------- Get time point for operation stop, console output checkpoint ------
    printf( "%s\n", msgTimerStop );
    for ( i=0; i<TCNT; i++ )
        {
        if ( ts1[i].tv_sec >= 0 )  // validation from get start time
            {  // get current time, return status, it checked
            status = clock_gettime(clk_ids[i], &ts2[i] );
            }
        else
            {  // set error condition if previous get start time failed
            status = -1;
            }
        if(status!=0)  // validation from get time result
            {
            ts2[i].tv_sec = -1;   // force invalid result
            ts2[i].tv_nsec = -1;
            }
        }

//---------- Calculate results, console output, exit ---------------------------
    printf( "\n%s ", msgCalculate );
    megabytes = reqCount * SECTOR;
    megabytes /=  1000000;  // 1048576.0;
    printf("total transferred %.3lf MB", megabytes);
    for ( i=0; i<TCNT; i++ )
        {
        fsec = ts2[i].tv_sec - ts1[i].tv_sec;
        fns  = ts2[i].tv_nsec - ts1[i].tv_nsec;
        fsec += fns / 1000000000.0;
        if (i==0)
            {
            timeTotal = fsec;
            mbps = megabytes / fsec;  // MBPS calculated by first timer
            }
        if (i==2)
            {
            timeUtilized = fsec;
            }
        printf( "\n%s  %.7lf %s", namesT[i], fsec, msgSeconds );
        }
    // Print final results
    timeRatio = timeUtilized / timeTotal;
    printf( "\n\n%.3lf %s" , mbps, msgMBPS );
    printf(   "\n%.3lf %s\n" , timeRatio, msgUtilization );
    
//---------- Get and print Linux application statistics ------------------------
    printf( "\n%s", msgPrintStatistics );
    printStatistics();
    
//---------- Done --------------------------------------------------------------
    printf( "\n%s\n\n", msgDone );
    exit(0);
    }

//---------- Helpers functions -------------------------------------------------
// get and print Linux application statistics
void printStatistics()
{
    struct rusage usage;
    int status = 0;
    int why = RUSAGE_SELF;
    status = getrusage ( why, &usage );
    if ( status < 0 )
        {
        printf ( "Get resource usage failed ( %s )" , strerror(errno) );
        exit(1);
        }
    printf ( "\nUser space CPU time used: %ld sec %ld usec ", 
             (long)(usage.ru_utime.tv_sec), (long)(usage.ru_utime.tv_usec) );
    printf ( "\nSystem space CPU time used: %ld sec %ld usec ", 
             (long)(usage.ru_stime.tv_sec), (long)(usage.ru_stime.tv_usec) );
    printf ( "\nMaximum resident set size        = %ld KB", usage.ru_maxrss );
    printf ( "\nIntegral shared memory size      = %ld KB", usage.ru_ixrss );
    printf ( "\nIntegral unshared data size      = %ld KB", usage.ru_idrss );
    printf ( "\nIntegral unshared stack size     = %ld KB", usage.ru_isrss );
    printf ( "\nPage reclaims (soft page faults) = %ld", usage.ru_minflt );
    printf ( "\nPage faults (hard page faults)   = %ld", usage.ru_majflt );
    printf ( "\nSwaps                            = %ld", usage.ru_nswap );
    printf ( "\nBlock input operations           = %ld", usage.ru_inblock );
    printf ( "\nBlock output operations          = %ld", usage.ru_oublock );
    printf ( "\nIPC messages sent                = %ld", usage.ru_msgsnd );
    printf ( "\nIPC messages received            = %ld", usage.ru_msgrcv );
    printf ( "\nSignals received                 = %ld", usage.ru_nsignals );
    printf ( "\nVoluntary context switches       = %ld", usage.ru_nvcsw );
    printf ( "\nInvoluntary context switches     = %ld\n", usage.ru_nivcsw );
    return;
}

