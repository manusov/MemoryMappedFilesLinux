/*

------------------------------------------------------------------------------
 Linux file operations benchmark.
 This version with extra debug messages.
 Usage:  sudo ./filebench x1 x2 x3
 x1 = path and first file name, create, write, and source for copy
 x2 = path and second file name, destination for copy
 x3 = number of sectors
 Example:  sudo ./filebench myfile1.bin myfile2.bin 1000
------------------------------------------------------------------------------

 FileBench3 supports compiling by makefile without NetBeans IDE.
 FileBench2 is result of adaptation BlockBench2
 for results comparisions in same conditions.
 No projects FileBench, FileBench1,
 but exists BlockBench, BlockBench1.
 Same version number assigned: v0.22.

 BUGS AND ROADMAP:
 1) Required BLKGETSIZE64 support.
 2)+ Detail errors messages, use errno value.
 3) Sector size fixed = 512 bytes, must be variable.
 4) Some strings defined in the code, better in the header.
 5)+ Unify fragments from some sources.
 6)+ Selection return/exit.
 7)+ Variable size1 declared inside.
 8) Direct use argv[1].
 9)+ Yet one sector read.
 10) Wrong MBPS by timers 2 and 3.
 11) Required Use O_DIRECT, no effect when open, still cacheable.
 12) Required 64-bit support geometry, function atoi() for integer only ?
 13) Remove duplicated variables.
 14)+ Timings accuracy, remove some ops (create) from measured interval.
 15) Bug with N/1048576=0 at block size calculations.
 16) Use correct special types, for example size_t, off_t, see die.net.

 see BBtasks (Block Benchmark Tasks separate debug),
 for this problems solutions as separate fragments.
 See also BlockBench, BlockBench1 projects for alternate solutions,
 this project BlockBench2.

 After Block Devices and Files, can test Memory and Persistent Memory.

*/

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
#include <sys/sendfile.h>

#define SECTOR 512            // Sector size, bytes, yet fixed
#define SECTORS_PER_IO 2560   // Num. of sectors per one I/O request, yet fixed

#define SLEEP_WRITE 10        // pause from Start to Write, seconds
#define SLEEP_READ  40        // pause from Write to Read, seconds
#define SLEEP_COPY  40        // pause from Read to Copy, seconds

using namespace std;

//---------- Title message strings ---------------------------------------------
static const char msgRun[] =
    "Linux file operations simple benchmark.";
static const char msgAbout[] =
    "(C)2018 IC Book Labs. v0.45 with extra debug messages.";

//---------- Messages strings for steps and sub-steps sequence -----------------
static const char msgCommandParms[] =
    "Command line parameters:";
static const char msgReqFirstFile[] =
    "First file = ";
static const char msgReqSecondFile[] =
    " , second file = ";
static const char msgReqCount[] =
    " , sectos count = ";
static const char msgReqSize[] =
    " , size = ";
static const char msgCreateFiles[] =
    "Create source and destination files...";
static const char msgTimersList[] =
    "Timers list with time units:";
static const char msgMemoryAllocate[] =
    "Memory allocation for aligned buffer:";
static const char msgSectPerIO[] =
    "I/O length (const):";
static const char msgTimerStart[] =
    "Timer start...";
static const char msgReadFile[] =
    "Read file...";
static const char msgWriteFile[] =
    "Write file...";
static const char msgCopyFile[] =
    "Copy file...";
static const char msgDeleteFiles[] =
    "Delete files...";
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
static const char msgSleep[] =
    "Sleep";
static const char msgDone[] =
    "Done.";

//---------- Message strings for errors reporting ------------------------------
static const char msgError[] =
    "ERROR: ";
static const char msgNumParms[] =
    "wrong number of parameters.";
static const char msgUsage[] = 
    "USAGE:   sudo ./filebench filename1 filename2 sectorscount";
static const char msgExample[] = 
    "EXAMPLE: sudo ./filebench myfile1.bin myfile2.bin 1000";
static const char msgParm[] = 
    "bad parameter.";
static const char msgErrorOpen[] = 
    "Cannot open device";
static const char msgFailedMemAlloc[] =
    "request failed";
static const char msgFailedRead[] =
    "data read failed";
static const char msgZeroRead[] =
    "data read unexpected zero length";
static const char msgFailedWrite[] =
    "data write failed";
static const char msgZeroWrite[] =
    "data write unexpected zero length";
static const char msgFailedCopy[] =
    "data copy failed";
static const char msgZeroCopy[] =
    "data copy unexpected zero length";
static const char msgFailedSeek[] =
    "request failed";
static const char msgFailedDelete[] =
    "request failed";
static const char msgFailedSleep[] =
    "sleep failed, unexpected interrupt";

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
// called at start of measured interval
void timerStart ( struct timespec[] , struct timespec[] );
// called at end of measured interval
void timerStop ( struct timespec[] , struct timespec[] );
// calculate transfer time and CPU utilization
void benchmarksCalculation
    ( unsigned long x , 
      double& megabytes , double &mbps ,
      double& timeTotal , double& timeUtilized , double& timeRatio ,
      struct timespec ts1[] , struct timespec ts2[] );
// get and print Linux application statistics
void printStatistics();

//---------- Application entry point -------------------------------------------
int main(int argc, char** argv)    // argc = number of command line arguments, 
    {                              //        include program name
                                   // argv = array of strings

//---------- Create dynamical memory variables ---------------------------------    
    int i = 0;                             // counter for some cycles
    int status = 0;                        // called API functions status
    double sizeMB = 0.0;                   // size for visual as floating point
    char* firstFile = 0;                   // pointer to first file name
    char* secondFile = 0;                  // pointer to second file name
    unsigned long int sectorsCount = -1;   // total number of sectors per file
    unsigned long int bytesCount = -1;     // total number of bytes per file
    unsigned long int sectorsPerIO = -1;   // sectors per one I/O request
    unsigned long int bytesPerIO = -1;     // bytes per one I/O request
    int fd1 = 0 , fd2 = 0;                 // file descriptors: src, dst
    // variables for OS timers support
    unsigned long int sec = 0, ns = 0;     // transit variables for time
    struct timespec ts[TCNT];              // reports of timers parameters
    struct timespec ts1[TCNT], ts2[TCNT];  // start and end moments
    // variables for memory control
    void* dataBuffer = 0;                  // pointer to i/o buffer
    // variables for speed calculation
    double megabytes = 0.0;                // megabytes transferred
    double mbps = 0.0;                     // megabytes per second calculated
    // variables for utilization factor calculation
    double timeTotal = 0.0;                // total time per operation
    double timeUtilized = 0.0;             // CPU utilized time per operation
    double timeRatio = 0.0;                // ratio = utilized / total
    // pauses support
    unsigned int sleepValue = 0;           // input for sleep function
    unsigned int sleepResult = 0;          // special status for sleep function

//---------- Console output first title message --------------------------------    
    printf("\n%s\n%s\n", msgRun, msgAbout);
    
 //---------- Get command line parameters, console output for check accept -----
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
    firstFile  = argv[1];           // set pointer to files names
    secondFile = argv[2];
    if ( argv[3] ) 
        {
        if ( isdigit(argv[3][0]) )
            {
            sectorsCount = atoi( argv[3] );    // convert strings to numbers
            }
        }
    if ( sectorsCount<=0 )                     // check parameter validity
        {
        printf ( "\n%s%s\n%s\n%s\n",
                 msgError, msgParm, msgUsage, msgExample );
        exit(1);
        }
    bytesCount = sectorsCount * SECTOR;
    sizeMB = bytesCount;
    sizeMB /= 1000000;  // 1048576
    printf( "\n%s%s%s%s%s%ld%s%.2f MB\n", 
            msgReqFirstFile  , firstFile ,
            msgReqSecondFile , secondFile ,
            msgReqCount      , sectorsCount ,
            msgReqSize       , sizeMB );
    
//---------- Create both files, this operations outside of measured time -------
    printf( "\n%s\n", msgCreateFiles );
    if ( ( fd1 = open( firstFile, O_RDWR|O_DIRECT|O_DSYNC|O_CREAT ) ) < 0 )
        {
        printf( "\n%s%s %s ( %s )\n", 
                msgError, msgErrorOpen, firstFile, strerror(errno) );
        exit(1);
        }
    if ( ( fd2 = open( secondFile, O_RDWR|O_DIRECT|O_DSYNC|O_CREAT ) ) < 0 )
        {
        printf( "\n%s%s %s ( %s )\n", 
                msgError, msgErrorOpen, secondFile, strerror(errno) );
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
    sectorsPerIO = SECTORS_PER_IO;
    bytesPerIO = SECTORS_PER_IO * SECTOR;
    sizeMB = bytesPerIO;
    sizeMB = sizeMB / 1048576;
    printf( "\n%s maximum %ld sectors per API call , means %.1lf MB",
             msgSectPerIO, sectorsPerIO, sizeMB );
    // allocate aligned memory region for buffer
    dataBuffer = memalign ( 4096, bytesPerIO );
    if ( dataBuffer > 0 )
        {
        printf(" , base = %p\n" , dataBuffer );
        }
    else
        {
        printf( "%s ( %s )\n", msgFailedMemAlloc, strerror(errno) );
        exit(1);
        }
    // pre-clear buffer to prevent possible page faults
    char* tmpdata = (char*)dataBuffer;
    for ( i=0; i<bytesPerIO; i++ )
        {
        *tmpdata = 0;
        }

//---------- Delay before Write ------------------------------------------------
    sleepValue = SLEEP_WRITE;
    printf( "\nSleep %d seconds...\n", sleepValue );
    sleepResult = sleep( sleepValue );
    if (  sleepResult != 0 )
        {
        printf( "%s ( %d )\n", msgFailedSleep, sleepResult );
        exit(1);
        }

//---------- Write file --------------------------------------------------------    
    ssize_t tmpsize = 0;
    ssize_t tmpadd = 0;
    ssize_t tmptotal = bytesCount;
    ssize_t tmprequest = bytesPerIO;
    // Get time point for operation start, console output checkpoint
    printf( "%s", msgTimerStart );
    timerStart( ts, ts1 );
    // Target operation, write first file (for copy, this file is source)
    printf( "\n%s\n", msgWriteFile );
    // support small files
    if ( tmprequest > tmptotal )
        {
        tmprequest = tmptotal;
        }
    // write cycle
    while ( tmpadd < tmptotal )
        {
        tmpsize = write( fd1, dataBuffer, tmprequest );
        if ( tmpsize < 0 )
            {
            printf( "%s ( %s )\n", msgFailedWrite, strerror(errno) );
            exit(1);
            }
        if ( tmpsize == 0 )
            {
            printf( "%s ( %s )\n", msgZeroWrite, strerror(errno) );
            exit(1);
            }
        tmpadd += tmpsize;
        }
    // Get time point for operation start, console output checkpoint
    printf( "%s", msgTimerStop );
    timerStop( ts1, ts2 );
    // Calculate results, console output, exit
    printf( "\n%s ", msgCalculate );
    benchmarksCalculation( bytesCount , 
                           megabytes , mbps ,
                           timeTotal , timeUtilized , timeRatio ,
                           ts1 , ts2 );
    printf( "\n%.3lf %s" , mbps, msgMBPS );
    printf( "\n%.3lf %s\n" , timeRatio, msgUtilization );

//---------- Delay before Read -------------------------------------------------
    sleepValue = SLEEP_READ;
    printf( "\nSleep %d seconds...\n", sleepValue );
    sleepResult = sleep( sleepValue );
    if (  sleepResult != 0 )
        {
        printf( "%s ( %d )\n", msgFailedSleep, sleepResult );
        exit(1);
        }
    
//---------- Read file ---------------------------------------------------------
    tmpsize = 0;
    tmpadd = 0;
    tmptotal = bytesCount;
    tmprequest = bytesPerIO;
    // Get time point for operation start, console output checkpoint
    printf( "%s\n", msgTimerStart );
    timerStart( ts, ts1 );
    // Target operation, write first file (for copy, this file is source)
    printf( "%s\n", msgReadFile );
    // seek to file start
    if ( ( lseek ( fd1, 0, SEEK_SET ) ) != 0 )
        {
        printf( "%s ( %s )\n", msgFailedSeek, strerror(errno) );
        exit(1);
        }
    // support small files
    if ( tmprequest > tmptotal )
        {
        tmprequest = tmptotal;
        }
    // read cycle
    while ( tmpadd < tmptotal )
        {
        tmpsize = read( fd1, dataBuffer, tmprequest );
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
    // Get time point for operation start, console output checkpoint
    printf( "%s\n", msgTimerStop );
    timerStop( ts1, ts2 );
    // Calculate results, console output, exit
    printf( "%s ", msgCalculate );
    benchmarksCalculation( bytesCount , 
                           megabytes , mbps ,
                           timeTotal , timeUtilized , timeRatio ,
                           ts1 , ts2 );
    printf( "\n%.3lf %s" , mbps, msgMBPS );
    printf( "\n%.3lf %s\n" , timeRatio, msgUtilization );

//---------- Delay before Copy -------------------------------------------------
    sleepValue = SLEEP_COPY;
    printf( "\nSleep %d seconds...\n", sleepValue );
    sleepResult = sleep( sleepValue );
    if (  sleepResult != 0 )
        {
        printf( "%s ( %d )\n", msgFailedSleep, sleepResult );
        exit(1);
        }

//---------- Copy file ---------------------------------------------------------
    tmpsize = 0;
    tmpadd = 0;
    tmptotal = bytesCount;
    tmprequest = bytesPerIO;
    // Get time point for operation start, console output checkpoint
    printf( "%s\n", msgTimerStart );
    timerStart( ts, ts1 );
    // Target operation, write first file (for copy, this file is source)
    printf( "%s\n", msgCopyFile );
    // seek to file start
    if ( ( lseek ( fd1, 0, SEEK_SET ) ) != 0 )
        {
        printf( "%s ( %s )\n", msgFailedSeek, strerror(errno) );
        exit(1);
        }
    // support small files
    if ( tmprequest > tmptotal )
        {
        tmprequest = tmptotal;
        }
    // copy cycle
    while ( tmpadd < tmptotal )
        {
        tmpsize = sendfile( fd2, fd1, NULL, tmprequest );
        if ( tmpsize < 0 )
            {
            printf( "%s ( %s )\n", msgFailedCopy, strerror(errno) );
            exit(1);
            }
        if ( tmpsize == 0 )
            {
            printf( "%s ( %s )\n", msgZeroCopy, strerror(errno) );
            exit(1);
            }
        tmpadd += tmpsize;
        }
    // Get time point for operation start, console output checkpoint
    printf( "%s\n", msgTimerStop );
    timerStop( ts1, ts2 );
    // Calculate results, console output, exit
    printf( "%s ", msgCalculate );
    benchmarksCalculation( bytesCount , 
                           megabytes , mbps ,
                           timeTotal , timeUtilized , timeRatio ,
                           ts1 , ts2 );
    printf( "\n%.3lf %s" , mbps, msgMBPS );
    printf( "\n%.3lf %s\n" , timeRatio, msgUtilization );

//---------- Delete both files -------------------------------------------------    
    printf( "\n%s\n", msgDeleteFiles );
    // delete first file
    if ( ( remove ( firstFile ) ) < 0 )
        {
        printf( "\n%s%s %s ( %s )\n", 
                msgError, msgFailedDelete, firstFile, strerror(errno) );
        }
    // delete second file
    if ( ( remove ( secondFile ) ) < 0 )
        {
        printf( "\n%s%s %s ( %s )\n", 
                msgError, msgFailedDelete, secondFile, strerror(errno) );
        }

//---------- Get and print Linux application statistics ------------------------
    printf( "\n%s", msgPrintStatistics );
    printStatistics();
    
//---------- Done --------------------------------------------------------------
    printf( "\n%s\n\n", msgDone );
    exit(0);
    }

//---------- Helpers functions -------------------------------------------------

// called at start of measured interval
void timerStart ( struct timespec ts[] , struct timespec ts1[] )
    {
    int i = 0;
    int status = 0;
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
    }

// called at end of measured interval
void timerStop ( struct timespec ts1[] , struct timespec ts2[] )
    {
    int i = 0;
    int status = 0;
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
    }

// calculate transfer time and CPU utilization
void benchmarksCalculation
    ( unsigned long x , 
      double& megabytes , double &mbps ,
      double& timeTotal , double& timeUtilized , double& timeRatio ,
      struct timespec ts1[] , struct timespec ts2[] )
    {
    int i = 0;
    double fsec = 0.0, fns = 0.0;          // transit variables for time
    megabytes = x;
    megabytes /=  1000000;  // 1048576.0;
    printf("total transferred %.3lf MB\n", megabytes);
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
        printf( "%s  %.7lf %s\n", namesT[i], fsec, msgSeconds );
        }
    // Print final results
    timeRatio = timeUtilized / timeTotal;
    }

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
