#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define inbufsiz (1024*1024*1024)
#define matchlim 0x2000
enum mode { m_none, m_direct, m_long, m_short0, m_short1, m_short2, m_short3, m_done };


int readin(FILE *infile, uint8_t **ind)
{
        uint8_t *indata = NULL;
        int size = 0;
        do {
                indata = realloc(indata, size + inbufsiz);
                //Read in a meg...
                size += fread(indata, 1, inbufsiz, infile);
                //..and check for more
        } while (!feof(infile));
        *ind = indata;
        return size;
}

struct compnode {
        enum mode type;
        uint16_t offset;
        uint16_t size;
        uint8_t data;
};

int findMatch(uint8_t *sourcedata, int insize, int index, int *offset)
{
        //Start at the prior byte, seek back to beginning of potential offset
        //Find the longest match
        //There are more efficient ways to do this, but...
        int matchlen = 0;
        for (int i = 1; matchlen < 255 && i < matchlim; i++) {
                int len = 0;
                if (i > index) break;
                while (sourcedata[index+len] == sourcedata[index+len-i]) {
                        len++;
                        if (index+len == insize) break;
                        if (len == 255) break;
                }
                if (len > matchlen) {
                        *offset = i;
                        matchlen = len;
                }
        }
        return matchlen;
}

int findShortMatch(uint8_t *sourcedata, int insize, int index, int *offset)
{
        //Seek for short matches only!
        int matchlen = 0;
        for (int i = 1; matchlen < 5 && i < 256; i++) {
                int len = 0;
                if (i > index) break;
                while (sourcedata[index+len] == sourcedata[index+len-i]) {
                        len++;
                        if (index+len == insize) break;
                        if (len == 5) break;
                }
                if (len > matchlen) {
                        *offset = i;
                        matchlen = len;
                }
        }
        return matchlen;
}

void compress(uint8_t *source, int insize, struct compnode *nodes)
{
        int nodein = 0;
        //Compress each byte
        for (int i = 0; i < insize; i++) {
                int offset;
                int size = findMatch(source, insize, i, &offset);
                //Check type of copy
                if (size > 5 || (size > 2 && offset > 255)) {
                        //Long copy
                        nodes[nodein].type = m_long;
                        nodes[nodein].offset = 0x2000-offset;
                        nodes[nodein].size = size-1;
                        i += nodes[nodein++].size;
                } else {
                        //Short copy?
                        size = findShortMatch(source, insize, i, &offset);
                        if (size > 1) {
                                //Short copy
                                nodes[nodein].type = m_short0;
                                nodes[nodein].offset = 256-offset;
                                size = size > 5 ? 5 : size;
                                i += size-1;
                                size -= 2;
                                nodes[nodein++].type += size;
                        } else {
                                //Literal
                                nodes[nodein].type = m_direct;
                                nodes[nodein++].data = source[i];
                        }
                }
        }
        nodes[nodein].type = m_done;
}

void addControl(uint8_t **c, uint8_t **d, struct compnode *node)
{
        uint8_t *control = *c;
        uint8_t *data = *d;
        static int bits = 0;
        switch (node->type) {
                case m_direct :
                        if (!bits) {
                                //no room in control byte
                                control = data++;
                                *control = 0;
                                bits = 8;
                        }
                        *control |= 1 << (8-bits);
                        bits--;
                        *data++ = node->data;
                        break;
                case m_short0 :
                case m_short1 :
                case m_short2 :
                case m_short3 :
                        //Handle the control byte first
                        if (!bits) {
                                //no room in control byte
                                control = data++;
                                *control = 0;
                                bits = 8;
                        }
                        if (bits == 1) {
                                //Not enough room for the front half
                                //Control split across two bytes
                                *control |= 0 << (8-bits);
                                control = data++;
                                *control = 0;
                                bits = 8;
                                *control |= 0 << (8-bits);
                                bits--;
                        } else {
                                //Room for front half
                                *control |= 0 << (8-bits);
                                bits--;
                                *control |= 0 << (8-bits);
                                bits--;
                        }
                        //Get the size
                        int shortsize = node->type - m_short0;
                        if (!bits) {
                                //no room in control byte
                                control = data++;
                                *control = 0;
                                bits = 8;
                        }
                        if (bits == 1) {
                                //Not enough room for the back half
                                //Control split across two bytes
                                *control |= (!!(shortsize & 2)) << (8-bits);
                                control = data++;
                                *control = 0;
                                bits = 8;
                                *control |= (shortsize & 1) << (8-bits);
                                bits--;
                        } else {
                                //Room for back half
                                *control |= (!!(shortsize & 2)) << (8-bits);
                                bits--;
                                *control |= (shortsize & 1) << (8-bits);
                                bits--;
                        }
                        //Data goes here
                        *data++ = node->offset;
                        break;
                case m_long :
                        if (!bits) {
                                //no room in control byte
                                control = data++;
                                *control = 0;
                                bits = 8;
                        }
                        if (bits == 1) {
                                //Control split across two bytes
                                *control |= 0 << (8-bits);
                                control = data++;
                                *control = 0;
                                bits = 8;
                                *control |= 1 << (8-bits);
                                bits--;
                        } else {
                                //Plenty of space
                                *control |= 0 << (8-bits);
                                bits--;
                                *control |= 1 << (8-bits);
                                bits--;
                        }
                        //What kind of size are we looking at?
                        if (node->size > 8) {
                                //Big size
                                *data++ = (node->offset & 0x1F) << 3;
                                *data++ = (node->offset & 0x1FE0) >> 5;
                                *data++ = node->size;
                        } else {
                                //Small size
                                *data = (node->size-1) & 0x07;
                                *data++ |= (node->offset & 0x1F) << 3;
                                *data++ = (node->offset & 0x1FE0) >> 5;
                        }
                        break;
                case m_done :
                        if (!bits) {
                                //no room in control byte
                                control = data++;
                                *control = 0;
                                bits = 8;
                        }
                        if (bits == 1) {
                                //Control split across two bytes
                                *control |= 0 << (8-bits);
                                control = data++;
                                *control = 0;
                                bits = 8;
                                *control |= 1 << (8-bits);
                                bits--;
                        } else {
                                //Plenty of space
                                *control |= 0 << (8-bits);
                                bits--;
                                *control |= 1 << (8-bits);
                                bits--;
                        }
                        *data++ = 0;
                        *data++ = 0;
        }
        *c = control;
        *d = data;
}

int store(uint8_t *data, struct compnode *nodes)
{
        uint8_t *start = data;
        uint8_t *control = data;
        //While there are nodes
        for (; nodes->type != m_done; nodes++) {
                addControl(&control, &data, nodes);
        }
        //One more for the terminus
        addControl(&control, &data, nodes);
        return data - start;
}

int main(int argc, char **argv)
{
        if (argc < 3) {
                puts("Usage: compress infile outfile");
                return 1;
        }
        FILE *infile = fopen(argv[1], "rb");
        uint8_t *indata;
        int insize = readin(infile, &indata);
        fclose(infile);
        struct compnode *nodes = malloc(sizeof(*nodes)*insize+1);
        compress(indata, insize, nodes);
        int outsize = 0;
        uint8_t *outdata = indata;
        outsize = store(outdata, nodes);
        free(nodes);
        FILE *outfile = fopen(argv[2], "wb");
        fwrite(outdata, 1, outsize, outfile);
        fclose(outfile);
        free(outdata);
        return 0;
}
