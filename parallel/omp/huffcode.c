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

#ifdef WIN32
#include <malloc.h>
extern int getopt(int, char**, char*);
extern char* optarg;
#else
#include <unistd.h>
#endif

static unsigned int memory_encode_file(FILE *in, FILE *out,
									   unsigned char **buf, unsigned long sz);
static unsigned int memory_decode_file(FILE *in, FILE *out,
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
	unsigned char *buf[4] = {NULL, NULL, NULL, NULL};
	char memory = 1;
	char compress = 1;
	int opt;
	unsigned int i, cur[4];
	const char *file_in = NULL, *file_out = NULL;
	
	unsigned char* bufout = NULL;
	unsigned int bufoutlen = 0;
	
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

	FILE *fp[4];

	/* If an input file is given then open it
	 * on several positions
	 */
	if(file_in)
	{
		#pragma omp parallel for schedule(dynamic) \
		num_threads(4)
		for (i = 0; i < 4; ++i) {
			fp[i] = fopen(file_in, "rb");
			if(!fp[i])
			{
				fprintf(stderr,
						"Can't open input file '%s': %s\n",
						file_in, strerror(errno));
				exit(1);
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

	fseek(fp[0], 0L, SEEK_END);
	unsigned long sz = (unsigned long)ftell(fp[0]);
	fseek(fp[0], 0L, SEEK_SET);

	#pragma omp parallel for schedule(dynamic) \
	num_threads(4)
	for(i = 0; i < 4; ++i)
	{
		fseek(fp[i], i * (unsigned long) (sz / 4), SEEK_SET);			
	}

	if(memory)
	{
		if (compress) {
			printf("----- COMPRESS -----\n");

			//#pragma omp parallel for
			#pragma omp parallel for schedule(dynamic) \
			num_threads(4)
			for(i = 0; i < 4; ++i) {
				cur[i] = memory_encode_file(fp[i], out, &buf[i], (unsigned long) (sz / 4));
			}

			// Allocate the new full buffer
			int newSize;
			for(i = 0; i < 4; ++i) {
				newSize += strlen(buf[i]);
			}
			++newSize;

			char *scarlat = malloc(newSize);

			strcpy(scarlat, buf[0]);
			
			for (i = 1; i < 4; i++) {
				strcat(scarlat, buf[i]);
			}

			for (i = 0; i < 4; i++) {
				free(buf[i]);
				buf[i] = NULL;
			}

			//fprintf(out, "%s", scarlat);

			/* Encode the memory. */
			for(i = 1; i < 4; i++) {
				cur[0] += cur[i];
			}
			printf("%d %ld\n", cur[0], (unsigned long)(sz));

			fflush(stdout);

			if(huffman_encode_memory(scarlat, cur[0], &bufout, &bufoutlen))
			{
				free(scarlat);
				return 1;
			}

			free(scarlat);

			/* Write the memory to the file. */
			if(fwrite(bufout, 1, bufoutlen, out) != bufoutlen)
			{
				free(bufout);
				return 1;
			}

			free(bufout);
		}
		else {
			printf("----- DECOMPRESS -----\n");
			//#pragma omp parallel for schedule(dynamic) \
			num_threads(4)
			for(i = 0; i < 4; ++i) {
				cur[i] = memory_decode_file(fp[i], out, &buf[i], (unsigned long) (sz / 4));
			}

			/* Encode the memory. */
			for(i = 1; i < 4; i++) {
				cur[0] += cur[i];
			}

			// char *scarlat = malloc(cur[0]);
			// strcpy(scarlat, buf[0]);
			// for (i = 1; i < 4; i++) {
			// 	strcat(scarlat, buf[i]);
			// }
			// fprintf(out, "%s", scarlat);

			for (i = 0; i < 4; ++i) {
				fprintf(out, "%s", buf[i]);
			}

			// for (i = 0; i < 4; i++) {
			// 	free(buf[i]);
			// 	buf[i] = NULL;
			// }

			// fflush(stdout);

			// /* Decode the memory. */
			// if(huffman_decode_memory(scarlat, cur[0], &bufout, &bufoutlen))
			// {
			// 	free(scarlat);
			// 	return 1;
			// }

			// free(scarlat);

			// printf("%s\n", bufout);

			// // Write the memory to the file. 
			// if(fwrite(bufout, 1, bufoutlen, out) != bufoutlen)
			// {
			// 	free(bufout);
			// 	return 1;
			// }

			// free(bufout);
		}

		return 0;
	}
}

static unsigned int
memory_encode_file(FILE *in, FILE *out,
				   unsigned char **buf, unsigned long sz)
{
	unsigned int i, len = 0, cur = 0, inc = 1024;

	assert(in && out);

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
memory_decode_file(FILE *in, FILE *out,
				   unsigned char **buf, unsigned long sz)
{
	unsigned int i, len = 0, cur = 0, inc = 1024;
	assert(in && out);

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
