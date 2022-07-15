// Adapted from src/opus_demo.c 

/* removed much of the code that isn't crucial to functionality
   such as parsing encoder/decoder parameters, simulated packet loss, 
   randomized framesize, and testing/debugging */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include "opus.h"
                                              
#define MAX_PACKET 1500

static opus_uint32 char_to_int(unsigned char ch[4])
{
    return ((opus_uint32)ch[0]<<24) | ((opus_uint32)ch[1]<<16)
         | ((opus_uint32)ch[2]<< 8) |  (opus_uint32)ch[3];
}

int main(int argc, char *argv[])
{
    FILE *fin, *fout;
    OpusDecoder *dec=NULL;
    char *inFile, *outFile;
    const int max_frame_size = 960*6;
    const int max_payload_bytes = MAX_PACKET;
    int err;
    int len;
    int lost = 0;
    int lost_prev = 1;
    opus_int32 count = 0;
    opus_int32 skip = 0;
    opus_uint32 dec_final_range;
    opus_uint32 enc_final_range;
    short *out;
    unsigned char *data;
    unsigned char *fbytes;

    const opus_int32 sampling_rate = 48000;
    const int channels = 1;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s -d <input> <output>\n", argv[0]);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "%s\n", opus_get_version_string());

    inFile = argv[1];
    fin = fopen(inFile, "rb");
    if (!fin)
    {
        fprintf (stderr, "Could not open input file %s\n", argv[argc-2]);
        return EXIT_FAILURE;
    }
    
    outFile = argv[2];
    fout = fopen(outFile, "wb+");
    if (!fout)
    {
        fprintf (stderr, "Could not open output file %s\n", argv[argc-1]);
        fclose(fin);
        return EXIT_FAILURE;
    }

    dec = opus_decoder_create(sampling_rate, channels, &err);
    if (err != OPUS_OK)
    {
        fprintf(stderr, "Cannot create decoder: %s\n", opus_strerror(err));
        fclose(fin);
        fclose(fout);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Decoding with %ld Hz output (%d channels)\n",
            (long)sampling_rate, channels);

    out    = (short*)malloc(max_frame_size*channels*sizeof(short));
    fbytes = (unsigned char*)malloc(max_frame_size*channels*sizeof(short));
    data   = (unsigned char*)calloc(max_payload_bytes,sizeof(char));

    for(;;)
    {
        unsigned char ch[4];
        err = fread(ch, 1, 4, fin);
        if (feof(fin))
            break;
        len = char_to_int(ch);
        if (len>max_payload_bytes || len<0)
        {
            fprintf(stderr, "Invalid payload length: %d\n",len);
            break;
        }
        err = fread(ch, 1, 4, fin);
        enc_final_range = char_to_int(ch);
        err = fread(data, 1, len, fin);
        if (err<len)
        {
            fprintf(stderr, "Ran out of input, expecting %d bytes got %d\n", len,err);
            break;
        }

        int output_samples;
        lost = len==0;
        if (lost)
           opus_decoder_ctl(dec, OPUS_GET_LAST_PACKET_DURATION(&output_samples));
        else
           output_samples = max_frame_size;
        if( count >= 0 ) {
            output_samples = opus_decode(dec, lost ? NULL : data, len, out, output_samples, 0);
            if (output_samples>0)
            {
                if (output_samples>skip) {
                   int i;
                   for(i=0;i<(output_samples-skip)*channels;i++)
                   {
                      short s;
                      s=out[i+(skip*channels)];
                      fbytes[2*i]=s&0xFF;
                      fbytes[2*i+1]=(s>>8)&0xFF;
                   }
                   if (fwrite(fbytes, sizeof(short)*channels, output_samples-skip, fout) != (unsigned)(output_samples-skip)){
                      fprintf(stderr, "Error writing.\n");
                      return EXIT_FAILURE;
                   }
                }
                if (output_samples<skip) skip -= output_samples;
                else skip = 0;
            } else {
               fprintf(stderr, "error decoding frame: %s\n", opus_strerror(output_samples));
            }
        }

        opus_decoder_ctl(dec, OPUS_GET_FINAL_RANGE(&dec_final_range));
        // compare final range encoder values of encoder and decoder
        if( enc_final_range!=0  
         && !lost && !lost_prev
         && dec_final_range != enc_final_range ) {
            fprintf (stderr, "Error: Range coder state mismatch between encoder and decoder "
                             "in frame %ld: 0x%8lx vs 0x%8lx\n",
                         (long)count,
                         (unsigned long)enc_final_range,
                         (unsigned long)dec_final_range);
            fclose(fin);
            fclose(fout);
            return EXIT_FAILURE;
        }

        lost_prev = lost;

        count++;
    }

    // gets rid of instances that were used
    opus_decoder_destroy(dec);
    free(data);
    fclose(fin);
    fclose(fout);
    free(out);
    free(fbytes);
    return EXIT_SUCCESS;
}