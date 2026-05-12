#ifndef ILI9881C_INIT_CMDS_H
#define ILI9881C_INIT_CMDS_H

#include "esp_lcd_ili9881c.h"

// ---- 所有命令数据数组定义 ----
static const uint8_t cmd_FF_03_data[]      = {0x98, 0x81, 0x03};
static const uint8_t cmd_01_data[]         = {0x00};
static const uint8_t cmd_02_data[]         = {0x00};
static const uint8_t cmd_03_data[]         = {0x73};
static const uint8_t cmd_04_data[]         = {0x00};
static const uint8_t cmd_05_data[]         = {0x00};
static const uint8_t cmd_06_data[]         = {0x0A};
static const uint8_t cmd_07_data[]         = {0x00};
static const uint8_t cmd_08_data[]         = {0x00};
static const uint8_t cmd_09_data[]         = {0x01};
static const uint8_t cmd_0a_data[]         = {0x00};
static const uint8_t cmd_0b_data[]         = {0x00};
static const uint8_t cmd_0c_data[]         = {0x01};
static const uint8_t cmd_0d_data[]         = {0x00};
static const uint8_t cmd_0e_data[]         = {0x00};
static const uint8_t cmd_0f_data[]         = {0x1D};
static const uint8_t cmd_10_data[]         = {0x1D};
static const uint8_t cmd_11_data[]         = {0x00};
static const uint8_t cmd_12_data[]         = {0x00};
static const uint8_t cmd_13_data[]         = {0x00};
static const uint8_t cmd_14_data[]         = {0x00};
static const uint8_t cmd_15_data[]         = {0x00};
static const uint8_t cmd_16_data[]         = {0x00};
static const uint8_t cmd_17_data[]         = {0x00};
static const uint8_t cmd_18_data[]         = {0x00};
static const uint8_t cmd_19_data[]         = {0x00};
static const uint8_t cmd_1a_data[]         = {0x00};
static const uint8_t cmd_1b_data[]         = {0x00};
static const uint8_t cmd_1c_data[]         = {0x00};
static const uint8_t cmd_1d_data[]         = {0x00};
static const uint8_t cmd_1e_data[]         = {0x40};
static const uint8_t cmd_1f_data[]         = {0x80};
static const uint8_t cmd_20_data[]         = {0x06};
static const uint8_t cmd_21_data[]         = {0x02};
static const uint8_t cmd_22_data[]         = {0x00};
static const uint8_t cmd_23_data[]         = {0x00};
static const uint8_t cmd_24_data[]         = {0x00};
static const uint8_t cmd_25_data[]         = {0x00};
static const uint8_t cmd_26_data[]         = {0x00};
static const uint8_t cmd_27_data[]         = {0x00};
static const uint8_t cmd_28_data[]         = {0x33};
static const uint8_t cmd_29_data[]         = {0x03};
static const uint8_t cmd_2a_data[]         = {0x00};
static const uint8_t cmd_2b_data[]         = {0x00};
static const uint8_t cmd_2c_data[]         = {0x00};
static const uint8_t cmd_2d_data[]         = {0x00};
static const uint8_t cmd_2e_data[]         = {0x00};
static const uint8_t cmd_2f_data[]         = {0x00};
static const uint8_t cmd_30_data[]         = {0x00};
static const uint8_t cmd_31_data[]         = {0x00};
static const uint8_t cmd_32_data[]         = {0x00};
static const uint8_t cmd_33_data[]         = {0x00};
static const uint8_t cmd_34_data[]         = {0x04};
static const uint8_t cmd_35_data[]         = {0x00};
static const uint8_t cmd_36_data[]         = {0x00};
static const uint8_t cmd_37_data[]         = {0x00};
static const uint8_t cmd_38_data[]         = {0x3C};
static const uint8_t cmd_39_data[]         = {0x00};
static const uint8_t cmd_3a_data[]         = {0x40};
static const uint8_t cmd_3b_data[]         = {0x40};
static const uint8_t cmd_3c_data[]         = {0x00};
static const uint8_t cmd_3d_data[]         = {0x00};
static const uint8_t cmd_3e_data[]         = {0x00};
static const uint8_t cmd_3f_data[]         = {0x00};
static const uint8_t cmd_40_data[]         = {0x00};
static const uint8_t cmd_41_data[]         = {0x00};
static const uint8_t cmd_42_data[]         = {0x00};
static const uint8_t cmd_43_data[]         = {0x00};
static const uint8_t cmd_44_data[]         = {0x00};
static const uint8_t cmd_50_data[]         = {0x01};
static const uint8_t cmd_51_data[]         = {0x23};
static const uint8_t cmd_52_data[]         = {0x45};
static const uint8_t cmd_53_data[]         = {0x67};
static const uint8_t cmd_54_data[]         = {0x89};
static const uint8_t cmd_55_data[]         = {0xab};
static const uint8_t cmd_56_data[]         = {0x01};
static const uint8_t cmd_57_data[]         = {0x23};
static const uint8_t cmd_58_data[]         = {0x45};
static const uint8_t cmd_59_data[]         = {0x67};
static const uint8_t cmd_5a_data[]         = {0x89};
static const uint8_t cmd_5b_data[]         = {0xab};
static const uint8_t cmd_5c_data[]         = {0xcd};
static const uint8_t cmd_5d_data[]         = {0xef};
static const uint8_t cmd_5e_data[]         = {0x11};
static const uint8_t cmd_5f_data[]         = {0x01};
static const uint8_t cmd_60_data[]         = {0x00};
static const uint8_t cmd_61_data[]         = {0x15};
static const uint8_t cmd_62_data[]         = {0x14};
static const uint8_t cmd_63_data[]         = {0x0E};
static const uint8_t cmd_64_data[]         = {0x0F};
static const uint8_t cmd_65_data[]         = {0x0C};
static const uint8_t cmd_66_data[]         = {0x0D};
static const uint8_t cmd_67_data[]         = {0x06};
static const uint8_t cmd_68_data[]         = {0x02};
static const uint8_t cmd_69_data[]         = {0x07};
static const uint8_t cmd_6a_data[]         = {0x02};
static const uint8_t cmd_6b_data[]         = {0x02};
static const uint8_t cmd_6c_data[]         = {0x02};
static const uint8_t cmd_6d_data[]         = {0x02};
static const uint8_t cmd_6e_data[]         = {0x02};
static const uint8_t cmd_6f_data[]         = {0x02};
static const uint8_t cmd_70_data[]         = {0x02};
static const uint8_t cmd_71_data[]         = {0x02};
static const uint8_t cmd_72_data[]         = {0x02};
static const uint8_t cmd_73_data[]         = {0x02};
static const uint8_t cmd_74_data[]         = {0x02};
static const uint8_t cmd_75_data[]         = {0x01};
static const uint8_t cmd_76_data[]         = {0x00};
static const uint8_t cmd_77_data[]         = {0x14};
static const uint8_t cmd_78_data[]         = {0x15};
static const uint8_t cmd_79_data[]         = {0x0E};
static const uint8_t cmd_7a_data[]         = {0x0F};
static const uint8_t cmd_7b_data[]         = {0x0C};
static const uint8_t cmd_7c_data[]         = {0x0D};
static const uint8_t cmd_7d_data[]         = {0x06};
static const uint8_t cmd_7e_data[]         = {0x02};
static const uint8_t cmd_7f_data[]         = {0x07};
static const uint8_t cmd_80_data[]         = {0x02};
static const uint8_t cmd_81_data[]         = {0x02};
static const uint8_t cmd_82_data[]         = {0x02};
static const uint8_t cmd_83_data[]         = {0x02};
static const uint8_t cmd_84_data[]         = {0x02};
static const uint8_t cmd_85_data[]         = {0x02};
static const uint8_t cmd_86_data[]         = {0x02};
static const uint8_t cmd_87_data[]         = {0x02};
static const uint8_t cmd_88_data[]         = {0x02};
static const uint8_t cmd_89_data[]         = {0x02};
static const uint8_t cmd_8A_data[]         = {0x02};
static const uint8_t cmd_FF_04_data[]      = {0x98, 0x81, 0x04};
// static const uint8_t cmd_00_data[]         = {0x00};
static const uint8_t cmd_38_01_data[]      = {0x01};
static const uint8_t cmd_39_00_data[]      = {0x00};
static const uint8_t cmd_6C_15_data[]      = {0x15};
static const uint8_t cmd_6E_data[]         = {0x2B};
static const uint8_t cmd_6F_data[]         = {0x33};
static const uint8_t cmd_8D_data[]         = {0x18};
static const uint8_t cmd_87_BA_data[]      = {0xBA};
static const uint8_t cmd_26_76_data[]      = {0x76};
static const uint8_t cmd_B2_data[]         = {0xD1};
static const uint8_t cmd_B5_data[]         = {0x06};
static const uint8_t cmd_3A_data[]         = {0x24};
static const uint8_t cmd_35_1F_data[]      = {0x1F};
static const uint8_t cmd_33_14_data[]      = {0x14};
static const uint8_t cmd_3B_data[]         = {0x98};
static const uint8_t cmd_FF_01_data[]      = {0x98, 0x81, 0x01};
static const uint8_t cmd_22_0A_data[]      = {0x0A};
static const uint8_t cmd_31_00_data[]      = {0x00};
static const uint8_t cmd_40_33_data[]      = {0x33};
static const uint8_t cmd_43_66_data[]      = {0x66};
static const uint8_t cmd_50_96_data[]      = {0x96};
static const uint8_t cmd_51_96_data[]      = {0x96};
static const uint8_t cmd_53_B0_data[]      = {0xB0};
static const uint8_t cmd_55_B0_data[]      = {0xB0};
static const uint8_t cmd_60_22_data[]      = {0x22};
static const uint8_t cmd_61_00_data[]      = {0x00};
static const uint8_t cmd_62_19_data[]      = {0x19};
static const uint8_t cmd_63_00_data[]      = {0x00};
static const uint8_t cmd_A0_data[]         = {0x08};
static const uint8_t cmd_A1_data[]         = {0x11};
static const uint8_t cmd_A2_data[]         = {0x19};
static const uint8_t cmd_A3_data[]         = {0x0D};
static const uint8_t cmd_A4_data[]         = {0x0D};
static const uint8_t cmd_A5_data[]         = {0x1E};
static const uint8_t cmd_A6_data[]         = {0x14};
static const uint8_t cmd_A7_data[]         = {0x17};
static const uint8_t cmd_A8_data[]         = {0x4F};
static const uint8_t cmd_A9_data[]         = {0x1A};
static const uint8_t cmd_AA_data[]         = {0x27};
static const uint8_t cmd_AB_data[]         = {0x49};
static const uint8_t cmd_AC_data[]         = {0x1A};
static const uint8_t cmd_AD_data[]         = {0x18};
static const uint8_t cmd_AE_data[]         = {0x4C};
static const uint8_t cmd_AF_data[]         = {0x22};
static const uint8_t cmd_B0_data[]         = {0x27};
static const uint8_t cmd_B1_data[]         = {0x4B};
static const uint8_t cmd_B2_60_data[]      = {0x60};
static const uint8_t cmd_B3_data[]         = {0x39};
static const uint8_t cmd_C0_data[]         = {0x08};
static const uint8_t cmd_C1_data[]         = {0x11};
static const uint8_t cmd_C2_data[]         = {0x19};
static const uint8_t cmd_C3_data[]         = {0x0D};
static const uint8_t cmd_C4_data[]         = {0x0D};
static const uint8_t cmd_C5_data[]         = {0x1E};
static const uint8_t cmd_C6_data[]         = {0x14};
static const uint8_t cmd_C7_data[]         = {0x17};
static const uint8_t cmd_C8_data[]         = {0x4F};
static const uint8_t cmd_C9_data[]         = {0x1A};
static const uint8_t cmd_CA_data[]         = {0x27};
static const uint8_t cmd_CB_data[]         = {0x49};
static const uint8_t cmd_CC_data[]         = {0x1A};
static const uint8_t cmd_CD_data[]         = {0x18};
static const uint8_t cmd_CE_data[]         = {0x4C};
static const uint8_t cmd_CF_data[]         = {0x33};
static const uint8_t cmd_D0_data[]         = {0x27};
static const uint8_t cmd_D1_data[]         = {0x4B};
static const uint8_t cmd_D2_data[]         = {0x60};
static const uint8_t cmd_D3_data[]         = {0x39};
static const uint8_t cmd_FF_00_data[]      = {0x98, 0x81, 0x00};
// static const uint8_t cmd_36_00_data[]      = {0x00};
static const uint8_t cmd_35_00_data[]      = {0x00};

// ---- 初始化命令序列 ----
static const ili9881c_lcd_init_cmd_t vendor_init_cmds[] = {
    // Page 3
    {0xFF, cmd_FF_03_data, 3, 0},
    {0x01, cmd_01_data, 1, 0},
    {0x02, cmd_02_data, 1, 0},
    {0x03, cmd_03_data, 1, 0},
    {0x04, cmd_04_data, 1, 0},
    {0x05, cmd_05_data, 1, 0},
    {0x06, cmd_06_data, 1, 0},
    {0x07, cmd_07_data, 1, 0},
    {0x08, cmd_08_data, 1, 0},
    {0x09, cmd_09_data, 1, 0},
    {0x0a, cmd_0a_data, 1, 0},
    {0x0b, cmd_0b_data, 1, 0},
    {0x0c, cmd_0c_data, 1, 0},
    {0x0d, cmd_0d_data, 1, 0},
    {0x0e, cmd_0e_data, 1, 0},
    {0x0f, cmd_0f_data, 1, 0},
    {0x10, cmd_10_data, 1, 0},
    {0x11, cmd_11_data, 1, 0},
    {0x12, cmd_12_data, 1, 0},
    {0x13, cmd_13_data, 1, 0},
    {0x14, cmd_14_data, 1, 0},
    {0x15, cmd_15_data, 1, 0},
    {0x16, cmd_16_data, 1, 0},
    {0x17, cmd_17_data, 1, 0},
    {0x18, cmd_18_data, 1, 0},
    {0x19, cmd_19_data, 1, 0},
    {0x1a, cmd_1a_data, 1, 0},
    {0x1b, cmd_1b_data, 1, 0},
    {0x1c, cmd_1c_data, 1, 0},
    {0x1d, cmd_1d_data, 1, 0},
    {0x1e, cmd_1e_data, 1, 0},
    {0x1f, cmd_1f_data, 1, 0},
    {0x20, cmd_20_data, 1, 0},
    {0x21, cmd_21_data, 1, 0},
    {0x22, cmd_22_data, 1, 0},
    {0x23, cmd_23_data, 1, 0},
    {0x24, cmd_24_data, 1, 0},
    {0x25, cmd_25_data, 1, 0},
    {0x26, cmd_26_data, 1, 0},
    {0x27, cmd_27_data, 1, 0},
    {0x28, cmd_28_data, 1, 0},
    {0x29, cmd_29_data, 1, 0},
    {0x2a, cmd_2a_data, 1, 0},
    {0x2b, cmd_2b_data, 1, 0},
    {0x2c, cmd_2c_data, 1, 0},
    {0x2d, cmd_2d_data, 1, 0},
    {0x2e, cmd_2e_data, 1, 0},
    {0x2f, cmd_2f_data, 1, 0},
    {0x30, cmd_30_data, 1, 0},
    {0x31, cmd_31_data, 1, 0},
    {0x32, cmd_32_data, 1, 0},
    {0x33, cmd_33_data, 1, 0},
    {0x34, cmd_34_data, 1, 0},
    {0x35, cmd_35_data, 1, 0},
    {0x36, cmd_36_data, 1, 0},
    {0x37, cmd_37_data, 1, 0},
    {0x38, cmd_38_data, 1, 0},
    {0x39, cmd_39_data, 1, 0},
    {0x3a, cmd_3a_data, 1, 0},
    {0x3b, cmd_3b_data, 1, 0},
    {0x3c, cmd_3c_data, 1, 0},
    {0x3d, cmd_3d_data, 1, 0},
    {0x3e, cmd_3e_data, 1, 0},
    {0x3f, cmd_3f_data, 1, 0},
    {0x40, cmd_40_data, 1, 0},
    {0x41, cmd_41_data, 1, 0},
    {0x42, cmd_42_data, 1, 0},
    {0x43, cmd_43_data, 1, 0},
    {0x44, cmd_44_data, 1, 0},
    {0x50, cmd_50_data, 1, 0},
    {0x51, cmd_51_data, 1, 0},
    {0x52, cmd_52_data, 1, 0},
    {0x53, cmd_53_data, 1, 0},
    {0x54, cmd_54_data, 1, 0},
    {0x55, cmd_55_data, 1, 0},
    {0x56, cmd_56_data, 1, 0},
    {0x57, cmd_57_data, 1, 0},
    {0x58, cmd_58_data, 1, 0},
    {0x59, cmd_59_data, 1, 0},
    {0x5a, cmd_5a_data, 1, 0},
    {0x5b, cmd_5b_data, 1, 0},
    {0x5c, cmd_5c_data, 1, 0},
    {0x5d, cmd_5d_data, 1, 0},
    {0x5e, cmd_5e_data, 1, 0},
    {0x5f, cmd_5f_data, 1, 0},
    {0x60, cmd_60_data, 1, 0},
    {0x61, cmd_61_data, 1, 0},
    {0x62, cmd_62_data, 1, 0},
    {0x63, cmd_63_data, 1, 0},
    {0x64, cmd_64_data, 1, 0},
    {0x65, cmd_65_data, 1, 0},
    {0x66, cmd_66_data, 1, 0},
    {0x67, cmd_67_data, 1, 0},
    {0x68, cmd_68_data, 1, 0},
    {0x69, cmd_69_data, 1, 0},
    {0x6a, cmd_6a_data, 1, 0},
    {0x6b, cmd_6b_data, 1, 0},
    {0x6c, cmd_6c_data, 1, 0},
    {0x6d, cmd_6d_data, 1, 0},
    {0x6e, cmd_6e_data, 1, 0},
    {0x6f, cmd_6f_data, 1, 0},
    {0x70, cmd_70_data, 1, 0},
    {0x71, cmd_71_data, 1, 0},
    {0x72, cmd_72_data, 1, 0},
    {0x73, cmd_73_data, 1, 0},
    {0x74, cmd_74_data, 1, 0},
    {0x75, cmd_75_data, 1, 0},
    {0x76, cmd_76_data, 1, 0},
    {0x77, cmd_77_data, 1, 0},
    {0x78, cmd_78_data, 1, 0},
    {0x79, cmd_79_data, 1, 0},
    {0x7a, cmd_7a_data, 1, 0},
    {0x7b, cmd_7b_data, 1, 0},
    {0x7c, cmd_7c_data, 1, 0},
    {0x7d, cmd_7d_data, 1, 0},
    {0x7e, cmd_7e_data, 1, 0},
    {0x7f, cmd_7f_data, 1, 0},
    {0x80, cmd_80_data, 1, 0},
    {0x81, cmd_81_data, 1, 0},
    {0x82, cmd_82_data, 1, 0},
    {0x83, cmd_83_data, 1, 0},
    {0x84, cmd_84_data, 1, 0},
    {0x85, cmd_85_data, 1, 0},
    {0x86, cmd_86_data, 1, 0},
    {0x87, cmd_87_data, 1, 0},
    {0x88, cmd_88_data, 1, 0},
    {0x89, cmd_89_data, 1, 0},
    {0x8A, cmd_8A_data, 1, 0},
    // Page 4
    {0xFF, cmd_FF_04_data, 3, 0},
    // {0x00, cmd_00_data, 1, 0},
    {0x38, cmd_38_01_data, 1, 0},
    {0x39, cmd_39_00_data, 1, 0},
    {0x6C, cmd_6C_15_data, 1, 0},
    {0x6E, cmd_6E_data, 1, 0},
    {0x6F, cmd_6F_data, 1, 0},
    {0x8D, cmd_8D_data, 1, 0},
    {0x87, cmd_87_BA_data, 1, 0},
    {0x26, cmd_26_76_data, 1, 0},
    {0xB2, cmd_B2_data, 1, 0},
    {0xB5, cmd_B5_data, 1, 0},
    {0x3A, cmd_3A_data, 1, 0},
    {0x35, cmd_35_1F_data, 1, 0},
    {0x33, cmd_33_14_data, 1, 0},
    {0x3B, cmd_3B_data, 1, 0},
    // Page 1
    {0xFF, cmd_FF_01_data, 3, 0},
    {0x22, cmd_22_0A_data, 1, 0},
    {0x31, cmd_31_00_data, 1, 0},
    {0x40, cmd_40_33_data, 1, 0},
    {0x43, cmd_43_66_data, 1, 0},
    {0x50, cmd_50_96_data, 1, 0},
    {0x51, cmd_51_96_data, 1, 0},
    {0x53, cmd_53_B0_data, 1, 0},
    {0x55, cmd_55_B0_data, 1, 0},
    {0x60, cmd_60_22_data, 1, 0},
    {0x61, cmd_61_00_data, 1, 0},
    {0x62, cmd_62_19_data, 1, 0},
    {0x63, cmd_63_00_data, 1, 0},
    // Gamma Positive
    {0xA0, cmd_A0_data, 1, 0},
    {0xA1, cmd_A1_data, 1, 0},
    {0xA2, cmd_A2_data, 1, 0},
    {0xA3, cmd_A3_data, 1, 0},
    {0xA4, cmd_A4_data, 1, 0},
    {0xA5, cmd_A5_data, 1, 0},
    {0xA6, cmd_A6_data, 1, 0},
    {0xA7, cmd_A7_data, 1, 0},
    {0xA8, cmd_A8_data, 1, 0},
    {0xA9, cmd_A9_data, 1, 0},
    {0xAA, cmd_AA_data, 1, 0},
    {0xAB, cmd_AB_data, 1, 0},
    {0xAC, cmd_AC_data, 1, 0},
    {0xAD, cmd_AD_data, 1, 0},
    {0xAE, cmd_AE_data, 1, 0},
    {0xAF, cmd_AF_data, 1, 0},
    {0xB0, cmd_B0_data, 1, 0},
    {0xB1, cmd_B1_data, 1, 0},
    {0xB2, cmd_B2_60_data, 1, 0},
    {0xB3, cmd_B3_data, 1, 0},
    // Gamma Negative
    {0xC0, cmd_C0_data, 1, 0},
    {0xC1, cmd_C1_data, 1, 0},
    {0xC2, cmd_C2_data, 1, 0},
    {0xC3, cmd_C3_data, 1, 0},
    {0xC4, cmd_C4_data, 1, 0},
    {0xC5, cmd_C5_data, 1, 0},
    {0xC6, cmd_C6_data, 1, 0},
    {0xC7, cmd_C7_data, 1, 0},
    {0xC8, cmd_C8_data, 1, 0},
    {0xC9, cmd_C9_data, 1, 0},
    {0xCA, cmd_CA_data, 1, 0},
    {0xCB, cmd_CB_data, 1, 0},
    {0xCC, cmd_CC_data, 1, 0},
    {0xCD, cmd_CD_data, 1, 0},
    {0xCE, cmd_CE_data, 1, 0},
    {0xCF, cmd_CF_data, 1, 0},
    {0xD0, cmd_D0_data, 1, 0},
    {0xD1, cmd_D1_data, 1, 0},
    {0xD2, cmd_D2_data, 1, 0},
    {0xD3, cmd_D3_data, 1, 0},
    // Page 0
    {0xFF, cmd_FF_00_data, 3, 0},
    // {0x36, cmd_36_00_data, 1, 0},
    {0x35, cmd_35_00_data, 1, 0},
    // Sleep Out
    {0x11, NULL, 0, 120},
    // Display On
    {0x29, NULL, 0, 20},
};

#endif // ILI9881C_INIT_CMDS_H
