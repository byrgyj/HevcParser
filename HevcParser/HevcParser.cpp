// HevcParser.cpp : Defines the entry point for the console application.
//

#include <assert.h>
#include <stdio.h>
//#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//#include <unistd.h>
       
#include "common.h"
#include "bits.h"
#include <io.h>


/******************************
 * define
 */
#define SIZE_OF_NAL_UNIT_HDR    2
#define ES_BUFFER_SIZE          (3840 * 2160)

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))


typedef struct
{
    uint32_t u32Width;
    uint32_t u32Height;
} HevcInfo_t;


/******************************
 * local function prototype
 */

static void parse_ptl(uint32_t max_sub_layers_minus1);

static void parse_scaling_list();

static void parse_short_term_ref_pic_set(SPS_t *pSPS, ReferencePictureSet_t *rps, uint32_t stRpsIdx);

static void parse_vui(uint32_t maxNumSubLayersMinus1);

static void parse_hrd(bool commonInfPresentFlag, uint32_t maxNumSubLayersMinus1);

static void parse_sub_layer_hrd_params(uint32_t CpbCnt);

static void ref_pic_lists_modification(Slice_t *pSlice);

static void pred_weight_table(SPS_t *pSPS);


/******************************
 * local variable
 */

static uint8_t u8endCode[] = { 0xFC, 0xFD, 0xFE, 0xFF };

static uint8_t u8EsBuffer[ES_BUFFER_SIZE + sizeof(u8endCode)];

static VPS_t vps;

/* sps id has 4 bits, so max is 15 */
static SPS_t sps[1 << 4];

static PPS_t pps[1024];

static Slice_t slice;
    
static uint32_t ScalingList[4][6][64];

static bool sub_pic_hrd_params_present_flag;


static HevcInfo_t tHevcInfo;

static char frameInfo[0x10000];
static char *pInfo = frameInfo;

static uint32_t u32frameCnt;


/******************************
 * local function
 */
 
static bool has_start_code
(
    uint8_t *addr,
    uint8_t  zeros
)
{
    int i;
    
    for (i = 0; i < zeros; i++)
    {
        if (addr[i]) return false;
    }
    
    return addr[i] == 0x01 ? true : false;
}

static bool has_end_code(uint8_t *p)
{
    if (memcmp(p, u8endCode, sizeof(u8endCode)) == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}


static bool scan_nal
(
    uint8_t     *start_addr,
    uint8_t     *nal_unit_header,
    uint32_t    *nal_len,
    uint32_t    *prefix_len
)
{
    uint8_t offset = 0;
    uint8_t *p;

    bool ret = true;
    
    p = start_addr;

    if (has_start_code(p, 2))      // short prefix
    {
        offset = 3;
    }
    else if (has_start_code(p, 3)) // long prefix
    {
        offset = 4;
    }

    //printf("prefix offset=%d\n", offset);
    *prefix_len = offset;
    p += offset;

    while (!has_end_code(p) && !has_start_code(p, 2) && !has_start_code(p, 3))
    {
        p++;
    }

    nal_unit_header[0] = start_addr[offset];
    nal_unit_header[1] = start_addr[offset + 1];
    *nal_len  = (uint32_t) (p - start_addr);

    if (has_end_code(p))
    {
        ret = false;
    }

    return ret;
}

static uint32_t EBSPtoRBSP
(
    uint8_t *streamBuffer,
    uint32_t end_bytepos,
    uint32_t begin_bytepos
)
{
    uint32_t i;
    uint32_t j;
    uint32_t count;
    
    count = 0;
    j = begin_bytepos;
    
    for (i = begin_bytepos; i < end_bytepos; i++)
    {
        // in NAL unit, 0x000000, 0x000001, 0x000002 shall not occur at any byte-aligned position
        if (count == 2 && streamBuffer[i] < 0x03)
        {
            return -1;
        }
        
        if (count == 2 && streamBuffer[i] == 0x03)
        {
            // check the 4th byte after 0x000003, except when cabac.....
            if ((i < end_bytepos - 1) && (streamBuffer[i + 1] > 0x03))
            {
                return -1;
            }
            
            if (i == end_bytepos - 1)
            {
                return j;
            }
            
            // escape 0x03 byte!
            i++;
            count = 0;
        }
        
        streamBuffer[j] = streamBuffer[i];
        //printf("[%02u] 0x%02x\n", j, streamBuffer[j]);
        
        if (streamBuffer[i] == 0x00)
        {
            count++;
        }
        else
        {
            count = 0;
        }
        
        j++;
    }
    
    return j;
}

static void createRPSList(SPS_t *pSPS, uint32_t numRPS)
{ 
    pSPS->m_RPSList.m_numberOfReferencePictureSets  = numRPS;
    pSPS->m_RPSList.m_referencePictureSets          = (ReferencePictureSet_t   *)malloc(sizeof(ReferencePictureSet_t) * numRPS);
}

static uint32_t getNumRpsCurrTempList(Slice_t *pSlice)
{
    uint32_t numRpsCurrTempList = 0;
    uint32_t i;
    
    if (pSlice->m_eSliceType == I_SLICE) 
    {
        return 0;
    }
    
    for (i = 0; i < pSlice->m_pcRPS->m_numberOfNegativePictures + pSlice->m_pcRPS->m_numberOfPositivePictures + pSlice->m_pcRPS->m_numberOfLongtermPictures; i++)
    {
        if (pSlice->m_pcRPS->m_used[i])
        {
            numRpsCurrTempList++;
        }
    }
    
    return numRpsCurrTempList;
}

static void parse_vps(VPS_t *pVPS)
{
  /*  uint32_t    vps_video_parameter_set_id;
    uint32_t    vps_reserved_three_2bits;
    uint8_t     vps_max_layers_minus1;
    uint32_t    vps_max_sub_layers_minus1;
    bool        vps_temporal_id_nesting_flag;
    uint16_t    vps_reserved_0xffff_16bits;
    bool        vps_sub_layer_ordering_info_present_flag;
    uint32_t    vps_max_layer_id;
    uint32_t    vps_num_layer_sets_minus1;
    bool        vps_extension_flag;
    bool        rbsp_stop_one_bit;
    
    
    vps_video_parameter_set_id                  = READ_CODE(4, "vps_video_parameter_set_id");
    vps_reserved_three_2bits                    = READ_CODE(2, "vps_reserved_three_2bits"); assert(vps_reserved_three_2bits == 3);
    vps_max_layers_minus1                       = READ_CODE(6, "vps_max_layers_minus1");
    vps_max_sub_layers_minus1                   = READ_CODE(3, "vps_max_sub_layers_minus1");
    vps_temporal_id_nesting_flag                = READ_FLAG("vps_temporal_id_nesting_flag");
    vps_reserved_0xffff_16bits                  = READ_CODE(16, "vps_reserved_0xffff_16bits"); assert(vps_reserved_0xffff_16bits == 0xffff);

    parse_ptl(vps_max_sub_layers_minus1);
    
    vps_sub_layer_ordering_info_present_flag = READ_FLAG("vps_sub_layer_ordering_info_present_flag");
        
    uint8_t  i, j;
    uint32_t *vps_max_dec_pic_buffering_minus1   = new uint32_t [vps_max_sub_layers_minus1 + 1];
    uint32_t *vps_max_num_reorder_pics           = new uint32_t [vps_max_sub_layers_minus1 + 1];
    uint32_t *vps_max_latency_increase_plus1     = new uint32_t [vps_max_sub_layers_minus1 + 1];


    
    for (i = (vps_sub_layer_ordering_info_present_flag ? 0 : vps_max_sub_layers_minus1); i <= vps_max_sub_layers_minus1; i++ )
    {
        vps_max_dec_pic_buffering_minus1[i] = READ_UVLC("vps_max_dec_pic_buffering_minus1");
        vps_max_num_reorder_pics[i]         = READ_UVLC("vps_max_num_reorder_pics");
        vps_max_latency_increase_plus1[i]   = READ_UVLC("vps_max_latency_increase_plus1");
    }

    vps_max_layer_id            = READ_CODE(6, "vps_max_layer_id");
    vps_num_layer_sets_minus1   = READ_UVLC("vps_num_layer_sets_minus1");

    bool layer_id_included_flag[vps_num_layer_sets_minus1 + 1][vps_max_layer_id + 1];

    for (i = 1; i <= vps_num_layer_sets_minus1; i++)
    {
        for (j = 0; j <= vps_max_layer_id; j++)
        {
            layer_id_included_flag[i][j] = READ_FLAG("layer_id_included_flag");
        }
    }

    bool vps_timing_info_present_flag;

    vps_timing_info_present_flag = READ_FLAG("vps_timing_info_present_flag");

    if (vps_timing_info_present_flag)
    {
        uint32_t    vps_num_units_in_tick;
        uint32_t    vps_time_scale;
        bool        vps_poc_proportional_to_timing_flag;
        uint32_t    vps_num_ticks_poc_diff_one_minus1;
        uint32_t    vps_num_hrd_parameters;

        vps_num_units_in_tick               = READ_CODE(32, "vps_num_units_in_tick");
        vps_time_scale                      = READ_CODE(32, "vps_time_scale");
        vps_poc_proportional_to_timing_flag = READ_FLAG("vps_poc_proportional_to_timing_flag");

        if (vps_poc_proportional_to_timing_flag)
        {
            vps_num_ticks_poc_diff_one_minus1 = READ_UVLC("vps_num_ticks_poc_diff_one_minus1");
        }

        vps_num_hrd_parameters = READ_UVLC("vps_num_hrd_parameters");

        uint32_t hrd_layer_set_idx[vps_num_hrd_parameters];
        bool     cprms_present_flag[vps_num_hrd_parameters];

        for (i = 0; i < vps_num_hrd_parameters; i++)
        {
            hrd_layer_set_idx[i] = READ_UVLC("hrd_layer_set_idx[i]");

            if (i > 0)
            {
                cprms_present_flag[i] = READ_FLAG("cprms_present_flag[i]");
            }

            parse_hrd(cprms_present_flag[i], vps_max_sub_layers_minus1);
        }
    }

    vps_extension_flag = READ_FLAG("vps_extension_flag");

    if (vps_extension_flag)
    {
        while (MORE_RBSP_DATA())
        {
            bool sps_extension_data_flag;

            sps_extension_data_flag = READ_FLAG("sps_extension_data_flag");
        }
    }

    rbsp_stop_one_bit = READ_FLAG("rbsp_stop_one_bit");


    pVPS->m_VPSId = vps_video_parameter_set_id;
    */
}

static void parse_sps(void)
{
    SPS_t      *p_sps = NULL;
    
    uint32_t    sps_video_parameter_set_id = 0;
    uint32_t    sps_max_sub_layers_minus1 = 0;
    bool        sps_temporal_id_nesting_flag;
    uint32_t    sps_seq_parameter_set_id = 0;
    uint32_t    chroma_format_idc;
    bool        separate_colour_plane_flag = false;
    uint32_t    pic_width_in_luma_samples;
    uint32_t    pic_height_in_luma_samples;
    bool        conformance_window_flag;
    uint32_t    conf_win_left_offset;
    uint32_t    conf_win_right_offset;
    uint32_t    conf_win_top_offset;
    uint32_t    conf_win_bottom_offset;
    uint32_t    bit_depth_luma_minus8;
    uint32_t    bit_depth_chroma_minus8;
    uint32_t    log2_max_pic_order_cnt_lsb_minus4;
    bool        sps_sub_layer_ordering_info_present_flag;
    bool        rbsp_stop_one_bit;

    
    sps_video_parameter_set_id      = READ_CODE(4, "sps_video_parameter_set_id");
    sps_max_sub_layers_minus1       = READ_CODE(3, "sps_max_sub_layers_minus1");
    sps_temporal_id_nesting_flag    = READ_FLAG("sps_temporal_id_nesting_flag");
    
    parse_ptl(sps_max_sub_layers_minus1);

    sps_seq_parameter_set_id    = READ_UVLC("sps_seq_parameter_set_id");
    p_sps = &sps[sps_seq_parameter_set_id];
    
    chroma_format_idc           = READ_UVLC("chroma_format_idc");

    if (3 == chroma_format_idc)
    {
        separate_colour_plane_flag = READ_FLAG("separate_colour_plane_flag");
    }

    pic_width_in_luma_samples   = READ_UVLC("pic_width_in_luma_samples");
    pic_height_in_luma_samples  = READ_UVLC("pic_height_in_luma_samples");

    conformance_window_flag = READ_FLAG("conformance_window_flag");

    if (conformance_window_flag)
    {
        conf_win_left_offset    = READ_UVLC("conf_win_left_offset");
        conf_win_right_offset   = READ_UVLC("conf_win_right_offset");
        conf_win_top_offset     = READ_UVLC("conf_win_top_offset");
        conf_win_bottom_offset  = READ_UVLC("conf_win_bottom_offset");
    }

    bit_depth_luma_minus8               = READ_UVLC("bit_depth_luma_minus8");
    bit_depth_chroma_minus8             = READ_UVLC("bit_depth_chroma_minus8");
    log2_max_pic_order_cnt_lsb_minus4   = READ_UVLC("log2_max_pic_order_cnt_lsb_minus4");

    sps_sub_layer_ordering_info_present_flag = READ_FLAG("sps_sub_layer_ordering_info_present_flag");

    int i;
    uint32_t *sps_max_dec_pic_buffering_minus1   = new uint32_t[sps_max_sub_layers_minus1 + 1];
    uint32_t *sps_max_num_reorder_pics           = new uint32_t[sps_max_sub_layers_minus1 + 1];
    uint32_t *sps_max_latency_increase_plus1     = new uint32_t[sps_max_sub_layers_minus1 + 1];

    for (i = (sps_sub_layer_ordering_info_present_flag ? 0 : sps_max_sub_layers_minus1); i <= sps_max_sub_layers_minus1; i++ )
    {
        sps_max_dec_pic_buffering_minus1[i] = READ_UVLC("sps_max_dec_pic_buffering_minus1[i]");
        sps_max_num_reorder_pics[i]         = READ_UVLC("sps_max_num_reorder_pics[i]");
        sps_max_latency_increase_plus1[i]   = READ_UVLC("sps_max_latency_increase_plus1[i]");
    }

    uint32_t log2_min_luma_coding_block_size_minus3;
    uint32_t log2_diff_max_min_luma_coding_block_size;
    uint32_t log2_min_transform_block_size_minus2;
    uint32_t log2_diff_max_min_transform_block_size;
    uint32_t max_transform_hierarchy_depth_inter;
    uint32_t max_transform_hierarchy_depth_intra;
    bool     scaling_list_enabled_flag;

    log2_min_luma_coding_block_size_minus3      = READ_UVLC("log2_min_luma_coding_block_size_minus3");
    log2_diff_max_min_luma_coding_block_size    = READ_UVLC("log2_diff_max_min_luma_coding_block_size");
    log2_min_transform_block_size_minus2        = READ_UVLC("log2_min_transform_block_size_minus2");
    log2_diff_max_min_transform_block_size      = READ_UVLC("log2_diff_max_min_transform_block_size");
    max_transform_hierarchy_depth_inter         = READ_UVLC("max_transform_hierarchy_depth_inter");
    max_transform_hierarchy_depth_intra         = READ_UVLC("max_transform_hierarchy_depth_intra");
    scaling_list_enabled_flag                   = READ_FLAG("scaling_list_enabled_flag");

    if (scaling_list_enabled_flag)
    {
        bool sps_scaling_list_data_present_flag;

        sps_scaling_list_data_present_flag = READ_FLAG("sps_scaling_list_data_present_flag");

        if (sps_scaling_list_data_present_flag)
        {
            parse_scaling_list();
        }
    }
    
    bool amp_enabled_flag;
    bool sample_adaptive_offset_enabled_flag;
    bool pcm_enabled_flag;
    uint32_t pcm_sample_bit_depth_luma_minus1;
    uint32_t pcm_sample_bit_depth_chroma_minus1;
    uint32_t log2_min_pcm_luma_coding_block_size_minus3;
    uint32_t log2_diff_max_min_pcm_luma_coding_block_size;
    bool pcm_loop_filter_disabled_flag;

    amp_enabled_flag = READ_FLAG("amp_enabled_flag");
    sample_adaptive_offset_enabled_flag = READ_FLAG("sample_adaptive_offset_enabled_flag");
    pcm_enabled_flag = READ_FLAG("pcm_enabled_flag");

    if (pcm_enabled_flag)
    {
        pcm_sample_bit_depth_luma_minus1    = READ_CODE(4, "pcm_sample_bit_depth_luma_minus1");
        pcm_sample_bit_depth_chroma_minus1  = READ_CODE(4, "pcm_sample_bit_depth_chroma_minus1");
        log2_min_pcm_luma_coding_block_size_minus3 = READ_UVLC("log2_min_pcm_luma_coding_block_size_minus3");
        log2_diff_max_min_pcm_luma_coding_block_size = READ_UVLC("log2_diff_max_min_pcm_luma_coding_block_size");
        pcm_loop_filter_disabled_flag       = READ_FLAG("pcm_loop_filter_disabled_flag");
    }

    uint32_t num_short_term_ref_pic_sets = 0;

    num_short_term_ref_pic_sets = READ_UVLC("num_short_term_ref_pic_sets");

    createRPSList(p_sps, num_short_term_ref_pic_sets);
    
//     for (i = 0; i < num_short_term_ref_pic_sets; i++)
//     {
//         ReferencePictureSet_t *rps = &p_sps->m_RPSList.m_referencePictureSets[i];
//         parse_short_term_ref_pic_set(p_sps, rps, i);
//     }

    bool long_term_ref_pics_present_flag = false;

    long_term_ref_pics_present_flag = READ_FLAG("long_term_ref_pics_present_flag");

    if (long_term_ref_pics_present_flag)
    {
        uint32_t num_long_term_ref_pics_sps;

        num_long_term_ref_pics_sps = READ_UVLC("num_long_term_ref_pics_sps");

        uint32_t *lt_ref_pic_poc_lsb_sps = new uint32_t[num_long_term_ref_pics_sps];
        uint32_t *used_by_curr_pic_lt_sps_flag = new uint32_t[num_long_term_ref_pics_sps];
        
        for (i = 0; i < num_long_term_ref_pics_sps; i++)
        {
            lt_ref_pic_poc_lsb_sps[i]       = READ_CODE(p_sps->m_uiBitsForPOC, "lt_ref_pic_poc_lsb_sps[i]");
            used_by_curr_pic_lt_sps_flag[i] = READ_FLAG("used_by_curr_pic_lt_sps_flag[i]");
        }
    }

    bool    sps_temporal_mvp_enabled_flag;
    bool    strong_intra_smoothing_enabled_flag;
    bool    vui_parameters_present_flag;

    sps_temporal_mvp_enabled_flag       = READ_FLAG("sps_temporal_mvp_enabled_flag");
    strong_intra_smoothing_enabled_flag = READ_FLAG("strong_intra_smoothing_enabled_flag");
    vui_parameters_present_flag         = READ_FLAG("vui_parameters_present_flag");

    if (vui_parameters_present_flag)
    {
        parse_vui(sps_max_sub_layers_minus1);
    }
    
    bool sps_extension_flag = false;

    sps_extension_flag = READ_FLAG("sps_extension_flag");

    if (sps_extension_flag)
    {
        while (MORE_RBSP_DATA())
        {
            bool sps_extension_data_flag;
            
            sps_extension_data_flag = READ_FLAG("sps_extension_data_flag");
        }
    }
    
    rbsp_stop_one_bit = READ_FLAG("rbsp_stop_one_bit");

    p_sps->m_VPSId                           = sps_video_parameter_set_id;
    p_sps->m_SPSId                           = sps_seq_parameter_set_id;
    p_sps->m_chromaFormatIdc                 = chroma_format_idc;

    p_sps->m_uiBitsForPOC                    = log2_max_pic_order_cnt_lsb_minus4 + 4;

    p_sps->m_separateColourPlaneFlag         = separate_colour_plane_flag;

    p_sps->m_log2MinCodingBlockSize          = log2_min_luma_coding_block_size_minus3 + 3;
    p_sps->m_log2DiffMaxMinCodingBlockSize   = log2_diff_max_min_luma_coding_block_size;
    p_sps->m_uiMaxCUWidth                    = 1 << (p_sps->m_log2MinCodingBlockSize + p_sps->m_log2DiffMaxMinCodingBlockSize);
    p_sps->m_uiMaxCUHeight                   = 1 << (p_sps->m_log2MinCodingBlockSize + p_sps->m_log2DiffMaxMinCodingBlockSize);

    p_sps->m_bLongTermRefsPresent            = long_term_ref_pics_present_flag;
    p_sps->m_TMVPFlagsPresent                = sps_temporal_mvp_enabled_flag;
    p_sps->m_bUseSAO                         = sample_adaptive_offset_enabled_flag;
    //printf("CtbSizeY=%u\n", p_sps->m_uiMaxCUWidth);

    tHevcInfo.u32Width  = pic_width_in_luma_samples;
    tHevcInfo.u32Height = pic_height_in_luma_samples;
}

static void parse_pps(void)
{
    PPS_t      *p_pps;
    
    uint32_t    pps_pic_parameter_set_id;
    uint32_t    pps_seq_parameter_set_id;
    bool        dependent_slice_segments_enabled_flag;
    bool        output_flag_present_flag;
    uint32_t    num_extra_slice_header_bits;
    bool        sign_data_hiding_enabled_flag;
    bool        cabac_init_present_flag;
    uint32_t    num_ref_idx_l0_default_active_minus1;
    uint32_t    num_ref_idx_l1_default_active_minus1;
    int32_t     init_qp_minus26;
    bool        constrained_intra_pred_flag;
    bool        transform_skip_enabled_flag;
    bool        cu_qp_delta_enabled_flag;
    uint32_t    diff_cu_qp_delta_depth;
    int32_t     pps_cb_qp_offset;
    int32_t     pps_cr_qp_offset;
    bool        pps_slice_chroma_qp_offsets_present_flag;
    bool        weighted_pred_flag;
    bool        weighted_bipred_flag;
    bool        transquant_bypass_enabled_flag;
    bool        tiles_enabled_flag;
    bool        entropy_coding_sync_enabled_flag;
    bool        pps_loop_filter_across_slices_enabled_flag;
    bool        deblocking_filter_control_present_flag;
    bool        pps_scaling_list_data_present_flag;
    bool        lists_modification_present_flag;
    uint32_t    log2_parallel_merge_level_minus2;
    bool        slice_segment_header_extension_present_flag;
    bool        pps_extension_flag;

    bool        deblocking_filter_override_enabled_flag = false;
    bool        pps_deblocking_filter_disabled_flag     = false;    
    int32_t     pps_beta_offset_div2                    = 0;
    int32_t     pps_tc_offset_div2                      = 0;    

    pps_pic_parameter_set_id    = READ_UVLC("pps_pic_parameter_set_id");

    p_pps = &pps[pps_pic_parameter_set_id];
    
    pps_seq_parameter_set_id    = READ_UVLC("pps_seq_parameter_set_id");
    dependent_slice_segments_enabled_flag = READ_FLAG("dependent_slice_segments_enabled_flag");
    output_flag_present_flag    = READ_FLAG("output_flag_present_flag");
    num_extra_slice_header_bits = READ_CODE(3, "num_extra_slice_header_bits");
    sign_data_hiding_enabled_flag = READ_FLAG("sign_data_hiding_enabled_flag");
    cabac_init_present_flag     = READ_FLAG("cabac_init_present_flag");

    num_ref_idx_l0_default_active_minus1    = READ_UVLC("num_ref_idx_l0_default_active_minus1"); assert(num_ref_idx_l0_default_active_minus1 <= 14);
    num_ref_idx_l1_default_active_minus1    = READ_UVLC("num_ref_idx_l1_default_active_minus1"); assert(num_ref_idx_l1_default_active_minus1 <= 14);
    init_qp_minus26                         = READ_SVLC("init_qp_minus26");
    constrained_intra_pred_flag             = READ_FLAG("constrained_intra_pred_flag");
    transform_skip_enabled_flag             = READ_FLAG("transform_skip_enabled_flag");
    cu_qp_delta_enabled_flag                = READ_FLAG("cu_qp_delta_enabled_flag");

    if (cu_qp_delta_enabled_flag)
    {
        diff_cu_qp_delta_depth = READ_UVLC("diff_cu_qp_delta_depth");
    }

    pps_cb_qp_offset    = READ_SVLC("pps_cb_qp_offset");
    pps_cr_qp_offset    = READ_SVLC("pps_cr_qp_offset");
    
    pps_slice_chroma_qp_offsets_present_flag    = READ_FLAG("pps_slice_chroma_qp_offsets_present_flag");
    weighted_pred_flag                          = READ_FLAG("weighted_pred_flag");
    weighted_bipred_flag                        = READ_FLAG("weighted_bipred_flag");
    transquant_bypass_enabled_flag              = READ_FLAG("transquant_bypass_enabled_flag");
    tiles_enabled_flag                          = READ_FLAG("tiles_enabled_flag");
    entropy_coding_sync_enabled_flag            = READ_FLAG("entropy_coding_sync_enabled_flag");

    if (tiles_enabled_flag)
    {
        uint32_t    num_tile_columns_minus1;
        uint32_t    num_tile_rows_minus1;
        bool        uniform_spacing_flag;
        bool        loop_filter_across_tiles_enabled_flag;

        num_tile_columns_minus1 = READ_UVLC("num_tile_columns_minus1");
        num_tile_rows_minus1    = READ_UVLC("num_tile_rows_minus1");
        uniform_spacing_flag    = READ_FLAG("uniform_spacing_flag");

        if (!uniform_spacing_flag)
        {
            uint32_t    i;
            uint32_t    *column_width_minus1= new uint32_t[num_tile_columns_minus1];
            uint32_t    *row_height_minus1= new uint32_t[num_tile_rows_minus1];
            
            for (i = 0; i < num_tile_columns_minus1; i++)
            {
                column_width_minus1[i] = READ_UVLC("column_width_minus1[i]");
            }
            for (i = 0; i < num_tile_rows_minus1; i++)
            {
                row_height_minus1[i] = READ_UVLC("row_height_minus1[i]");
            }
        }

        loop_filter_across_tiles_enabled_flag   = READ_FLAG("loop_filter_across_tiles_enabled_flag");      
    }

    pps_loop_filter_across_slices_enabled_flag  = READ_FLAG("pps_loop_filter_across_slices_enabled_flag");
    deblocking_filter_control_present_flag      = READ_FLAG("deblocking_filter_control_present_flag");

    if (deblocking_filter_control_present_flag)
    {
        deblocking_filter_override_enabled_flag = READ_FLAG("deblocking_filter_override_enabled_flag");
        pps_deblocking_filter_disabled_flag     = READ_FLAG("pps_deblocking_filter_disabled_flag");

        if (!pps_deblocking_filter_disabled_flag)
        {
            pps_beta_offset_div2    = READ_SVLC("pps_beta_offset_div2");
            pps_tc_offset_div2      = READ_SVLC("pps_tc_offset_div2");
        }
    }

    pps_scaling_list_data_present_flag  = READ_FLAG("pps_scaling_list_data_present_flag");

    if (pps_scaling_list_data_present_flag)
    {
        parse_scaling_list();
    }

    lists_modification_present_flag             = READ_FLAG("lists_modification_present_flag");
    log2_parallel_merge_level_minus2            = READ_UVLC("log2_parallel_merge_level_minus2");
    slice_segment_header_extension_present_flag = READ_FLAG("slice_segment_header_extension_present_flag");
    pps_extension_flag                          = READ_FLAG("pps_extension_flag");

    if (pps_extension_flag)
    {
    }

    bool rbsp_stop_one_bit = READ_FLAG("rbsp_stop_one_bit");

    p_pps->m_PPSId                               = pps_pic_parameter_set_id;
    p_pps->m_SPSId                               = pps_seq_parameter_set_id;
    p_pps->m_picInitQPMinus26                    = init_qp_minus26;
    p_pps->m_bSliceChromaQpFlag                  = pps_slice_chroma_qp_offsets_present_flag;

    p_pps->m_bUseWeightPred                      = weighted_pred_flag;
    p_pps->m_useWeightedBiPred                   = weighted_bipred_flag;

    p_pps->m_numRefIdxL0DefaultActive            = num_ref_idx_l0_default_active_minus1 + 1;
    p_pps->m_numRefIdxL1DefaultActive            = num_ref_idx_l1_default_active_minus1 + 1;
    
    p_pps->m_dependentSliceSegmentsEnabledFlag   = dependent_slice_segments_enabled_flag;
    p_pps->m_tilesEnabledFlag                    = tiles_enabled_flag;
    p_pps->m_entropyCodingSyncEnabledFlag        = entropy_coding_sync_enabled_flag;
    p_pps->m_numExtraSliceHeaderBits             = num_extra_slice_header_bits;
    p_pps->m_OutputFlagPresentFlag               = output_flag_present_flag;
    p_pps->m_cabacInitPresentFlag                = cabac_init_present_flag;
    p_pps->m_sliceHeaderExtensionPresentFlag     = slice_segment_header_extension_present_flag;
    p_pps->m_loopFilterAcrossSlicesEnabledFlag   = pps_loop_filter_across_slices_enabled_flag;
    p_pps->m_deblockingFilterControlPresentFlag  = deblocking_filter_control_present_flag;
    p_pps->m_deblockingFilterOverrideEnabledFlag = deblocking_filter_override_enabled_flag;
    p_pps->m_picDisableDeblockingFilterFlag      = pps_deblocking_filter_disabled_flag;
    p_pps->m_deblockingFilterBetaOffsetDiv2      = pps_beta_offset_div2;
    p_pps->m_deblockingFilterTcOffsetDiv2        = pps_tc_offset_div2;
    p_pps->m_listsModificationPresentFlag        = lists_modification_present_flag;
}

static void parse_aud()
{
    bool rbsp_stop_one_bit = false;
    
    READ_CODE(3, "pic_type");

    rbsp_stop_one_bit = READ_FLAG("rbsp_stop_one_bit");
}

/* profile_tier_level */
static void parse_ptl(uint32_t max_sub_layers_minus1)
{
    uint8_t general_profile_space;
    bool    general_tier_flag;
    uint8_t general_profile_idc;
    uint8_t general_profile_compatibility_flag[32];
    bool    general_progressive_source_flag;
    bool    general_interlaced_source_flag;
    bool    general_non_packed_constraint_flag;
    bool    general_frame_only_constraint_flag;
    uint64_t general_reserved_zero_44bits;
    uint8_t general_level_idc;
    
    bool    *sub_layer_profile_present_flag = new bool[max_sub_layers_minus1];
    bool    *sub_layer_level_present_flag = new bool[max_sub_layers_minus1];
    uint8_t *sub_layer_profile_space = new uint8_t[max_sub_layers_minus1];
    bool    *sub_layer_tier_flag = new bool[max_sub_layers_minus1];
    uint8_t *sub_layer_profile_idc = new uint8_t[max_sub_layers_minus1];
    
    uint32_t i;
    uint32_t j;
    
    general_profile_space   = READ_CODE(2, "general_profile_space");
    general_tier_flag       = READ_FLAG("general_tier_flag");
    general_profile_idc     = READ_CODE(5, "general_profile_idc");   
    
    for (j = 0; j < 32; j++)
    {
        general_profile_compatibility_flag[j] = READ_FLAG("general_profile_compatibility_flag[j]");
    }
    
    general_progressive_source_flag     = READ_FLAG("general_progressive_source_flag");
    general_interlaced_source_flag      = READ_FLAG("general_interlaced_source_flag");
    general_non_packed_constraint_flag  = READ_FLAG("general_non_packed_constraint_flag");
    general_frame_only_constraint_flag  = READ_FLAG("general_frame_only_constraint_flag");
    
    READ_CODE(16, "XXX_reserved_zero_44bits[0..15]");
    READ_CODE(16, "XXX_reserved_zero_44bits[16..31]");
    READ_CODE(12, "XXX_reserved_zero_44bits[32..43]");
    
    general_level_idc = READ_CODE(8, "general_level_idc");
    
    for (i = 0; i < max_sub_layers_minus1; i++)
    {
        sub_layer_profile_present_flag[i]   = READ_FLAG("sub_layer_profile_present_flag[i]");
        sub_layer_level_present_flag[i]     = READ_FLAG("sub_layer_level_present_flag[i]");
    }
    
    if (max_sub_layers_minus1 > 0)
    {
        for (i = max_sub_layers_minus1; i < 8; i++)
        {
            READ_CODE(2, "reserved_zero_2bits");
        }
    }
    
    for (i = 0; i < max_sub_layers_minus1; i++)
    {
        if (sub_layer_profile_present_flag[i])
        {
            sub_layer_profile_space[i]  = READ_CODE(2, "sub_layer_profile_space[i]");
            sub_layer_tier_flag[i]      = READ_CODE(1, "sub_layer_tier_flag[i]");
            sub_layer_profile_idc[i]    = READ_CODE(5, "sub_layer_profile_idc[i]");
        }
        
        if (sub_layer_level_present_flag[i])
        {
        }
    }
}

static void parse_slice_hdr(int nal_unit_type)
{
    bool        first_slice_segment_in_pic_flag;
    bool        no_output_of_prior_pics_flag;
    uint32_t    slice_pic_parameter_set_id      = 0;
    bool        dependent_slice_segment_flag    = false;
    uint32_t    slice_segment_address;
    SliceType   slice_type                      = B_SLICE;
    bool        pic_output_flag                 = true;
    uint32_t    colour_plane_id                 = 0;
    uint32_t    slice_pic_order_cnt_lsb         = 0;
    bool        short_term_ref_pic_set_sps_flag;
    uint32_t    short_term_ref_pic_set_idx;
    uint32_t    num_long_term_sps               = 0;
    uint32_t    num_long_term_pics;
    bool        slice_temporal_mvp_enabled_flag = false;

    bool        slice_sao_luma_flag             = false;
    bool        slice_sao_chroma_flag           = false;

    uint32_t    num_ref_idx_l0_active_minus1;
    uint32_t    num_ref_idx_l1_active_minus1;

    bool        mvd_l1_zero_flag                = false;
    bool        cabac_init_flag                 = false;

    uint32_t    five_minus_max_num_merge_cand;
    
    int32_t     slice_qp_delta;
    int32_t     slice_cb_qp_offset = 0;
    int32_t     slice_cr_qp_offset = 0;

    bool        deblocking_filter_override_flag         = false;

    uint32_t    num_entry_point_offsets                 = 0;
    
    SPS_t      *p_sps = NULL;
    PPS_t      *p_pps = NULL;


    first_slice_segment_in_pic_flag = READ_FLAG("first_slice_segment_in_pic_flag");

    if (nal_unit_type >= NAL_UNIT_CODED_SLICE_BLA_W_LP && nal_unit_type <= NAL_UNIT_RESERVED_IRAP_VCL23)
    {
        no_output_of_prior_pics_flag = READ_FLAG("no_output_of_prior_pics_flag");

        slice_type = I_SLICE;
    }

    slice_pic_parameter_set_id = READ_UVLC("slice_pic_parameter_set_id");
    
    p_pps = &pps[slice_pic_parameter_set_id];
    p_sps = &sps[p_pps->m_SPSId];

    num_ref_idx_l1_active_minus1 = p_pps->m_numRefIdxL1DefaultActive;
    
    bool        *slice_reserved_flag = new bool[p_pps->m_numExtraSliceHeaderBits];
    bool        slice_deblocking_filter_disabled_flag   = p_pps->m_picDisableDeblockingFilterFlag;
    int32_t     slice_beta_offset_div2                  = p_pps->m_deblockingFilterBetaOffsetDiv2;
    int32_t     slice_tc_offset_div2                    = p_pps->m_deblockingFilterTcOffsetDiv2;
    
    if (!first_slice_segment_in_pic_flag)
    {
        if (p_pps->m_dependentSliceSegmentsEnabledFlag)
        {
            dependent_slice_segment_flag = READ_FLAG("dependent_slice_segment_flag");
        }

        slice_segment_address   = READ_UVLC("slice_segment_address");
    }

    uint32_t i;
    if (!dependent_slice_segment_flag)
    {
        for (i = 0; i < p_pps->m_numExtraSliceHeaderBits; i++)
        {
            slice_reserved_flag[i] = READ_FLAG("slice_reserved_flag");
        }

        slice_type = (SliceType) READ_UVLC("slice_type");

        if (p_pps->m_OutputFlagPresentFlag)
        {
            pic_output_flag = READ_FLAG("pic_output_flag");
        }
        else
        {
            pic_output_flag = true;
        }

        if (p_sps->m_separateColourPlaneFlag)
        {
            colour_plane_id = READ_CODE(2, "colour_plane_id");
        }

        // IDR
        if (nal_unit_type == NAL_UNIT_CODED_SLICE_IDR_W_RADL || nal_unit_type == NAL_UNIT_CODED_SLICE_IDR_N_LP)
        {
            ReferencePictureSet_t *rps = &slice.m_LocalRPS;

            rps->m_numberOfNegativePictures = 0;
            rps->m_numberOfPositivePictures = 0;
            rps->m_numberOfLongtermPictures = 0;
            rps->m_numberOfPictures         = 0;

            slice.m_pcRPS = rps;
        }
        
        if (nal_unit_type != NAL_UNIT_CODED_SLICE_IDR_W_RADL && nal_unit_type != NAL_UNIT_CODED_SLICE_IDR_N_LP)
        {
            //printf("pSPS->m_uiBitsForPOC=%d\n", p_sps->m_uiBitsForPOC);
            slice_pic_order_cnt_lsb = READ_CODE(p_sps->m_uiBitsForPOC, "slice_pic_order_cnt_lsb");

            short_term_ref_pic_set_sps_flag = READ_FLAG("short_term_ref_pic_set_sps_flag");

            if (!short_term_ref_pic_set_sps_flag)
            {
                ReferencePictureSet_t *rps;

                slice.m_pcRPS   = &slice.m_LocalRPS;
                rps             = slice.m_pcRPS;
                
                parse_short_term_ref_pic_set(p_sps, rps, p_sps->m_RPSList.m_numberOfReferencePictureSets);
            }
            else if (p_sps->m_RPSList.m_numberOfReferencePictureSets > 1)
            {

                uint32_t numBits = 0;

                while ((1 << numBits) < p_sps->m_RPSList.m_numberOfReferencePictureSets)
                {
                    numBits++;
                }

                if (numBits)
                {
                    short_term_ref_pic_set_idx = READ_CODE(numBits, "short_term_ref_pic_set_idx");
                }
                else
                {
                    short_term_ref_pic_set_idx = 0;
                }
            }

            if (p_sps->m_bLongTermRefsPresent)
            {
                if (p_sps->m_numLongTermRefPicSPS > 0)
                {
                    num_long_term_sps = READ_UVLC("num_long_term_sps");
                }

                uint32_t bitsForLtrpInSPS = 0;
                while (p_sps->m_numLongTermRefPicSPS > (1 << bitsForLtrpInSPS))
                {
                    bitsForLtrpInSPS++;
                }

                num_long_term_pics  = READ_UVLC("num_long_term_pics");

                uint32_t    *lt_idx_sps = new uint32_t[num_long_term_sps + num_long_term_pics];
                uint32_t    *poc_lsb_lt = new uint32_t[num_long_term_sps + num_long_term_pics];
                bool        *used_by_curr_pic_lt_flag = new bool[num_long_term_sps + num_long_term_pics];
                bool        *delta_poc_msb_present_flag = new bool[num_long_term_sps + num_long_term_pics];
                uint32_t    *delta_poc_msb_cycle_lt = new uint32_t[num_long_term_sps + num_long_term_pics];
                
                for (i = 0; i < num_long_term_sps + num_long_term_pics; i++)
                {
                    if (i < num_long_term_sps)
                    {
                        if (bitsForLtrpInSPS > 0)
                        {
                            lt_idx_sps[i] = READ_CODE(bitsForLtrpInSPS, "lt_idx_sps[i]");
                        }
                    }
                    else
                    {
                        poc_lsb_lt[i] = READ_CODE(p_sps->m_uiBitsForPOC, "poc_lsb_lt[i]");
                        used_by_curr_pic_lt_flag[i] = READ_FLAG("used_by_curr_pic_lt_flag[i]");
                    }

                    delta_poc_msb_present_flag[i] = READ_FLAG("delta_poc_msb_present_flag[i]");
                    if (delta_poc_msb_present_flag[i])
                    {
                        delta_poc_msb_cycle_lt[i] = READ_UVLC("delta_poc_msb_cycle_lt[i]");
                    }
                }
            }

            if (p_sps->m_TMVPFlagsPresent)
            {
                slice_temporal_mvp_enabled_flag = READ_FLAG("slice_temporal_mvp_enabled_flag");
            }
        }

        if (p_sps->m_bUseSAO)
        {
            slice_sao_luma_flag     = READ_FLAG("slice_sao_luma_flag");
            slice_sao_chroma_flag   = READ_FLAG("slice_sao_chroma_flag");
        }

        if (slice_type == P_SLICE || slice_type == B_SLICE)
        {
            bool num_ref_idx_active_override_flag = READ_FLAG("num_ref_idx_active_override_flag");
            if (num_ref_idx_active_override_flag)
            {
                num_ref_idx_l0_active_minus1 = READ_UVLC("num_ref_idx_l0_active_minus1");

                slice.m_aiNumRefIdx[0] = num_ref_idx_l0_active_minus1 + 1;

                if (slice_type == B_SLICE)
                {
                    num_ref_idx_l1_active_minus1    = READ_UVLC("num_ref_idx_l1_active_minus1");                   
                    slice.m_aiNumRefIdx[1]          = num_ref_idx_l1_active_minus1 + 1;
                }
                else
                {
                    slice.m_aiNumRefIdx[1]          = 0;
                }
            }
            else
            {
                num_ref_idx_l0_active_minus1    = p_pps->m_numRefIdxL0DefaultActive - 1;
                slice.m_aiNumRefIdx[0]          = p_pps->m_numRefIdxL0DefaultActive;

                if (slice_type == B_SLICE)
                {
                    num_ref_idx_l1_active_minus1    = p_pps->m_numRefIdxL1DefaultActive - 1;
                    slice.m_aiNumRefIdx[1]          = p_pps->m_numRefIdxL1DefaultActive;
                }
                else
                {
                    slice.m_aiNumRefIdx[1]          = 0;
                }
            }

            if (p_pps->m_listsModificationPresentFlag && getNumRpsCurrTempList(&slice) > 1)
            {
                ref_pic_lists_modification(&slice);
            }

            if (slice_type == B_SLICE)
            {
                mvd_l1_zero_flag = READ_FLAG("mvd_l1_zero_flag");
            }

            if (p_pps->m_cabacInitPresentFlag)
            {
                cabac_init_flag = READ_FLAG("cabac_init_flag");
                slice.m_cabacInitFlag = cabac_init_flag;
            }

            if (slice_temporal_mvp_enabled_flag)
            {
                bool collocated_from_l0_flag = true;
                if (slice_type == B_SLICE)
                {
                    collocated_from_l0_flag = READ_FLAG("collocated_from_l0_flag");
                }

                if ((collocated_from_l0_flag && num_ref_idx_l0_active_minus1 > 0)
                    || (!collocated_from_l0_flag && num_ref_idx_l1_active_minus1 > 0))
                {
                    uint32_t collocated_ref_idx;

                    collocated_ref_idx = READ_UVLC("collocated_ref_idx");
                }
            }

            if ((p_pps->m_bUseWeightPred && slice_type == P_SLICE)
                || (p_pps->m_useWeightedBiPred && slice_type == B_SLICE))
            {
                pred_weight_table(p_sps);
            }

            five_minus_max_num_merge_cand = READ_UVLC("five_minus_max_num_merge_cand");
        }

        slice_qp_delta = READ_SVLC("slice_qp_delta");
        slice.m_iSliceQp = p_pps->m_picInitQPMinus26 + 26 + slice_qp_delta;

        if (p_pps->m_bSliceChromaQpFlag)
        {
            slice_cb_qp_offset = READ_SVLC("slice_cb_qp_offset");
            slice_cr_qp_offset = READ_SVLC("slice_cr_qp_offset");
        }

        if (p_pps->m_deblockingFilterOverrideEnabledFlag)
        {
            deblocking_filter_override_flag = READ_FLAG("deblocking_filter_override_flag");
        }

        if (deblocking_filter_override_flag)
        {
            slice_deblocking_filter_disabled_flag = READ_FLAG("slice_deblocking_filter_disabled_flag");
            if (slice_deblocking_filter_disabled_flag)
            {
                slice_beta_offset_div2  = READ_SVLC("slice_beta_offset_div2");
                slice_tc_offset_div2    = READ_SVLC("slice_tc_offset_div2");
            }
        }

        if (p_pps->m_loopFilterAcrossSlicesEnabledFlag && 
            (slice_sao_luma_flag || slice_sao_chroma_flag || !slice_deblocking_filter_disabled_flag))
        {
            bool slice_loop_filter_across_slices_enabled_flag;
            slice_loop_filter_across_slices_enabled_flag = READ_FLAG("slice_loop_filter_across_slices_enabled_flag");
        }
    }

    if (p_pps->m_tilesEnabledFlag || p_pps->m_entropyCodingSyncEnabledFlag)
    {
        num_entry_point_offsets = READ_UVLC("num_entry_point_offsets");

        uint32_t *entry_point_offset_minus1 = new uint32_t[num_entry_point_offsets];

        if (num_entry_point_offsets > 0)
        {
            uint32_t offset_len_minus1;

            offset_len_minus1 = READ_UVLC("offset_len_minus1");

            for (i = 0; i < num_entry_point_offsets; i++)
            {
                entry_point_offset_minus1[i] = READ_CODE(offset_len_minus1 + 1, "entry_point_offset_minus1");
            }
        }
    }

    if (p_pps->m_sliceHeaderExtensionPresentFlag)
    {
        int i;

        uint32_t slice_segment_header_extension_length;
        
        slice_segment_header_extension_length = READ_UVLC("slice_segment_header_extension_length");

        uint8_t *slice_segment_header_extension_data_byte = new uint8_t[slice_segment_header_extension_length];

        for (i = 0; i < slice_segment_header_extension_length; i++)
        {
            slice_segment_header_extension_data_byte[i] = READ_CODE(8, "slice_segment_header_extension_data_byte");
        }
    }

    slice.m_saoEnabledFlag                  = slice_sao_luma_flag;
    slice.m_saoEnabledFlagChroma            = slice_sao_chroma_flag;
    slice.m_iPPSId                          = slice_pic_parameter_set_id;
    slice.m_PicOutputFlag                   = pic_output_flag;
    
    slice.m_iPOC                            = slice_pic_order_cnt_lsb; 
    slice.m_eSliceType                      = slice_type;
    
    slice.m_dependentSliceSegmentFlag       = dependent_slice_segment_flag;
    slice.m_deblockingFilterDisable         = slice_deblocking_filter_disabled_flag;
    slice.m_deblockingFilterBetaOffsetDiv2  = slice_beta_offset_div2;
    slice.m_deblockingFilterTcOffsetDiv2    = slice_tc_offset_div2;

    slice.m_iSliceQpDeltaCb                 = slice_cb_qp_offset;
    slice.m_iSliceQpDeltaCr                 = slice_cr_qp_offset;

    slice.m_bLMvdL1Zero                     = mvd_l1_zero_flag;
    slice.m_numEntryPointOffsets            = num_entry_point_offsets;
    slice.m_enableTMVPFlag                  = slice_temporal_mvp_enabled_flag;

    char *p_str;

    switch (slice_type)
    {
        case I_SLICE:
        {
            p_str = "I Frame";
            break;
        }
        case P_SLICE:
        {
            p_str = "P Frame";
            break;
        }
        case B_SLICE:
        default:
        {
            p_str = "B Frame";
            break;
        }
    }

    pInfo += sprintf(pInfo, "[%4d] %s\n", u32frameCnt++, p_str);
}

static void pred_weight_table(SPS_t *pSPS)
{
    uint32_t    luma_log2_weight_denom;
    int32_t     delta_chroma_log2_weight_denom;
    
    bool        *luma_weight_l0_flag = new bool[slice.m_aiNumRefIdx[0]];
    bool        *luma_weight_l1_flag = new bool[slice.m_aiNumRefIdx[1]];

    bool        *chroma_weight_l0_flag = new bool[slice.m_aiNumRefIdx[0]];
    bool        *chroma_weight_l1_flag = new bool[slice.m_aiNumRefIdx[1]];
    
    int32_t     *delta_luma_weight_l0 = new int32_t[slice.m_aiNumRefIdx[0]];
    int32_t     *delta_luma_weight_l1= new int32_t[slice.m_aiNumRefIdx[1]];
    
    int32_t     *luma_offset_l0 = new int32_t[slice.m_aiNumRefIdx[0]];
    int32_t     *luma_offset_l1= new int32_t[slice.m_aiNumRefIdx[1]];

    int32_t     (*delta_chroma_weight_l0)[2]= new int32_t[slice.m_aiNumRefIdx[0]][2];
    int32_t     (*delta_chroma_weight_l1)[2]= new int32_t[slice.m_aiNumRefIdx[1]][2];
    
    int32_t     (*delta_chroma_offset_l0)[2]= new int32_t[slice.m_aiNumRefIdx[0]][2];
    int32_t     (*delta_chroma_offset_l1)[2]= new int32_t[slice.m_aiNumRefIdx[1]][2];
    

    luma_log2_weight_denom = READ_UVLC("luma_log2_weight_denom");

    if (pSPS->m_chromaFormatIdc != 0)
    {
        delta_chroma_log2_weight_denom = READ_SVLC("delta_chroma_log2_weight_denom");
    }

    int i, j;
    for (i = 0; i < slice.m_aiNumRefIdx[0]; i++)
    {
        luma_weight_l0_flag[i] = READ_FLAG("luma_weight_l0_flag");
    }

    if (pSPS->m_chromaFormatIdc != 0)
    {
        for (i = 0; i < slice.m_aiNumRefIdx[0]; i++)
        {
            chroma_weight_l0_flag[i] = READ_FLAG("chroma_weight_l0_flag");
        }
    }

    for (i = 0; i < slice.m_aiNumRefIdx[0]; i++)
    {
        if (luma_weight_l0_flag[i])
        {
            delta_luma_weight_l0[i] = READ_SVLC("delta_luma_weight_l0");
            luma_offset_l0[i]       = READ_SVLC("luma_offset_l0");
        }

        if (chroma_weight_l0_flag[i])
        {
            for (j = 0; j < 2; j++)
            {
                delta_chroma_weight_l0[i][j] = READ_SVLC("delta_chroma_weight_l0");
                delta_chroma_offset_l0[i][j] = READ_SVLC("delta_chroma_offset_l0");
            }
        }    
    }

    if (slice.m_eSliceType == B_SLICE)
    {
        for (i = 0; i < slice.m_aiNumRefIdx[1]; i++)
        {
            luma_weight_l1_flag[i] = READ_FLAG("luma_weight_l1_flag");
        }

        if (pSPS->m_chromaFormatIdc != 0)
        {
            for (i = 0; i < slice.m_aiNumRefIdx[1]; i++)
            {
                chroma_weight_l1_flag[i] = READ_FLAG("chroma_weight_l1_flag");
            }
        }

        for (i = 0; i < slice.m_aiNumRefIdx[1]; i++)
        {
            if (luma_weight_l1_flag[i])
            {
                delta_luma_weight_l1[i] = READ_SVLC("delta_luma_weight_l1");
                luma_offset_l1[i]       = READ_SVLC("luma_offset_l1");
            }

            if (chroma_weight_l1_flag[i])
            {
                for (j = 0; j < 2; j++)
                {
                    delta_chroma_weight_l1[i][j] = READ_SVLC("delta_chroma_weight_l1");
                    delta_chroma_offset_l1[i][j] = READ_SVLC("delta_chroma_offset_l1");
                }
            }    
        }
    }
}

static void ref_pic_lists_modification(Slice_t *pSlice)
{
    uint32_t i;
    
    bool ref_pic_list_modification_flag_l0 = READ_FLAG("ref_pic_list_modification_flag_l0");

    pSlice->m_RefPicListModification.m_bRefPicListModificationFlagL0 = ref_pic_list_modification_flag_l0;
    
    if (ref_pic_list_modification_flag_l0)
    {
        uint32_t list_entry_l0[32];
        uint32_t numRpsCurrTempList0 = getNumRpsCurrTempList(pSlice);

        if (numRpsCurrTempList0 > 1)
        {
            uint32_t length = 1;
            numRpsCurrTempList0--;
            while (numRpsCurrTempList0 >>= 1)
            {
                length++;
            }
 
            for (i = 0; i < pSlice->m_aiNumRefIdx[0]; i++)
            {
                list_entry_l0[i] = READ_CODE(length, "list_entry_l0");
            }
        }
        else
        {
            for (i = 0; i < pSlice->m_aiNumRefIdx[0]; i ++)
            {
                list_entry_l0[i] = 0;
            }
        }
        
        memcpy(pSlice->m_RefPicListModification.m_RefPicSetIdxL0, list_entry_l0, sizeof(list_entry_l0));
    }

    if (pSlice->m_eSliceType == B_SLICE)
    {
        bool ref_pic_list_modification_flag_l1 = READ_FLAG("ref_pic_list_modification_flag_l1");

        pSlice->m_RefPicListModification.m_bRefPicListModificationFlagL1 = ref_pic_list_modification_flag_l1;
        
        if (ref_pic_list_modification_flag_l1)
        {
            uint32_t list_entry_l1[32];
            uint32_t numRpsCurrTempList1 = getNumRpsCurrTempList(pSlice);

            if (numRpsCurrTempList1 > 1)
            {
                uint32_t length = 1;
                numRpsCurrTempList1--;
                while (numRpsCurrTempList1 >>= 1)
                {
                    length++;
                }
     
                for (i = 0; i < pSlice->m_aiNumRefIdx[1]; i++)
                {
                    list_entry_l1[i] = READ_CODE(length, "list_entry_l1");
                }
            }
            else
            {
                for (i = 0; i < pSlice->m_aiNumRefIdx[1]; i++)
                {
                    list_entry_l1[i] = 0;
                }
            }

            memcpy(pSlice->m_RefPicListModification.m_RefPicSetIdxL1, list_entry_l1, sizeof(list_entry_l1));
        }
    }
}

/** decode quantization matrix */
static void parse_scaling_list()
{
    int         sizeId;
    int         matrixId;
    bool        scaling_list_pred_mode_flag[4][6];
    uint32_t    scaling_list_pred_matrix_id_delta[4][6];

    for (sizeId = 0; sizeId < 4; sizeId++)
    {
        for (matrixId = 0; matrixId < ( (sizeId == 3) ? 2 : 6 ); matrixId++)
        {
            scaling_list_pred_mode_flag[sizeId][matrixId] = READ_FLAG("scaling_list_pred_mode_flag[sizeId][matrixId]");

            if (!scaling_list_pred_mode_flag[sizeId][matrixId])
            {
                scaling_list_pred_matrix_id_delta[sizeId][matrixId] = READ_UVLC("scaling_list_pred_matrix_id_delta[sizeId][matrixId]");
            }
            else
            {
                uint32_t    nextCoef;
                uint32_t    coefNum;
                int32_t     scaling_list_dc_coef_minus8[4][6];

                nextCoef    = 8;
                coefNum     = min(64, (1 << (4 + (sizeId << 1))));

                if (sizeId > 1)
                {
                    scaling_list_dc_coef_minus8[sizeId - 2][matrixId] = READ_SVLC("scaling_list_dc_coef_minus8");

                    nextCoef = scaling_list_dc_coef_minus8[sizeId - 2][matrixId] + 8;
                }

                int i;
                for (i = 0; i < coefNum; i++)
                {
                    int32_t scaling_list_delta_coef;

                    scaling_list_delta_coef = READ_SVLC("scaling_list_delta_coef");
                    nextCoef = (nextCoef + scaling_list_delta_coef + 256) % 256;
                    ScalingList[sizeId][matrixId][i] = nextCoef;
                }
            }
        }
    }
}

static void parse_short_term_ref_pic_set(SPS_t *pSPS, ReferencePictureSet_t *rps, uint32_t stRpsIdx)
{   
    bool        inter_ref_pic_set_prediction_flag = false;
    uint32_t    delta_idx_minus1 = 0;
    
    if (stRpsIdx != 0)
    {
        inter_ref_pic_set_prediction_flag = READ_FLAG("inter_ref_pic_set_prediction_flag");
    }

    if (inter_ref_pic_set_prediction_flag)
    {
        bool delta_rps_sign;
        uint32_t abs_delta_rps_minus1;
        
        if (stRpsIdx == pSPS->m_RPSList.m_numberOfReferencePictureSets)
        {
            delta_idx_minus1 = READ_UVLC("delta_idx_minus1");
        }

        int32_t rIdx;
        ReferencePictureSet_t *rpsRef;

        rIdx    = stRpsIdx - 1 - delta_idx_minus1;
        rpsRef  = &pSPS->m_RPSList.m_referencePictureSets[rIdx];
 
        delta_rps_sign          = READ_FLAG("delta_rps_sign");
        abs_delta_rps_minus1    = READ_UVLC("abs_delta_rps_minus1");

        int j;

        bool *used_by_curr_pic_flag = new bool[rpsRef->m_numberOfPictures + 1];
        bool *use_delta_flag = new bool[rpsRef->m_numberOfPictures + 1];
        for (j = 0; j <= rpsRef->m_numberOfPictures; j++)
        {
            used_by_curr_pic_flag[j] = READ_FLAG("used_by_curr_pic_flag[i]");

            if (!used_by_curr_pic_flag[j])
            {
                use_delta_flag[j] = READ_FLAG("use_delta_flag[j]");
            }
        }
    }
    else
    {
        uint32_t num_negative_pics;
        uint32_t num_positive_pics;

        num_negative_pics = READ_UVLC("num_negative_pics");
        num_positive_pics = READ_UVLC("num_positive_pics");

        rps->m_numberOfNegativePictures = num_negative_pics;
        rps->m_numberOfPositivePictures = num_positive_pics;

        int i;
        uint32_t *delta_poc_s0_minus1= new uint32_t[num_negative_pics];
        bool     *used_by_curr_pic_s0_flag = new bool[num_negative_pics];
        for (i = 0; i < num_negative_pics; i++)
        {
            delta_poc_s0_minus1[i]      = READ_UVLC("delta_poc_s0_minus1[i]");
            used_by_curr_pic_s0_flag[i] = READ_FLAG("used_by_curr_pic_s0_flag[i]");

            rps->m_used[i]              = used_by_curr_pic_s0_flag[i];
        }

        uint32_t *delta_poc_s1_minus1 = new uint32_t[num_positive_pics];
        bool     *used_by_curr_pic_s1_flag = new bool[num_positive_pics];
        for (i = 0; i < num_positive_pics; i++)
        {
            delta_poc_s1_minus1[i]      = READ_UVLC("delta_poc_s1_minus1[i]");
            used_by_curr_pic_s1_flag[i] = READ_FLAG("used_by_curr_pic_s1_flag[i]");

            rps->m_used[i + num_negative_pics]  = used_by_curr_pic_s1_flag[i];
        }

        rps->m_numberOfPictures = rps->m_numberOfNegativePictures + rps->m_numberOfPositivePictures;
    }
}

static void parse_vui(uint32_t maxNumSubLayersMinus1)
{
    bool            aspect_ratio_info_present_flag;
    int  aspect_ratio_idc = ASPECT_RATIO_UNSPECIFIED;
    uint16_t        sar_width;
    uint16_t        sar_height;

    aspect_ratio_info_present_flag = READ_FLAG("aspect_ratio_info_present_flag");

    if (aspect_ratio_info_present_flag)
    {
        aspect_ratio_idc = READ_CODE(8, "aspect_ratio_idc");

        if (aspect_ratio_idc == ASPECT_RATIO_EXTENDED_SAR)
        {
            sar_width   = READ_CODE(16, "sar_width");
            sar_height  = READ_CODE(16, "sar_height");
        }
    }

    bool overscan_info_present_flag;
    bool overscan_appropriate_flag;

    overscan_info_present_flag = READ_FLAG("overscan_info_present_flag");

    if (overscan_info_present_flag)
    {
        overscan_appropriate_flag = READ_FLAG("overscan_appropriate_flag");
    }

    bool    video_signal_type_present_flag;
    uint8_t video_format;
    bool    video_full_range_flag;
    bool    colour_description_present_flag;
    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coeffs;
    
    video_signal_type_present_flag = READ_FLAG("video_signal_type_present_flag");

    if (video_signal_type_present_flag)
    {
        video_format            = READ_CODE(3, "video_format");
        video_full_range_flag   = READ_FLAG("video_full_range_flag");
        colour_description_present_flag = READ_FLAG("colour_description_present_flag");

        if (colour_description_present_flag)
        {
            colour_primaries            = READ_CODE(8, "colour_primaries");
            transfer_characteristics    = READ_CODE(8, "transfer_characteristics");
            matrix_coeffs               = READ_CODE(8, "matrix_coeffs");
        }
    }

    bool        chroma_loc_info_present_flag;
    uint32_t    chroma_sample_loc_type_top_field;
    uint32_t    chroma_sample_loc_type_bottom_field;

    chroma_loc_info_present_flag = READ_FLAG("chroma_loc_info_present_flag");

    if (chroma_loc_info_present_flag)
    {
        chroma_sample_loc_type_top_field    = READ_UVLC("chroma_sample_loc_type_top_field");
        chroma_sample_loc_type_bottom_field = READ_UVLC("chroma_sample_loc_type_bottom_field");
    }

    bool        neutral_chroma_indication_flag;
    bool        field_seq_flag;
    bool        frame_field_info_present_flag;
    bool        default_display_window_flag;
    uint32_t    def_disp_win_left_offset;
    uint32_t    def_disp_win_right_offset;
    uint32_t    def_disp_win_top_offset;
    uint32_t    def_disp_win_bottom_offset;
  
    neutral_chroma_indication_flag  = READ_FLAG("neutral_chroma_indication_flag");
    field_seq_flag                  = READ_FLAG("field_seq_flag");
    frame_field_info_present_flag   = READ_FLAG("frame_field_info_present_flag");
    default_display_window_flag     = READ_FLAG("default_display_window_flag");

    if (default_display_window_flag)
    {
        def_disp_win_left_offset    = READ_UVLC("def_disp_win_left_offset");
        def_disp_win_right_offset   = READ_UVLC("def_disp_win_right_offset");
        def_disp_win_top_offset     = READ_UVLC("def_disp_win_top_offset");
        def_disp_win_bottom_offset  = READ_UVLC("def_disp_win_bottom_offset");
    }


    bool        vui_timing_info_present_flag;
    uint32_t    vui_num_units_in_tick;
    uint32_t    vui_time_scale;
    bool        vui_poc_proportional_to_timing_flag;
    uint32_t    vui_num_ticks_poc_diff_one_minus1;
    bool        vui_hrd_parameters_present_flag;

    vui_timing_info_present_flag = READ_FLAG("vui_timing_info_present_flag");

    if (vui_timing_info_present_flag)
    {
        vui_num_units_in_tick   = READ_CODE(32, "vui_num_units_in_tick");
        vui_time_scale          = READ_CODE(32, "vui_time_scale");
        vui_poc_proportional_to_timing_flag = READ_FLAG("vui_poc_proportional_to_timing_flag");

        if (vui_poc_proportional_to_timing_flag)
        {
            vui_num_ticks_poc_diff_one_minus1 = READ_UVLC("vui_num_ticks_poc_diff_one_minus1");
        }

        vui_hrd_parameters_present_flag = READ_FLAG("hrd_parameters_present_flag");

        if (vui_hrd_parameters_present_flag)
        {
            parse_hrd(true, maxNumSubLayersMinus1);
        }
    }

    bool        bitstream_restriction_flag;
    bool        tiles_fixed_structure_flag;
    bool        motion_vectors_over_pic_boundaries_flag;
    bool        restricted_ref_pic_lists_flag;
    uint32_t    min_spatial_segmentation_idc;
    uint32_t    max_bytes_per_pic_denom;
    uint32_t    max_bits_per_min_cu_denom;
    uint32_t    log2_max_mv_length_horizontal;
    uint32_t    log2_max_mv_length_vertical;
    
    bitstream_restriction_flag = READ_FLAG("bitstream_restriction_flag");

    if (bitstream_restriction_flag)
    {
        tiles_fixed_structure_flag              = READ_FLAG("tiles_fixed_structure_flag");
        motion_vectors_over_pic_boundaries_flag = READ_FLAG("motion_vectors_over_pic_boundaries_flag");
        restricted_ref_pic_lists_flag           = READ_FLAG("restricted_ref_pic_lists_flag");
        min_spatial_segmentation_idc            = READ_UVLC("min_spatial_segmentation_idc");
        max_bytes_per_pic_denom                 = READ_UVLC("max_bytes_per_pic_denom");
        max_bits_per_min_cu_denom               = READ_UVLC("max_bits_per_min_cu_denom");
        log2_max_mv_length_horizontal           = READ_UVLC("log2_max_mv_length_horizontal");
        log2_max_mv_length_vertical             = READ_UVLC("log2_max_mv_length_vertical");
    }
}

static void parse_hrd(bool commonInfPresentFlag, uint32_t maxNumSubLayersMinus1)
{
    int         i;
    bool        nal_hrd_parameters_present_flag;
    bool        vcl_hrd_parameters_present_flag;
    uint32_t    bit_rate_scale;
    uint32_t    cpb_size_scale;
    uint32_t    cpb_size_du_scale;
    uint32_t    initial_cpb_removal_delay_length_minus1;
    uint32_t    au_cpb_removal_delay_length_minus1;
    uint32_t    dpb_output_delay_length_minus1;
    
    if (commonInfPresentFlag)
    {
        nal_hrd_parameters_present_flag = READ_FLAG("nal_hrd_parameters_present_flag");
        vcl_hrd_parameters_present_flag = READ_FLAG("vcl_hrd_parameters_present_flag");

        if (nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag)
        {
            sub_pic_hrd_params_present_flag = READ_FLAG("sub_pic_hrd_params_present_flag");

            if (sub_pic_hrd_params_present_flag)
            {
                uint32_t    tick_divisor_minus2;
                uint32_t    du_cpb_removal_delay_increment_length_minus1;
                bool        sub_pic_cpb_params_in_pic_timing_sei_flag;
                uint32_t    dpb_output_delay_du_length_minus1;
                
                tick_divisor_minus2 = READ_CODE(8, "tick_divisor_minus2");
                du_cpb_removal_delay_increment_length_minus1 = READ_CODE(5, "du_cpb_removal_delay_increment_length_minus1");
                sub_pic_cpb_params_in_pic_timing_sei_flag = READ_FLAG("sub_pic_cpb_params_in_pic_timing_sei_flag");
                dpb_output_delay_du_length_minus1 = READ_CODE(5, "dpb_output_delay_du_length_minus1");
            }

            bit_rate_scale = READ_CODE(4, "bit_rate_scale");
            cpb_size_scale = READ_CODE(4, "cpb_size_scale");

            if (sub_pic_hrd_params_present_flag)
            {
                cpb_size_du_scale = READ_CODE(4, "cpb_size_du_scale");
            }

            initial_cpb_removal_delay_length_minus1 = READ_CODE(5, "initial_cpb_removal_delay_length_minus1");
            au_cpb_removal_delay_length_minus1      = READ_CODE(5, "au_cpb_removal_delay_length_minus1");
            dpb_output_delay_length_minus1          = READ_CODE(5, "dpb_output_delay_length_minus1");
        }
    }

    bool        *fixed_pic_rate_general_flag     = new bool[maxNumSubLayersMinus1 + 1];
    bool        *fixed_pic_rate_within_cvs_flag  = new bool[maxNumSubLayersMinus1 + 1];
    uint32_t    *elemental_duration_in_tc_minus1  = new uint32_t[maxNumSubLayersMinus1 + 1];
    bool        *low_delay_hrd_flag              = new bool[maxNumSubLayersMinus1 + 1];
    uint32_t    *cpb_cnt_minus1                  = new uint32_t[maxNumSubLayersMinus1 + 1];
    
    for (i = 0; i <= maxNumSubLayersMinus1; i++)
    {
        fixed_pic_rate_general_flag[i] = READ_FLAG("fixed_pic_rate_general_flag[i]");

        if (!fixed_pic_rate_general_flag[i])
        {
            fixed_pic_rate_within_cvs_flag[i] = READ_FLAG("fixed_pic_rate_within_cvs_flag[i]");
        }
        else
        {
            fixed_pic_rate_within_cvs_flag[i] = true;
        }
        
        if (fixed_pic_rate_within_cvs_flag[i])
        {
            elemental_duration_in_tc_minus1[maxNumSubLayersMinus1 + 1] = READ_UVLC("elemental_duration_in_tc_minus1[i]");
        }
        else
        {
            low_delay_hrd_flag[i] = READ_FLAG("low_delay_hrd_flag[i]");
        }

        if (!low_delay_hrd_flag[i])
        {
            cpb_cnt_minus1[i] = READ_UVLC("cpb_cnt_minus1[i]");
        }

        if (nal_hrd_parameters_present_flag)
        {
            parse_sub_layer_hrd_params(cpb_cnt_minus1[i]);
        }

        if (vcl_hrd_parameters_present_flag)
        {
            parse_sub_layer_hrd_params(cpb_cnt_minus1[i]);
        }
    }
}

static void parse_sub_layer_hrd_params(uint32_t CpbCnt)
{
    int i;

    uint32_t *bit_rate_value_minus1 = new uint32_t[CpbCnt + 1];
    uint32_t *cpb_size_value_minus1= new uint32_t[CpbCnt + 1];
    uint32_t *cpb_size_du_value_minus1= new uint32_t[CpbCnt + 1];
    uint32_t *bit_rate_du_value_minus1= new uint32_t[CpbCnt + 1];
    bool     *cbr_flag = new bool[CpbCnt + 1];

    for (i = 0; i <= CpbCnt; i++)
    {
        bit_rate_value_minus1[i] = READ_UVLC("bit_rate_value_minus1[i]");
        cpb_size_value_minus1[i] = READ_UVLC("cpb_size_value_minus1[i]");

        if (sub_pic_hrd_params_present_flag)
        {
            cpb_size_du_value_minus1[i] = READ_UVLC("cpb_size_du_value_minus1[i]");
            bit_rate_du_value_minus1[i] = READ_UVLC("bit_rate_du_value_minus1[i]");
        }

        cbr_flag[i] = READ_FLAG("cbr_flag[i]");
    }
}

bool fill_es_buffer
(
    uint8_t *pu8NalAddr,
    uint32_t u32NalSize,
    int fd
)
{
    //ssize_t rd_sz;
    size_t rd_sz;
    
    memmove(u8EsBuffer, pu8NalAddr, u32NalSize);

    rd_sz = read(fd, &u8EsBuffer[u32NalSize], ES_BUFFER_SIZE - u32NalSize);
    if (rd_sz == 0) // EOF
    {
        return false;
    }
    else if (rd_sz < (ES_BUFFER_SIZE - u32NalSize))  // last read!  
    {
        printf("last read!\n");
        
        // Append Stop Code at the end of last read
        memcpy(&u8EsBuffer[u32NalSize + rd_sz], u8endCode, sizeof(u8endCode));        
    }
    else
    {
        // Append Stop Code at the end of buffer
        memcpy(&u8EsBuffer[ES_BUFFER_SIZE], u8endCode, sizeof(u8endCode));
    }

    return true;
}

int main(int argc, const char * argv[])
{
    int fd;
    size_t rd_sz;
        
    if (argc < 2)
    {
        printf("useage: %s [input_file]\n", argv[0]);
        
        return -1;
    }


    fd = open(argv[1], O_RDONLY);
    if (fd < 0)
    {
        perror(argv[1]);
        exit(-1);
    }
    
    uint8_t *ptr = u8EsBuffer;
    uint8_t nal_unit_header[SIZE_OF_NAL_UNIT_HDR];
    
    bool        forbidden_zero_bit;
    int nal_unit_type;
    uint8_t     nuh_layer_id;
    uint8_t     nuh_temporal_id_plus1;
    uint32_t    nal_len;
    uint32_t    offset = 0;
    uint32_t    prefix_len = 0;

    fill_es_buffer(u8EsBuffer, 0, fd);
    
    while (1)
    {        
        if (!scan_nal
             (
                ptr,
                nal_unit_header,
                &nal_len,
                &prefix_len
             )
           )
        {
            // fill buffer
            printf("fill buffer!\n");
            
            if (!fill_es_buffer(ptr, nal_len, fd))
            {
                printf("No more data to read!\n");
                break;
            }

            // rewind ptr
            ptr = u8EsBuffer;
            offset = 0;

            // try scan NAL again!
            bool rescan = scan_nal
             (
                ptr,
                nal_unit_header,
                &nal_len,
                &prefix_len
             );

            printf("Try rescan NAL=%s\n", rescan ? "T" : "F");
        }

        printf("offset=%ld\n", ptr - u8EsBuffer);

        nal_unit_type           = (nal_unit_header[0] & (BIT6 | BIT5 | BIT4 | BIT3 | BIT2 | BIT1)) >> 1;
        
        forbidden_zero_bit      = (nal_unit_header[0] & BIT7) >> 7;
        nuh_layer_id            = (nal_unit_header[0] & BIT0) << 5 | (nal_unit_header[1] & (BIT7 | BIT6 | BIT5 | BIT4 | BIT3)) >> 3;
        nuh_temporal_id_plus1   = nal_unit_header[1] & (BIT2 | BIT1 | BIT0);

        printf("forbidden_zero_bit=%d, nal_unit_type=%02u, nuh_layer_id=%u, nuh_temporal_id_plus1=%u, nal_len=%6u, offset=0x%x\n",
               forbidden_zero_bit,
               nal_unit_type,
               nuh_layer_id,
               nuh_temporal_id_plus1,
               nal_len,
               offset);
        
        
        m_pcBitstream.m_fifo            = &u8EsBuffer[offset + prefix_len + SIZE_OF_NAL_UNIT_HDR];
        m_pcBitstream.m_fifo_size       = nal_len - prefix_len - SIZE_OF_NAL_UNIT_HDR;
        m_pcBitstream.m_fifo_idx        = 0;
        m_pcBitstream.m_num_held_bits   = 0;
        m_pcBitstream.m_held_bits       = 0;
        m_pcBitstream.m_numBitsRead     = 0;
        
        switch (nal_unit_type)
        {
            case NAL_UNIT_VPS:
            {             
                EBSPtoRBSP(&u8EsBuffer[offset + prefix_len], nal_len, 0);
                
                parse_vps(&vps);
                
                break;
            }
            case NAL_UNIT_SPS:
            {              
                EBSPtoRBSP(&u8EsBuffer[offset + prefix_len], nal_len, 0);
                
                parse_sps();
                
                break;
            }
            case NAL_UNIT_PPS:
            {
                EBSPtoRBSP(&u8EsBuffer[offset + prefix_len], nal_len, 0);
                
                parse_pps();
                
                break;
            }
            case NAL_UNIT_ACCESS_UNIT_DELIMITER:
            {
                EBSPtoRBSP(&u8EsBuffer[offset + prefix_len], nal_len, 0);

                parse_aud();
                
                break;
            }
            case NAL_UNIT_CODED_SLICE_TRAIL_N:
            case NAL_UNIT_CODED_SLICE_TRAIL_R:
            case NAL_UNIT_CODED_SLICE_TSA_N:
            case NAL_UNIT_CODED_SLICE_TSA_R:
            case NAL_UNIT_CODED_SLICE_STSA_N:
            case NAL_UNIT_CODED_SLICE_STSA_R:
            case NAL_UNIT_CODED_SLICE_RADL_N:
            case NAL_UNIT_CODED_SLICE_RADL_R:
            case NAL_UNIT_CODED_SLICE_RASL_N:
            case NAL_UNIT_CODED_SLICE_RASL_R:
            case NAL_UNIT_CODED_SLICE_BLA_W_LP:    // 16
            case NAL_UNIT_CODED_SLICE_BLA_W_RADL:  // 17
            case NAL_UNIT_CODED_SLICE_BLA_N_LP:    // 18
            case NAL_UNIT_CODED_SLICE_IDR_W_RADL:  // 19
            case NAL_UNIT_CODED_SLICE_IDR_N_LP:    // 20
            case NAL_UNIT_CODED_SLICE_CRA:         // 21
            {
                EBSPtoRBSP(&u8EsBuffer[offset + prefix_len], nal_len, 0);

                parse_slice_hdr(nal_unit_type);

                break;
            }
            default:
            {
                break;
            }
        }
        
        offset = offset + nal_len;
        
        ptr += nal_len;        
    }

    fprintf(stderr, "Resolution: %d x %d\n", tHevcInfo.u32Width, tHevcInfo.u32Height);

    fprintf(stderr, frameInfo);
    
    return 0;
}
