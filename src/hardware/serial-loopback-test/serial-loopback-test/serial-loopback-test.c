/*
    writeread.c - based on writeread.cpp
    [SOLVED] Serial Programming, Write-Read Issue - http://www.linuxquestions.org/questions/programming-9/serial-programming-write-read-issue-822980/

    build with: gcc -o writeread -Wall -g writeread.c
*/

#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include <stdlib.h>
#include <sys/time.h>

#include "serial-loopback-test.h"


int serport_fd;

void usage(char **argv)
{
    fprintf(stdout, "Usage:\n"); 
    fprintf(stdout, "%s port baudrate file/string\n", argv[0]); 
    fprintf(stdout, "Examples:\n"); 
    fprintf(stdout, "%s /dev/ttyUSB0 115200 /path/to/somefile.txt\n", argv[0]); 
    fprintf(stdout, "%s /dev/ttyUSB0 115200 \"some text test\"\n", argv[0]); 
}

//CTRL+C handler
//weilun@adv - begin
//int execute;
//void trap(int signal) {execute = 0;}
//weilun@adv - end

int main( int argc, char **argv ) 
{

    if( argc != 4 ) { 
        usage(argv);
        return 1; 
    }

    char *serport;
    char *serspeed;
    speed_t serspeed_t;
    char *serfstr, *recievedBytes;
    int serf_fd; // if < 0, then serfstr is a string
    int bytesToSend; 
    int sentBytes, sentBytesTotal = 0;
    char byteToSend[2];
    int readChars;
    int recdBytes, totlBytes; 

    char sResp[11];

    struct timeval timeStart, timeEnd, timeDelta;
    float deltasec; 

    int write_failed;
    int cyclesPass = 0, cyclesFailed = 0;

    // Get the PORT name
    serport = argv[1];
    fprintf(stdout, "Opening port %s;\n", serport);

    // Get the baudrate
    serspeed = argv[2];
    serspeed_t = string_to_baud(serspeed);
    fprintf(stdout, "Got speed %s (%d/0x%x);\n", serspeed, serspeed_t, serspeed_t);

    //Get file or command;
    serfstr = argv[3];
    serf_fd = open( serfstr, O_RDONLY );
    fprintf(stdout, "Got file/string '%s'; ", serfstr);
    if (serf_fd < 0) {
        bytesToSend = strlen(serfstr);
//        fprintf(stdout, "interpreting as string (%d).\n", bytesToSend);
    } else {
        struct stat st;
        stat(serfstr, &st);
        bytesToSend = st.st_size;
//        fprintf(stdout, "opened as file (%d).\n", bytesToSend);
    }


    // Open and Initialise port
    //weilun@adv - begin
    //serport_fd = open( serport, O_RDWR | O_NOCTTY | O_NONBLOCK );
    serport_fd = open( serport, O_RDWR | O_NOCTTY);
    //weilun@adv - end
    if ( serport_fd < 0 ) { perror(serport); return 1; }
    initport( serport_fd, serspeed_t );

    sentBytes = 0; recdBytes = 0;
    byteToSend[0]='x'; byteToSend[1]='\0';
    gettimeofday( &timeStart, NULL );

    //CTRL+C handler
    //weilun@adv - begin
    //signal(SIGINT,&trap);
    //execute = 1;
    //weilun@adv - end

    fprintf(stdout, "\n+++START+++\n");

    fprintf(stdout, "CTRL+C to exit\n", serspeed, serspeed_t, serspeed_t);


    //alloc number of bytes to send
    recievedBytes = (char*) malloc(sizeof(char) *  bytesToSend);
   
    // write / read loop - interleaved (i.e. will always write 
    // one byte at a time, before 'emptying' the read buffer ) 
    //weilun@adv - begin
    //for ( ; ; )
    //weilun@adv - end
    {
        while ( sentBytes < bytesToSend )
        {
	   //printf("weilun@adv sentBytes = %d, bytesToSend = %d\n", sentBytes, bytesToSend);

            // read next byte from input...
            if (serf_fd < 0) { //interpreting as string
                byteToSend[0] = serfstr[sentBytes];
            } else { //opened as file 
                read( serf_fd, &byteToSend[0], 1 );
            }

	    //printf("weilun@adv %s(), L:%d\n",__FUNCTION__, __LINE__);

            write_failed = TRUE;
            do
            {
                 //CTRL+C handler
		//weilun@adv - begin
                //if(execute == 0)
                 //   goto show_results;
		//weilun@adv - end

                if ( !writeport( serport_fd, byteToSend ) )
                {
                    write_failed = TRUE;
                    fprintf(stdout, "!WARNING: Write failed.\n");
                } else
                    write_failed = FALSE;

            } while (write_failed == TRUE);

	    //printf("weilun@adv %s(), L:%d\n",__FUNCTION__, __LINE__);


            //weilun@adv - begin
            //while ( wait_flag == TRUE );
            //weilun@adv - end

            // read was interrupted, try to read again
            while ( ((readChars = readport( serport_fd, sResp, 1)) == -1) && (errno == EINTR) )
                fprintf(stdout, "!WARNING: Read was interrupted, readchars = %d, read again.\n", readChars);

            if(strcmp(sResp, byteToSend) != 0)
                fprintf(stdout, "!!!%d of %d, ERROR BYTE: Written: %s Read: %s\n", sentBytes, bytesToSend, byteToSend, sResp);
	    //weilun@adv - begin
	    //else
             //   fprintf(stdout, "Weilun: debug BYTE: Written: %s Read: %s\n", byteToSend, sResp);
	    //weilun@adv - end
            recdBytes += readChars;
            recievedBytes[sentBytes] = *sResp;

	    //printf("weilun@adv %s(), L:%d\n",__FUNCTION__, __LINE__);

	    //weilun@adv - begin
            //wait_flag = TRUE; // was ==
	    //weilun@adv - end
            //~ usleep(50000);
            sentBytes++;
        }

        if ( strcmp(serfstr, recievedBytes) != 0)
        {
            fprintf(stdout, "!!! ERROR STRING: Written: %s Read: %s\n", serfstr, recievedBytes);
            readChars = readport( serport_fd, sResp, 1);

            cyclesFailed++;
        }
        else
            cyclesPass++;

        // CTRL+C handler
	//weilun@adv - begin
        //if(execute == 0)
        //    goto show_results;
	//weilun@adv - end

        sentBytesTotal += sentBytes;
        sentBytes = 0;
    }
show_results:

    //CTRL+C handler
    //weilun@adv - begin
    //signal(SIGINT,SIG_DFL);
    //weilun@adv - end

    gettimeofday( &timeEnd, NULL );

    free(recievedBytes);
    // Close the open port
    close( serport_fd );
    if (!(serf_fd < 0)) close( serf_fd );

    fprintf(stdout, "\n+++DONE+++\n");

    totlBytes = sentBytesTotal + recdBytes;
    timeval_subtract(&timeDelta, &timeEnd, &timeStart);
    deltasec = timeDelta.tv_sec+timeDelta.tv_usec*1e-6;

    fprintf(stdout, "CYCLES PASS: %d FAILED: %d\n", cyclesPass, cyclesFailed);
    fprintf(stdout, "Wrote: %d bytes; Read: %d bytes; Total: %d bytes. \n", sentBytesTotal, recdBytes, totlBytes);
    fprintf(stdout, "Test time: %ld s %ld us. \n", timeDelta.tv_sec, timeDelta.tv_usec);
//    fprintf(stdout, "Start: %ld s %ld us; End: %ld s %ld us; Delta: %ld s %ld us. \n", timeStart.tv_sec, timeStart.tv_usec, timeEnd.tv_sec, timeEnd.tv_usec, timeDelta.tv_sec, timeDelta.tv_usec);
//    fprintf(stdout, "%s baud for 8N1 is %d Bps (bytes/sec).\n", serspeed, atoi(serspeed)/10);
    fprintf(stdout, "Measured: write %.02f Bps, read %.02f Bps, total %.02f Bps.\n", sentBytesTotal/deltasec, recdBytes/deltasec, totlBytes/deltasec);

    return 0;
}
