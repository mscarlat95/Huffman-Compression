/*
 *  huffman - Encode/Decode files using Huffman encoding.
 *  http://huffman.sourceforge.net
 *  Copyright (C) 2003  Douglas Ryan Richardson
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "huffman.h"

#ifdef WIN32
#include <winsock2.h>
#include <malloc.h>
#define alloca _alloca
#else
#include <netinet/in.h>
#endif

typedef struct huffman_node_tag
{
	unsigned char isLeaf;
	unsigned long count;
	struct huffman_node_tag *parent;

	union
	{
		struct
		{
			struct huffman_node_tag *zero, *one;
		};
		unsigned char symbol;
	};
} huffman_node;

typedef struct huffman_code_tag
{
	/* The length of this code in bits. */
	unsigned long numbits;

	/* The bits that make up this code. The first
	   bit is at position 0 in bits[0]. The second
	   bit is at position 1 in bits[0]. The eighth
	   bit is at position 7 in bits[0]. The ninth
	   bit is at position 0 in bits[1]. */
	unsigned char *bits;
} huffman_code;

static unsigned long
numbytes_from_numbits(unsigned long numbits)
{
	return numbits / 8 + (numbits % 8 ? 1 : 0);
}

/*
 * get_bit returns the ith bit in the bits array
 * in the 0th position of the return value.
 */
static unsigned char
get_bit(unsigned char* bits, unsigned long i)
{
	return (bits[i / 8] >> i % 8) & 1;
}

static void
reverse_bits(unsigned char* bits, unsigned long numbits)
{
	unsigned long numbytes = numbytes_from_numbits(numbits);
	unsigned char *tmp =
	    (unsigned char*)alloca(numbytes);
	unsigned long curbit;
	long curbyte = 0;
	
	memset(tmp, 0, numbytes);

	for(curbit = 0; curbit < numbits; ++curbit)
	{
		unsigned int bitpos = curbit % 8;

		if(curbit > 0 && curbit % 8 == 0)
			++curbyte;
		
		tmp[curbyte] |= (get_bit(bits, numbits - curbit - 1) << bitpos);
	}

	memcpy(bits, tmp, numbytes);
}

/*
 * new_code builds a huffman_code from a leaf in
 * a Huffman tree.
 */
static huffman_code*
new_code(const huffman_node* leaf)
{
	/* Build the huffman code by walking up to
	 * the root node and then reversing the bits,
	 * since the Huffman code is calculated by
	 * walking down the tree. */
	unsigned long numbits = 0;
	unsigned char* bits = NULL;
	huffman_code *p;

	while(leaf && leaf->parent)
	{
		huffman_node *parent = leaf->parent;
		unsigned char cur_bit = (unsigned char)(numbits % 8);
		unsigned long cur_byte = numbits / 8;

		/* If we need another byte to hold the code,
		   then allocate it. */
		if(cur_bit == 0)
		{
			size_t newSize = cur_byte + 1;
			bits = (unsigned char*)realloc(bits, newSize);
			bits[newSize - 1] = 0; /* Initialize the new byte. */
		}

		/* If a one must be added then or it in. If a zero
		 * must be added then do nothing, since the byte
		 * was initialized to zero. */
		if(leaf == parent->one)
			bits[cur_byte] |= 1 << cur_bit;

		++numbits;
		leaf = parent;
	}

	if(bits)
		reverse_bits(bits, numbits);

	p = (huffman_code*)malloc(sizeof(huffman_code));
	p->numbits = numbits;
	p->bits = bits;
	return p;
}

#define MAX_SYMBOLS 256
typedef huffman_node* SymbolFrequencies[MAX_SYMBOLS];
typedef huffman_code* SymbolEncoder[MAX_SYMBOLS];

static huffman_node*
new_leaf_node(unsigned char symbol)
{
	huffman_node *p = (huffman_node*)malloc(sizeof(huffman_node));
	p->isLeaf = 1;
	p->symbol = symbol;
	p->count = 0;
	p->parent = 0;
	return p;
}

static huffman_node*
new_nonleaf_node(unsigned long count, huffman_node *zero, huffman_node *one)
{
	huffman_node *p = (huffman_node*)malloc(sizeof(huffman_node));
	p->isLeaf = 0;
	p->count = count;
	p->zero = zero;
	p->one = one;
	p->parent = 0;
	
	return p;
}

static void
free_huffman_tree(huffman_node *subtree)
{
	if(subtree == NULL)
		return;

	if(!subtree->isLeaf)
	{
		free_huffman_tree(subtree->zero);
		free_huffman_tree(subtree->one);
	}
	
	free(subtree);
}

static void
free_code(huffman_code* p)
{
	free(p->bits);
	free(p);
}

static void
free_encoder(SymbolEncoder *pSE)
{
	unsigned long i;
	for(i = 0; i < MAX_SYMBOLS; ++i)
	{
		huffman_code *p = (*pSE)[i];
		if(p)
			free_code(p);
	}

	free(pSE);
}

static void
init_frequencies(SymbolFrequencies *pSF)
{
	memset(*pSF, 0, sizeof(SymbolFrequencies));
}

typedef struct buf_cache_tag
{
	unsigned char *cache;
	unsigned int cache_len;
	unsigned int cache_cur;
	unsigned char **pbufout;
	unsigned int *pbufoutlen;
} buf_cache;

static int init_cache(buf_cache* pc,
					  unsigned int cache_size,
					  unsigned char **pbufout,
					  unsigned int *pbufoutlen)
{
	assert(pc && pbufout && pbufoutlen);
	if(!pbufout || !pbufoutlen)
		return 1;
	
	pc->cache = (unsigned char*)malloc(cache_size);
	pc->cache_len = cache_size;
	pc->cache_cur = 0;
	pc->pbufout = pbufout;
	*pbufout = NULL;
	pc->pbufoutlen = pbufoutlen;
	*pbufoutlen = 0;

	return pc->cache ? 0 : 1;
}

static void free_cache(buf_cache* pc)
{
	assert(pc);
	if(pc->cache)
	{
		free(pc->cache);
		pc->cache = NULL;
	}
}

static int flush_cache(buf_cache* pc)
{
	assert(pc);
	
	if(pc->cache_cur > 0)
	{
		unsigned int newlen = pc->cache_cur + *pc->pbufoutlen;
		unsigned char* tmp = realloc(*pc->pbufout, newlen);
		if(!tmp)
			return 1;

		memcpy(tmp + *pc->pbufoutlen, pc->cache, pc->cache_cur);

		*pc->pbufout = tmp;
		*pc->pbufoutlen = newlen;
		pc->cache_cur = 0;
	}

	return 0;
}

static int write_cache(buf_cache* pc,
					   const void *to_write,
					   unsigned int to_write_len)
{
	unsigned char* tmp;

	assert(pc && to_write);
	assert(pc->cache_len >= pc->cache_cur);
	
	/* If trying to write more than the cache will hold
	 * flush the cache and allocate enough space immediately,
	 * that is, don't use the cache. */
	if(to_write_len > pc->cache_len - pc->cache_cur)
	{
		unsigned int newlen;
		flush_cache(pc);
		newlen = *pc->pbufoutlen + to_write_len;
		tmp = realloc(*pc->pbufout, newlen);
		if(!tmp)
			return 1;
		memcpy(tmp + *pc->pbufoutlen, to_write, to_write_len);
		*pc->pbufout = tmp;
		*pc->pbufoutlen = newlen;
	}
	else
	{
		/* Write the data to the cache. */
		memcpy(pc->cache + pc->cache_cur, to_write, to_write_len);
		pc->cache_cur += to_write_len;
	}

	return 0;
}

static unsigned int
get_symbol_frequencies_from_memory(SymbolFrequencies *pSF,
								   const unsigned char *bufin,
								   unsigned int bufinlen)
{
	unsigned int i;
	
	/* Set all frequencies to 0. */
	init_frequencies(pSF);
	
	/* Count the frequency of each symbol in the input file. */
	for(i = 0; i < bufinlen; ++i)
	{
		unsigned char uc = bufin[i];
		if(!(*pSF)[uc])
			(*pSF)[uc] = new_leaf_node(uc);
		++(*pSF)[uc]->count;
	}

	return bufinlen;
}

/*
 * When used by qsort, SFComp sorts the array so that
 * the symbol with the lowest frequency is first. Any
 * NULL entries will be sorted to the end of the list.
 */
static int
SFComp(const void *p1, const void *p2)
{
	const huffman_node *hn1 = *(const huffman_node**)p1;
	const huffman_node *hn2 = *(const huffman_node**)p2;

	/* Sort all NULLs to the end. */
	if(hn1 == NULL && hn2 == NULL)
		return 0;
	if(hn1 == NULL)
		return 1;
	if(hn2 == NULL)
		return -1;
	
	if(hn1->count > hn2->count)
		return 1;
	else if(hn1->count < hn2->count)
		return -1;

	return 0;
}


/*
 * build_symbol_encoder builds a SymbolEncoder by walking
 * down to the leaves of the Huffman tree and then,
 * for each leaf, determines its code.
 */
static void
build_symbol_encoder(huffman_node *subtree, SymbolEncoder *pSF)
{
	if(subtree == NULL)
		return;

	if(subtree->isLeaf)
		(*pSF)[subtree->symbol] = new_code(subtree);
	else
	{
		build_symbol_encoder(subtree->zero, pSF);
		build_symbol_encoder(subtree->one, pSF);
	}
}

/*
 * calculate_huffman_codes turns pSF into an array
 * with a single entry that is the root of the
 * huffman tree. The return value is a SymbolEncoder,
 * which is an array of huffman codes index by symbol value.
 */
static SymbolEncoder*
calculate_huffman_codes(SymbolFrequencies * pSF)
{
	unsigned int i = 0;
	unsigned int n = 0;
	huffman_node *m1 = NULL, *m2 = NULL;
	SymbolEncoder *pSE = NULL;

	/* Sort the symbol frequency array by ascending frequency. */
	qsort((*pSF), MAX_SYMBOLS, sizeof((*pSF)[0]), SFComp);

	/* Get the number of symbols. */
	for(n = 0; n < MAX_SYMBOLS && (*pSF)[n]; ++n)
		;

	/*
	 * Construct a Huffman tree. This code is based
	 * on the algorithm given in Managing Gigabytes
	 * by Ian Witten et al, 2nd edition, page 34.
	 * Note that this implementation uses a simple
	 * count instead of probability.
	 */
	for(i = 0; i < n - 1; ++i)
	{
		/* Set m1 and m2 to the two subsets of least probability. */
		m1 = (*pSF)[0];
		m2 = (*pSF)[1];

		/* Replace m1 and m2 with a set {m1, m2} whose probability
		 * is the sum of that of m1 and m2. */
		(*pSF)[0] = m1->parent = m2->parent =
			new_nonleaf_node(m1->count + m2->count, m1, m2);
		(*pSF)[1] = NULL;
		
		/* Put newSet into the correct count position in pSF. */
		qsort((*pSF), n, sizeof((*pSF)[0]), SFComp);
	}

	/* Build the SymbolEncoder array from the tree. */
	pSE = (SymbolEncoder*)malloc(sizeof(SymbolEncoder));
	memset(pSE, 0, sizeof(SymbolEncoder));
	build_symbol_encoder((*pSF)[0], pSE);
	return pSE;
}


/*
 * Allocates memory and sets *pbufout to point to it. The memory
 * contains the code table.
 */
static int
write_code_table_to_memory(buf_cache *pc,
						   SymbolEncoder *se,
						   uint32_t symbol_count)
{
	uint32_t i, count = 0;

	/* Determine the number of entries in se. */
	for(i = 0; i < MAX_SYMBOLS; ++i)
	{
		if((*se)[i])
			++count;
	}

	/* Write the number of entries in network byte order. */
	i = htonl(count);
	
	if(write_cache(pc, &i, sizeof(i)))
		return 1;

	/* Write the number of bytes that will be encoded. */
	symbol_count = htonl(symbol_count);
	if(write_cache(pc, &symbol_count, sizeof(symbol_count)))
		return 1;

	/* Write the entries. */
	for(i = 0; i < MAX_SYMBOLS; ++i)
	{
		huffman_code *p = (*se)[i];
		if(p)
		{
			unsigned int numbytes;
			/* The value of i is < MAX_SYMBOLS (256), so it can
			be stored in an unsigned char. */
			unsigned char uc = (unsigned char)i;
			/* Write the 1 byte symbol. */
			if(write_cache(pc, &uc, sizeof(uc)))
				return 1;
			/* Write the 1 byte code bit length. */
			uc = (unsigned char)p->numbits;
			if(write_cache(pc, &uc, sizeof(uc)))
				return 1;
			/* Write the code bytes. */
			numbytes = numbytes_from_numbits(p->numbits);
			if(write_cache(pc, p->bits, numbytes))
				return 1;
		}
	}

	return 0;
}

static int
memread(const unsigned char* buf,
		unsigned int buflen,
		unsigned int *pindex,
		void* bufout,
		unsigned int readlen)
{
	assert(buf && pindex && bufout);
	assert(buflen >= *pindex);
	if(buflen < *pindex)
		return 1;
	if(readlen + *pindex >= buflen)
		return 1;
	memcpy(bufout, buf + *pindex, readlen);
	*pindex += readlen;
	return 0;
}

static huffman_node*
read_code_table_from_memory(const unsigned char* bufin,
							unsigned int bufinlen,
							unsigned int *pindex,
							uint32_t *pDataBytes)
{
	huffman_node *root = new_nonleaf_node(0, NULL, NULL);
	uint32_t count;
	
	/* Read the number of entries.
	   (it is stored in network byte order). */
	if(memread(bufin, bufinlen, pindex, &count, sizeof(count)))
	{
		free_huffman_tree(root);
		return NULL;
	}

	count = ntohl(count);

	/* Read the number of data bytes this encoding represents. */
	if(memread(bufin, bufinlen, pindex, pDataBytes, sizeof(*pDataBytes)))
	{
		free_huffman_tree(root);
		return NULL;
	}

	*pDataBytes = ntohl(*pDataBytes);

	/* Read the entries. */
	while(count-- > 0)
	{
		unsigned int curbit;
		unsigned char symbol;
		unsigned char numbits;
		unsigned char numbytes;
		unsigned char *bytes;
		huffman_node *p = root;

		if(memread(bufin, bufinlen, pindex, &symbol, sizeof(symbol)))
		{
			free_huffman_tree(root);
			return NULL;
		}

		if(memread(bufin, bufinlen, pindex, &numbits, sizeof(numbits)))
		{
			free_huffman_tree(root);
			return NULL;
		}
		
		numbytes = (unsigned char)numbytes_from_numbits(numbits);
		bytes = (unsigned char*)malloc(numbytes);
		if(memread(bufin, bufinlen, pindex, bytes, numbytes))
		{
			free(bytes);
			free_huffman_tree(root);
			return NULL;
		}

		/*
		 * Add the entry to the Huffman tree. The value
		 * of the current bit is used switch between
		 * zero and one child nodes in the tree. New nodes
		 * are added as needed in the tree.
		 */
		for(curbit = 0; curbit < numbits; ++curbit)
		{
			if(get_bit(bytes, curbit))
			{
				if(p->one == NULL)
				{
					p->one = curbit == (unsigned char)(numbits - 1)
						? new_leaf_node(symbol)
						: new_nonleaf_node(0, NULL, NULL);
					p->one->parent = p;
				}
				p = p->one;
			}
			else
			{
				if(p->zero == NULL)
				{
					p->zero = curbit == (unsigned char)(numbits - 1)
						? new_leaf_node(symbol)
						: new_nonleaf_node(0, NULL, NULL);
					p->zero->parent = p;
				}
				p = p->zero;
			}
		}
		
		free(bytes);
	}

	return root;
}

static int
do_memory_encode(buf_cache *pc,
				 const unsigned char* bufin,
				 unsigned int bufinlen,
				 SymbolEncoder *se)
{
	unsigned char curbyte = 0;
	unsigned char curbit = 0;
	unsigned int i;
	
	for(i = 0; i < bufinlen; ++i)
	{
		unsigned char uc = bufin[i];
		huffman_code *code = (*se)[uc];
		unsigned long i;
		
		for(i = 0; i < code->numbits; ++i)
		{
			/* Add the current bit to curbyte. */
			curbyte |= get_bit(code->bits, i) << curbit;

			/* If this byte is filled up then write it
			 * out and reset the curbit and curbyte. */
			if(++curbit == 8)
			{
				if(write_cache(pc, &curbyte, sizeof(curbyte)))
					return 1;
				curbyte = 0;
				curbit = 0;
			}
		}
	}

	/*
	 * If there is data in curbyte that has not been
	 * output yet, which means that the last encoded
	 * character did not fall on a byte boundary,
	 * then output it.
	 */
	curbit > 0 ? write_cache(pc, &curbyte, sizeof(curbyte)) : 0;
	
	return (8 - curbit) % 8;
}

unsigned int merge_buffers(unsigned char **output,
						   unsigned char **bufout_piece,
						   unsigned int *bufout_piece_len,
						   unsigned int *zeros,
						   int nTasks)
{

	int i;
	unsigned int size = 0;
	unsigned int cur_len = 0;

	unsigned char sel_mask[9];

	/**
	 * Little Endian
	sel_mask[0] = 0x00;	sel_mask[1] = 0x80;
	sel_mask[2] = 0xc0;	sel_mask[3] = 0xe0;
	sel_mask[4] = 0xf0;	sel_mask[5] = 0xf8;
	sel_mask[6] = 0xfc;	sel_mask[7] = 0xfe;
	sel_mask[8] = 0xff; 
	*/

	// Big Endian
	sel_mask[0] = 0x00;	sel_mask[1] = 0x01;
	sel_mask[2] = 0x03;	sel_mask[3] = 0x07;
	sel_mask[4] = 0x0f;	sel_mask[5] = 0x1f;
	sel_mask[6] = 0x3f;	sel_mask[7] = 0x7f;
	sel_mask[8] = 0xff;

	for (i = 0; i < nTasks; ++i) {
		size += bufout_piece_len[i];
	}

	*output = calloc(size, sizeof(unsigned char));

	// if (bufout_piece == NULL || bufout_piece[0] == NULL) {
	// 	printf("[merge_buffers]\tbufout_piece is NULL\n");
	// 	return 0;
	// }

	// printf("bufout_piece_len[0] = %d\n", bufout_piece_len[0]);
	// printf("bufout_piece[0] = %s\n", bufout_piece[0]);
	// return 0;


	memcpy(*output, bufout_piece[0], bufout_piece_len[0]);
	cur_len += bufout_piece_len[0];

	unsigned char bits;
	unsigned int padding;
	int kk;

	for (kk = 1; kk < nTasks; ++kk) {
		if (zeros[kk - 1] != 0) { 
			bits = sel_mask[zeros[kk-1]] & bufout_piece[kk][0];

			(*output)[cur_len - 1] |= (bits << (8 - zeros[kk - 1]));

			for (i = 0; i < bufout_piece_len[kk]; ++i) {
				if (i == (bufout_piece_len[kk] - 1)) {
					if (zeros[kk - 1] + zeros[kk] >= 8) {
						bufout_piece_len[kk]--;
						zeros[kk] = zeros[kk - 1] + zeros[kk] - 8;
					} else {			
						bufout_piece[kk][i] = bufout_piece[kk][i] >> zeros[kk - 1];
						zeros[kk] += zeros[kk - 1];
					}
				} else {
					padding = zeros[kk - 1];
					bufout_piece[kk][i] >>= padding;
					bits = sel_mask[padding] & bufout_piece[kk][i + 1];
					bufout_piece[kk][i] |= (bits << (8 - padding));
				}
			}
		}
		
		memcpy(*output + cur_len, bufout_piece[kk], bufout_piece_len[kk]);
		cur_len += bufout_piece_len[kk];
	}

	return cur_len;
}

#define CACHE_SIZE 1024

int huffman_encode_memory(const unsigned char *bufin,
						  unsigned int bufinlen,
						  unsigned char **pbufout,
						  unsigned int *pbufoutlen,
						  int rank,
						  int nTasks,
						  MPI_Comm communicator)
{

	//int rank = -1;
	//int nTasks = -1;

	// MPI_Comm_rank (communicator, &rank);
	//  Gives the number of tasks 
	// MPI_Comm_size (communicator, &nTasks);

	SymbolFrequencies sf;
	SymbolEncoder *se;
	huffman_node *root = NULL;
	int rc = 0, i;
	unsigned int symbol_count;
	MPI_Status status;
	buf_cache cache;
	
	buf_cache cache_proc;
	
	unsigned char* _bufout_local;
	unsigned char* _bufout_root[nTasks];

	unsigned int _bufoutlen_local;
	unsigned int _bufoutlen_root[nTasks];
	
	unsigned int remains_local;
	unsigned int remains_root[nTasks];

	if (rank == 0) {
		/* Ensure the arguments are valid. */
		if(!pbufout || !pbufoutlen)
			return 1;

		if(init_cache(&cache, CACHE_SIZE, pbufout, pbufoutlen))
			return 1;
	}

	/**
	 * Init cache for every MPI process
	 */
	_bufout_local = NULL;
	_bufoutlen_local = 0;
	init_cache(&cache_proc, CACHE_SIZE, &_bufout_local, &_bufoutlen_local);

	/* Get the frequency of each symbol in the input memory. */
	symbol_count = get_symbol_frequencies_from_memory(&sf, bufin, bufinlen);

	/* Build an optimal table from the symbolCount. */
	se = calculate_huffman_codes(&sf);
	//root = sf[0];

	//printf("rank: %d, %s, %d\n", rank, bufin, bufinlen);
	if (rank == 0) {
		/* Scan the memory again and, using the table
		   previously built, encode it into the output memory. */
		write_code_table_to_memory(&cache, se, symbol_count);
		flush_cache(&cache);
	}

	remains_local = do_memory_encode(&cache_proc, bufin + (rank * bufinlen / nTasks), bufinlen / nTasks, se);
	flush_cache(&cache_proc);

	if (rank != 0) {		
		MPI_Send(
	    		&_bufoutlen_local,
	    		1,
	    		MPI_UNSIGNED,
	    		0,
	    		13,
	    		communicator);

		MPI_Send(
	    		_bufout_local,
	    		_bufoutlen_local,
	    		MPI_CHAR,
	    		0,
	    		14,
	    		communicator);

		MPI_Send(
	    		&remains_local,
	    		1,
	    		MPI_UNSIGNED,
	    		0,
	    		15,
	    		communicator);
	}

	if (rank == 0) {
		
		unsigned int tmp_size = *pbufoutlen;

		_bufoutlen_root[0] = _bufoutlen_local;
		_bufout_root[0] = malloc(_bufoutlen_root[0] * sizeof(char));
		
		memcpy(_bufout_root[0], _bufout_local, _bufoutlen_root[0]);
		
		remains_root[0] = remains_local;

		for (i = 1; i < nTasks; ++i) {
			
			MPI_Recv(
    			&_bufoutlen_root[i],
    			1,
			    MPI_UNSIGNED,
			    i,
			    13,
			    communicator,
			    &status);

			_bufout_root[i] = malloc(_bufoutlen_root[i] * sizeof(char));

			MPI_Recv(
				_bufout_root[i],
    			_bufoutlen_root[i],
			    MPI_CHAR,
			    i,
			    14,
			    communicator,
			    &status);

			MPI_Recv(
    			&remains_root[i],
    			1,
			    MPI_UNSIGNED,
			    i,
			    15,
			    communicator,
			    &status);

			tmp_size += _bufoutlen_root[i];
		}

		// unsigned char *tmp = calloc (tmp_size, sizeof(char));
		// memcpy (tmp, *pbufout, sizeof(*pbufout));
		
		unsigned char *tmp = realloc(*pbufout, tmp_size * sizeof(char));

		unsigned char *aux = NULL;
		unsigned int res = merge_buffers(&aux, _bufout_root, _bufoutlen_root, remains_root, nTasks);

		memcpy(tmp + *pbufoutlen, aux, res);

		*pbufout = tmp;
		*pbufoutlen += res;

		free_cache(&cache);
	}
	
	/* Free the Huffman tree. */
	free_huffman_tree(root);
	free_encoder(se);
	free_cache(&cache_proc);
	return 0;
}

int huffman_decode_memory(const unsigned char *bufin,
						  unsigned int bufinlen,
						  unsigned char **pbufout,
						  unsigned int *pbufoutlen)
{
	huffman_node *root, *p;
	unsigned int data_count;
	unsigned int i = 0;
	unsigned char *buf;
	unsigned int bufcur = 0;

	/* Ensure the arguments are valid. */
	if(!pbufout || !pbufoutlen) {
		return 1;
	}

	/* Read the Huffman code table. */
	root = read_code_table_from_memory(bufin, bufinlen, &i, &data_count);
	if(!root) {
		return 1;
	}

	buf = (unsigned char*)malloc(data_count);

	/* Decode the memory. */
	p = root;
	for(; i < bufinlen && data_count > 0; ++i) 
	{
		unsigned char byte = bufin[i];
		unsigned char mask = 1;
		while(data_count > 0 && mask)
		{
			p = byte & mask ? p->one : p->zero;
			mask <<= 1;

			if(p->isLeaf)
			{
				buf[bufcur++] = p->symbol;
				p = root;
				--data_count;
			}
		}
	}

	free_huffman_tree(root);
	*pbufout = buf;
	*pbufoutlen = bufcur;
	return 0;
}
