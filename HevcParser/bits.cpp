
#include <assert.h>
//#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bits.h"

int dbg = 1;

#define min(a, b) (((a) < (b)) ? (a) : (b)) 

#define TRACE(fmt, name, val)   \
    if (dbg > 0)                \
        fprintf(stdout, fmt, name, val);

#define TRACE_4(fmt, name, len, val)   \
    if (dbg > 0)                \
        fprintf(stdout, fmt, name, len, val);


InputBitstream_t m_pcBitstream;

static int32_t  read_svlc(void);

static uint32_t read_uvlc(void);

static uint32_t read_bits
(
    uint32_t uiNumberOfBits
);

static uint32_t get_num_bits_left(void);

static uint32_t peek_bits
(
    uint32_t uiBits
);

static uint32_t pseudo_read
( 
    uint32_t uiNumberOfBits
);


static int32_t read_svlc(void)
{
    uint32_t leadingZeroBits = (uint32_t) -1;
    uint32_t kplus1 = 0;        // k is the codeNum in uvlc
    int32_t  codeNum;           // codeNum in svlc
    bool     b;
    
    for (b = 0; !b; leadingZeroBits++)
    {
        b = (bool) (read_bits(1) & 0x01);
    }
    
    kplus1 = (1 << leadingZeroBits) + read_bits(leadingZeroBits);

    codeNum = (kplus1 & 1) ? - (int32_t) (kplus1 >> 1) : (int32_t) (kplus1 >> 1);
    
    return codeNum;    
}

static uint32_t read_uvlc(void)
{
    /* coding according to 9-1 */
    uint32_t leadingZeroBits = (uint32_t) -1;
    uint32_t codeNum = 0;
    bool     b;
    
    for (b = 0; !b; leadingZeroBits++)
    {
        b = (bool) (read_bits(1) & 0x01);
    }
    
    codeNum = (1 << leadingZeroBits) - 1 + read_bits(leadingZeroBits);
    
    return codeNum;
}


/**
 * TComInputBitstream::read() in HM
 *
 * read_bits(n) in H.265 spec
 */
static uint32_t read_bits(uint32_t uiNumberOfBits)
{
    //assert(uiNumberOfBits <= 32);
    
    m_pcBitstream.m_numBitsRead += uiNumberOfBits;
    
    /* NB, bits are extracted from the MSB of each byte. */
    uint32_t retval = 0;
    
    if (uiNumberOfBits <= m_pcBitstream.m_num_held_bits)
    {
        /* n=1, len(H)=7:   -VHH HHHH, shift_down=6, mask=0xfe
         * n=3, len(H)=7:   -VVV HHHH, shift_down=4, mask=0xf8
         */
        retval = m_pcBitstream.m_held_bits >> (m_pcBitstream.m_num_held_bits - uiNumberOfBits);
        retval &= ~(0xff << uiNumberOfBits);
        m_pcBitstream.m_num_held_bits -= uiNumberOfBits;
        
        return retval;
    }
    
    /* all num_held_bits will go into retval
     *   => need to mask leftover bits from previous extractions
     *   => align retval with top of extracted word */
    /* n=5, len(H)=3: ---- -VVV, mask=0x07, shift_up=5-3=2,
     * n=9, len(H)=3: ---- -VVV, mask=0x07, shift_up=9-3=6 */
    uiNumberOfBits -= m_pcBitstream.m_num_held_bits;
    retval = m_pcBitstream.m_held_bits & ~(0xff << m_pcBitstream.m_num_held_bits);
    retval <<= uiNumberOfBits;
    
    /* number of whole bytes that need to be loaded to form retval */
    /* n=32, len(H)=0, load 4bytes, shift_down=0
     * n=32, len(H)=1, load 4bytes, shift_down=1
     * n=31, len(H)=1, load 4bytes, shift_down=1+1
     * n=8,  len(H)=0, load 1byte,  shift_down=0
     * n=8,  len(H)=3, load 1byte,  shift_down=3
     * n=5,  len(H)=1, load 1byte,  shift_down=1+3
     */
    uint32_t aligned_word = 0;
    uint32_t num_bytes_to_load = (uiNumberOfBits - 1) >> 3;
    
    //assert(m_fifo_idx + num_bytes_to_load < m_fifo->size());
    
    switch (num_bytes_to_load)
    {
        case 3: aligned_word  = (m_pcBitstream.m_fifo)[m_pcBitstream.m_fifo_idx++] << 24;
        case 2: aligned_word |= (m_pcBitstream.m_fifo)[m_pcBitstream.m_fifo_idx++] << 16;
        case 1: aligned_word |= (m_pcBitstream.m_fifo)[m_pcBitstream.m_fifo_idx++] << 8;
        case 0: aligned_word |= (m_pcBitstream.m_fifo)[m_pcBitstream.m_fifo_idx++];
    }
    
    /* resolve remainder bits */
    uint32_t next_num_held_bits = (32 - uiNumberOfBits) % 8;
    
    /* copy required part of aligned_word into retval */
    retval |= aligned_word >> next_num_held_bits;
    
    /* store held bits */
    m_pcBitstream.m_num_held_bits = next_num_held_bits;
    m_pcBitstream.m_held_bits = (uint8_t) (aligned_word & 0xFF);
    
    return retval;
}


static uint32_t get_num_bits_left(void) 
{ 
    return 8 * (m_pcBitstream.m_fifo_size - m_pcBitstream.m_fifo_idx) + m_pcBitstream.m_num_held_bits;
}


static uint32_t peek_bits
(
    uint32_t uiBits
)
{
    return pseudo_read(uiBits);
}


/**
 * TComInputBitstream::pseudoRead() in HM
 *
 */
static uint32_t pseudo_read
( 
    uint32_t uiNumberOfBits
)
{
    uint32_t saved_num_held_bits;
    uint8_t  saved_held_bits;
    uint32_t saved_fifo_idx;

    uint32_t retVal;
    
    uint32_t num_bits_to_read = min(uiNumberOfBits, get_num_bits_left());


    saved_num_held_bits = m_pcBitstream.m_num_held_bits;
    saved_held_bits     = m_pcBitstream.m_held_bits;
    saved_fifo_idx      = m_pcBitstream.m_fifo_idx;
    
    retVal = read_bits(num_bits_to_read);

    retVal <<= (uiNumberOfBits - num_bits_to_read);
    
    m_pcBitstream.m_fifo_idx      = saved_fifo_idx;
    m_pcBitstream.m_held_bits     = saved_held_bits;
    m_pcBitstream.m_num_held_bits = saved_num_held_bits;

    return retVal;
}


uint32_t READ_CODE
(
    uint32_t length, 
    char    *name
)
{
    uint32_t ret;
    
    ret = read_bits(length);

    TRACE_4("%-50s u(%d)  : %u\n", name, length, ret);

    return ret;
}


bool READ_FLAG
(
    char *name
)
{
    bool ret;

    ret = (bool) (read_bits(1) & 0x01);
    
    TRACE("%-50s u(1)  : %u\n", name, ret);

    return ret;
}


uint32_t READ_UVLC
(
    char *name
)
{
    uint32_t ret;

    ret = read_uvlc();

    TRACE("%-50s ue(v) : %u\n", name, ret);

    return ret;
}


int32_t READ_SVLC
(
    char *name
)
{
    int32_t ret;
    
    ret = read_svlc();
    
    TRACE("%-50s se(v) : %d\n", name, ret);
    
    return ret;
}


bool MORE_RBSP_DATA(void)
{ 
    int bitsLeft = get_num_bits_left();
    
    // if there are more than 8 bits, it cannot be rbsp_trailing_bits
    if (bitsLeft > 8)
    {
        return true;
    }
    
    uint8_t lastByte = peek_bits(bitsLeft);
    int cnt = bitsLeft;
    
    // remove trailing bits equal to zero
    while ((cnt > 0) && ((lastByte & 1) == 0))
    {
        lastByte >>= 1;
        cnt--;
    }

    // remove bit equal to one
    cnt--;
    
    // we should not have a negative number of bits
    assert(cnt >= 0);
    
    // we have more data, if cnt is not zero
    return (cnt > 0);
}

