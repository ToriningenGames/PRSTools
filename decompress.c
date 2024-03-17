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

enum mode getControl(uint8_t **indata)
{
        static uint8_t control;
        static int bits = 0;
        if (!bits) {
                control = **indata;
                (*indata)++;
                bits = 8;
        }
        if (control & 1) {
                //Literal
                control >>= 1;
                bits--;
                return m_direct;
        }
        control >>= 1;
        bits--;
        if (!bits) {
                control = **indata;
                (*indata)++;
                bits = 8;
        }
        if (control & 1) {
                //Long copy
                control >>= 1;
                bits--;
                return m_long;
        }
        //Short copy
        control >>= 1;
        bits--;
        if (!bits) {
                control = **indata;
                (*indata)++;
                bits = 8;
        }
        int len = 0;
        len = control & 1 ? 2 : 0;
        control >>= 1;
        bits--;
        if (!bits) {
                control = **indata;
                (*indata)++;
                bits = 8;
        }
        len += control & 1 ? 1 : 0;
        control >>= 1;
        bits--;
        return m_short0 + len;
}

void decompress(uint8_t *indata, int insize, struct compnode *nodes)
{
        for (int i = 0; i < insize; i++) {
                switch (nodes->type = getControl(&indata)) {
                        case m_direct :
                                nodes->data = *indata++;
                                nodes++;
                                break;
                        case m_short0 :
                        case m_short1 :
                        case m_short2 :
                        case m_short3 :
                                nodes->offset = *indata++;
                                nodes++;
                                break;
                        case m_long :
                                nodes->size = *indata & 0x07;
                                nodes->offset = (*indata++ & 0xF8) >> 3;
                                nodes->offset |= *indata++ << 5;
                                //Special detect for the end phrase
                                if (!nodes->size && nodes->offset) {
                                        nodes->size--;
                                }
                                if (!nodes->size) {
                                        nodes->size = *indata++;
                                } else {
                                        nodes->size++;
                                }
                                nodes++;
                                break;
                }
        }
}

int store(uint8_t *data, struct compnode *nodes)
{
        int size;
        uint8_t *start = data;
        for (; nodes->type != m_long || nodes->offset != 0 || nodes->size != 0; nodes++) {
                switch (nodes->type) {
                        case m_direct :
                                *data++ = nodes->data;
                                break;
                        case m_short3 :
                                size = 5; if (0)
                        case m_short2 :
                                size = 4; if (0)
                        case m_short1 :
                                size = 3; if (0)
                        case m_short0 :
                                size = 2;
                                for (int i = 0; i < size; i++) {
                                        *data = *(data-(256-nodes->offset));
                                        data++;
                                }
                                break;
                        case m_long :
                                size = nodes->size + 1;
                                for (int i = 0; i < size; i++) {
                                        *data = *(data-(0x2000-nodes->offset));
                                        data++;
                                }
                                break;
                }
        }
        return data - start;
}

int main(int argc, char **argv)
{
        if (argc < 3) {
                puts("Usage: decompress infile outfile");
                return 1;
        }
        FILE *infile = fopen(argv[1], "rb");
        uint8_t *indata;
        int insize = readin(infile, &indata);
        fclose(infile);
        struct compnode *nodes = malloc(sizeof(*nodes)*insize);
        decompress(indata, insize, nodes);
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
