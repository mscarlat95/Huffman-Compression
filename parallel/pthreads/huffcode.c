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
#include <pthread.h>

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

struct open_files
{
  FILE *fp;
  char *file_in;
};

struct fseek_files
{
  FILE *fp;
  unsigned long sz;
  unsigned int pos;
};

struct code_struct
{
  FILE *fp;
  unsigned int sz;
  unsigned char **buf;
  unsigned int cur;

};

void *thread_open_file(void *arguments)
{
	unsigned int i;
	struct open_files *args = (struct open_files *)arguments;
	args -> fp = fopen(args -> file_in, "rb");
	if( !(args -> fp) )
	{
		fprintf(stderr,
				"Can't open input file '%s': %s\n",
				args -> file_in, strerror(errno));
		exit(1);
	}
}

void *thread_fseek_file(void *arguments)
{

	struct fseek_files *args = (struct fseek_files *)arguments;	

	fseek(args -> fp, args -> pos * (unsigned long) (args -> sz / THREADS), SEEK_SET);
}

void *thread_encode_file(void *arguments)
{
	struct code_struct *args = (struct code_struct *)arguments;
	
	args -> cur = memory_encode_read_file(args -> fp, args -> buf,
				  (unsigned long) (args -> sz / THREADS));
}

void *thread_decode_file(void *arguments)
{
	struct code_struct *args = (struct code_struct *)arguments;

	args -> cur = memory_decode_read_file(args -> fp, args -> buf, args -> sz);
}

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
	unsigned char *buf[THREADS] = {NULL, NULL, NULL, NULL};
	char memory = 1;
	char compress = 1;
	int opt;
	unsigned int i;
    char *file_in = NULL, *file_out = NULL;
	
	unsigned char* bufout = NULL;
	unsigned int bufoutlen = 0;

	struct open_files arguments_1[THREADS];
	struct fseek_files arguments_2[THREADS];
	struct code_struct arguments_3[THREADS];

	pthread_t threads[THREADS] = {0};
	
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

	FILE *fp[THREADS];

	/* If an input file is given then open it
	 * on several positions
	 */
	if(file_in)
	{
		for(i = 0; i < THREADS; ++i) {
			arguments_1[i].fp = fp[i];
			arguments_1[i].file_in = file_in;

			if ( pthread_create(&threads[i], NULL, thread_open_file, (void *)&arguments_1[i]) ) {
	         fprintf(stderr, "Error creating threads\n");
	         return -1;
	        }
	    }

	    for(i = 0; i < THREADS; i++) {
	        // wait for the threads to finish the initialisation
	        if ( pthread_join(threads[i], NULL) ) {
	          fprintf(stderr, "Error joining threads\n");
	          return -1;
	        }
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
	fseek(arguments_1[0].fp, 0L, SEEK_END);
	unsigned long sz = (unsigned long)ftell(arguments_1[0].fp);
	fseek(arguments_1[0].fp, 0L, SEEK_SET);

	/**
	 * Increment each file pointer to its specific chunk size
	 */
	for(i = 0; i < THREADS; ++i)
	{
		arguments_2[i].fp = arguments_1[i].fp;
		arguments_2[i].sz = sz;
		arguments_2[i].pos = i;
		
		if ( pthread_create(&threads[i], NULL, thread_fseek_file, (void *)&arguments_2[i]) ) {
	         fprintf(stderr, "Error creating threads\n");
	         return -1;
	    }
	}

	for(i = 0; i < THREADS; ++i)
	{
		if ( pthread_join(threads[i], NULL) ) {
	          fprintf(stderr, "Error joining threads\n");
	          return -1;
	    }
	}

	if(memory)
	{
		if (compress) {
			/**
			 * Read file from disk in parallel
			 */
			for(i = 0; i < THREADS; ++i) {

				arguments_3[i].fp = arguments_1[i].fp;
				arguments_3[i].sz = sz;
				arguments_3[i].buf = &buf[i];
				
				if ( pthread_create(&threads[i], NULL, thread_encode_file, (void *)&arguments_3[i]) ) {
	         		fprintf(stderr, "Error creating threads\n");
	         		return -1;
	        	}
			}

			for(i = 0; i < THREADS; ++i) {
				if ( pthread_join(threads[i], NULL) ) {
	          		fprintf(stderr, "Error joining threads\n");
	          		return -1;
	    		}
			}

			// Allocate the new full buffer
			int newSize = 0;
			for(i = 0; i < THREADS; ++i) {
				newSize += strlen(buf[i]);
			}

			/**
			 * Copy the contents of all 
			 * partial buffers into one
			 */
			char *text = malloc(newSize * sizeof(char));

			strcpy(text, buf[0]);
			
			for (i = 1; i < THREADS; ++i) {
				strcat(text, buf[i]);
			}

			/**
			 * Do actual huffman algorithm
			 * TODO - add 1 thread to write to memory the table
			 *		- add 4 threads to write to memory their segments of content
			 */
			if(huffman_encode_memory(text, newSize, &bufout, &bufoutlen))
			{
				free(text);
				return 1;
			}

			free(text);

			/* Write the memory to the file. */
			if(fwrite(bufout, 1, bufoutlen, out) != bufoutlen)
			{
				free(bufout);
				return 1;
			}

			free(bufout);
		}
		else {
			int a, pos = 0;
			unsigned long size = sz / THREADS;

			for(i = 0; i < THREADS; ++i) {

				arguments_3[i].fp = arguments_1[i].fp;
				arguments_3[i].buf = &buf[i];

				if (i == THREADS - 1) {
					arguments_3[i].sz = sz - (THREADS - 1) * size;
				}
				else {
					arguments_3[i].sz = size;
				}
			
				if ( pthread_create(&threads[i], NULL, thread_decode_file, (void *)&arguments_3[i]) ) {
	         		fprintf(stderr, "Error creating threads\n");
	         		return -1;
	        	}
			}

			for(i = 0; i < THREADS; ++i) {
				if ( pthread_join(threads[i], NULL) ) {
	          		fprintf(stderr, "Error joining threads\n");
	          		return -1;
	    		}
			}

			unsigned int sum = 0;
			for(i = 0; i < THREADS; i++) {
				sum += arguments_3[i].cur;
			}

			char *text = malloc(sum * sizeof(char));

			for (i = 0; i <	THREADS; ++i) {
				memcpy(text + pos, buf[i], arguments_3[i].cur);
				pos += arguments_3[i].cur;
			}

			/* Decode the memory. */
			if(huffman_decode_memory(text, sum, &bufout, &bufoutlen))
			{
				free(text);
				return 1;
			}

			free(text);

			// Write the memory to the file. 
			if(fwrite(bufout, 1, bufoutlen, out) != bufoutlen)
			{
				free(bufout);
				return 1;
			}

			free(bufout);
		}

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
