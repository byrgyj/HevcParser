

#ifndef ___I_HEVC_COMMON_H___
#define ___I_HEVC_COMMON_H___


#define BIT7            0x80
#define BIT6            0x40
#define BIT5            0x20
#define BIT4            0x10
#define BIT3            0x08
#define BIT2            0x04
#define BIT1            0x02
#define BIT0            0x01

#define MAX_NUM_REF_PICS    16


typedef enum
{
    NAL_UNIT_CODED_SLICE_TRAIL_N = 0, // 0
    NAL_UNIT_CODED_SLICE_TRAIL_R,     // 1
    
    NAL_UNIT_CODED_SLICE_TSA_N,       // 2
    NAL_UNIT_CODED_SLICE_TSA_R,       // 3
    
    NAL_UNIT_CODED_SLICE_STSA_N,      // 4
    NAL_UNIT_CODED_SLICE_STSA_R,      // 5
    
    NAL_UNIT_CODED_SLICE_RADL_N,      // 6
    NAL_UNIT_CODED_SLICE_RADL_R,      // 7
    
    NAL_UNIT_CODED_SLICE_RASL_N,      // 8
    NAL_UNIT_CODED_SLICE_RASL_R,      // 9
    
    NAL_UNIT_RESERVED_VCL_N10,
    NAL_UNIT_RESERVED_VCL_R11,
    NAL_UNIT_RESERVED_VCL_N12,
    NAL_UNIT_RESERVED_VCL_R13,
    NAL_UNIT_RESERVED_VCL_N14,
    NAL_UNIT_RESERVED_VCL_R15,
    
    NAL_UNIT_CODED_SLICE_BLA_W_LP,    // 16
    NAL_UNIT_CODED_SLICE_BLA_W_RADL,  // 17
    NAL_UNIT_CODED_SLICE_BLA_N_LP,    // 18
    NAL_UNIT_CODED_SLICE_IDR_W_RADL,  // 19
    NAL_UNIT_CODED_SLICE_IDR_N_LP,    // 20
    NAL_UNIT_CODED_SLICE_CRA,         // 21
    NAL_UNIT_RESERVED_IRAP_VCL22,
    NAL_UNIT_RESERVED_IRAP_VCL23,
    
    NAL_UNIT_RESERVED_VCL24,
    NAL_UNIT_RESERVED_VCL25,
    NAL_UNIT_RESERVED_VCL26,
    NAL_UNIT_RESERVED_VCL27,
    NAL_UNIT_RESERVED_VCL28,
    NAL_UNIT_RESERVED_VCL29,
    NAL_UNIT_RESERVED_VCL30,
    NAL_UNIT_RESERVED_VCL31,
    
    NAL_UNIT_VPS,                     // 32
    NAL_UNIT_SPS,                     // 33
    NAL_UNIT_PPS,                     // 34
    NAL_UNIT_ACCESS_UNIT_DELIMITER,   // 35
    NAL_UNIT_EOS,                     // 36
    NAL_UNIT_EOB,                     // 37
    NAL_UNIT_FILLER_DATA,             // 38
    NAL_UNIT_PREFIX_SEI,              // 39
    NAL_UNIT_SUFFIX_SEI,              // 40
    
    NAL_UNIT_RESERVED_NVCL41,
    NAL_UNIT_RESERVED_NVCL42,
    NAL_UNIT_RESERVED_NVCL43,
    NAL_UNIT_RESERVED_NVCL44,
    NAL_UNIT_RESERVED_NVCL45,
    NAL_UNIT_RESERVED_NVCL46,
    NAL_UNIT_RESERVED_NVCL47,
    NAL_UNIT_UNSPECIFIED_48,
    NAL_UNIT_UNSPECIFIED_49,
    NAL_UNIT_UNSPECIFIED_50,
    NAL_UNIT_UNSPECIFIED_51,
    NAL_UNIT_UNSPECIFIED_52,
    NAL_UNIT_UNSPECIFIED_53,
    NAL_UNIT_UNSPECIFIED_54,
    NAL_UNIT_UNSPECIFIED_55,
    NAL_UNIT_UNSPECIFIED_56,
    NAL_UNIT_UNSPECIFIED_57,
    NAL_UNIT_UNSPECIFIED_58,
    NAL_UNIT_UNSPECIFIED_59,
    NAL_UNIT_UNSPECIFIED_60,
    NAL_UNIT_UNSPECIFIED_61,
    NAL_UNIT_UNSPECIFIED_62,
    NAL_UNIT_UNSPECIFIED_63,
    NAL_UNIT_INVALID,
} NalUnitType;

typedef enum
{
    ASPECT_RATIO_UNSPECIFIED    = 0,
    ASPECT_RATIO_1_1,
    ASPECT_RATIO_12_11,
    ASPECT_RATIO_10_11,
    ASPECT_RATIO_16_11,
    ASPECT_RATIO_40_33,
    ASPECT_RATIO_24_11,
    ASPECT_RATIO_20_11,
    ASPECT_RATIO_32_11,
    ASPECT_RATIO_80_33,
    ASPECT_RATIO_18_11,
    ASPECT_RATIO_15_11,
    ASPECT_RATIO_64_33,
    ASPECT_RATIO_160_99,
    ASPECT_RATIO_4_3,
    ASPECT_RATIO_3_2,
    ASPECT_RATIO_2_1,
    ASPECT_RATIO_EXTENDED_SAR   = 255,
} AspectRatioIdc;

typedef enum
{
    B_SLICE,
    P_SLICE,
    I_SLICE,
} SliceType;




typedef struct
{
    uint32_t    m_bRefPicListModificationFlagL0;  
    uint32_t    m_bRefPicListModificationFlagL1;  
    uint32_t    m_RefPicSetIdxL0[32];
    uint32_t    m_RefPicSetIdxL1[32];
} RefPicListModification_t;

typedef struct ReferencePictureSet_t
{
    ReferencePictureSet_t() : m_numberOfPictures(0), m_numberOfNegativePictures(0), m_numberOfPositivePictures(0), m_numberOfLongtermPictures(0) {}
    uint32_t    m_numberOfPictures;
    uint32_t    m_numberOfNegativePictures;
    uint32_t    m_numberOfPositivePictures;
    uint32_t    m_numberOfLongtermPictures;

    bool        m_used[MAX_NUM_REF_PICS];
    
} ReferencePictureSet_t;

typedef struct
{
    uint32_t                m_numberOfReferencePictureSets;
    
    ReferencePictureSet_t   *m_referencePictureSets;
} RPSList_t;

typedef struct
{
    uint32_t    m_VPSId;
} VPS_t;

typedef struct 
{
    uint32_t    m_SPSId;
    uint32_t    m_VPSId;
    uint32_t    m_chromaFormatIdc;

    
    int32_t     m_log2MinCodingBlockSize;
    int32_t     m_log2DiffMaxMinCodingBlockSize;
    uint32_t    m_uiMaxCUWidth;
    uint32_t    m_uiMaxCUHeight;
    
    bool        m_separateColourPlaneFlag;

    RPSList_t   m_RPSList;
    bool        m_bLongTermRefsPresent;
    bool        m_TMVPFlagsPresent;

    uint32_t    m_numLongTermRefPicSPS;
    uint32_t    m_uiBitsForPOC;

    bool        m_bUseSAO;
} SPS_t;

typedef struct
{
    uint32_t    m_PPSId;
    uint32_t    m_SPSId;
    int32_t     m_picInitQPMinus26;
    bool        m_bSliceChromaQpFlag;

    uint32_t    m_numRefIdxL0DefaultActive;
    uint32_t    m_numRefIdxL1DefaultActive;
    
    bool        m_dependentSliceSegmentsEnabledFlag;
    bool        m_tilesEnabledFlag;
    bool        m_entropyCodingSyncEnabledFlag;

    bool        m_bUseWeightPred;           // Use of Weighting Prediction (P_SLICE)
    bool        m_useWeightedBiPred;        // Use of Weighting Bi-Prediction (B_SLICE)
    bool        m_OutputFlagPresentFlag;

    uint32_t    m_numExtraSliceHeaderBits;

    bool        m_cabacInitPresentFlag;

    bool        m_sliceHeaderExtensionPresentFlag;
    
    bool        m_loopFilterAcrossSlicesEnabledFlag;
    bool        m_deblockingFilterControlPresentFlag;
    bool        m_deblockingFilterOverrideEnabledFlag;
    bool        m_picDisableDeblockingFilterFlag;
    int32_t     m_deblockingFilterBetaOffsetDiv2;    //< beta offset for deblocking filter
    int32_t     m_deblockingFilterTcOffsetDiv2;      //< tc offset for deblocking filter
    
    bool        m_listsModificationPresentFlag;
} PPS_t;

typedef struct
{
    bool        m_saoEnabledFlag;
    bool        m_saoEnabledFlagChroma;
    uint32_t    m_iPPSId;
    bool        m_PicOutputFlag;
    uint32_t    m_iPOC;

    ReferencePictureSet_t *m_pcRPS;
    ReferencePictureSet_t m_LocalRPS;

    RefPicListModification_t    m_RefPicListModification;
                
    SliceType   m_eSliceType;
    int32_t     m_iSliceQp;
    bool        m_dependentSliceSegmentFlag;

    bool        m_deblockingFilterDisable;
    bool        m_deblockingFilterOverrideFlag;
    int32_t     m_deblockingFilterBetaOffsetDiv2;
    int32_t     m_deblockingFilterTcOffsetDiv2;

    uint32_t    m_aiNumRefIdx[2];

    int32_t     m_iSliceQpDelta;
    int32_t     m_iSliceQpDeltaCb;
    int32_t     m_iSliceQpDeltaCr;

    bool        m_cabacInitFlag;

    bool        m_bLMvdL1Zero;
    uint32_t    m_numEntryPointOffsets;

    bool        m_enableTMVPFlag;
} Slice_t;

#endif
