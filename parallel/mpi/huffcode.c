/*
 *  huffcode - Encode/Decode files using Huffman encoding.
 *  http://huffman.sourceforge.net
 *  Copyright (C) 2003  Douglas Ryan Richardson
 */

#include "huffman.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <mpi.h>

#ifdef WIN32
#include <malloc.h>
extern int getopt(int, char**, char*);
extern char* optarg;
#else
#include <unistd.h>
#endif

#define THREADS 4

static unsigned int memory_encode_read_file(FILE *in,
									   unsigned char **buf, unsigned long sz);
static unsigned int memory_decode_read_file(FILE *in,
									   unsigned char **buf, unsigned long sz);

static void
version(FILE *out)
{
	fputs("huffcode 0.3\n"
	      "Copyright (C) 2003 Douglas Ryan Richardson"
	      "; Gauss Interprise, Inc\n",
	      out);
}

static void
usage(FILE* out)
{
	fputs("Usage: huffcode [-i<input file>] [-o<output file>] [-d|-c]\n"
		  "-i - input file (default is standard input)\n"
		  "-o - output file (default is standard output)\n"
		  "-d - decompress\n"
		  "-c - compress (default)\n"
		  "-m - read file into memory, compress, then write to file (not default)\n",
		  out);
}

int
main(int argc, char** argv)
{
	unsigned char *buf = NULL;
	char memory = 1;
	char compress = 1;
	int opt;
	unsigned int i, j, cur;
	const char *file_in = NULL, *file_out = NULL;
	
	unsigned char* bufout = NULL;
	unsigned int bufoutlen = 0;
	
	int rank = -1, nTasks = -1;

	/* Initialize MPI execution environment */
	MPI_Init (&argc, &argv);
	/* Determines the rank/ID of the current task */
	MPI_Comm_rank (MPI_COMM_WORLD, &rank);
	/* Gives the number of tasks */
	MPI_Comm_size (MPI_COMM_WORLD, &nTasks);

	FILE *out = stdout;

	/* Get the command line arguments. */
	while((opt = getopt(argc, argv, "i:o:cdhvm")) != -1)
	{
		switch(opt)
		{
		case 'i':
			file_in = optarg;
			break;
		case 'o':
			file_out = optarg;
			break;
		case 'c':
			compress = 1;
			break;
		case 'd':
			compress = 0;
			break;
		case 'h':
			usage(stdout);
			return 0;
		case 'v':
			version(stdout);
			return 0;
		default:
			usage(stderr);
			return 1;
		}
	}

	FILE *fp;

	/* If an input file is given then open it
	 * on several positions
	 */
	if(file_in)
	{
			fp = fopen(file_in, "rb");
			if(!fp)
			{
				fprintf(stderr,
						"Can't open input file '%s': %s\n",
						file_in, strerror(errno));
				exit(1);
			}
	}

	/* If an output file is given then create it. */
	if(file_out)
	{
		out = fopen(file_out, "wb");
		if(!out)
		{
			fprintf(stderr,
					"Can't open output file '%s': %s\n",
					file_out, strerror(errno));
			return 1;
		}
	}

	/**
	 * Get file size
	 */
	unsigned long sz = 0;
	int to_read[nTasks];
	int displs[nTasks];

	if (rank == 0) {
		fseek(fp, 0L, SEEK_END);
		sz = (unsigned long)ftell(fp);
		fseek(fp, 0L, SEEK_SET);
	}

	/**
	 * Sending the size to read
	 */
	MPI_Bcast (&sz, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);

	for (i = 0; i < nTasks; ++i) {
		if (i == nTasks - 1) {
			to_read[nTasks - 1] = sz - (nTasks - 1) * (sz / nTasks);
		} else {
			to_read[i] = sz / nTasks;
		}
		
		displs[i] = 0;

		for (j = 0; j < i; ++j) {
			displs[i] += to_read[j];
		}
	}
	
	/**
	 * Increment each file pointer to its specific chunk size
	 */
	fseek(fp, rank * (unsigned long) (sz / nTasks), SEEK_SET);			
	
	if(memory)
	{
		if (compress) {

			cur = memory_encode_read_file(fp, &buf, to_read[rank]);
			printf("rank: %d, to_read: %d\n", rank, to_read[rank]);
			/**
			 * Copy the contents of all 
			 * partial buffers into one
			 */

			char *text;

			text = malloc(sz * sizeof(char));					

			MPI_Gatherv(buf, to_read[rank], MPI_CHAR, text, to_read, displs, MPI_CHAR, 0, MPI_COMM_WORLD);

			MPI_Bcast (text, sz, MPI_CHAR, 0, MPI_COMM_WORLD);

			/**
			 * Do actual huffman algorithm
			 * TODO - add 1 thread to write to memory the table
			 *		- add 4 threads to write to memory their segments of content
			 */
			
			if(huffman_encode_memory(text, sz, &bufout, &bufoutlen, MPI_COMM_WORLD))
			{
				free(text);
				return 1;
			}

			// free(scarlat);

			if (rank == 0) {
				// Write the memory to the file. 
				if(fwrite(bufout, 1, bufoutlen, out) != bufoutlen)
				{
					free(bufout);
					return 1;
				}

				free(bufout);
			}
		}
		else {
			// int a, pos = 0;
			// unsigned long size = sz / THREADS;

			// #pragma omp parallel for schedule(dynamic) \
			// num_threads(THREADS)
			// for(i = 0; i < THREADS; ++i) {
			// 	if (i == THREADS - 1) {
			// 		size = sz - (THREADS - 1) * size;
			// 	}
			// 	cur[i] = memory_decode_read_file(fp[i], &buf[i], size);
			// }

			// unsigned int sum = 0;
			// for(i = 0; i < THREADS; i++) {
			// 	sum += cur[i];
			// }

			// char *scarlat = malloc(sum * sizeof(char));

			// for (i = 0; i <	THREADS; ++i) {
			// 	memcpy(scarlat + pos, buf[i], cur[i]);
			// 	pos += cur[i];
			// }

			// // for (i = 0; i < THREADS; i++) {
			// // 	free(buf[i]);
			// // 	buf[i] = NULL;
			// // }

			// /* Decode the memory. */
			// if(huffman_decode_memory(scarlat, sum, &bufout, &bufoutlen))
			// {
			// 	free(scarlat);
			// 	return 1;
			// }

			// free(scarlat);

			// // Write the memory to the file. 
			// if(fwrite(bufout, 1, bufoutlen, out) != bufoutlen)
			// {
			// 	free(bufout);
			// 	return 1;
			// }

			// free(bufout);
		}

		MPI_Finalize();

		return 0;
	}
}

static unsigned int
memory_encode_read_file(FILE *in,
				   unsigned char **buf, unsigned long sz)
{
	unsigned int i, len = 0, cur = 0, inc = 1024;

	assert(in);

	/* Read the file into memory. */
	for(i = 0; i < (unsigned int)sz; i += inc)
	{
		//printf("%d\n", omp_get_thread_num());
		unsigned char *tmp;
		len += inc;
		tmp = (unsigned char*)realloc(*buf, len);
		if(!tmp)
		{
			if(*buf)
				free(buf);
			return -1;
		}

		*buf = tmp;
		if(cur + inc > sz) {
			cur += fread(*buf + cur, 1, (unsigned int)(sz - cur), in);
		} 
		else {
			cur += fread(*buf + cur, 1, inc, in);
		}
	}

	if(NULL != *buf) {
		return cur;
	}
	return -1;
}

static unsigned int
memory_decode_read_file(FILE *in,
				   unsigned char **buf, unsigned long sz)
{
	unsigned int i, len = 0, cur = 0, inc = 1024;
	assert(in);

	/* Read the file into memory. */
	for (i = 0; i < (unsigned int)sz; i+=inc)
	{
		unsigned char *tmp;
		len += inc;
		tmp = (unsigned char*)realloc(*buf, len);
		if(!tmp)
		{
			if(*buf) {
				free(*buf);
			}
			return 1;
		}

		*buf = tmp;
		if(cur + inc > sz) {
			cur += fread(*buf + cur, 1, (unsigned int)(sz - cur), in);
		} 
		else {
			cur += fread(*buf + cur, 1, inc, in);
		}
	}

	if(NULL != *buf) {
		return cur;
	}
	return -1;
}
