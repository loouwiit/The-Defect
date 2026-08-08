/* 由 tools/gen_elf_syms.py 自动生成 — 勿手改。
 * 符号从主固件 .elf 提取，&name 链接期取址；C++ mangled 名声明为 extern int 仅用于取址。 */
#include "esp_elf.h"
#include <stddef.h>

/* extern 声明（跳过内置声明类型不匹配告警） */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
extern int _ZN10BleGamepad10autoScanCbEP13ble_gap_eventPv;
extern int _ZN10BleGamepad10conectNextEm;
extern int _ZN10BleGamepad10conectNextEv;
extern int _ZN10BleGamepad10disconnectEh;
extern int _ZN10BleGamepad10initNimbleEv;
extern int _ZN10BleGamepad10movePlayerEhh;
extern int _ZN10BleGamepad11processTaskEPv;
extern int _ZN10BleGamepad13allocPlayerIdEv;
extern int _ZN10BleGamepad13connectDirectERK10ScanDevice;
extern int _ZN10BleGamepad14scanGapEventCbEP13ble_gap_eventPv;
extern int _ZN10BleGamepad15connectGapEventEP13ble_gap_eventPv;
extern int _ZN10BleGamepad15initEspHostedBtEv;
extern int _ZN10BleGamepad15syncPairedToNvsEv;
extern int _ZN10BleGamepad17autoConnectPairedEv;
extern int _ZN10BleGamepad17loadPairedFromNvsEv;
extern int _ZN10BleGamepad5startEP7Display;
extern int _ZN10BleGamepad6onSyncEv;
extern int _ZN10BleGamepad7connectEPKh;
extern int _ZN10BleGamepad7connectEh;
extern int _ZN10BleGamepad7onResetEi;
extern int _ZN10BleGamepad8hostTaskEPv;
extern int _ZN10BleGamepad8instanceEv;
extern int _ZN10BleGamepad8stopScanEv;
extern int _ZN10BleGamepad9startScanEv;
extern int _ZN10BleGamepadC1Ev;
extern int _ZN10BleGamepadC2Ev;
extern int _ZN10BleGamepadD1Ev;
extern int _ZN10BleGamepadD2Ev;
extern int _ZN10FontLoader10getDefaultENS_8FontSizeE;
extern int _ZN10FontLoader10setDefaultEPK10_lv_font_tNS_8FontSizeE;
extern int _ZN10FontLoader4loadEPKct;
extern int _ZN10GT911Indev10threadMainEPv;
extern int _ZN10GT911Indev11indevReadCbEP11_lv_indev_tP15lv_indev_data_t;
extern int _ZN10GT911Indev14updateAndRouteEv;
extern int _ZN10GT911Indev5isrCbEP15esp_lcd_touch_s;
extern int _ZN10GT911Indev5startEP7DisplayP5Touch;
extern int _ZN10GT911Indev8instanceEv;
extern int _ZN10GT911IndevC1Ev;
extern int _ZN10GT911IndevC2Ev;
extern int _ZN10GT911IndevD1Ev;
extern int _ZN10GT911IndevD2Ev;
extern int _ZN11AudioHandle4Impl11applyVolumeEPsjf;
extern int _ZN11AudioHandle4Impl8taskFuncEPv;
extern int _ZN11AudioHandle4Impl8taskLoopEv;
extern int _ZN11AudioHandle4ImplC1Ev;
extern int _ZN11AudioHandle4ImplC2Ev;
extern int _ZN11AudioHandle4ImplD1Ev;
extern int _ZN11AudioHandle4ImplD2Ev;
extern int _ZN11AudioHandle4playEv;
extern int _ZN11AudioHandle6detachEv;
extern int _ZN11AudioHandle7setLoopEb;
extern int _ZN11AudioHandleC1EOS_;
extern int _ZN11AudioHandleC2EOS_;
extern int _ZN11AudioHandleD1Ev;
extern int _ZN11AudioHandleD2Ev;
extern int _ZN11AudioHandleaSEOS_;
extern int _ZN11AudioSample4loadEPKc;
extern int _ZN11AudioSample4playEb;
extern int _ZN11AudioSample6unloadEv;
extern int _ZN11TimeService11setAutoSyncEb;
extern int _ZN11TimeService12onGotIpEventEPvPKclS0_;
extern int _ZN11TimeService12onTimeSyncCbEP7timeval;
extern int _ZN11TimeService12removeSyncCbEm;
extern int _ZN11TimeService14notifySyncDoneEv;
extern int _ZN11TimeService5startEv;
extern int _ZN11TimeService8instanceEv;
extern int _ZN11TimeService9addSyncCbESt8functionIFvvEE;
extern int _ZN11TimeService9forceSyncEv;
extern int _ZN11TimeServiceC1Ev;
extern int _ZN11TimeServiceC2Ev;
extern int _ZN11TimeServiceD1Ev;
extern int _ZN11TimeServiceD2Ev;
extern int _ZN12VirtualIndev11indevReadCbEP11_lv_indev_tP15lv_indev_data_t;
extern int _ZN12VirtualIndev5startEP7Display;
extern int _ZN12VirtualIndev8instanceEv;
extern int _ZN12VirtualIndev9sendTouchE16lv_indev_state_t10lv_point_t;
extern int _ZN12VirtualIndevC1Ev;
extern int _ZN12VirtualIndevC2Ev;
extern int _ZN12VirtualIndevD1Ev;
extern int _ZN12VirtualIndevD2Ev;
extern int _ZN14BatteryManager10getPercentEv;
extern int _ZN14BatteryManager10isChargingEv;
extern int _ZN14BatteryManager12getVoltageMvEv;
extern int _ZN14BatteryManager16chargingDetectCbEPv;
extern int _ZN14BatteryManager16stopChargingAnimEP9_lv_obj_t;
extern int _ZN14BatteryManager17startChargingAnimEP9_lv_obj_t;
extern int _ZN14BatteryManager4initEv;
extern int _ZN14BatteryManager7getIconEi;
extern int _ZN14BatteryManager8getColorEi;
extern int _ZN14BatteryManager8instanceEv;
extern int _ZN14VirtualGamepad11resetPlayerEaa;
extern int _ZN14VirtualGamepad11setAllMasksEPKh;
extern int _ZN14VirtualGamepad12dispatchTaskEPv;
extern int _ZN14VirtualGamepad13forwarderLoopEv;
extern int _ZN14VirtualGamepad13forwarderTaskEPv;
extern int _ZN14VirtualGamepad13stopForwarderEv;
extern int _ZN14VirtualGamepad14startForwarderEv;
extern int _ZN14VirtualGamepad5startEP7Display;
extern int _ZN14VirtualGamepad6injectEaaRK12GamepadState;
extern int _ZN14VirtualGamepad8instanceEv;
extern int _ZN14VirtualGamepad9packFrameEPhaaRK12GamepadState;
extern int _ZN14VirtualGamepadD1Ev;
extern int _ZN14VirtualGamepadD2Ev;
extern int _ZN15AppStackManager11createStackEv;
extern int _ZN15AppStackManager12destroyStackEP8AppStack;
extern int _ZN15AppStackManager13switchToStackEP8AppStack;
extern int _ZN15AppStackManager13switchToStackEj;
extern int _ZN15AppStackManager14pushToNewStackEP3App;
extern int _ZN15AppStackManager16transitionStacksEP8AppStackS1_;
extern int _ZN15AppStackManager3popEv;
extern int _ZN15AppStackManager4pushEP3App;
extern int _ZN15AppStackManager7replaceEP3App;
extern int _ZN15AppStackManagerC1EP7Display;
extern int _ZN15AppStackManagerC1Ev;
extern int _ZN15AppStackManagerC2EP7Display;
extern int _ZN15AppStackManagerC2Ev;
extern int _ZN15AppStackManagerD1Ev;
extern int _ZN15AppStackManagerD2Ev;
extern int _ZN15AppStackManageraSEOS_;
extern int _ZN3App10onAiSelectERK14AiSelectTarget;
extern int _ZN3App10setManagerEP15AppStackManager;
extern int _ZN3App11replaceWithEPS_;
extern int _ZN3App12getAiContextB5cxx11Ev;
extern int _ZN3App12onAiActivateEv;
extern int _ZN3App12onBackgroundEv;
extern int _ZN3App12onForegroundEv;
extern int _ZN3App14onGamepadInputEhhRK12GamepadState;
extern int _ZN3App15executeAiActionERK8AiAction;
extern int _ZN3App15getOpenableAppsEv;
extern int _ZN3App18onGamepadConnectedEh;
extern int _ZN3App21onGamepadDisconnectedEh;
extern int _ZN3App4initEv;
extern int _ZN3App6deinitEv;
extern int _ZN3App6popAppEv;
extern int _ZN3App7pushAppEPS_;
extern int _ZN3AppC1EP7Display;
extern int _ZN3AppC2EP7Display;
extern int _ZN3AppD0Ev;
extern int _ZN3AppD1Ev;
extern int _ZN3AppD2Ev;
extern int _ZN3GUI10createCardEP9_lv_obj_tll;
extern int _ZN3GUI10createFlexEP9_lv_obj_t14lv_flex_flow_tll;
extern int _ZN3GUI11createLabelEP9_lv_obj_tPKc;
extern int _ZN3GUI11createTitleEP9_lv_obj_tPKc;
extern int _ZN3GUI12createButtonEP9_lv_obj_tPKcll;
extern int _ZN3GUI14createSubtitleEP9_lv_obj_tPKc;
extern int _ZN3GUI17createProgressBarEP9_lv_obj_tllll;
extern int _ZN4Task10daemonMainEPv;
extern int _ZN4Task10removeTaskEPS_;
extern int _ZN4Task11setAffinityEPS_NS_8AffinityE;
extern int _ZN4Task4initEj;
extern int _ZN4Task7addTaskEPFmPvEPKcS0_mNS_8AffinityE;
extern int _ZN4Task8dumpTaskEv;
extern int _ZN4TaskC1Ev;
extern int _ZN4TaskC2Ev;
extern int _ZN4TaskD1Ev;
extern int _ZN4TaskD2Ev;
extern int _ZN5Audio12detectFormatEPKc;
extern int _ZN5Audio12freeStreamIdEh;
extern int _ZN5Audio13allocStreamIdEv;
extern int _ZN5Audio13onRenderEventE29esp_audio_render_event_type_tPv;
extern int _ZN5Audio15getMasterVolumeEv;
extern int _ZN5Audio15saveVolumeToNvsEv;
extern int _ZN5Audio15setMasterVolumeEi;
extern int _ZN5Audio17loadVolumeFromNvsEv;
extern int _ZN5Audio4initER6ES8311;
extern int _ZN5Audio4playEPKc;
extern int _ZN5Audio4playERK11AudioSampleb;
extern int _ZN5Audio6deinitEv;
extern int _ZN5Audio8instanceEv;
extern int _ZN5Audio9outWriterEPhmPv;
extern int _ZN5AudioC1Ev;
extern int _ZN5AudioC2Ev;
extern int _ZN5AudioD1Ev;
extern int _ZN5AudioD2Ev;
extern int _ZN5MutexD1Ev;
extern int _ZN5MutexD2Ev;
extern int _ZN5Touch6updateEv;
extern int _ZN5Touch8getCountEv;
extern int _ZN5Touch9getHandleEv;
extern int _ZN5TouchC1ER3IIC4GPIOS2_hj;
extern int _ZN5TouchC2ER3IIC4GPIOS2_hj;
extern int _ZN5TouchD1Ev;
extern int _ZN5TouchD2Ev;
extern int _ZN5TouchaSEOS_;
extern int _ZN5TouchixEh;
extern int _ZN6ES83114initER3IICNS_6ConfigE;
extern int _ZN6ES83114openEP27esp_codec_dev_sample_info_t;
extern int _ZN6ES83115closeEv;
extern int _ZN6ES83115writeEPKvi;
extern int _ZN6ES83116deinitEv;
extern int _ZN6ES83119getVolumeEv;
extern int _ZN6ES83119setVolumeEi;
extern int _ZN6ES8311D1Ev;
extern int _ZN6ES8311D2Ev;
extern int _ZN6Thread9isRunningEv;
extern int _ZN6ThreadC1EPFvPvEPKcS0_N4Task8PriorityEj;
extern int _ZN6ThreadC2EPFvPvEPKcS0_N4Task8PriorityEj;
extern int _ZN6ThreadD1Ev;
extern int _ZN6ThreadD2Ev;
extern int _ZN7Display11bindDisplayEP15esp_lcd_panel_tP18esp_lcd_panel_io_ttt32esp_lv_adapter_tear_avoid_mode_t25esp_lv_adapter_rotation_t;
extern int _ZN7Display13setBrightnessEi;
extern int _ZN7Display14saveBrightnessEv;
extern int _ZN7Display4initEv;
extern int _ZN7Display5startEv;
extern int _ZN7DisplayC1Ev;
extern int _ZN7DisplayC2Ev;
extern int _ZN7DisplayD1Ev;
extern int _ZN7DisplayD2Ev;
extern int _ZN8AppStack16scheduleDeletionEP3App;
extern int _ZN8AppStack3popEv;
extern int _ZN8AppStack4pushEP3App;
extern int _ZN8AppStack7replaceEP3App;
extern int _ZN8AppStackC1EP7DisplayP15AppStackManager;
extern int _ZN8AppStackC2EP7DisplayP15AppStackManager;
extern int _ZN8AppStackD1Ev;
extern int _ZN8AppStackD2Ev;
extern int _ZN8ILI9881c11getInstanceEv;
extern int _ZN8ILI9881c13getBrightnessEv;
extern int _ZN8ILI9881c13setBrightnessEi;
extern int _ZN8ILI9881c14initBrightnessEv;
extern int _ZN8ILI9881c19saveBrightnessToNvsEv;
extern int _ZN8ILI9881c21loadBrightnessFromNvsEv;
extern int _ZN8ILI9881c4initEiih;
extern int _ZN8ILI9881c6deinitEv;
extern int _ZN8ILI9881cC1Ev;
extern int _ZN8ILI9881cC2Ev;
extern int _ZN8ILI9881cD1Ev;
extern int _ZN8ILI9881cD2Ev;
extern int _ZNK10BleGamepad14connectedCountEv;
extern int _ZNK10BleGamepad15getBatteryLevelEh;
extern int _ZNK10BleGamepad17getScannedDevicesEv;
extern int _ZNK10BleGamepad9getDeviceEh;
extern int _ZNK10GT911Indev12findFreeSlotEv;
extern int _ZNK10GT911Indev8getSlotsEPNS_8SlotInfoEj;
extern int _ZNK11AudioHandle9isPlayingEv;
extern int _ZNK11TimeService10isAutoSyncEv;
extern int _ZNK14VirtualGamepad8getStateEaa;
extern int _ZNK5Mutex6unlockEv;
extern int _ZNK7Display13getBrightnessEv;
extern int _ZNK7Display23setFpsStatisticsEnabledEb;
extern int _ZNK7Display6getFpsEv;
extern int _ZNK7Display8applyAppEP3App;
extern int _ZNK8AppStack3topEv;
extern int _ZSt17__throw_bad_allocv;
extern int _ZSt19__throw_logic_errorPKc;
extern int _ZSt20__throw_length_errorPKc;
extern int _ZSt28__throw_bad_array_new_lengthv;
extern int _ZdaPv;
extern int _ZdaPvj;
extern int _ZdlPv;
extern int _ZdlPvSt11align_val_t;
extern int _ZdlPvj;
extern int _ZdlPvjSt11align_val_t;
extern int _Znaj;
extern int _ZnajRKSt9nothrow_t;
extern int _Znwj;
extern int __cxa_atexit;
extern int __cxa_guard_abort;
extern int __cxa_guard_acquire;
extern int __cxa_guard_release;
extern int __cxa_pure_virtual;
extern int atan2f;
extern int cJSON_AddArrayToObject;
extern int cJSON_AddBoolToObject;
extern int cJSON_AddItemToArray;
extern int cJSON_AddItemToObject;
extern int cJSON_AddNumberToObject;
extern int cJSON_AddStringToObject;
extern int cJSON_CreateArray;
extern int cJSON_CreateBool;
extern int cJSON_CreateNumber;
extern int cJSON_CreateObject;
extern int cJSON_CreateString;
extern int cJSON_Delete;
extern int cJSON_Duplicate;
extern int cJSON_Duplicate_rec;
extern int cJSON_GetArrayItem;
extern int cJSON_GetArraySize;
extern int cJSON_GetObjectItem;
extern int cJSON_IsArray;
extern int cJSON_IsBool;
extern int cJSON_IsNumber;
extern int cJSON_IsObject;
extern int cJSON_IsString;
extern int cJSON_IsTrue;
extern int cJSON_Parse;
extern int cJSON_ParseWithLengthOpts;
extern int cJSON_ParseWithOpts;
extern int cJSON_PrintUnformatted;
extern int cJSON_free;
extern int ceilf;
extern int cosf;
extern int esp_log;
extern int esp_log_cache_add;
extern int esp_log_cache_clean;
extern int esp_log_cache_get_level;
extern int esp_log_cache_set_level;
extern int esp_log_early_timestamp;
extern int esp_log_impl_lock;
extern int esp_log_impl_lock_timeout;
extern int esp_log_impl_unlock;
extern int esp_log_is_tag_loggable;
extern int esp_log_level_get;
extern int esp_log_level_get_timeout;
extern int esp_log_level_set;
extern int esp_log_linked_list_clean;
extern int esp_log_linked_list_get_level;
extern int esp_log_linked_list_set_level;
extern int esp_log_set_default_level;
extern int esp_log_timestamp;
extern int esp_log_util_set_cache_enabled_cb;
extern int esp_log_va;
extern int esp_log_write;
extern int esp_log_writev;
extern int esp_lv_adapter_deinit;
extern int esp_lv_adapter_display_bridge_v9_create;
extern int esp_lv_adapter_fps_stats_enable;
extern int esp_lv_adapter_ft_font_get;
extern int esp_lv_adapter_ft_font_init;
extern int esp_lv_adapter_get_context;
extern int esp_lv_adapter_get_fps;
extern int esp_lv_adapter_get_required_frame_buffer_count;
extern int esp_lv_adapter_init;
extern int esp_lv_adapter_lock;
extern int esp_lv_adapter_pause;
extern int esp_lv_adapter_register_display;
extern int esp_lv_adapter_start;
extern int esp_lv_adapter_te_sync_begin_frame;
extern int esp_lv_adapter_te_sync_create;
extern int esp_lv_adapter_te_sync_destroy;
extern int esp_lv_adapter_te_sync_is_enabled;
extern int esp_lv_adapter_te_sync_record_tx_done;
extern int esp_lv_adapter_te_sync_record_tx_start;
extern int esp_lv_adapter_te_sync_wait_for_vsync;
extern int esp_lv_adapter_unlock;
extern int esp_lv_adapter_unregister_input_device;
extern int esp_lv_adapter_unregister_touch;
extern int esp_random;
extern int fabsf;
extern int floorf;
extern int fmodf;
extern int jpeg_acquire_codec_handle;
extern int jpeg_alloc_decoder_mem;
extern int jpeg_calloc_align_inner;
extern int jpeg_calloc_inner;
extern int jpeg_check_intr_priority;
extern int jpeg_dec_close;
extern int jpeg_dec_create_huffman_tbl;
extern int jpeg_dec_huffman;
extern int jpeg_dec_open;
extern int jpeg_dec_parse_dqt;
extern int jpeg_dec_parse_header;
extern int jpeg_dec_parse_sof0;
extern int jpeg_dec_parse_soi;
extern int jpeg_dec_parse_sos;
extern int jpeg_dec_process;
extern int jpeg_dec_process_0;
extern int jpeg_dec_process_180;
extern int jpeg_dec_process_270;
extern int jpeg_dec_process_90;
extern int jpeg_dec_process_scale;
extern int jpeg_decoder_get_info;
extern int jpeg_decoder_process;
extern int jpeg_del_decoder_engine;
extern int jpeg_del_encoder_engine;
extern int jpeg_encoder_process;
extern int jpeg_free;
extern int jpeg_free_align;
extern int jpeg_get_bytes;
extern int jpeg_hal_config_ac0_table;
extern int jpeg_hal_config_ac1_table;
extern int jpeg_hal_config_dc0_table;
extern int jpeg_hal_config_dc1_table;
extern int jpeg_hal_init;
extern int jpeg_hal_set_quantization_coefficient;
extern int jpeg_isr_deregister;
extern int jpeg_isr_register;
extern int jpeg_new_decoder_engine;
extern int jpeg_new_encoder_engine;
extern int jpeg_parse_appn_marker;
extern int jpeg_parse_com_marker;
extern int jpeg_parse_dht_marker;
extern int jpeg_parse_dqt_marker;
extern int jpeg_parse_dri_marker;
extern int jpeg_parse_inv_marker;
extern int jpeg_parse_sof_marker;
extern int jpeg_parse_sos_marker;
extern int jpeg_release_codec_handle;
extern int lv_anim_core_deinit;
extern int lv_anim_core_init;
extern int lv_anim_delete;
extern int lv_anim_delete_all;
extern int lv_anim_get;
extern int lv_anim_init;
extern int lv_anim_path_custom_bezier3;
extern int lv_anim_path_ease_out;
extern int lv_anim_path_linear;
extern int lv_anim_path_step;
extern int lv_anim_resolve_speed;
extern int lv_anim_set_bezier3_param;
extern int lv_anim_set_completed_cb;
extern int lv_anim_set_delay;
extern int lv_anim_set_deleted_cb;
extern int lv_anim_set_duration;
extern int lv_anim_set_early_apply;
extern int lv_anim_set_exec_cb;
extern int lv_anim_set_path_cb;
extern int lv_anim_set_repeat_count;
extern int lv_anim_set_repeat_delay;
extern int lv_anim_set_reverse_delay;
extern int lv_anim_set_reverse_duration;
extern int lv_anim_set_start_cb;
extern int lv_anim_set_user_data;
extern int lv_anim_set_values;
extern int lv_anim_set_var;
extern int lv_anim_speed_clamped;
extern int lv_anim_start;
extern int lv_arc_get_knob_offset;
extern int lv_arc_get_value;
extern int lv_arc_set_angles;
extern int lv_arc_set_bg_angles;
extern int lv_arc_set_bg_end_angle;
extern int lv_arc_set_bg_start_angle;
extern int lv_arc_set_end_angle;
extern int lv_arc_set_rotation;
extern int lv_arc_set_start_angle;
extern int lv_arc_set_value;
extern int lv_area_align;
extern int lv_area_diff;
extern int lv_area_get_height;
extern int lv_area_get_size;
extern int lv_area_get_width;
extern int lv_area_increase;
extern int lv_area_intersect;
extern int lv_area_is_in;
extern int lv_area_is_on;
extern int lv_area_is_out;
extern int lv_area_is_point_on;
extern int lv_area_join;
extern int lv_area_move;
extern int lv_area_set;
extern int lv_area_set_height;
extern int lv_area_set_width;
extern int lv_array_at;
extern int lv_array_deinit;
extern int lv_array_init;
extern int lv_array_push_back;
extern int lv_array_remove;
extern int lv_array_resize;
extern int lv_array_shrink;
extern int lv_async_call;
extern int lv_async_call_cancel;
extern int lv_atan2;
extern int lv_bar_create;
extern int lv_bar_get_mode;
extern int lv_bar_get_start_value;
extern int lv_bar_get_value;
extern int lv_bar_is_symmetrical;
extern int lv_bar_set_range;
extern int lv_bar_set_start_value;
extern int lv_bar_set_value;
extern int lv_bin_decoder_close;
extern int lv_bin_decoder_get_area;
extern int lv_bin_decoder_info;
extern int lv_bin_decoder_init;
extern int lv_bin_decoder_open;
extern int lv_button_create;
extern int lv_buttonmatrix_clear_button_ctrl;
extern int lv_buttonmatrix_clear_button_ctrl_all;
extern int lv_buttonmatrix_create;
extern int lv_buttonmatrix_get_button_text;
extern int lv_buttonmatrix_get_selected_button;
extern int lv_buttonmatrix_has_button_ctrl;
extern int lv_buttonmatrix_set_button_ctrl;
extern int lv_buttonmatrix_set_button_ctrl_all;
extern int lv_buttonmatrix_set_ctrl_map;
extern int lv_buttonmatrix_set_map;
extern int lv_buttonmatrix_set_one_checked;
extern int lv_buttonmatrix_set_selected_button;
extern int lv_cache_acquire;
extern int lv_cache_acquire_or_create;
extern int lv_cache_add;
extern int lv_cache_create;
extern int lv_cache_destroy;
extern int lv_cache_drop;
extern int lv_cache_drop_all;
extern int lv_cache_entry_acquire_data;
extern int lv_cache_entry_dec_ref;
extern int lv_cache_entry_delete;
extern int lv_cache_entry_get_data;
extern int lv_cache_entry_get_entry;
extern int lv_cache_entry_get_ref;
extern int lv_cache_entry_get_size;
extern int lv_cache_entry_inc_ref;
extern int lv_cache_entry_init;
extern int lv_cache_entry_is_invalid;
extern int lv_cache_entry_release_data;
extern int lv_cache_entry_set_flag;
extern int lv_cache_is_enabled;
extern int lv_cache_release;
extern int lv_cache_set_name;
extern int lv_calendar_get_showed_date;
extern int lv_calendar_set_month_shown;
extern int lv_calendar_set_today_date;
extern int lv_canvas_create;
extern int lv_canvas_get_draw_buf;
extern int lv_canvas_set_buffer;
extern int lv_chart_get_point_pos_by_id;
extern int lv_chart_get_series_next;
extern int lv_color16_luminance;
extern int lv_color16_premultiply;
extern int lv_color24_luminance;
extern int lv_color32_eq;
extern int lv_color32_luminance;
extern int lv_color32_make;
extern int lv_color_16_16_mix;
extern int lv_color_black;
extern int lv_color_brightness;
extern int lv_color_eq;
extern int lv_color_format_get_bpp;
extern int lv_color_format_get_size;
extern int lv_color_format_has_alpha;
extern int lv_color_hex;
extern int lv_color_luminance;
extern int lv_color_make;
extern int lv_color_mix;
extern int lv_color_mix32;
extern int lv_color_mix32_premultiplied;
extern int lv_color_over32;
extern int lv_color_premultiply;
extern int lv_color_to_32;
extern int lv_color_to_int;
extern int lv_color_to_u16;
extern int lv_color_to_u32;
extern int lv_color_white;
extern int lv_cubic_bezier;
extern int lv_deinit;
extern int lv_display_add_event_cb;
extern int lv_display_create;
extern int lv_display_delete;
extern int lv_display_delete_event;
extern int lv_display_enable_invalidation;
extern int lv_display_flush_is_last;
extern int lv_display_flush_ready;
extern int lv_display_get_color_format;
extern int lv_display_get_default;
extern int lv_display_get_dpi;
extern int lv_display_get_event_count;
extern int lv_display_get_event_dsc;
extern int lv_display_get_horizontal_resolution;
extern int lv_display_get_layer_bottom;
extern int lv_display_get_layer_sys;
extern int lv_display_get_layer_top;
extern int lv_display_get_matrix_rotation;
extern int lv_display_get_next;
extern int lv_display_get_original_horizontal_resolution;
extern int lv_display_get_original_vertical_resolution;
extern int lv_display_get_rotation;
extern int lv_display_get_screen_active;
extern int lv_display_get_screen_prev;
extern int lv_display_get_theme;
extern int lv_display_get_user_data;
extern int lv_display_get_vertical_resolution;
extern int lv_display_is_double_buffered;
extern int lv_display_is_invalidation_enabled;
extern int lv_display_refr_timer;
extern int lv_display_register_vsync_event;
extern int lv_display_remove_event_cb_with_user_data;
extern int lv_display_rotate_point;
extern int lv_display_send_event;
extern int lv_display_set_buffers;
extern int lv_display_set_default;
extern int lv_display_set_draw_buffers;
extern int lv_display_set_flush_cb;
extern int lv_display_set_render_mode;
extern int lv_display_set_user_data;
extern int lv_display_unregister_vsync_event;
extern int lv_dpx;
extern int lv_draw_add_task;
extern int lv_draw_arc;
extern int lv_draw_arc_dsc_init;
extern int lv_draw_arc_get_area;
extern int lv_draw_blur;
extern int lv_draw_blur_dsc_init;
extern int lv_draw_border_dsc_init;
extern int lv_draw_buf_adjust_stride;
extern int lv_draw_buf_align;
extern int lv_draw_buf_align_ex;
extern int lv_draw_buf_clear;
extern int lv_draw_buf_convert_premultiply;
extern int lv_draw_buf_copy;
extern int lv_draw_buf_create;
extern int lv_draw_buf_create_ex;
extern int lv_draw_buf_destroy;
extern int lv_draw_buf_dup_ex;
extern int lv_draw_buf_flush_cache;
extern int lv_draw_buf_from_image;
extern int lv_draw_buf_get_handlers;
extern int lv_draw_buf_goto_xy;
extern int lv_draw_buf_handlers_init;
extern int lv_draw_buf_init;
extern int lv_draw_buf_init_handlers;
extern int lv_draw_buf_init_with_default_handlers;
extern int lv_draw_buf_invalidate_cache;
extern int lv_draw_buf_ppa_init_handlers;
extern int lv_draw_buf_premultiply;
extern int lv_draw_buf_reshape;
extern int lv_draw_buf_width_to_stride;
extern int lv_draw_buf_width_to_stride_ex;
extern int lv_draw_create_unit;
extern int lv_draw_deinit;
extern int lv_draw_dispatch;
extern int lv_draw_dispatch_layer;
extern int lv_draw_dispatch_request;
extern int lv_draw_dispatch_wait_for_request;
extern int lv_draw_fill_dsc_init;
extern int lv_draw_finalize_task_creation;
extern int lv_draw_get_available_task;
extern int lv_draw_get_next_available_task;
extern int lv_draw_glyph_dsc_init;
extern int lv_draw_image;
extern int lv_draw_image_dsc_init;
extern int lv_draw_image_normal_helper;
extern int lv_draw_image_tiled_helper;
extern int lv_draw_init;
extern int lv_draw_label;
extern int lv_draw_label_dsc_init;
extern int lv_draw_label_iterate_characters;
extern int lv_draw_layer;
extern int lv_draw_layer_alloc_buf;
extern int lv_draw_layer_create;
extern int lv_draw_layer_create_drop_shadow;
extern int lv_draw_layer_finish_drop_shadow;
extern int lv_draw_layer_go_to_xy;
extern int lv_draw_layer_init;
extern int lv_draw_line;
extern int lv_draw_line_dsc_init;
extern int lv_draw_line_iterate;
extern int lv_draw_mask_rect;
extern int lv_draw_mask_rect_dsc_init;
extern int lv_draw_ppa_fill;
extern int lv_draw_ppa_img;
extern int lv_draw_ppa_init;
extern int lv_draw_rect;
extern int lv_draw_rect_dsc_init;
extern int lv_draw_sw_arc;
extern int lv_draw_sw_blend;
extern int lv_draw_sw_blend_color_to_a8;
extern int lv_draw_sw_blend_color_to_al88;
extern int lv_draw_sw_blend_color_to_argb8888;
extern int lv_draw_sw_blend_color_to_argb8888_premultiplied;
extern int lv_draw_sw_blend_color_to_i1;
extern int lv_draw_sw_blend_color_to_l8;
extern int lv_draw_sw_blend_color_to_rgb565;
extern int lv_draw_sw_blend_color_to_rgb565_swapped;
extern int lv_draw_sw_blend_color_to_rgb888;
extern int lv_draw_sw_blend_image_to_a8;
extern int lv_draw_sw_blend_image_to_al88;
extern int lv_draw_sw_blend_image_to_argb8888;
extern int lv_draw_sw_blend_image_to_argb8888_premultiplied;
extern int lv_draw_sw_blend_image_to_i1;
extern int lv_draw_sw_blend_image_to_l8;
extern int lv_draw_sw_blend_image_to_rgb565;
extern int lv_draw_sw_blend_image_to_rgb565_swapped;
extern int lv_draw_sw_blend_image_to_rgb888;
extern int lv_draw_sw_blend_riscv_v_argb8888_premultiplied_to_rgb888;
extern int lv_draw_sw_blend_riscv_v_argb8888_to_rgb888;
extern int lv_draw_sw_blend_riscv_v_argb8888_to_rgb888_with_mask;
extern int lv_draw_sw_blend_riscv_v_argb8888_to_rgb888_with_opa;
extern int lv_draw_sw_blend_riscv_v_argb8888_to_rgb888_with_opa_mask;
extern int lv_draw_sw_blend_riscv_v_color_to_rgb888;
extern int lv_draw_sw_blend_riscv_v_color_to_rgb888_with_mask;
extern int lv_draw_sw_blend_riscv_v_color_to_rgb888_with_opa;
extern int lv_draw_sw_blend_riscv_v_color_to_rgb888_with_opa_mask;
extern int lv_draw_sw_blend_riscv_v_rgb565_to_rgb888;
extern int lv_draw_sw_blend_riscv_v_rgb565_to_rgb888_with_mask;
extern int lv_draw_sw_blend_riscv_v_rgb565_to_rgb888_with_opa;
extern int lv_draw_sw_blend_riscv_v_rgb565_to_rgb888_with_opa_mask;
extern int lv_draw_sw_blend_riscv_v_rgb888_to_rgb888;
extern int lv_draw_sw_blend_riscv_v_rgb888_to_rgb888_with_mask;
extern int lv_draw_sw_blend_riscv_v_rgb888_to_rgb888_with_opa;
extern int lv_draw_sw_blend_riscv_v_rgb888_to_rgb888_with_opa_mask;
extern int lv_draw_sw_blur;
extern int lv_draw_sw_border;
extern int lv_draw_sw_box_shadow;
extern int lv_draw_sw_deinit;
extern int lv_draw_sw_fill;
extern int lv_draw_sw_get_blend_handler;
extern int lv_draw_sw_grad_cleanup;
extern int lv_draw_sw_grad_color_calculate;
extern int lv_draw_sw_grad_get;
extern int lv_draw_sw_image;
extern int lv_draw_sw_init;
extern int lv_draw_sw_label;
extern int lv_draw_sw_layer;
extern int lv_draw_sw_letter;
extern int lv_draw_sw_line;
extern int lv_draw_sw_mask_angle_init;
extern int lv_draw_sw_mask_apply;
extern int lv_draw_sw_mask_cleanup;
extern int lv_draw_sw_mask_deinit;
extern int lv_draw_sw_mask_free_param;
extern int lv_draw_sw_mask_init;
extern int lv_draw_sw_mask_line_angle_init;
extern int lv_draw_sw_mask_line_points_init;
extern int lv_draw_sw_mask_radius_init;
extern int lv_draw_sw_mask_rect;
extern int lv_draw_sw_register_blend_handler;
extern int lv_draw_sw_rgb565_swap;
extern int lv_draw_sw_transform;
extern int lv_draw_sw_triangle;
extern int lv_draw_task_get_border_dsc;
extern int lv_draw_task_get_fill_dsc;
extern int lv_draw_task_get_label_dsc;
extern int lv_draw_unit_draw_letter;
extern int lv_draw_unit_send_event;
extern int lv_draw_wait_for_finish;
extern int lv_dropdown_close;
extern int lv_dropdown_create;
extern int lv_dropdown_get_options;
extern int lv_dropdown_get_selected;
extern int lv_dropdown_get_selected_str;
extern int lv_dropdown_is_open;
extern int lv_dropdown_open;
extern int lv_dropdown_set_options;
extern int lv_dropdown_set_options_static;
extern int lv_dropdown_set_selected;
extern int lv_event_add;
extern int lv_event_dsc_get_cb;
extern int lv_event_dsc_get_user_data;
extern int lv_event_get_code;
extern int lv_event_get_count;
extern int lv_event_get_current_target;
extern int lv_event_get_dsc;
extern int lv_event_get_indev;
extern int lv_event_get_invalidated_area;
extern int lv_event_get_key;
extern int lv_event_get_layer;
extern int lv_event_get_param;
extern int lv_event_get_prev_state;
extern int lv_event_get_rotary_diff;
extern int lv_event_get_target;
extern int lv_event_get_target_obj;
extern int lv_event_get_user_data;
extern int lv_event_mark_deleted;
extern int lv_event_pop;
extern int lv_event_push;
extern int lv_event_push_and_send;
extern int lv_event_remove;
extern int lv_event_remove_all;
extern int lv_event_send;
extern int lv_event_set_ext_draw_size;
extern int lv_event_stop_processing;
extern int lv_flex_init;
extern int lv_font_get_bitmap_fmt_txt;
extern int lv_font_get_default;
extern int lv_font_get_glyph_bitmap;
extern int lv_font_get_glyph_dsc;
extern int lv_font_get_glyph_dsc_fmt_txt;
extern int lv_font_get_glyph_static_bitmap;
extern int lv_font_get_glyph_width;
extern int lv_font_get_line_height;
extern int lv_font_glyph_release_draw_data;
extern int lv_font_has_static_bitmap;
extern int lv_free;
extern int lv_free_core;
extern int lv_freetype_create_draw_data_image;
extern int lv_freetype_create_draw_data_outline;
extern int lv_freetype_create_glyph_cache;
extern int lv_freetype_font_create_with_info;
extern int lv_freetype_font_delete;
extern int lv_freetype_get_context;
extern int lv_freetype_init;
extern int lv_freetype_init_font_info;
extern int lv_freetype_italic_transform;
extern int lv_freetype_italic_transform_on_pos;
extern int lv_freetype_set_cbs_glyph;
extern int lv_freetype_set_cbs_image_font;
extern int lv_freetype_set_cbs_outline_font;
extern int lv_freetype_uninit;
extern int lv_fs_close;
extern int lv_fs_deinit;
extern int lv_fs_drv_init;
extern int lv_fs_drv_register;
extern int lv_fs_get_buffer_from_path;
extern int lv_fs_get_drv;
extern int lv_fs_get_ext;
extern int lv_fs_init;
extern int lv_fs_open;
extern int lv_fs_read;
extern int lv_fs_seek;
extern int lv_fs_tell;
extern int lv_grid_init;
extern int lv_group_add_obj;
extern int lv_group_create;
extern int lv_group_deinit;
extern int lv_group_delete;
extern int lv_group_focus_next;
extern int lv_group_focus_obj;
extern int lv_group_focus_prev;
extern int lv_group_get_default;
extern int lv_group_get_editing;
extern int lv_group_get_focused;
extern int lv_group_get_obj_count;
extern int lv_group_init;
extern int lv_group_remove_obj;
extern int lv_group_send_data;
extern int lv_group_set_default;
extern int lv_group_set_editing;
extern int lv_image_buf_get_transformed_area;
extern int lv_image_cache_drop;
extern int lv_image_cache_init;
extern int lv_image_cache_is_enabled;
extern int lv_image_create;
extern int lv_image_decoder_add_to_cache;
extern int lv_image_decoder_close;
extern int lv_image_decoder_create;
extern int lv_image_decoder_deinit;
extern int lv_image_decoder_delete;
extern int lv_image_decoder_get_area;
extern int lv_image_decoder_get_info;
extern int lv_image_decoder_init;
extern int lv_image_decoder_open;
extern int lv_image_decoder_post_process;
extern int lv_image_decoder_set_close_cb;
extern int lv_image_decoder_set_get_area_cb;
extern int lv_image_decoder_set_info_cb;
extern int lv_image_decoder_set_open_cb;
extern int lv_image_get_pivot;
extern int lv_image_get_scale;
extern int lv_image_get_src;
extern int lv_image_header_cache_drop;
extern int lv_image_header_cache_init;
extern int lv_image_header_cache_is_enabled;
extern int lv_image_set_inner_align;
extern int lv_image_set_pivot;
extern int lv_image_set_rotation;
extern int lv_image_set_scale;
extern int lv_image_set_src;
extern int lv_image_src_get_type;
extern int lv_ime_pinyin_create;
extern int lv_ime_pinyin_get_cand_panel;
extern int lv_ime_pinyin_set_dict;
extern int lv_ime_pinyin_set_keyboard;
extern int lv_ime_pinyin_set_mode;
extern int lv_indev_active;
extern int lv_indev_create;
extern int lv_indev_delete;
extern int lv_indev_find_scroll_obj;
extern int lv_indev_get_active_obj;
extern int lv_indev_get_display;
extern int lv_indev_get_driver_data;
extern int lv_indev_get_group;
extern int lv_indev_get_next;
extern int lv_indev_get_point;
extern int lv_indev_get_scroll_dir;
extern int lv_indev_get_scroll_obj;
extern int lv_indev_get_state;
extern int lv_indev_get_type;
extern int lv_indev_get_user_data;
extern int lv_indev_get_vect;
extern int lv_indev_read;
extern int lv_indev_read_timer_cb;
extern int lv_indev_reset;
extern int lv_indev_scroll_handler;
extern int lv_indev_scroll_throw_handler;
extern int lv_indev_scroll_throw_predict;
extern int lv_indev_search_obj;
extern int lv_indev_send_event;
extern int lv_indev_set_display;
extern int lv_indev_set_driver_data;
extern int lv_indev_set_group;
extern int lv_indev_set_read_cb;
extern int lv_indev_set_type;
extern int lv_indev_set_user_data;
extern int lv_indev_wait_release;
extern int lv_init;
extern int lv_inv_area;
extern int lv_iter_create;
extern int lv_keyboard_create;
extern int lv_keyboard_def_event_cb;
extern int lv_keyboard_get_textarea;
extern int lv_keyboard_set_mode;
extern int lv_keyboard_set_textarea;
extern int lv_label_create;
extern int lv_label_get_letter_on;
extern int lv_label_get_letter_pos;
extern int lv_label_get_recolor;
extern int lv_label_get_text;
extern int lv_label_get_text_selection_end;
extern int lv_label_get_text_selection_start;
extern int lv_label_ins_text;
extern int lv_label_is_char_under_pos;
extern int lv_label_set_long_mode;
extern int lv_label_set_text;
extern int lv_label_set_text_fmt;
extern int lv_label_set_text_selection_end;
extern int lv_label_set_text_selection_start;
extern int lv_label_set_text_static;
extern int lv_label_set_text_vfmt;
extern int lv_layer_init;
extern int lv_layer_reset;
extern int lv_layer_sys;
extern int lv_layer_top;
extern int lv_layout_apply;
extern int lv_layout_deinit;
extern int lv_layout_get_min_size;
extern int lv_layout_init;
extern int lv_line_get_point_count;
extern int lv_line_get_points_mutable;
extern int lv_line_is_point_array_mutable;
extern int lv_line_set_points_mutable;
extern int lv_ll_clear;
extern int lv_ll_clear_custom;
extern int lv_ll_get_head;
extern int lv_ll_get_len;
extern int lv_ll_get_next;
extern int lv_ll_get_prev;
extern int lv_ll_get_tail;
extern int lv_ll_init;
extern int lv_ll_ins_head;
extern int lv_ll_ins_prev;
extern int lv_ll_ins_tail;
extern int lv_ll_is_empty;
extern int lv_ll_move_before;
extern int lv_ll_remove;
extern int lv_lock;
extern int lv_malloc;
extern int lv_malloc_core;
extern int lv_malloc_zeroed;
extern int lv_map;
extern int lv_mem_deinit;
extern int lv_mem_init;
extern int lv_memcpy;
extern int lv_memmove;
extern int lv_memset;
extern int lv_menu_back_button_is_root;
extern int lv_menu_clear_history;
extern int lv_menu_get_cur_main_page;
extern int lv_menu_get_cur_sidebar_page;
extern int lv_menu_set_page;
extern int lv_mutex_delete;
extern int lv_mutex_init;
extern int lv_mutex_lock;
extern int lv_mutex_unlock;
extern int lv_obj_add_event_cb;
extern int lv_obj_add_flag;
extern int lv_obj_add_state;
extern int lv_obj_add_style;
extern int lv_obj_align;
extern int lv_obj_align_to;
extern int lv_obj_allocate_spec_attr;
extern int lv_obj_area_is_visible;
extern int lv_obj_calc_dynamic_height;
extern int lv_obj_calc_dynamic_width;
extern int lv_obj_calculate_ext_draw_size;
extern int lv_obj_calculate_style_text_align;
extern int lv_obj_center;
extern int lv_obj_check_type;
extern int lv_obj_class_create_obj;
extern int lv_obj_class_init_obj;
extern int lv_obj_create;
extern int lv_obj_delete;
extern int lv_obj_delete_async;
extern int lv_obj_destruct;
extern int lv_obj_enable_style_refresh;
extern int lv_obj_event_base;
extern int lv_obj_get_child;
extern int lv_obj_get_child_by_type;
extern int lv_obj_get_child_count;
extern int lv_obj_get_child_count_by_type;
extern int lv_obj_get_click_area;
extern int lv_obj_get_content_coords;
extern int lv_obj_get_content_height;
extern int lv_obj_get_content_width;
extern int lv_obj_get_coords;
extern int lv_obj_get_display;
extern int lv_obj_get_event_count;
extern int lv_obj_get_event_dsc;
extern int lv_obj_get_ext_draw_size;
extern int lv_obj_get_group;
extern int lv_obj_get_height;
extern int lv_obj_get_index;
extern int lv_obj_get_layer_type;
extern int lv_obj_get_local_style_prop;
extern int lv_obj_get_parent;
extern int lv_obj_get_screen;
extern int lv_obj_get_scroll_bottom;
extern int lv_obj_get_scroll_dir;
extern int lv_obj_get_scroll_end;
extern int lv_obj_get_scroll_left;
extern int lv_obj_get_scroll_right;
extern int lv_obj_get_scroll_snap_x;
extern int lv_obj_get_scroll_snap_y;
extern int lv_obj_get_scroll_top;
extern int lv_obj_get_scroll_x;
extern int lv_obj_get_scroll_y;
extern int lv_obj_get_scrollbar_area;
extern int lv_obj_get_scrollbar_mode;
extern int lv_obj_get_self_height;
extern int lv_obj_get_self_width;
extern int lv_obj_get_state;
extern int lv_obj_get_style_opa_recursive;
extern int lv_obj_get_style_prop;
extern int lv_obj_get_style_recolor_recursive;
extern int lv_obj_get_transformed_area;
extern int lv_obj_get_user_data;
extern int lv_obj_get_width;
extern int lv_obj_get_x;
extern int lv_obj_get_y;
extern int lv_obj_get_y2;
extern int lv_obj_get_y_aligned;
extern int lv_obj_has_class;
extern int lv_obj_has_flag;
extern int lv_obj_has_flag_any;
extern int lv_obj_has_state;
extern int lv_obj_hit_test;
extern int lv_obj_init_draw_arc_dsc;
extern int lv_obj_init_draw_blur_dsc;
extern int lv_obj_init_draw_image_dsc;
extern int lv_obj_init_draw_label_dsc;
extern int lv_obj_init_draw_line_dsc;
extern int lv_obj_init_draw_rect_dsc;
extern int lv_obj_invalidate;
extern int lv_obj_invalidate_area;
extern int lv_obj_is_editable;
extern int lv_obj_is_group_def;
extern int lv_obj_is_layout_positioned;
extern int lv_obj_is_radio_button;
extern int lv_obj_is_valid;
extern int lv_obj_is_visible;
extern int lv_obj_mark_layout_as_dirty;
extern int lv_obj_move_children_by;
extern int lv_obj_move_to;
extern int lv_obj_move_to_index;
extern int lv_obj_readjust_scroll;
extern int lv_obj_redraw;
extern int lv_obj_refr;
extern int lv_obj_refr_pos;
extern int lv_obj_refr_size;
extern int lv_obj_refresh_ext_draw_size;
extern int lv_obj_refresh_self_size;
extern int lv_obj_refresh_style;
extern int lv_obj_remove_event;
extern int lv_obj_remove_event_cb;
extern int lv_obj_remove_flag;
extern int lv_obj_remove_local_style_prop;
extern int lv_obj_remove_state;
extern int lv_obj_remove_style;
extern int lv_obj_remove_style_all;
extern int lv_obj_report_style_change;
extern int lv_obj_scroll_by;
extern int lv_obj_scroll_by_bounded;
extern int lv_obj_scroll_by_raw;
extern int lv_obj_scroll_to;
extern int lv_obj_scroll_to_view;
extern int lv_obj_scroll_to_view_recursive;
extern int lv_obj_scroll_to_x;
extern int lv_obj_scroll_to_y;
extern int lv_obj_scrollbar_invalidate;
extern int lv_obj_send_event;
extern int lv_obj_set_ext_click_area;
extern int lv_obj_set_flex_align;
extern int lv_obj_set_flex_flow;
extern int lv_obj_set_flex_grow;
extern int lv_obj_set_height;
extern int lv_obj_set_layout;
extern int lv_obj_set_local_style_prop;
extern int lv_obj_set_parent;
extern int lv_obj_set_pos;
extern int lv_obj_set_scroll_dir;
extern int lv_obj_set_scroll_snap_x;
extern int lv_obj_set_scroll_snap_y;
extern int lv_obj_set_scrollbar_mode;
extern int lv_obj_set_size;
extern int lv_obj_set_state;
extern int lv_obj_set_style_align;
extern int lv_obj_set_style_base_dir;
extern int lv_obj_set_style_bg_color;
extern int lv_obj_set_style_bg_image_src;
extern int lv_obj_set_style_bg_opa;
extern int lv_obj_set_style_border_color;
extern int lv_obj_set_style_border_opa;
extern int lv_obj_set_style_border_side;
extern int lv_obj_set_style_border_width;
extern int lv_obj_set_style_clip_corner;
extern int lv_obj_set_style_flex_cross_place;
extern int lv_obj_set_style_flex_flow;
extern int lv_obj_set_style_flex_grow;
extern int lv_obj_set_style_flex_main_place;
extern int lv_obj_set_style_flex_track_place;
extern int lv_obj_set_style_height;
extern int lv_obj_set_style_layout;
extern int lv_obj_set_style_margin_bottom;
extern int lv_obj_set_style_margin_top;
extern int lv_obj_set_style_min_width;
extern int lv_obj_set_style_opa;
extern int lv_obj_set_style_outline_color;
extern int lv_obj_set_style_outline_opa;
extern int lv_obj_set_style_outline_pad;
extern int lv_obj_set_style_outline_width;
extern int lv_obj_set_style_pad_bottom;
extern int lv_obj_set_style_pad_column;
extern int lv_obj_set_style_pad_left;
extern int lv_obj_set_style_pad_right;
extern int lv_obj_set_style_pad_row;
extern int lv_obj_set_style_pad_top;
extern int lv_obj_set_style_radius;
extern int lv_obj_set_style_shadow_color;
extern int lv_obj_set_style_shadow_opa;
extern int lv_obj_set_style_shadow_width;
extern int lv_obj_set_style_text_align;
extern int lv_obj_set_style_text_color;
extern int lv_obj_set_style_text_font;
extern int lv_obj_set_style_text_line_space;
extern int lv_obj_set_style_transform_pivot_x;
extern int lv_obj_set_style_transform_pivot_y;
extern int lv_obj_set_style_transform_rotation;
extern int lv_obj_set_style_transform_scale_x;
extern int lv_obj_set_style_transform_scale_y;
extern int lv_obj_set_style_width;
extern int lv_obj_set_style_x;
extern int lv_obj_set_style_y;
extern int lv_obj_set_user_data;
extern int lv_obj_set_width;
extern int lv_obj_set_x;
extern int lv_obj_set_y;
extern int lv_obj_stop_scroll_anim;
extern int lv_obj_style_apply_color_filter;
extern int lv_obj_style_apply_recolor;
extern int lv_obj_style_create_transition;
extern int lv_obj_style_deinit;
extern int lv_obj_style_init;
extern int lv_obj_style_state_compare;
extern int lv_obj_transform_point;
extern int lv_obj_transform_point_array;
extern int lv_obj_tree_walk;
extern int lv_obj_update_layer_type;
extern int lv_obj_update_layout;
extern int lv_os_init;
extern int lv_palette_darken;
extern int lv_palette_lighten;
extern int lv_palette_main;
extern int lv_pct;
extern int lv_pct_to_px;
extern int lv_point_array_transform;
extern int lv_point_from_precise;
extern int lv_point_set;
extern int lv_point_swap;
extern int lv_point_to_precise;
extern int lv_point_transform;
extern int lv_pow;
extern int lv_rand_set_seed;
extern int lv_rb_destroy;
extern int lv_rb_drop_node;
extern int lv_rb_find;
extern int lv_rb_init;
extern int lv_rb_insert;
extern int lv_rb_minimum_from;
extern int lv_rb_remove_node;
extern int lv_realloc;
extern int lv_realloc_core;
extern int lv_refr_deinit;
extern int lv_refr_get_disp_refreshing;
extern int lv_refr_get_top_obj;
extern int lv_refr_init;
extern int lv_refr_set_disp_refreshing;
extern int lv_roller_set_options;
extern int lv_roller_set_selected;
extern int lv_scale_set_image_needle_value;
extern int lv_scale_set_line_needle_value;
extern int lv_screen_active;
extern int lv_screen_load;
extern int lv_screen_load_anim;
extern int lv_slider_create;
extern int lv_slider_get_left_value;
extern int lv_slider_get_mode;
extern int lv_slider_get_value;
extern int lv_slider_is_symmetrical;
extern int lv_slider_set_range;
extern int lv_slider_set_start_value;
extern int lv_slider_set_value;
extern int lv_snprintf;
extern int lv_span_stack_deinit;
extern int lv_span_stack_init;
extern int lv_spinbox_decrement;
extern int lv_spinbox_increment;
extern int lv_spinbox_step_next;
extern int lv_spinbox_step_prev;
extern int lv_spinner_set_anim_params;
extern int lv_sqrt32;
extern int lv_strcmp;
extern int lv_strcpy;
extern int lv_strdup;
extern int lv_strlen;
extern int lv_strncpy;
extern int lv_strndup;
extern int lv_strnlen;
extern int lv_style_get_prop;
extern int lv_style_init;
extern int lv_style_is_empty;
extern int lv_style_prop_get_default;
extern int lv_style_prop_lookup_flags;
extern int lv_style_remove_prop;
extern int lv_style_reset;
extern int lv_style_set_anim_duration;
extern int lv_style_set_arc_color;
extern int lv_style_set_arc_rounded;
extern int lv_style_set_arc_width;
extern int lv_style_set_bg_color;
extern int lv_style_set_bg_grad_color;
extern int lv_style_set_bg_image_src;
extern int lv_style_set_bg_opa;
extern int lv_style_set_border_color;
extern int lv_style_set_border_opa;
extern int lv_style_set_border_post;
extern int lv_style_set_border_side;
extern int lv_style_set_border_width;
extern int lv_style_set_clip_corner;
extern int lv_style_set_height;
extern int lv_style_set_length;
extern int lv_style_set_line_color;
extern int lv_style_set_line_width;
extern int lv_style_set_max_height;
extern int lv_style_set_outline_color;
extern int lv_style_set_outline_opa;
extern int lv_style_set_outline_pad;
extern int lv_style_set_outline_width;
extern int lv_style_set_pad_bottom;
extern int lv_style_set_pad_column;
extern int lv_style_set_pad_left;
extern int lv_style_set_pad_right;
extern int lv_style_set_pad_row;
extern int lv_style_set_pad_top;
extern int lv_style_set_prop;
extern int lv_style_set_radius;
extern int lv_style_set_recolor;
extern int lv_style_set_recolor_opa;
extern int lv_style_set_rotary_sensitivity;
extern int lv_style_set_shadow_color;
extern int lv_style_set_shadow_offset_y;
extern int lv_style_set_shadow_opa;
extern int lv_style_set_shadow_spread;
extern int lv_style_set_shadow_width;
extern int lv_style_set_text_align;
extern int lv_style_set_text_color;
extern int lv_style_set_text_font;
extern int lv_style_set_text_line_space;
extern int lv_style_set_transform_height;
extern int lv_style_set_transform_width;
extern int lv_style_set_transition;
extern int lv_style_set_width;
extern int lv_style_transition_dsc_init;
extern int lv_tabview_get_content;
extern int lv_tabview_get_tab_active;
extern int lv_tabview_get_tab_bar;
extern int lv_tabview_get_tab_count;
extern int lv_tabview_set_active;
extern int lv_tabview_set_tab_bar_position;
extern int lv_tabview_set_tab_bar_size;
extern int lv_text_cut;
extern int lv_text_encoded_letter_next_2;
extern int lv_text_get_next_line;
extern int lv_text_get_size_attributes;
extern int lv_text_get_width;
extern int lv_text_ins;
extern int lv_text_is_cmd;
extern int lv_text_set_text_vfmt;
extern int lv_textarea_add_char;
extern int lv_textarea_add_text;
extern int lv_textarea_clear_selection;
extern int lv_textarea_create;
extern int lv_textarea_cursor_down;
extern int lv_textarea_cursor_left;
extern int lv_textarea_cursor_right;
extern int lv_textarea_cursor_up;
extern int lv_textarea_delete_char;
extern int lv_textarea_delete_char_forward;
extern int lv_textarea_get_accepted_chars;
extern int lv_textarea_get_cursor_pos;
extern int lv_textarea_get_max_length;
extern int lv_textarea_get_one_line;
extern int lv_textarea_get_password_bullet;
extern int lv_textarea_get_text;
extern int lv_textarea_set_cursor_click_pos;
extern int lv_textarea_set_cursor_pos;
extern int lv_textarea_set_one_line;
extern int lv_textarea_set_password_mode;
extern int lv_textarea_set_placeholder_text;
extern int lv_textarea_set_text;
extern int lv_theme_apply;
extern int lv_theme_default_deinit;
extern int lv_theme_default_get;
extern int lv_theme_default_init;
extern int lv_theme_default_is_inited;
extern int lv_theme_get_color_primary;
extern int lv_theme_get_from_obj;
extern int lv_theme_simple_deinit;
extern int lv_thread_delete;
extern int lv_thread_init;
extern int lv_thread_sync_delete;
extern int lv_thread_sync_init;
extern int lv_thread_sync_signal;
extern int lv_thread_sync_wait;
extern int lv_tick_diff;
extern int lv_tick_elaps;
extern int lv_tick_get;
extern int lv_tick_inc;
extern int lv_timer_core_deinit;
extern int lv_timer_core_init;
extern int lv_timer_create;
extern int lv_timer_delete;
extern int lv_timer_enable;
extern int lv_timer_get_next;
extern int lv_timer_get_paused;
extern int lv_timer_get_user_data;
extern int lv_timer_handler;
extern int lv_timer_pause;
extern int lv_timer_ready;
extern int lv_timer_resume;
extern int lv_timer_set_repeat_count;
extern int lv_trigo_cos;
extern int lv_trigo_sin;
extern int lv_unlock;
extern int lv_utils_bsearch;
extern int lv_vsnprintf;
extern int memcmp;
extern int memcpy;
extern int memset;
extern int powf;
extern int rand;
extern int sinf;
extern int snprintf;
extern int sqrtf;
extern int srand;
extern int strchr;
extern int strcmp;
extern int strlen;
extern int strrchr;
extern int strstr;
extern int vsnprintf;
#pragma GCC diagnostic pop

const struct esp_elfsym g_main_elfsyms[] = {
    { "_ZN10BleGamepad10autoScanCbEP13ble_gap_eventPv", (void*)&_ZN10BleGamepad10autoScanCbEP13ble_gap_eventPv },
    { "_ZN10BleGamepad10conectNextEm", (void*)&_ZN10BleGamepad10conectNextEm },
    { "_ZN10BleGamepad10conectNextEv", (void*)&_ZN10BleGamepad10conectNextEv },
    { "_ZN10BleGamepad10disconnectEh", (void*)&_ZN10BleGamepad10disconnectEh },
    { "_ZN10BleGamepad10initNimbleEv", (void*)&_ZN10BleGamepad10initNimbleEv },
    { "_ZN10BleGamepad10movePlayerEhh", (void*)&_ZN10BleGamepad10movePlayerEhh },
    { "_ZN10BleGamepad11processTaskEPv", (void*)&_ZN10BleGamepad11processTaskEPv },
    { "_ZN10BleGamepad13allocPlayerIdEv", (void*)&_ZN10BleGamepad13allocPlayerIdEv },
    { "_ZN10BleGamepad13connectDirectERK10ScanDevice", (void*)&_ZN10BleGamepad13connectDirectERK10ScanDevice },
    { "_ZN10BleGamepad14scanGapEventCbEP13ble_gap_eventPv", (void*)&_ZN10BleGamepad14scanGapEventCbEP13ble_gap_eventPv },
    { "_ZN10BleGamepad15connectGapEventEP13ble_gap_eventPv", (void*)&_ZN10BleGamepad15connectGapEventEP13ble_gap_eventPv },
    { "_ZN10BleGamepad15initEspHostedBtEv", (void*)&_ZN10BleGamepad15initEspHostedBtEv },
    { "_ZN10BleGamepad15syncPairedToNvsEv", (void*)&_ZN10BleGamepad15syncPairedToNvsEv },
    { "_ZN10BleGamepad17autoConnectPairedEv", (void*)&_ZN10BleGamepad17autoConnectPairedEv },
    { "_ZN10BleGamepad17loadPairedFromNvsEv", (void*)&_ZN10BleGamepad17loadPairedFromNvsEv },
    { "_ZN10BleGamepad5startEP7Display", (void*)&_ZN10BleGamepad5startEP7Display },
    { "_ZN10BleGamepad6onSyncEv", (void*)&_ZN10BleGamepad6onSyncEv },
    { "_ZN10BleGamepad7connectEPKh", (void*)&_ZN10BleGamepad7connectEPKh },
    { "_ZN10BleGamepad7connectEh", (void*)&_ZN10BleGamepad7connectEh },
    { "_ZN10BleGamepad7onResetEi", (void*)&_ZN10BleGamepad7onResetEi },
    { "_ZN10BleGamepad8hostTaskEPv", (void*)&_ZN10BleGamepad8hostTaskEPv },
    { "_ZN10BleGamepad8instanceEv", (void*)&_ZN10BleGamepad8instanceEv },
    { "_ZN10BleGamepad8stopScanEv", (void*)&_ZN10BleGamepad8stopScanEv },
    { "_ZN10BleGamepad9startScanEv", (void*)&_ZN10BleGamepad9startScanEv },
    { "_ZN10BleGamepadC1Ev", (void*)&_ZN10BleGamepadC1Ev },
    { "_ZN10BleGamepadC2Ev", (void*)&_ZN10BleGamepadC2Ev },
    { "_ZN10BleGamepadD1Ev", (void*)&_ZN10BleGamepadD1Ev },
    { "_ZN10BleGamepadD2Ev", (void*)&_ZN10BleGamepadD2Ev },
    { "_ZN10FontLoader10getDefaultENS_8FontSizeE", (void*)&_ZN10FontLoader10getDefaultENS_8FontSizeE },
    { "_ZN10FontLoader10setDefaultEPK10_lv_font_tNS_8FontSizeE", (void*)&_ZN10FontLoader10setDefaultEPK10_lv_font_tNS_8FontSizeE },
    { "_ZN10FontLoader4loadEPKct", (void*)&_ZN10FontLoader4loadEPKct },
    { "_ZN10GT911Indev10threadMainEPv", (void*)&_ZN10GT911Indev10threadMainEPv },
    { "_ZN10GT911Indev11indevReadCbEP11_lv_indev_tP15lv_indev_data_t", (void*)&_ZN10GT911Indev11indevReadCbEP11_lv_indev_tP15lv_indev_data_t },
    { "_ZN10GT911Indev14updateAndRouteEv", (void*)&_ZN10GT911Indev14updateAndRouteEv },
    { "_ZN10GT911Indev5isrCbEP15esp_lcd_touch_s", (void*)&_ZN10GT911Indev5isrCbEP15esp_lcd_touch_s },
    { "_ZN10GT911Indev5startEP7DisplayP5Touch", (void*)&_ZN10GT911Indev5startEP7DisplayP5Touch },
    { "_ZN10GT911Indev8instanceEv", (void*)&_ZN10GT911Indev8instanceEv },
    { "_ZN10GT911IndevC1Ev", (void*)&_ZN10GT911IndevC1Ev },
    { "_ZN10GT911IndevC2Ev", (void*)&_ZN10GT911IndevC2Ev },
    { "_ZN10GT911IndevD1Ev", (void*)&_ZN10GT911IndevD1Ev },
    { "_ZN10GT911IndevD2Ev", (void*)&_ZN10GT911IndevD2Ev },
    { "_ZN11AudioHandle4Impl11applyVolumeEPsjf", (void*)&_ZN11AudioHandle4Impl11applyVolumeEPsjf },
    { "_ZN11AudioHandle4Impl8taskFuncEPv", (void*)&_ZN11AudioHandle4Impl8taskFuncEPv },
    { "_ZN11AudioHandle4Impl8taskLoopEv", (void*)&_ZN11AudioHandle4Impl8taskLoopEv },
    { "_ZN11AudioHandle4ImplC1Ev", (void*)&_ZN11AudioHandle4ImplC1Ev },
    { "_ZN11AudioHandle4ImplC2Ev", (void*)&_ZN11AudioHandle4ImplC2Ev },
    { "_ZN11AudioHandle4ImplD1Ev", (void*)&_ZN11AudioHandle4ImplD1Ev },
    { "_ZN11AudioHandle4ImplD2Ev", (void*)&_ZN11AudioHandle4ImplD2Ev },
    { "_ZN11AudioHandle4playEv", (void*)&_ZN11AudioHandle4playEv },
    { "_ZN11AudioHandle6detachEv", (void*)&_ZN11AudioHandle6detachEv },
    { "_ZN11AudioHandle7setLoopEb", (void*)&_ZN11AudioHandle7setLoopEb },
    { "_ZN11AudioHandleC1EOS_", (void*)&_ZN11AudioHandleC1EOS_ },
    { "_ZN11AudioHandleC2EOS_", (void*)&_ZN11AudioHandleC2EOS_ },
    { "_ZN11AudioHandleD1Ev", (void*)&_ZN11AudioHandleD1Ev },
    { "_ZN11AudioHandleD2Ev", (void*)&_ZN11AudioHandleD2Ev },
    { "_ZN11AudioHandleaSEOS_", (void*)&_ZN11AudioHandleaSEOS_ },
    { "_ZN11AudioSample4loadEPKc", (void*)&_ZN11AudioSample4loadEPKc },
    { "_ZN11AudioSample4playEb", (void*)&_ZN11AudioSample4playEb },
    { "_ZN11AudioSample6unloadEv", (void*)&_ZN11AudioSample6unloadEv },
    { "_ZN11TimeService11setAutoSyncEb", (void*)&_ZN11TimeService11setAutoSyncEb },
    { "_ZN11TimeService12onGotIpEventEPvPKclS0_", (void*)&_ZN11TimeService12onGotIpEventEPvPKclS0_ },
    { "_ZN11TimeService12onTimeSyncCbEP7timeval", (void*)&_ZN11TimeService12onTimeSyncCbEP7timeval },
    { "_ZN11TimeService12removeSyncCbEm", (void*)&_ZN11TimeService12removeSyncCbEm },
    { "_ZN11TimeService14notifySyncDoneEv", (void*)&_ZN11TimeService14notifySyncDoneEv },
    { "_ZN11TimeService5startEv", (void*)&_ZN11TimeService5startEv },
    { "_ZN11TimeService8instanceEv", (void*)&_ZN11TimeService8instanceEv },
    { "_ZN11TimeService9addSyncCbESt8functionIFvvEE", (void*)&_ZN11TimeService9addSyncCbESt8functionIFvvEE },
    { "_ZN11TimeService9forceSyncEv", (void*)&_ZN11TimeService9forceSyncEv },
    { "_ZN11TimeServiceC1Ev", (void*)&_ZN11TimeServiceC1Ev },
    { "_ZN11TimeServiceC2Ev", (void*)&_ZN11TimeServiceC2Ev },
    { "_ZN11TimeServiceD1Ev", (void*)&_ZN11TimeServiceD1Ev },
    { "_ZN11TimeServiceD2Ev", (void*)&_ZN11TimeServiceD2Ev },
    { "_ZN12VirtualIndev11indevReadCbEP11_lv_indev_tP15lv_indev_data_t", (void*)&_ZN12VirtualIndev11indevReadCbEP11_lv_indev_tP15lv_indev_data_t },
    { "_ZN12VirtualIndev5startEP7Display", (void*)&_ZN12VirtualIndev5startEP7Display },
    { "_ZN12VirtualIndev8instanceEv", (void*)&_ZN12VirtualIndev8instanceEv },
    { "_ZN12VirtualIndev9sendTouchE16lv_indev_state_t10lv_point_t", (void*)&_ZN12VirtualIndev9sendTouchE16lv_indev_state_t10lv_point_t },
    { "_ZN12VirtualIndevC1Ev", (void*)&_ZN12VirtualIndevC1Ev },
    { "_ZN12VirtualIndevC2Ev", (void*)&_ZN12VirtualIndevC2Ev },
    { "_ZN12VirtualIndevD1Ev", (void*)&_ZN12VirtualIndevD1Ev },
    { "_ZN12VirtualIndevD2Ev", (void*)&_ZN12VirtualIndevD2Ev },
    { "_ZN14BatteryManager10getPercentEv", (void*)&_ZN14BatteryManager10getPercentEv },
    { "_ZN14BatteryManager10isChargingEv", (void*)&_ZN14BatteryManager10isChargingEv },
    { "_ZN14BatteryManager12getVoltageMvEv", (void*)&_ZN14BatteryManager12getVoltageMvEv },
    { "_ZN14BatteryManager16chargingDetectCbEPv", (void*)&_ZN14BatteryManager16chargingDetectCbEPv },
    { "_ZN14BatteryManager16stopChargingAnimEP9_lv_obj_t", (void*)&_ZN14BatteryManager16stopChargingAnimEP9_lv_obj_t },
    { "_ZN14BatteryManager17startChargingAnimEP9_lv_obj_t", (void*)&_ZN14BatteryManager17startChargingAnimEP9_lv_obj_t },
    { "_ZN14BatteryManager4initEv", (void*)&_ZN14BatteryManager4initEv },
    { "_ZN14BatteryManager7getIconEi", (void*)&_ZN14BatteryManager7getIconEi },
    { "_ZN14BatteryManager8getColorEi", (void*)&_ZN14BatteryManager8getColorEi },
    { "_ZN14BatteryManager8instanceEv", (void*)&_ZN14BatteryManager8instanceEv },
    { "_ZN14VirtualGamepad11resetPlayerEaa", (void*)&_ZN14VirtualGamepad11resetPlayerEaa },
    { "_ZN14VirtualGamepad11setAllMasksEPKh", (void*)&_ZN14VirtualGamepad11setAllMasksEPKh },
    { "_ZN14VirtualGamepad12dispatchTaskEPv", (void*)&_ZN14VirtualGamepad12dispatchTaskEPv },
    { "_ZN14VirtualGamepad13forwarderLoopEv", (void*)&_ZN14VirtualGamepad13forwarderLoopEv },
    { "_ZN14VirtualGamepad13forwarderTaskEPv", (void*)&_ZN14VirtualGamepad13forwarderTaskEPv },
    { "_ZN14VirtualGamepad13stopForwarderEv", (void*)&_ZN14VirtualGamepad13stopForwarderEv },
    { "_ZN14VirtualGamepad14startForwarderEv", (void*)&_ZN14VirtualGamepad14startForwarderEv },
    { "_ZN14VirtualGamepad5startEP7Display", (void*)&_ZN14VirtualGamepad5startEP7Display },
    { "_ZN14VirtualGamepad6injectEaaRK12GamepadState", (void*)&_ZN14VirtualGamepad6injectEaaRK12GamepadState },
    { "_ZN14VirtualGamepad8instanceEv", (void*)&_ZN14VirtualGamepad8instanceEv },
    { "_ZN14VirtualGamepad9packFrameEPhaaRK12GamepadState", (void*)&_ZN14VirtualGamepad9packFrameEPhaaRK12GamepadState },
    { "_ZN14VirtualGamepadD1Ev", (void*)&_ZN14VirtualGamepadD1Ev },
    { "_ZN14VirtualGamepadD2Ev", (void*)&_ZN14VirtualGamepadD2Ev },
    { "_ZN15AppStackManager11createStackEv", (void*)&_ZN15AppStackManager11createStackEv },
    { "_ZN15AppStackManager12destroyStackEP8AppStack", (void*)&_ZN15AppStackManager12destroyStackEP8AppStack },
    { "_ZN15AppStackManager13switchToStackEP8AppStack", (void*)&_ZN15AppStackManager13switchToStackEP8AppStack },
    { "_ZN15AppStackManager13switchToStackEj", (void*)&_ZN15AppStackManager13switchToStackEj },
    { "_ZN15AppStackManager14pushToNewStackEP3App", (void*)&_ZN15AppStackManager14pushToNewStackEP3App },
    { "_ZN15AppStackManager16transitionStacksEP8AppStackS1_", (void*)&_ZN15AppStackManager16transitionStacksEP8AppStackS1_ },
    { "_ZN15AppStackManager3popEv", (void*)&_ZN15AppStackManager3popEv },
    { "_ZN15AppStackManager4pushEP3App", (void*)&_ZN15AppStackManager4pushEP3App },
    { "_ZN15AppStackManager7replaceEP3App", (void*)&_ZN15AppStackManager7replaceEP3App },
    { "_ZN15AppStackManagerC1EP7Display", (void*)&_ZN15AppStackManagerC1EP7Display },
    { "_ZN15AppStackManagerC1Ev", (void*)&_ZN15AppStackManagerC1Ev },
    { "_ZN15AppStackManagerC2EP7Display", (void*)&_ZN15AppStackManagerC2EP7Display },
    { "_ZN15AppStackManagerC2Ev", (void*)&_ZN15AppStackManagerC2Ev },
    { "_ZN15AppStackManagerD1Ev", (void*)&_ZN15AppStackManagerD1Ev },
    { "_ZN15AppStackManagerD2Ev", (void*)&_ZN15AppStackManagerD2Ev },
    { "_ZN15AppStackManageraSEOS_", (void*)&_ZN15AppStackManageraSEOS_ },
    { "_ZN3App10onAiSelectERK14AiSelectTarget", (void*)&_ZN3App10onAiSelectERK14AiSelectTarget },
    { "_ZN3App10setManagerEP15AppStackManager", (void*)&_ZN3App10setManagerEP15AppStackManager },
    { "_ZN3App11replaceWithEPS_", (void*)&_ZN3App11replaceWithEPS_ },
    { "_ZN3App12getAiContextB5cxx11Ev", (void*)&_ZN3App12getAiContextB5cxx11Ev },
    { "_ZN3App12onAiActivateEv", (void*)&_ZN3App12onAiActivateEv },
    { "_ZN3App12onBackgroundEv", (void*)&_ZN3App12onBackgroundEv },
    { "_ZN3App12onForegroundEv", (void*)&_ZN3App12onForegroundEv },
    { "_ZN3App14onGamepadInputEhhRK12GamepadState", (void*)&_ZN3App14onGamepadInputEhhRK12GamepadState },
    { "_ZN3App15executeAiActionERK8AiAction", (void*)&_ZN3App15executeAiActionERK8AiAction },
    { "_ZN3App15getOpenableAppsEv", (void*)&_ZN3App15getOpenableAppsEv },
    { "_ZN3App18onGamepadConnectedEh", (void*)&_ZN3App18onGamepadConnectedEh },
    { "_ZN3App21onGamepadDisconnectedEh", (void*)&_ZN3App21onGamepadDisconnectedEh },
    { "_ZN3App4initEv", (void*)&_ZN3App4initEv },
    { "_ZN3App6deinitEv", (void*)&_ZN3App6deinitEv },
    { "_ZN3App6popAppEv", (void*)&_ZN3App6popAppEv },
    { "_ZN3App7pushAppEPS_", (void*)&_ZN3App7pushAppEPS_ },
    { "_ZN3AppC1EP7Display", (void*)&_ZN3AppC1EP7Display },
    { "_ZN3AppC2EP7Display", (void*)&_ZN3AppC2EP7Display },
    { "_ZN3AppD0Ev", (void*)&_ZN3AppD0Ev },
    { "_ZN3AppD1Ev", (void*)&_ZN3AppD1Ev },
    { "_ZN3AppD2Ev", (void*)&_ZN3AppD2Ev },
    { "_ZN3GUI10createCardEP9_lv_obj_tll", (void*)&_ZN3GUI10createCardEP9_lv_obj_tll },
    { "_ZN3GUI10createFlexEP9_lv_obj_t14lv_flex_flow_tll", (void*)&_ZN3GUI10createFlexEP9_lv_obj_t14lv_flex_flow_tll },
    { "_ZN3GUI11createLabelEP9_lv_obj_tPKc", (void*)&_ZN3GUI11createLabelEP9_lv_obj_tPKc },
    { "_ZN3GUI11createTitleEP9_lv_obj_tPKc", (void*)&_ZN3GUI11createTitleEP9_lv_obj_tPKc },
    { "_ZN3GUI12createButtonEP9_lv_obj_tPKcll", (void*)&_ZN3GUI12createButtonEP9_lv_obj_tPKcll },
    { "_ZN3GUI14createSubtitleEP9_lv_obj_tPKc", (void*)&_ZN3GUI14createSubtitleEP9_lv_obj_tPKc },
    { "_ZN3GUI17createProgressBarEP9_lv_obj_tllll", (void*)&_ZN3GUI17createProgressBarEP9_lv_obj_tllll },
    { "_ZN4Task10daemonMainEPv", (void*)&_ZN4Task10daemonMainEPv },
    { "_ZN4Task10removeTaskEPS_", (void*)&_ZN4Task10removeTaskEPS_ },
    { "_ZN4Task11setAffinityEPS_NS_8AffinityE", (void*)&_ZN4Task11setAffinityEPS_NS_8AffinityE },
    { "_ZN4Task4initEj", (void*)&_ZN4Task4initEj },
    { "_ZN4Task7addTaskEPFmPvEPKcS0_mNS_8AffinityE", (void*)&_ZN4Task7addTaskEPFmPvEPKcS0_mNS_8AffinityE },
    { "_ZN4Task8dumpTaskEv", (void*)&_ZN4Task8dumpTaskEv },
    { "_ZN4TaskC1Ev", (void*)&_ZN4TaskC1Ev },
    { "_ZN4TaskC2Ev", (void*)&_ZN4TaskC2Ev },
    { "_ZN4TaskD1Ev", (void*)&_ZN4TaskD1Ev },
    { "_ZN4TaskD2Ev", (void*)&_ZN4TaskD2Ev },
    { "_ZN5Audio12detectFormatEPKc", (void*)&_ZN5Audio12detectFormatEPKc },
    { "_ZN5Audio12freeStreamIdEh", (void*)&_ZN5Audio12freeStreamIdEh },
    { "_ZN5Audio13allocStreamIdEv", (void*)&_ZN5Audio13allocStreamIdEv },
    { "_ZN5Audio13onRenderEventE29esp_audio_render_event_type_tPv", (void*)&_ZN5Audio13onRenderEventE29esp_audio_render_event_type_tPv },
    { "_ZN5Audio15getMasterVolumeEv", (void*)&_ZN5Audio15getMasterVolumeEv },
    { "_ZN5Audio15saveVolumeToNvsEv", (void*)&_ZN5Audio15saveVolumeToNvsEv },
    { "_ZN5Audio15setMasterVolumeEi", (void*)&_ZN5Audio15setMasterVolumeEi },
    { "_ZN5Audio17loadVolumeFromNvsEv", (void*)&_ZN5Audio17loadVolumeFromNvsEv },
    { "_ZN5Audio4initER6ES8311", (void*)&_ZN5Audio4initER6ES8311 },
    { "_ZN5Audio4playEPKc", (void*)&_ZN5Audio4playEPKc },
    { "_ZN5Audio4playERK11AudioSampleb", (void*)&_ZN5Audio4playERK11AudioSampleb },
    { "_ZN5Audio6deinitEv", (void*)&_ZN5Audio6deinitEv },
    { "_ZN5Audio8instanceEv", (void*)&_ZN5Audio8instanceEv },
    { "_ZN5Audio9outWriterEPhmPv", (void*)&_ZN5Audio9outWriterEPhmPv },
    { "_ZN5AudioC1Ev", (void*)&_ZN5AudioC1Ev },
    { "_ZN5AudioC2Ev", (void*)&_ZN5AudioC2Ev },
    { "_ZN5AudioD1Ev", (void*)&_ZN5AudioD1Ev },
    { "_ZN5AudioD2Ev", (void*)&_ZN5AudioD2Ev },
    { "_ZN5MutexD1Ev", (void*)&_ZN5MutexD1Ev },
    { "_ZN5MutexD2Ev", (void*)&_ZN5MutexD2Ev },
    { "_ZN5Touch6updateEv", (void*)&_ZN5Touch6updateEv },
    { "_ZN5Touch8getCountEv", (void*)&_ZN5Touch8getCountEv },
    { "_ZN5Touch9getHandleEv", (void*)&_ZN5Touch9getHandleEv },
    { "_ZN5TouchC1ER3IIC4GPIOS2_hj", (void*)&_ZN5TouchC1ER3IIC4GPIOS2_hj },
    { "_ZN5TouchC2ER3IIC4GPIOS2_hj", (void*)&_ZN5TouchC2ER3IIC4GPIOS2_hj },
    { "_ZN5TouchD1Ev", (void*)&_ZN5TouchD1Ev },
    { "_ZN5TouchD2Ev", (void*)&_ZN5TouchD2Ev },
    { "_ZN5TouchaSEOS_", (void*)&_ZN5TouchaSEOS_ },
    { "_ZN5TouchixEh", (void*)&_ZN5TouchixEh },
    { "_ZN6ES83114initER3IICNS_6ConfigE", (void*)&_ZN6ES83114initER3IICNS_6ConfigE },
    { "_ZN6ES83114openEP27esp_codec_dev_sample_info_t", (void*)&_ZN6ES83114openEP27esp_codec_dev_sample_info_t },
    { "_ZN6ES83115closeEv", (void*)&_ZN6ES83115closeEv },
    { "_ZN6ES83115writeEPKvi", (void*)&_ZN6ES83115writeEPKvi },
    { "_ZN6ES83116deinitEv", (void*)&_ZN6ES83116deinitEv },
    { "_ZN6ES83119getVolumeEv", (void*)&_ZN6ES83119getVolumeEv },
    { "_ZN6ES83119setVolumeEi", (void*)&_ZN6ES83119setVolumeEi },
    { "_ZN6ES8311D1Ev", (void*)&_ZN6ES8311D1Ev },
    { "_ZN6ES8311D2Ev", (void*)&_ZN6ES8311D2Ev },
    { "_ZN6Thread9isRunningEv", (void*)&_ZN6Thread9isRunningEv },
    { "_ZN6ThreadC1EPFvPvEPKcS0_N4Task8PriorityEj", (void*)&_ZN6ThreadC1EPFvPvEPKcS0_N4Task8PriorityEj },
    { "_ZN6ThreadC2EPFvPvEPKcS0_N4Task8PriorityEj", (void*)&_ZN6ThreadC2EPFvPvEPKcS0_N4Task8PriorityEj },
    { "_ZN6ThreadD1Ev", (void*)&_ZN6ThreadD1Ev },
    { "_ZN6ThreadD2Ev", (void*)&_ZN6ThreadD2Ev },
    { "_ZN7Display11bindDisplayEP15esp_lcd_panel_tP18esp_lcd_panel_io_ttt32esp_lv_adapter_tear_avoid_mode_t25esp_lv_adapter_rotation_t", (void*)&_ZN7Display11bindDisplayEP15esp_lcd_panel_tP18esp_lcd_panel_io_ttt32esp_lv_adapter_tear_avoid_mode_t25esp_lv_adapter_rotation_t },
    { "_ZN7Display13setBrightnessEi", (void*)&_ZN7Display13setBrightnessEi },
    { "_ZN7Display14saveBrightnessEv", (void*)&_ZN7Display14saveBrightnessEv },
    { "_ZN7Display4initEv", (void*)&_ZN7Display4initEv },
    { "_ZN7Display5startEv", (void*)&_ZN7Display5startEv },
    { "_ZN7DisplayC1Ev", (void*)&_ZN7DisplayC1Ev },
    { "_ZN7DisplayC2Ev", (void*)&_ZN7DisplayC2Ev },
    { "_ZN7DisplayD1Ev", (void*)&_ZN7DisplayD1Ev },
    { "_ZN7DisplayD2Ev", (void*)&_ZN7DisplayD2Ev },
    { "_ZN8AppStack16scheduleDeletionEP3App", (void*)&_ZN8AppStack16scheduleDeletionEP3App },
    { "_ZN8AppStack3popEv", (void*)&_ZN8AppStack3popEv },
    { "_ZN8AppStack4pushEP3App", (void*)&_ZN8AppStack4pushEP3App },
    { "_ZN8AppStack7replaceEP3App", (void*)&_ZN8AppStack7replaceEP3App },
    { "_ZN8AppStackC1EP7DisplayP15AppStackManager", (void*)&_ZN8AppStackC1EP7DisplayP15AppStackManager },
    { "_ZN8AppStackC2EP7DisplayP15AppStackManager", (void*)&_ZN8AppStackC2EP7DisplayP15AppStackManager },
    { "_ZN8AppStackD1Ev", (void*)&_ZN8AppStackD1Ev },
    { "_ZN8AppStackD2Ev", (void*)&_ZN8AppStackD2Ev },
    { "_ZN8ILI9881c11getInstanceEv", (void*)&_ZN8ILI9881c11getInstanceEv },
    { "_ZN8ILI9881c13getBrightnessEv", (void*)&_ZN8ILI9881c13getBrightnessEv },
    { "_ZN8ILI9881c13setBrightnessEi", (void*)&_ZN8ILI9881c13setBrightnessEi },
    { "_ZN8ILI9881c14initBrightnessEv", (void*)&_ZN8ILI9881c14initBrightnessEv },
    { "_ZN8ILI9881c19saveBrightnessToNvsEv", (void*)&_ZN8ILI9881c19saveBrightnessToNvsEv },
    { "_ZN8ILI9881c21loadBrightnessFromNvsEv", (void*)&_ZN8ILI9881c21loadBrightnessFromNvsEv },
    { "_ZN8ILI9881c4initEiih", (void*)&_ZN8ILI9881c4initEiih },
    { "_ZN8ILI9881c6deinitEv", (void*)&_ZN8ILI9881c6deinitEv },
    { "_ZN8ILI9881cC1Ev", (void*)&_ZN8ILI9881cC1Ev },
    { "_ZN8ILI9881cC2Ev", (void*)&_ZN8ILI9881cC2Ev },
    { "_ZN8ILI9881cD1Ev", (void*)&_ZN8ILI9881cD1Ev },
    { "_ZN8ILI9881cD2Ev", (void*)&_ZN8ILI9881cD2Ev },
    { "_ZNK10BleGamepad14connectedCountEv", (void*)&_ZNK10BleGamepad14connectedCountEv },
    { "_ZNK10BleGamepad15getBatteryLevelEh", (void*)&_ZNK10BleGamepad15getBatteryLevelEh },
    { "_ZNK10BleGamepad17getScannedDevicesEv", (void*)&_ZNK10BleGamepad17getScannedDevicesEv },
    { "_ZNK10BleGamepad9getDeviceEh", (void*)&_ZNK10BleGamepad9getDeviceEh },
    { "_ZNK10GT911Indev12findFreeSlotEv", (void*)&_ZNK10GT911Indev12findFreeSlotEv },
    { "_ZNK10GT911Indev8getSlotsEPNS_8SlotInfoEj", (void*)&_ZNK10GT911Indev8getSlotsEPNS_8SlotInfoEj },
    { "_ZNK11AudioHandle9isPlayingEv", (void*)&_ZNK11AudioHandle9isPlayingEv },
    { "_ZNK11TimeService10isAutoSyncEv", (void*)&_ZNK11TimeService10isAutoSyncEv },
    { "_ZNK14VirtualGamepad8getStateEaa", (void*)&_ZNK14VirtualGamepad8getStateEaa },
    { "_ZNK5Mutex6unlockEv", (void*)&_ZNK5Mutex6unlockEv },
    { "_ZNK7Display13getBrightnessEv", (void*)&_ZNK7Display13getBrightnessEv },
    { "_ZNK7Display23setFpsStatisticsEnabledEb", (void*)&_ZNK7Display23setFpsStatisticsEnabledEb },
    { "_ZNK7Display6getFpsEv", (void*)&_ZNK7Display6getFpsEv },
    { "_ZNK7Display8applyAppEP3App", (void*)&_ZNK7Display8applyAppEP3App },
    { "_ZNK8AppStack3topEv", (void*)&_ZNK8AppStack3topEv },
    { "_ZSt17__throw_bad_allocv", (void*)&_ZSt17__throw_bad_allocv },
    { "_ZSt19__throw_logic_errorPKc", (void*)&_ZSt19__throw_logic_errorPKc },
    { "_ZSt20__throw_length_errorPKc", (void*)&_ZSt20__throw_length_errorPKc },
    { "_ZSt28__throw_bad_array_new_lengthv", (void*)&_ZSt28__throw_bad_array_new_lengthv },
    { "_ZdaPv", (void*)&_ZdaPv },
    { "_ZdaPvj", (void*)&_ZdaPvj },
    { "_ZdlPv", (void*)&_ZdlPv },
    { "_ZdlPvSt11align_val_t", (void*)&_ZdlPvSt11align_val_t },
    { "_ZdlPvj", (void*)&_ZdlPvj },
    { "_ZdlPvjSt11align_val_t", (void*)&_ZdlPvjSt11align_val_t },
    { "_Znaj", (void*)&_Znaj },
    { "_ZnajRKSt9nothrow_t", (void*)&_ZnajRKSt9nothrow_t },
    { "_Znwj", (void*)&_Znwj },
    { "__cxa_atexit", (void*)&__cxa_atexit },
    { "__cxa_guard_abort", (void*)&__cxa_guard_abort },
    { "__cxa_guard_acquire", (void*)&__cxa_guard_acquire },
    { "__cxa_guard_release", (void*)&__cxa_guard_release },
    { "__cxa_pure_virtual", (void*)&__cxa_pure_virtual },
    { "atan2f", (void*)&atan2f },
    { "cJSON_AddArrayToObject", (void*)&cJSON_AddArrayToObject },
    { "cJSON_AddBoolToObject", (void*)&cJSON_AddBoolToObject },
    { "cJSON_AddItemToArray", (void*)&cJSON_AddItemToArray },
    { "cJSON_AddItemToObject", (void*)&cJSON_AddItemToObject },
    { "cJSON_AddNumberToObject", (void*)&cJSON_AddNumberToObject },
    { "cJSON_AddStringToObject", (void*)&cJSON_AddStringToObject },
    { "cJSON_CreateArray", (void*)&cJSON_CreateArray },
    { "cJSON_CreateBool", (void*)&cJSON_CreateBool },
    { "cJSON_CreateNumber", (void*)&cJSON_CreateNumber },
    { "cJSON_CreateObject", (void*)&cJSON_CreateObject },
    { "cJSON_CreateString", (void*)&cJSON_CreateString },
    { "cJSON_Delete", (void*)&cJSON_Delete },
    { "cJSON_Duplicate", (void*)&cJSON_Duplicate },
    { "cJSON_Duplicate_rec", (void*)&cJSON_Duplicate_rec },
    { "cJSON_GetArrayItem", (void*)&cJSON_GetArrayItem },
    { "cJSON_GetArraySize", (void*)&cJSON_GetArraySize },
    { "cJSON_GetObjectItem", (void*)&cJSON_GetObjectItem },
    { "cJSON_IsArray", (void*)&cJSON_IsArray },
    { "cJSON_IsBool", (void*)&cJSON_IsBool },
    { "cJSON_IsNumber", (void*)&cJSON_IsNumber },
    { "cJSON_IsObject", (void*)&cJSON_IsObject },
    { "cJSON_IsString", (void*)&cJSON_IsString },
    { "cJSON_IsTrue", (void*)&cJSON_IsTrue },
    { "cJSON_Parse", (void*)&cJSON_Parse },
    { "cJSON_ParseWithLengthOpts", (void*)&cJSON_ParseWithLengthOpts },
    { "cJSON_ParseWithOpts", (void*)&cJSON_ParseWithOpts },
    { "cJSON_PrintUnformatted", (void*)&cJSON_PrintUnformatted },
    { "cJSON_free", (void*)&cJSON_free },
    { "ceilf", (void*)&ceilf },
    { "cosf", (void*)&cosf },
    { "esp_log", (void*)&esp_log },
    { "esp_log_cache_add", (void*)&esp_log_cache_add },
    { "esp_log_cache_clean", (void*)&esp_log_cache_clean },
    { "esp_log_cache_get_level", (void*)&esp_log_cache_get_level },
    { "esp_log_cache_set_level", (void*)&esp_log_cache_set_level },
    { "esp_log_early_timestamp", (void*)&esp_log_early_timestamp },
    { "esp_log_impl_lock", (void*)&esp_log_impl_lock },
    { "esp_log_impl_lock_timeout", (void*)&esp_log_impl_lock_timeout },
    { "esp_log_impl_unlock", (void*)&esp_log_impl_unlock },
    { "esp_log_is_tag_loggable", (void*)&esp_log_is_tag_loggable },
    { "esp_log_level_get", (void*)&esp_log_level_get },
    { "esp_log_level_get_timeout", (void*)&esp_log_level_get_timeout },
    { "esp_log_level_set", (void*)&esp_log_level_set },
    { "esp_log_linked_list_clean", (void*)&esp_log_linked_list_clean },
    { "esp_log_linked_list_get_level", (void*)&esp_log_linked_list_get_level },
    { "esp_log_linked_list_set_level", (void*)&esp_log_linked_list_set_level },
    { "esp_log_set_default_level", (void*)&esp_log_set_default_level },
    { "esp_log_timestamp", (void*)&esp_log_timestamp },
    { "esp_log_util_set_cache_enabled_cb", (void*)&esp_log_util_set_cache_enabled_cb },
    { "esp_log_va", (void*)&esp_log_va },
    { "esp_log_write", (void*)&esp_log_write },
    { "esp_log_writev", (void*)&esp_log_writev },
    { "esp_lv_adapter_deinit", (void*)&esp_lv_adapter_deinit },
    { "esp_lv_adapter_display_bridge_v9_create", (void*)&esp_lv_adapter_display_bridge_v9_create },
    { "esp_lv_adapter_fps_stats_enable", (void*)&esp_lv_adapter_fps_stats_enable },
    { "esp_lv_adapter_ft_font_get", (void*)&esp_lv_adapter_ft_font_get },
    { "esp_lv_adapter_ft_font_init", (void*)&esp_lv_adapter_ft_font_init },
    { "esp_lv_adapter_get_context", (void*)&esp_lv_adapter_get_context },
    { "esp_lv_adapter_get_fps", (void*)&esp_lv_adapter_get_fps },
    { "esp_lv_adapter_get_required_frame_buffer_count", (void*)&esp_lv_adapter_get_required_frame_buffer_count },
    { "esp_lv_adapter_init", (void*)&esp_lv_adapter_init },
    { "esp_lv_adapter_lock", (void*)&esp_lv_adapter_lock },
    { "esp_lv_adapter_pause", (void*)&esp_lv_adapter_pause },
    { "esp_lv_adapter_register_display", (void*)&esp_lv_adapter_register_display },
    { "esp_lv_adapter_start", (void*)&esp_lv_adapter_start },
    { "esp_lv_adapter_te_sync_begin_frame", (void*)&esp_lv_adapter_te_sync_begin_frame },
    { "esp_lv_adapter_te_sync_create", (void*)&esp_lv_adapter_te_sync_create },
    { "esp_lv_adapter_te_sync_destroy", (void*)&esp_lv_adapter_te_sync_destroy },
    { "esp_lv_adapter_te_sync_is_enabled", (void*)&esp_lv_adapter_te_sync_is_enabled },
    { "esp_lv_adapter_te_sync_record_tx_done", (void*)&esp_lv_adapter_te_sync_record_tx_done },
    { "esp_lv_adapter_te_sync_record_tx_start", (void*)&esp_lv_adapter_te_sync_record_tx_start },
    { "esp_lv_adapter_te_sync_wait_for_vsync", (void*)&esp_lv_adapter_te_sync_wait_for_vsync },
    { "esp_lv_adapter_unlock", (void*)&esp_lv_adapter_unlock },
    { "esp_lv_adapter_unregister_input_device", (void*)&esp_lv_adapter_unregister_input_device },
    { "esp_lv_adapter_unregister_touch", (void*)&esp_lv_adapter_unregister_touch },
    { "esp_random", (void*)&esp_random },
    { "fabsf", (void*)&fabsf },
    { "floorf", (void*)&floorf },
    { "fmodf", (void*)&fmodf },
    { "jpeg_acquire_codec_handle", (void*)&jpeg_acquire_codec_handle },
    { "jpeg_alloc_decoder_mem", (void*)&jpeg_alloc_decoder_mem },
    { "jpeg_calloc_align_inner", (void*)&jpeg_calloc_align_inner },
    { "jpeg_calloc_inner", (void*)&jpeg_calloc_inner },
    { "jpeg_check_intr_priority", (void*)&jpeg_check_intr_priority },
    { "jpeg_dec_close", (void*)&jpeg_dec_close },
    { "jpeg_dec_create_huffman_tbl", (void*)&jpeg_dec_create_huffman_tbl },
    { "jpeg_dec_huffman", (void*)&jpeg_dec_huffman },
    { "jpeg_dec_open", (void*)&jpeg_dec_open },
    { "jpeg_dec_parse_dqt", (void*)&jpeg_dec_parse_dqt },
    { "jpeg_dec_parse_header", (void*)&jpeg_dec_parse_header },
    { "jpeg_dec_parse_sof0", (void*)&jpeg_dec_parse_sof0 },
    { "jpeg_dec_parse_soi", (void*)&jpeg_dec_parse_soi },
    { "jpeg_dec_parse_sos", (void*)&jpeg_dec_parse_sos },
    { "jpeg_dec_process", (void*)&jpeg_dec_process },
    { "jpeg_dec_process_0", (void*)&jpeg_dec_process_0 },
    { "jpeg_dec_process_180", (void*)&jpeg_dec_process_180 },
    { "jpeg_dec_process_270", (void*)&jpeg_dec_process_270 },
    { "jpeg_dec_process_90", (void*)&jpeg_dec_process_90 },
    { "jpeg_dec_process_scale", (void*)&jpeg_dec_process_scale },
    { "jpeg_decoder_get_info", (void*)&jpeg_decoder_get_info },
    { "jpeg_decoder_process", (void*)&jpeg_decoder_process },
    { "jpeg_del_decoder_engine", (void*)&jpeg_del_decoder_engine },
    { "jpeg_del_encoder_engine", (void*)&jpeg_del_encoder_engine },
    { "jpeg_encoder_process", (void*)&jpeg_encoder_process },
    { "jpeg_free", (void*)&jpeg_free },
    { "jpeg_free_align", (void*)&jpeg_free_align },
    { "jpeg_get_bytes", (void*)&jpeg_get_bytes },
    { "jpeg_hal_config_ac0_table", (void*)&jpeg_hal_config_ac0_table },
    { "jpeg_hal_config_ac1_table", (void*)&jpeg_hal_config_ac1_table },
    { "jpeg_hal_config_dc0_table", (void*)&jpeg_hal_config_dc0_table },
    { "jpeg_hal_config_dc1_table", (void*)&jpeg_hal_config_dc1_table },
    { "jpeg_hal_init", (void*)&jpeg_hal_init },
    { "jpeg_hal_set_quantization_coefficient", (void*)&jpeg_hal_set_quantization_coefficient },
    { "jpeg_isr_deregister", (void*)&jpeg_isr_deregister },
    { "jpeg_isr_register", (void*)&jpeg_isr_register },
    { "jpeg_new_decoder_engine", (void*)&jpeg_new_decoder_engine },
    { "jpeg_new_encoder_engine", (void*)&jpeg_new_encoder_engine },
    { "jpeg_parse_appn_marker", (void*)&jpeg_parse_appn_marker },
    { "jpeg_parse_com_marker", (void*)&jpeg_parse_com_marker },
    { "jpeg_parse_dht_marker", (void*)&jpeg_parse_dht_marker },
    { "jpeg_parse_dqt_marker", (void*)&jpeg_parse_dqt_marker },
    { "jpeg_parse_dri_marker", (void*)&jpeg_parse_dri_marker },
    { "jpeg_parse_inv_marker", (void*)&jpeg_parse_inv_marker },
    { "jpeg_parse_sof_marker", (void*)&jpeg_parse_sof_marker },
    { "jpeg_parse_sos_marker", (void*)&jpeg_parse_sos_marker },
    { "jpeg_release_codec_handle", (void*)&jpeg_release_codec_handle },
    { "lv_anim_core_deinit", (void*)&lv_anim_core_deinit },
    { "lv_anim_core_init", (void*)&lv_anim_core_init },
    { "lv_anim_delete", (void*)&lv_anim_delete },
    { "lv_anim_delete_all", (void*)&lv_anim_delete_all },
    { "lv_anim_get", (void*)&lv_anim_get },
    { "lv_anim_init", (void*)&lv_anim_init },
    { "lv_anim_path_custom_bezier3", (void*)&lv_anim_path_custom_bezier3 },
    { "lv_anim_path_ease_out", (void*)&lv_anim_path_ease_out },
    { "lv_anim_path_linear", (void*)&lv_anim_path_linear },
    { "lv_anim_path_step", (void*)&lv_anim_path_step },
    { "lv_anim_resolve_speed", (void*)&lv_anim_resolve_speed },
    { "lv_anim_set_bezier3_param", (void*)&lv_anim_set_bezier3_param },
    { "lv_anim_set_completed_cb", (void*)&lv_anim_set_completed_cb },
    { "lv_anim_set_delay", (void*)&lv_anim_set_delay },
    { "lv_anim_set_deleted_cb", (void*)&lv_anim_set_deleted_cb },
    { "lv_anim_set_duration", (void*)&lv_anim_set_duration },
    { "lv_anim_set_early_apply", (void*)&lv_anim_set_early_apply },
    { "lv_anim_set_exec_cb", (void*)&lv_anim_set_exec_cb },
    { "lv_anim_set_path_cb", (void*)&lv_anim_set_path_cb },
    { "lv_anim_set_repeat_count", (void*)&lv_anim_set_repeat_count },
    { "lv_anim_set_repeat_delay", (void*)&lv_anim_set_repeat_delay },
    { "lv_anim_set_reverse_delay", (void*)&lv_anim_set_reverse_delay },
    { "lv_anim_set_reverse_duration", (void*)&lv_anim_set_reverse_duration },
    { "lv_anim_set_start_cb", (void*)&lv_anim_set_start_cb },
    { "lv_anim_set_user_data", (void*)&lv_anim_set_user_data },
    { "lv_anim_set_values", (void*)&lv_anim_set_values },
    { "lv_anim_set_var", (void*)&lv_anim_set_var },
    { "lv_anim_speed_clamped", (void*)&lv_anim_speed_clamped },
    { "lv_anim_start", (void*)&lv_anim_start },
    { "lv_arc_get_knob_offset", (void*)&lv_arc_get_knob_offset },
    { "lv_arc_get_value", (void*)&lv_arc_get_value },
    { "lv_arc_set_angles", (void*)&lv_arc_set_angles },
    { "lv_arc_set_bg_angles", (void*)&lv_arc_set_bg_angles },
    { "lv_arc_set_bg_end_angle", (void*)&lv_arc_set_bg_end_angle },
    { "lv_arc_set_bg_start_angle", (void*)&lv_arc_set_bg_start_angle },
    { "lv_arc_set_end_angle", (void*)&lv_arc_set_end_angle },
    { "lv_arc_set_rotation", (void*)&lv_arc_set_rotation },
    { "lv_arc_set_start_angle", (void*)&lv_arc_set_start_angle },
    { "lv_arc_set_value", (void*)&lv_arc_set_value },
    { "lv_area_align", (void*)&lv_area_align },
    { "lv_area_diff", (void*)&lv_area_diff },
    { "lv_area_get_height", (void*)&lv_area_get_height },
    { "lv_area_get_size", (void*)&lv_area_get_size },
    { "lv_area_get_width", (void*)&lv_area_get_width },
    { "lv_area_increase", (void*)&lv_area_increase },
    { "lv_area_intersect", (void*)&lv_area_intersect },
    { "lv_area_is_in", (void*)&lv_area_is_in },
    { "lv_area_is_on", (void*)&lv_area_is_on },
    { "lv_area_is_out", (void*)&lv_area_is_out },
    { "lv_area_is_point_on", (void*)&lv_area_is_point_on },
    { "lv_area_join", (void*)&lv_area_join },
    { "lv_area_move", (void*)&lv_area_move },
    { "lv_area_set", (void*)&lv_area_set },
    { "lv_area_set_height", (void*)&lv_area_set_height },
    { "lv_area_set_width", (void*)&lv_area_set_width },
    { "lv_array_at", (void*)&lv_array_at },
    { "lv_array_deinit", (void*)&lv_array_deinit },
    { "lv_array_init", (void*)&lv_array_init },
    { "lv_array_push_back", (void*)&lv_array_push_back },
    { "lv_array_remove", (void*)&lv_array_remove },
    { "lv_array_resize", (void*)&lv_array_resize },
    { "lv_array_shrink", (void*)&lv_array_shrink },
    { "lv_async_call", (void*)&lv_async_call },
    { "lv_async_call_cancel", (void*)&lv_async_call_cancel },
    { "lv_atan2", (void*)&lv_atan2 },
    { "lv_bar_create", (void*)&lv_bar_create },
    { "lv_bar_get_mode", (void*)&lv_bar_get_mode },
    { "lv_bar_get_start_value", (void*)&lv_bar_get_start_value },
    { "lv_bar_get_value", (void*)&lv_bar_get_value },
    { "lv_bar_is_symmetrical", (void*)&lv_bar_is_symmetrical },
    { "lv_bar_set_range", (void*)&lv_bar_set_range },
    { "lv_bar_set_start_value", (void*)&lv_bar_set_start_value },
    { "lv_bar_set_value", (void*)&lv_bar_set_value },
    { "lv_bin_decoder_close", (void*)&lv_bin_decoder_close },
    { "lv_bin_decoder_get_area", (void*)&lv_bin_decoder_get_area },
    { "lv_bin_decoder_info", (void*)&lv_bin_decoder_info },
    { "lv_bin_decoder_init", (void*)&lv_bin_decoder_init },
    { "lv_bin_decoder_open", (void*)&lv_bin_decoder_open },
    { "lv_button_create", (void*)&lv_button_create },
    { "lv_buttonmatrix_clear_button_ctrl", (void*)&lv_buttonmatrix_clear_button_ctrl },
    { "lv_buttonmatrix_clear_button_ctrl_all", (void*)&lv_buttonmatrix_clear_button_ctrl_all },
    { "lv_buttonmatrix_create", (void*)&lv_buttonmatrix_create },
    { "lv_buttonmatrix_get_button_text", (void*)&lv_buttonmatrix_get_button_text },
    { "lv_buttonmatrix_get_selected_button", (void*)&lv_buttonmatrix_get_selected_button },
    { "lv_buttonmatrix_has_button_ctrl", (void*)&lv_buttonmatrix_has_button_ctrl },
    { "lv_buttonmatrix_set_button_ctrl", (void*)&lv_buttonmatrix_set_button_ctrl },
    { "lv_buttonmatrix_set_button_ctrl_all", (void*)&lv_buttonmatrix_set_button_ctrl_all },
    { "lv_buttonmatrix_set_ctrl_map", (void*)&lv_buttonmatrix_set_ctrl_map },
    { "lv_buttonmatrix_set_map", (void*)&lv_buttonmatrix_set_map },
    { "lv_buttonmatrix_set_one_checked", (void*)&lv_buttonmatrix_set_one_checked },
    { "lv_buttonmatrix_set_selected_button", (void*)&lv_buttonmatrix_set_selected_button },
    { "lv_cache_acquire", (void*)&lv_cache_acquire },
    { "lv_cache_acquire_or_create", (void*)&lv_cache_acquire_or_create },
    { "lv_cache_add", (void*)&lv_cache_add },
    { "lv_cache_create", (void*)&lv_cache_create },
    { "lv_cache_destroy", (void*)&lv_cache_destroy },
    { "lv_cache_drop", (void*)&lv_cache_drop },
    { "lv_cache_drop_all", (void*)&lv_cache_drop_all },
    { "lv_cache_entry_acquire_data", (void*)&lv_cache_entry_acquire_data },
    { "lv_cache_entry_dec_ref", (void*)&lv_cache_entry_dec_ref },
    { "lv_cache_entry_delete", (void*)&lv_cache_entry_delete },
    { "lv_cache_entry_get_data", (void*)&lv_cache_entry_get_data },
    { "lv_cache_entry_get_entry", (void*)&lv_cache_entry_get_entry },
    { "lv_cache_entry_get_ref", (void*)&lv_cache_entry_get_ref },
    { "lv_cache_entry_get_size", (void*)&lv_cache_entry_get_size },
    { "lv_cache_entry_inc_ref", (void*)&lv_cache_entry_inc_ref },
    { "lv_cache_entry_init", (void*)&lv_cache_entry_init },
    { "lv_cache_entry_is_invalid", (void*)&lv_cache_entry_is_invalid },
    { "lv_cache_entry_release_data", (void*)&lv_cache_entry_release_data },
    { "lv_cache_entry_set_flag", (void*)&lv_cache_entry_set_flag },
    { "lv_cache_is_enabled", (void*)&lv_cache_is_enabled },
    { "lv_cache_release", (void*)&lv_cache_release },
    { "lv_cache_set_name", (void*)&lv_cache_set_name },
    { "lv_calendar_get_showed_date", (void*)&lv_calendar_get_showed_date },
    { "lv_calendar_set_month_shown", (void*)&lv_calendar_set_month_shown },
    { "lv_calendar_set_today_date", (void*)&lv_calendar_set_today_date },
    { "lv_canvas_create", (void*)&lv_canvas_create },
    { "lv_canvas_get_draw_buf", (void*)&lv_canvas_get_draw_buf },
    { "lv_canvas_set_buffer", (void*)&lv_canvas_set_buffer },
    { "lv_chart_get_point_pos_by_id", (void*)&lv_chart_get_point_pos_by_id },
    { "lv_chart_get_series_next", (void*)&lv_chart_get_series_next },
    { "lv_color16_luminance", (void*)&lv_color16_luminance },
    { "lv_color16_premultiply", (void*)&lv_color16_premultiply },
    { "lv_color24_luminance", (void*)&lv_color24_luminance },
    { "lv_color32_eq", (void*)&lv_color32_eq },
    { "lv_color32_luminance", (void*)&lv_color32_luminance },
    { "lv_color32_make", (void*)&lv_color32_make },
    { "lv_color_16_16_mix", (void*)&lv_color_16_16_mix },
    { "lv_color_black", (void*)&lv_color_black },
    { "lv_color_brightness", (void*)&lv_color_brightness },
    { "lv_color_eq", (void*)&lv_color_eq },
    { "lv_color_format_get_bpp", (void*)&lv_color_format_get_bpp },
    { "lv_color_format_get_size", (void*)&lv_color_format_get_size },
    { "lv_color_format_has_alpha", (void*)&lv_color_format_has_alpha },
    { "lv_color_hex", (void*)&lv_color_hex },
    { "lv_color_luminance", (void*)&lv_color_luminance },
    { "lv_color_make", (void*)&lv_color_make },
    { "lv_color_mix", (void*)&lv_color_mix },
    { "lv_color_mix32", (void*)&lv_color_mix32 },
    { "lv_color_mix32_premultiplied", (void*)&lv_color_mix32_premultiplied },
    { "lv_color_over32", (void*)&lv_color_over32 },
    { "lv_color_premultiply", (void*)&lv_color_premultiply },
    { "lv_color_to_32", (void*)&lv_color_to_32 },
    { "lv_color_to_int", (void*)&lv_color_to_int },
    { "lv_color_to_u16", (void*)&lv_color_to_u16 },
    { "lv_color_to_u32", (void*)&lv_color_to_u32 },
    { "lv_color_white", (void*)&lv_color_white },
    { "lv_cubic_bezier", (void*)&lv_cubic_bezier },
    { "lv_deinit", (void*)&lv_deinit },
    { "lv_display_add_event_cb", (void*)&lv_display_add_event_cb },
    { "lv_display_create", (void*)&lv_display_create },
    { "lv_display_delete", (void*)&lv_display_delete },
    { "lv_display_delete_event", (void*)&lv_display_delete_event },
    { "lv_display_enable_invalidation", (void*)&lv_display_enable_invalidation },
    { "lv_display_flush_is_last", (void*)&lv_display_flush_is_last },
    { "lv_display_flush_ready", (void*)&lv_display_flush_ready },
    { "lv_display_get_color_format", (void*)&lv_display_get_color_format },
    { "lv_display_get_default", (void*)&lv_display_get_default },
    { "lv_display_get_dpi", (void*)&lv_display_get_dpi },
    { "lv_display_get_event_count", (void*)&lv_display_get_event_count },
    { "lv_display_get_event_dsc", (void*)&lv_display_get_event_dsc },
    { "lv_display_get_horizontal_resolution", (void*)&lv_display_get_horizontal_resolution },
    { "lv_display_get_layer_bottom", (void*)&lv_display_get_layer_bottom },
    { "lv_display_get_layer_sys", (void*)&lv_display_get_layer_sys },
    { "lv_display_get_layer_top", (void*)&lv_display_get_layer_top },
    { "lv_display_get_matrix_rotation", (void*)&lv_display_get_matrix_rotation },
    { "lv_display_get_next", (void*)&lv_display_get_next },
    { "lv_display_get_original_horizontal_resolution", (void*)&lv_display_get_original_horizontal_resolution },
    { "lv_display_get_original_vertical_resolution", (void*)&lv_display_get_original_vertical_resolution },
    { "lv_display_get_rotation", (void*)&lv_display_get_rotation },
    { "lv_display_get_screen_active", (void*)&lv_display_get_screen_active },
    { "lv_display_get_screen_prev", (void*)&lv_display_get_screen_prev },
    { "lv_display_get_theme", (void*)&lv_display_get_theme },
    { "lv_display_get_user_data", (void*)&lv_display_get_user_data },
    { "lv_display_get_vertical_resolution", (void*)&lv_display_get_vertical_resolution },
    { "lv_display_is_double_buffered", (void*)&lv_display_is_double_buffered },
    { "lv_display_is_invalidation_enabled", (void*)&lv_display_is_invalidation_enabled },
    { "lv_display_refr_timer", (void*)&lv_display_refr_timer },
    { "lv_display_register_vsync_event", (void*)&lv_display_register_vsync_event },
    { "lv_display_remove_event_cb_with_user_data", (void*)&lv_display_remove_event_cb_with_user_data },
    { "lv_display_rotate_point", (void*)&lv_display_rotate_point },
    { "lv_display_send_event", (void*)&lv_display_send_event },
    { "lv_display_set_buffers", (void*)&lv_display_set_buffers },
    { "lv_display_set_default", (void*)&lv_display_set_default },
    { "lv_display_set_draw_buffers", (void*)&lv_display_set_draw_buffers },
    { "lv_display_set_flush_cb", (void*)&lv_display_set_flush_cb },
    { "lv_display_set_render_mode", (void*)&lv_display_set_render_mode },
    { "lv_display_set_user_data", (void*)&lv_display_set_user_data },
    { "lv_display_unregister_vsync_event", (void*)&lv_display_unregister_vsync_event },
    { "lv_dpx", (void*)&lv_dpx },
    { "lv_draw_add_task", (void*)&lv_draw_add_task },
    { "lv_draw_arc", (void*)&lv_draw_arc },
    { "lv_draw_arc_dsc_init", (void*)&lv_draw_arc_dsc_init },
    { "lv_draw_arc_get_area", (void*)&lv_draw_arc_get_area },
    { "lv_draw_blur", (void*)&lv_draw_blur },
    { "lv_draw_blur_dsc_init", (void*)&lv_draw_blur_dsc_init },
    { "lv_draw_border_dsc_init", (void*)&lv_draw_border_dsc_init },
    { "lv_draw_buf_adjust_stride", (void*)&lv_draw_buf_adjust_stride },
    { "lv_draw_buf_align", (void*)&lv_draw_buf_align },
    { "lv_draw_buf_align_ex", (void*)&lv_draw_buf_align_ex },
    { "lv_draw_buf_clear", (void*)&lv_draw_buf_clear },
    { "lv_draw_buf_convert_premultiply", (void*)&lv_draw_buf_convert_premultiply },
    { "lv_draw_buf_copy", (void*)&lv_draw_buf_copy },
    { "lv_draw_buf_create", (void*)&lv_draw_buf_create },
    { "lv_draw_buf_create_ex", (void*)&lv_draw_buf_create_ex },
    { "lv_draw_buf_destroy", (void*)&lv_draw_buf_destroy },
    { "lv_draw_buf_dup_ex", (void*)&lv_draw_buf_dup_ex },
    { "lv_draw_buf_flush_cache", (void*)&lv_draw_buf_flush_cache },
    { "lv_draw_buf_from_image", (void*)&lv_draw_buf_from_image },
    { "lv_draw_buf_get_handlers", (void*)&lv_draw_buf_get_handlers },
    { "lv_draw_buf_goto_xy", (void*)&lv_draw_buf_goto_xy },
    { "lv_draw_buf_handlers_init", (void*)&lv_draw_buf_handlers_init },
    { "lv_draw_buf_init", (void*)&lv_draw_buf_init },
    { "lv_draw_buf_init_handlers", (void*)&lv_draw_buf_init_handlers },
    { "lv_draw_buf_init_with_default_handlers", (void*)&lv_draw_buf_init_with_default_handlers },
    { "lv_draw_buf_invalidate_cache", (void*)&lv_draw_buf_invalidate_cache },
    { "lv_draw_buf_ppa_init_handlers", (void*)&lv_draw_buf_ppa_init_handlers },
    { "lv_draw_buf_premultiply", (void*)&lv_draw_buf_premultiply },
    { "lv_draw_buf_reshape", (void*)&lv_draw_buf_reshape },
    { "lv_draw_buf_width_to_stride", (void*)&lv_draw_buf_width_to_stride },
    { "lv_draw_buf_width_to_stride_ex", (void*)&lv_draw_buf_width_to_stride_ex },
    { "lv_draw_create_unit", (void*)&lv_draw_create_unit },
    { "lv_draw_deinit", (void*)&lv_draw_deinit },
    { "lv_draw_dispatch", (void*)&lv_draw_dispatch },
    { "lv_draw_dispatch_layer", (void*)&lv_draw_dispatch_layer },
    { "lv_draw_dispatch_request", (void*)&lv_draw_dispatch_request },
    { "lv_draw_dispatch_wait_for_request", (void*)&lv_draw_dispatch_wait_for_request },
    { "lv_draw_fill_dsc_init", (void*)&lv_draw_fill_dsc_init },
    { "lv_draw_finalize_task_creation", (void*)&lv_draw_finalize_task_creation },
    { "lv_draw_get_available_task", (void*)&lv_draw_get_available_task },
    { "lv_draw_get_next_available_task", (void*)&lv_draw_get_next_available_task },
    { "lv_draw_glyph_dsc_init", (void*)&lv_draw_glyph_dsc_init },
    { "lv_draw_image", (void*)&lv_draw_image },
    { "lv_draw_image_dsc_init", (void*)&lv_draw_image_dsc_init },
    { "lv_draw_image_normal_helper", (void*)&lv_draw_image_normal_helper },
    { "lv_draw_image_tiled_helper", (void*)&lv_draw_image_tiled_helper },
    { "lv_draw_init", (void*)&lv_draw_init },
    { "lv_draw_label", (void*)&lv_draw_label },
    { "lv_draw_label_dsc_init", (void*)&lv_draw_label_dsc_init },
    { "lv_draw_label_iterate_characters", (void*)&lv_draw_label_iterate_characters },
    { "lv_draw_layer", (void*)&lv_draw_layer },
    { "lv_draw_layer_alloc_buf", (void*)&lv_draw_layer_alloc_buf },
    { "lv_draw_layer_create", (void*)&lv_draw_layer_create },
    { "lv_draw_layer_create_drop_shadow", (void*)&lv_draw_layer_create_drop_shadow },
    { "lv_draw_layer_finish_drop_shadow", (void*)&lv_draw_layer_finish_drop_shadow },
    { "lv_draw_layer_go_to_xy", (void*)&lv_draw_layer_go_to_xy },
    { "lv_draw_layer_init", (void*)&lv_draw_layer_init },
    { "lv_draw_line", (void*)&lv_draw_line },
    { "lv_draw_line_dsc_init", (void*)&lv_draw_line_dsc_init },
    { "lv_draw_line_iterate", (void*)&lv_draw_line_iterate },
    { "lv_draw_mask_rect", (void*)&lv_draw_mask_rect },
    { "lv_draw_mask_rect_dsc_init", (void*)&lv_draw_mask_rect_dsc_init },
    { "lv_draw_ppa_fill", (void*)&lv_draw_ppa_fill },
    { "lv_draw_ppa_img", (void*)&lv_draw_ppa_img },
    { "lv_draw_ppa_init", (void*)&lv_draw_ppa_init },
    { "lv_draw_rect", (void*)&lv_draw_rect },
    { "lv_draw_rect_dsc_init", (void*)&lv_draw_rect_dsc_init },
    { "lv_draw_sw_arc", (void*)&lv_draw_sw_arc },
    { "lv_draw_sw_blend", (void*)&lv_draw_sw_blend },
    { "lv_draw_sw_blend_color_to_a8", (void*)&lv_draw_sw_blend_color_to_a8 },
    { "lv_draw_sw_blend_color_to_al88", (void*)&lv_draw_sw_blend_color_to_al88 },
    { "lv_draw_sw_blend_color_to_argb8888", (void*)&lv_draw_sw_blend_color_to_argb8888 },
    { "lv_draw_sw_blend_color_to_argb8888_premultiplied", (void*)&lv_draw_sw_blend_color_to_argb8888_premultiplied },
    { "lv_draw_sw_blend_color_to_i1", (void*)&lv_draw_sw_blend_color_to_i1 },
    { "lv_draw_sw_blend_color_to_l8", (void*)&lv_draw_sw_blend_color_to_l8 },
    { "lv_draw_sw_blend_color_to_rgb565", (void*)&lv_draw_sw_blend_color_to_rgb565 },
    { "lv_draw_sw_blend_color_to_rgb565_swapped", (void*)&lv_draw_sw_blend_color_to_rgb565_swapped },
    { "lv_draw_sw_blend_color_to_rgb888", (void*)&lv_draw_sw_blend_color_to_rgb888 },
    { "lv_draw_sw_blend_image_to_a8", (void*)&lv_draw_sw_blend_image_to_a8 },
    { "lv_draw_sw_blend_image_to_al88", (void*)&lv_draw_sw_blend_image_to_al88 },
    { "lv_draw_sw_blend_image_to_argb8888", (void*)&lv_draw_sw_blend_image_to_argb8888 },
    { "lv_draw_sw_blend_image_to_argb8888_premultiplied", (void*)&lv_draw_sw_blend_image_to_argb8888_premultiplied },
    { "lv_draw_sw_blend_image_to_i1", (void*)&lv_draw_sw_blend_image_to_i1 },
    { "lv_draw_sw_blend_image_to_l8", (void*)&lv_draw_sw_blend_image_to_l8 },
    { "lv_draw_sw_blend_image_to_rgb565", (void*)&lv_draw_sw_blend_image_to_rgb565 },
    { "lv_draw_sw_blend_image_to_rgb565_swapped", (void*)&lv_draw_sw_blend_image_to_rgb565_swapped },
    { "lv_draw_sw_blend_image_to_rgb888", (void*)&lv_draw_sw_blend_image_to_rgb888 },
    { "lv_draw_sw_blend_riscv_v_argb8888_premultiplied_to_rgb888", (void*)&lv_draw_sw_blend_riscv_v_argb8888_premultiplied_to_rgb888 },
    { "lv_draw_sw_blend_riscv_v_argb8888_to_rgb888", (void*)&lv_draw_sw_blend_riscv_v_argb8888_to_rgb888 },
    { "lv_draw_sw_blend_riscv_v_argb8888_to_rgb888_with_mask", (void*)&lv_draw_sw_blend_riscv_v_argb8888_to_rgb888_with_mask },
    { "lv_draw_sw_blend_riscv_v_argb8888_to_rgb888_with_opa", (void*)&lv_draw_sw_blend_riscv_v_argb8888_to_rgb888_with_opa },
    { "lv_draw_sw_blend_riscv_v_argb8888_to_rgb888_with_opa_mask", (void*)&lv_draw_sw_blend_riscv_v_argb8888_to_rgb888_with_opa_mask },
    { "lv_draw_sw_blend_riscv_v_color_to_rgb888", (void*)&lv_draw_sw_blend_riscv_v_color_to_rgb888 },
    { "lv_draw_sw_blend_riscv_v_color_to_rgb888_with_mask", (void*)&lv_draw_sw_blend_riscv_v_color_to_rgb888_with_mask },
    { "lv_draw_sw_blend_riscv_v_color_to_rgb888_with_opa", (void*)&lv_draw_sw_blend_riscv_v_color_to_rgb888_with_opa },
    { "lv_draw_sw_blend_riscv_v_color_to_rgb888_with_opa_mask", (void*)&lv_draw_sw_blend_riscv_v_color_to_rgb888_with_opa_mask },
    { "lv_draw_sw_blend_riscv_v_rgb565_to_rgb888", (void*)&lv_draw_sw_blend_riscv_v_rgb565_to_rgb888 },
    { "lv_draw_sw_blend_riscv_v_rgb565_to_rgb888_with_mask", (void*)&lv_draw_sw_blend_riscv_v_rgb565_to_rgb888_with_mask },
    { "lv_draw_sw_blend_riscv_v_rgb565_to_rgb888_with_opa", (void*)&lv_draw_sw_blend_riscv_v_rgb565_to_rgb888_with_opa },
    { "lv_draw_sw_blend_riscv_v_rgb565_to_rgb888_with_opa_mask", (void*)&lv_draw_sw_blend_riscv_v_rgb565_to_rgb888_with_opa_mask },
    { "lv_draw_sw_blend_riscv_v_rgb888_to_rgb888", (void*)&lv_draw_sw_blend_riscv_v_rgb888_to_rgb888 },
    { "lv_draw_sw_blend_riscv_v_rgb888_to_rgb888_with_mask", (void*)&lv_draw_sw_blend_riscv_v_rgb888_to_rgb888_with_mask },
    { "lv_draw_sw_blend_riscv_v_rgb888_to_rgb888_with_opa", (void*)&lv_draw_sw_blend_riscv_v_rgb888_to_rgb888_with_opa },
    { "lv_draw_sw_blend_riscv_v_rgb888_to_rgb888_with_opa_mask", (void*)&lv_draw_sw_blend_riscv_v_rgb888_to_rgb888_with_opa_mask },
    { "lv_draw_sw_blur", (void*)&lv_draw_sw_blur },
    { "lv_draw_sw_border", (void*)&lv_draw_sw_border },
    { "lv_draw_sw_box_shadow", (void*)&lv_draw_sw_box_shadow },
    { "lv_draw_sw_deinit", (void*)&lv_draw_sw_deinit },
    { "lv_draw_sw_fill", (void*)&lv_draw_sw_fill },
    { "lv_draw_sw_get_blend_handler", (void*)&lv_draw_sw_get_blend_handler },
    { "lv_draw_sw_grad_cleanup", (void*)&lv_draw_sw_grad_cleanup },
    { "lv_draw_sw_grad_color_calculate", (void*)&lv_draw_sw_grad_color_calculate },
    { "lv_draw_sw_grad_get", (void*)&lv_draw_sw_grad_get },
    { "lv_draw_sw_image", (void*)&lv_draw_sw_image },
    { "lv_draw_sw_init", (void*)&lv_draw_sw_init },
    { "lv_draw_sw_label", (void*)&lv_draw_sw_label },
    { "lv_draw_sw_layer", (void*)&lv_draw_sw_layer },
    { "lv_draw_sw_letter", (void*)&lv_draw_sw_letter },
    { "lv_draw_sw_line", (void*)&lv_draw_sw_line },
    { "lv_draw_sw_mask_angle_init", (void*)&lv_draw_sw_mask_angle_init },
    { "lv_draw_sw_mask_apply", (void*)&lv_draw_sw_mask_apply },
    { "lv_draw_sw_mask_cleanup", (void*)&lv_draw_sw_mask_cleanup },
    { "lv_draw_sw_mask_deinit", (void*)&lv_draw_sw_mask_deinit },
    { "lv_draw_sw_mask_free_param", (void*)&lv_draw_sw_mask_free_param },
    { "lv_draw_sw_mask_init", (void*)&lv_draw_sw_mask_init },
    { "lv_draw_sw_mask_line_angle_init", (void*)&lv_draw_sw_mask_line_angle_init },
    { "lv_draw_sw_mask_line_points_init", (void*)&lv_draw_sw_mask_line_points_init },
    { "lv_draw_sw_mask_radius_init", (void*)&lv_draw_sw_mask_radius_init },
    { "lv_draw_sw_mask_rect", (void*)&lv_draw_sw_mask_rect },
    { "lv_draw_sw_register_blend_handler", (void*)&lv_draw_sw_register_blend_handler },
    { "lv_draw_sw_rgb565_swap", (void*)&lv_draw_sw_rgb565_swap },
    { "lv_draw_sw_transform", (void*)&lv_draw_sw_transform },
    { "lv_draw_sw_triangle", (void*)&lv_draw_sw_triangle },
    { "lv_draw_task_get_border_dsc", (void*)&lv_draw_task_get_border_dsc },
    { "lv_draw_task_get_fill_dsc", (void*)&lv_draw_task_get_fill_dsc },
    { "lv_draw_task_get_label_dsc", (void*)&lv_draw_task_get_label_dsc },
    { "lv_draw_unit_draw_letter", (void*)&lv_draw_unit_draw_letter },
    { "lv_draw_unit_send_event", (void*)&lv_draw_unit_send_event },
    { "lv_draw_wait_for_finish", (void*)&lv_draw_wait_for_finish },
    { "lv_dropdown_close", (void*)&lv_dropdown_close },
    { "lv_dropdown_create", (void*)&lv_dropdown_create },
    { "lv_dropdown_get_options", (void*)&lv_dropdown_get_options },
    { "lv_dropdown_get_selected", (void*)&lv_dropdown_get_selected },
    { "lv_dropdown_get_selected_str", (void*)&lv_dropdown_get_selected_str },
    { "lv_dropdown_is_open", (void*)&lv_dropdown_is_open },
    { "lv_dropdown_open", (void*)&lv_dropdown_open },
    { "lv_dropdown_set_options", (void*)&lv_dropdown_set_options },
    { "lv_dropdown_set_options_static", (void*)&lv_dropdown_set_options_static },
    { "lv_dropdown_set_selected", (void*)&lv_dropdown_set_selected },
    { "lv_event_add", (void*)&lv_event_add },
    { "lv_event_dsc_get_cb", (void*)&lv_event_dsc_get_cb },
    { "lv_event_dsc_get_user_data", (void*)&lv_event_dsc_get_user_data },
    { "lv_event_get_code", (void*)&lv_event_get_code },
    { "lv_event_get_count", (void*)&lv_event_get_count },
    { "lv_event_get_current_target", (void*)&lv_event_get_current_target },
    { "lv_event_get_dsc", (void*)&lv_event_get_dsc },
    { "lv_event_get_indev", (void*)&lv_event_get_indev },
    { "lv_event_get_invalidated_area", (void*)&lv_event_get_invalidated_area },
    { "lv_event_get_key", (void*)&lv_event_get_key },
    { "lv_event_get_layer", (void*)&lv_event_get_layer },
    { "lv_event_get_param", (void*)&lv_event_get_param },
    { "lv_event_get_prev_state", (void*)&lv_event_get_prev_state },
    { "lv_event_get_rotary_diff", (void*)&lv_event_get_rotary_diff },
    { "lv_event_get_target", (void*)&lv_event_get_target },
    { "lv_event_get_target_obj", (void*)&lv_event_get_target_obj },
    { "lv_event_get_user_data", (void*)&lv_event_get_user_data },
    { "lv_event_mark_deleted", (void*)&lv_event_mark_deleted },
    { "lv_event_pop", (void*)&lv_event_pop },
    { "lv_event_push", (void*)&lv_event_push },
    { "lv_event_push_and_send", (void*)&lv_event_push_and_send },
    { "lv_event_remove", (void*)&lv_event_remove },
    { "lv_event_remove_all", (void*)&lv_event_remove_all },
    { "lv_event_send", (void*)&lv_event_send },
    { "lv_event_set_ext_draw_size", (void*)&lv_event_set_ext_draw_size },
    { "lv_event_stop_processing", (void*)&lv_event_stop_processing },
    { "lv_flex_init", (void*)&lv_flex_init },
    { "lv_font_get_bitmap_fmt_txt", (void*)&lv_font_get_bitmap_fmt_txt },
    { "lv_font_get_default", (void*)&lv_font_get_default },
    { "lv_font_get_glyph_bitmap", (void*)&lv_font_get_glyph_bitmap },
    { "lv_font_get_glyph_dsc", (void*)&lv_font_get_glyph_dsc },
    { "lv_font_get_glyph_dsc_fmt_txt", (void*)&lv_font_get_glyph_dsc_fmt_txt },
    { "lv_font_get_glyph_static_bitmap", (void*)&lv_font_get_glyph_static_bitmap },
    { "lv_font_get_glyph_width", (void*)&lv_font_get_glyph_width },
    { "lv_font_get_line_height", (void*)&lv_font_get_line_height },
    { "lv_font_glyph_release_draw_data", (void*)&lv_font_glyph_release_draw_data },
    { "lv_font_has_static_bitmap", (void*)&lv_font_has_static_bitmap },
    { "lv_free", (void*)&lv_free },
    { "lv_free_core", (void*)&lv_free_core },
    { "lv_freetype_create_draw_data_image", (void*)&lv_freetype_create_draw_data_image },
    { "lv_freetype_create_draw_data_outline", (void*)&lv_freetype_create_draw_data_outline },
    { "lv_freetype_create_glyph_cache", (void*)&lv_freetype_create_glyph_cache },
    { "lv_freetype_font_create_with_info", (void*)&lv_freetype_font_create_with_info },
    { "lv_freetype_font_delete", (void*)&lv_freetype_font_delete },
    { "lv_freetype_get_context", (void*)&lv_freetype_get_context },
    { "lv_freetype_init", (void*)&lv_freetype_init },
    { "lv_freetype_init_font_info", (void*)&lv_freetype_init_font_info },
    { "lv_freetype_italic_transform", (void*)&lv_freetype_italic_transform },
    { "lv_freetype_italic_transform_on_pos", (void*)&lv_freetype_italic_transform_on_pos },
    { "lv_freetype_set_cbs_glyph", (void*)&lv_freetype_set_cbs_glyph },
    { "lv_freetype_set_cbs_image_font", (void*)&lv_freetype_set_cbs_image_font },
    { "lv_freetype_set_cbs_outline_font", (void*)&lv_freetype_set_cbs_outline_font },
    { "lv_freetype_uninit", (void*)&lv_freetype_uninit },
    { "lv_fs_close", (void*)&lv_fs_close },
    { "lv_fs_deinit", (void*)&lv_fs_deinit },
    { "lv_fs_drv_init", (void*)&lv_fs_drv_init },
    { "lv_fs_drv_register", (void*)&lv_fs_drv_register },
    { "lv_fs_get_buffer_from_path", (void*)&lv_fs_get_buffer_from_path },
    { "lv_fs_get_drv", (void*)&lv_fs_get_drv },
    { "lv_fs_get_ext", (void*)&lv_fs_get_ext },
    { "lv_fs_init", (void*)&lv_fs_init },
    { "lv_fs_open", (void*)&lv_fs_open },
    { "lv_fs_read", (void*)&lv_fs_read },
    { "lv_fs_seek", (void*)&lv_fs_seek },
    { "lv_fs_tell", (void*)&lv_fs_tell },
    { "lv_grid_init", (void*)&lv_grid_init },
    { "lv_group_add_obj", (void*)&lv_group_add_obj },
    { "lv_group_create", (void*)&lv_group_create },
    { "lv_group_deinit", (void*)&lv_group_deinit },
    { "lv_group_delete", (void*)&lv_group_delete },
    { "lv_group_focus_next", (void*)&lv_group_focus_next },
    { "lv_group_focus_obj", (void*)&lv_group_focus_obj },
    { "lv_group_focus_prev", (void*)&lv_group_focus_prev },
    { "lv_group_get_default", (void*)&lv_group_get_default },
    { "lv_group_get_editing", (void*)&lv_group_get_editing },
    { "lv_group_get_focused", (void*)&lv_group_get_focused },
    { "lv_group_get_obj_count", (void*)&lv_group_get_obj_count },
    { "lv_group_init", (void*)&lv_group_init },
    { "lv_group_remove_obj", (void*)&lv_group_remove_obj },
    { "lv_group_send_data", (void*)&lv_group_send_data },
    { "lv_group_set_default", (void*)&lv_group_set_default },
    { "lv_group_set_editing", (void*)&lv_group_set_editing },
    { "lv_image_buf_get_transformed_area", (void*)&lv_image_buf_get_transformed_area },
    { "lv_image_cache_drop", (void*)&lv_image_cache_drop },
    { "lv_image_cache_init", (void*)&lv_image_cache_init },
    { "lv_image_cache_is_enabled", (void*)&lv_image_cache_is_enabled },
    { "lv_image_create", (void*)&lv_image_create },
    { "lv_image_decoder_add_to_cache", (void*)&lv_image_decoder_add_to_cache },
    { "lv_image_decoder_close", (void*)&lv_image_decoder_close },
    { "lv_image_decoder_create", (void*)&lv_image_decoder_create },
    { "lv_image_decoder_deinit", (void*)&lv_image_decoder_deinit },
    { "lv_image_decoder_delete", (void*)&lv_image_decoder_delete },
    { "lv_image_decoder_get_area", (void*)&lv_image_decoder_get_area },
    { "lv_image_decoder_get_info", (void*)&lv_image_decoder_get_info },
    { "lv_image_decoder_init", (void*)&lv_image_decoder_init },
    { "lv_image_decoder_open", (void*)&lv_image_decoder_open },
    { "lv_image_decoder_post_process", (void*)&lv_image_decoder_post_process },
    { "lv_image_decoder_set_close_cb", (void*)&lv_image_decoder_set_close_cb },
    { "lv_image_decoder_set_get_area_cb", (void*)&lv_image_decoder_set_get_area_cb },
    { "lv_image_decoder_set_info_cb", (void*)&lv_image_decoder_set_info_cb },
    { "lv_image_decoder_set_open_cb", (void*)&lv_image_decoder_set_open_cb },
    { "lv_image_get_pivot", (void*)&lv_image_get_pivot },
    { "lv_image_get_scale", (void*)&lv_image_get_scale },
    { "lv_image_get_src", (void*)&lv_image_get_src },
    { "lv_image_header_cache_drop", (void*)&lv_image_header_cache_drop },
    { "lv_image_header_cache_init", (void*)&lv_image_header_cache_init },
    { "lv_image_header_cache_is_enabled", (void*)&lv_image_header_cache_is_enabled },
    { "lv_image_set_inner_align", (void*)&lv_image_set_inner_align },
    { "lv_image_set_pivot", (void*)&lv_image_set_pivot },
    { "lv_image_set_rotation", (void*)&lv_image_set_rotation },
    { "lv_image_set_scale", (void*)&lv_image_set_scale },
    { "lv_image_set_src", (void*)&lv_image_set_src },
    { "lv_image_src_get_type", (void*)&lv_image_src_get_type },
    { "lv_ime_pinyin_create", (void*)&lv_ime_pinyin_create },
    { "lv_ime_pinyin_get_cand_panel", (void*)&lv_ime_pinyin_get_cand_panel },
    { "lv_ime_pinyin_set_dict", (void*)&lv_ime_pinyin_set_dict },
    { "lv_ime_pinyin_set_keyboard", (void*)&lv_ime_pinyin_set_keyboard },
    { "lv_ime_pinyin_set_mode", (void*)&lv_ime_pinyin_set_mode },
    { "lv_indev_active", (void*)&lv_indev_active },
    { "lv_indev_create", (void*)&lv_indev_create },
    { "lv_indev_delete", (void*)&lv_indev_delete },
    { "lv_indev_find_scroll_obj", (void*)&lv_indev_find_scroll_obj },
    { "lv_indev_get_active_obj", (void*)&lv_indev_get_active_obj },
    { "lv_indev_get_display", (void*)&lv_indev_get_display },
    { "lv_indev_get_driver_data", (void*)&lv_indev_get_driver_data },
    { "lv_indev_get_group", (void*)&lv_indev_get_group },
    { "lv_indev_get_next", (void*)&lv_indev_get_next },
    { "lv_indev_get_point", (void*)&lv_indev_get_point },
    { "lv_indev_get_scroll_dir", (void*)&lv_indev_get_scroll_dir },
    { "lv_indev_get_scroll_obj", (void*)&lv_indev_get_scroll_obj },
    { "lv_indev_get_state", (void*)&lv_indev_get_state },
    { "lv_indev_get_type", (void*)&lv_indev_get_type },
    { "lv_indev_get_user_data", (void*)&lv_indev_get_user_data },
    { "lv_indev_get_vect", (void*)&lv_indev_get_vect },
    { "lv_indev_read", (void*)&lv_indev_read },
    { "lv_indev_read_timer_cb", (void*)&lv_indev_read_timer_cb },
    { "lv_indev_reset", (void*)&lv_indev_reset },
    { "lv_indev_scroll_handler", (void*)&lv_indev_scroll_handler },
    { "lv_indev_scroll_throw_handler", (void*)&lv_indev_scroll_throw_handler },
    { "lv_indev_scroll_throw_predict", (void*)&lv_indev_scroll_throw_predict },
    { "lv_indev_search_obj", (void*)&lv_indev_search_obj },
    { "lv_indev_send_event", (void*)&lv_indev_send_event },
    { "lv_indev_set_display", (void*)&lv_indev_set_display },
    { "lv_indev_set_driver_data", (void*)&lv_indev_set_driver_data },
    { "lv_indev_set_group", (void*)&lv_indev_set_group },
    { "lv_indev_set_read_cb", (void*)&lv_indev_set_read_cb },
    { "lv_indev_set_type", (void*)&lv_indev_set_type },
    { "lv_indev_set_user_data", (void*)&lv_indev_set_user_data },
    { "lv_indev_wait_release", (void*)&lv_indev_wait_release },
    { "lv_init", (void*)&lv_init },
    { "lv_inv_area", (void*)&lv_inv_area },
    { "lv_iter_create", (void*)&lv_iter_create },
    { "lv_keyboard_create", (void*)&lv_keyboard_create },
    { "lv_keyboard_def_event_cb", (void*)&lv_keyboard_def_event_cb },
    { "lv_keyboard_get_textarea", (void*)&lv_keyboard_get_textarea },
    { "lv_keyboard_set_mode", (void*)&lv_keyboard_set_mode },
    { "lv_keyboard_set_textarea", (void*)&lv_keyboard_set_textarea },
    { "lv_label_create", (void*)&lv_label_create },
    { "lv_label_get_letter_on", (void*)&lv_label_get_letter_on },
    { "lv_label_get_letter_pos", (void*)&lv_label_get_letter_pos },
    { "lv_label_get_recolor", (void*)&lv_label_get_recolor },
    { "lv_label_get_text", (void*)&lv_label_get_text },
    { "lv_label_get_text_selection_end", (void*)&lv_label_get_text_selection_end },
    { "lv_label_get_text_selection_start", (void*)&lv_label_get_text_selection_start },
    { "lv_label_ins_text", (void*)&lv_label_ins_text },
    { "lv_label_is_char_under_pos", (void*)&lv_label_is_char_under_pos },
    { "lv_label_set_long_mode", (void*)&lv_label_set_long_mode },
    { "lv_label_set_text", (void*)&lv_label_set_text },
    { "lv_label_set_text_fmt", (void*)&lv_label_set_text_fmt },
    { "lv_label_set_text_selection_end", (void*)&lv_label_set_text_selection_end },
    { "lv_label_set_text_selection_start", (void*)&lv_label_set_text_selection_start },
    { "lv_label_set_text_static", (void*)&lv_label_set_text_static },
    { "lv_label_set_text_vfmt", (void*)&lv_label_set_text_vfmt },
    { "lv_layer_init", (void*)&lv_layer_init },
    { "lv_layer_reset", (void*)&lv_layer_reset },
    { "lv_layer_sys", (void*)&lv_layer_sys },
    { "lv_layer_top", (void*)&lv_layer_top },
    { "lv_layout_apply", (void*)&lv_layout_apply },
    { "lv_layout_deinit", (void*)&lv_layout_deinit },
    { "lv_layout_get_min_size", (void*)&lv_layout_get_min_size },
    { "lv_layout_init", (void*)&lv_layout_init },
    { "lv_line_get_point_count", (void*)&lv_line_get_point_count },
    { "lv_line_get_points_mutable", (void*)&lv_line_get_points_mutable },
    { "lv_line_is_point_array_mutable", (void*)&lv_line_is_point_array_mutable },
    { "lv_line_set_points_mutable", (void*)&lv_line_set_points_mutable },
    { "lv_ll_clear", (void*)&lv_ll_clear },
    { "lv_ll_clear_custom", (void*)&lv_ll_clear_custom },
    { "lv_ll_get_head", (void*)&lv_ll_get_head },
    { "lv_ll_get_len", (void*)&lv_ll_get_len },
    { "lv_ll_get_next", (void*)&lv_ll_get_next },
    { "lv_ll_get_prev", (void*)&lv_ll_get_prev },
    { "lv_ll_get_tail", (void*)&lv_ll_get_tail },
    { "lv_ll_init", (void*)&lv_ll_init },
    { "lv_ll_ins_head", (void*)&lv_ll_ins_head },
    { "lv_ll_ins_prev", (void*)&lv_ll_ins_prev },
    { "lv_ll_ins_tail", (void*)&lv_ll_ins_tail },
    { "lv_ll_is_empty", (void*)&lv_ll_is_empty },
    { "lv_ll_move_before", (void*)&lv_ll_move_before },
    { "lv_ll_remove", (void*)&lv_ll_remove },
    { "lv_lock", (void*)&lv_lock },
    { "lv_malloc", (void*)&lv_malloc },
    { "lv_malloc_core", (void*)&lv_malloc_core },
    { "lv_malloc_zeroed", (void*)&lv_malloc_zeroed },
    { "lv_map", (void*)&lv_map },
    { "lv_mem_deinit", (void*)&lv_mem_deinit },
    { "lv_mem_init", (void*)&lv_mem_init },
    { "lv_memcpy", (void*)&lv_memcpy },
    { "lv_memmove", (void*)&lv_memmove },
    { "lv_memset", (void*)&lv_memset },
    { "lv_menu_back_button_is_root", (void*)&lv_menu_back_button_is_root },
    { "lv_menu_clear_history", (void*)&lv_menu_clear_history },
    { "lv_menu_get_cur_main_page", (void*)&lv_menu_get_cur_main_page },
    { "lv_menu_get_cur_sidebar_page", (void*)&lv_menu_get_cur_sidebar_page },
    { "lv_menu_set_page", (void*)&lv_menu_set_page },
    { "lv_mutex_delete", (void*)&lv_mutex_delete },
    { "lv_mutex_init", (void*)&lv_mutex_init },
    { "lv_mutex_lock", (void*)&lv_mutex_lock },
    { "lv_mutex_unlock", (void*)&lv_mutex_unlock },
    { "lv_obj_add_event_cb", (void*)&lv_obj_add_event_cb },
    { "lv_obj_add_flag", (void*)&lv_obj_add_flag },
    { "lv_obj_add_state", (void*)&lv_obj_add_state },
    { "lv_obj_add_style", (void*)&lv_obj_add_style },
    { "lv_obj_align", (void*)&lv_obj_align },
    { "lv_obj_align_to", (void*)&lv_obj_align_to },
    { "lv_obj_allocate_spec_attr", (void*)&lv_obj_allocate_spec_attr },
    { "lv_obj_area_is_visible", (void*)&lv_obj_area_is_visible },
    { "lv_obj_calc_dynamic_height", (void*)&lv_obj_calc_dynamic_height },
    { "lv_obj_calc_dynamic_width", (void*)&lv_obj_calc_dynamic_width },
    { "lv_obj_calculate_ext_draw_size", (void*)&lv_obj_calculate_ext_draw_size },
    { "lv_obj_calculate_style_text_align", (void*)&lv_obj_calculate_style_text_align },
    { "lv_obj_center", (void*)&lv_obj_center },
    { "lv_obj_check_type", (void*)&lv_obj_check_type },
    { "lv_obj_class_create_obj", (void*)&lv_obj_class_create_obj },
    { "lv_obj_class_init_obj", (void*)&lv_obj_class_init_obj },
    { "lv_obj_create", (void*)&lv_obj_create },
    { "lv_obj_delete", (void*)&lv_obj_delete },
    { "lv_obj_delete_async", (void*)&lv_obj_delete_async },
    { "lv_obj_destruct", (void*)&lv_obj_destruct },
    { "lv_obj_enable_style_refresh", (void*)&lv_obj_enable_style_refresh },
    { "lv_obj_event_base", (void*)&lv_obj_event_base },
    { "lv_obj_get_child", (void*)&lv_obj_get_child },
    { "lv_obj_get_child_by_type", (void*)&lv_obj_get_child_by_type },
    { "lv_obj_get_child_count", (void*)&lv_obj_get_child_count },
    { "lv_obj_get_child_count_by_type", (void*)&lv_obj_get_child_count_by_type },
    { "lv_obj_get_click_area", (void*)&lv_obj_get_click_area },
    { "lv_obj_get_content_coords", (void*)&lv_obj_get_content_coords },
    { "lv_obj_get_content_height", (void*)&lv_obj_get_content_height },
    { "lv_obj_get_content_width", (void*)&lv_obj_get_content_width },
    { "lv_obj_get_coords", (void*)&lv_obj_get_coords },
    { "lv_obj_get_display", (void*)&lv_obj_get_display },
    { "lv_obj_get_event_count", (void*)&lv_obj_get_event_count },
    { "lv_obj_get_event_dsc", (void*)&lv_obj_get_event_dsc },
    { "lv_obj_get_ext_draw_size", (void*)&lv_obj_get_ext_draw_size },
    { "lv_obj_get_group", (void*)&lv_obj_get_group },
    { "lv_obj_get_height", (void*)&lv_obj_get_height },
    { "lv_obj_get_index", (void*)&lv_obj_get_index },
    { "lv_obj_get_layer_type", (void*)&lv_obj_get_layer_type },
    { "lv_obj_get_local_style_prop", (void*)&lv_obj_get_local_style_prop },
    { "lv_obj_get_parent", (void*)&lv_obj_get_parent },
    { "lv_obj_get_screen", (void*)&lv_obj_get_screen },
    { "lv_obj_get_scroll_bottom", (void*)&lv_obj_get_scroll_bottom },
    { "lv_obj_get_scroll_dir", (void*)&lv_obj_get_scroll_dir },
    { "lv_obj_get_scroll_end", (void*)&lv_obj_get_scroll_end },
    { "lv_obj_get_scroll_left", (void*)&lv_obj_get_scroll_left },
    { "lv_obj_get_scroll_right", (void*)&lv_obj_get_scroll_right },
    { "lv_obj_get_scroll_snap_x", (void*)&lv_obj_get_scroll_snap_x },
    { "lv_obj_get_scroll_snap_y", (void*)&lv_obj_get_scroll_snap_y },
    { "lv_obj_get_scroll_top", (void*)&lv_obj_get_scroll_top },
    { "lv_obj_get_scroll_x", (void*)&lv_obj_get_scroll_x },
    { "lv_obj_get_scroll_y", (void*)&lv_obj_get_scroll_y },
    { "lv_obj_get_scrollbar_area", (void*)&lv_obj_get_scrollbar_area },
    { "lv_obj_get_scrollbar_mode", (void*)&lv_obj_get_scrollbar_mode },
    { "lv_obj_get_self_height", (void*)&lv_obj_get_self_height },
    { "lv_obj_get_self_width", (void*)&lv_obj_get_self_width },
    { "lv_obj_get_state", (void*)&lv_obj_get_state },
    { "lv_obj_get_style_opa_recursive", (void*)&lv_obj_get_style_opa_recursive },
    { "lv_obj_get_style_prop", (void*)&lv_obj_get_style_prop },
    { "lv_obj_get_style_recolor_recursive", (void*)&lv_obj_get_style_recolor_recursive },
    { "lv_obj_get_transformed_area", (void*)&lv_obj_get_transformed_area },
    { "lv_obj_get_user_data", (void*)&lv_obj_get_user_data },
    { "lv_obj_get_width", (void*)&lv_obj_get_width },
    { "lv_obj_get_x", (void*)&lv_obj_get_x },
    { "lv_obj_get_y", (void*)&lv_obj_get_y },
    { "lv_obj_get_y2", (void*)&lv_obj_get_y2 },
    { "lv_obj_get_y_aligned", (void*)&lv_obj_get_y_aligned },
    { "lv_obj_has_class", (void*)&lv_obj_has_class },
    { "lv_obj_has_flag", (void*)&lv_obj_has_flag },
    { "lv_obj_has_flag_any", (void*)&lv_obj_has_flag_any },
    { "lv_obj_has_state", (void*)&lv_obj_has_state },
    { "lv_obj_hit_test", (void*)&lv_obj_hit_test },
    { "lv_obj_init_draw_arc_dsc", (void*)&lv_obj_init_draw_arc_dsc },
    { "lv_obj_init_draw_blur_dsc", (void*)&lv_obj_init_draw_blur_dsc },
    { "lv_obj_init_draw_image_dsc", (void*)&lv_obj_init_draw_image_dsc },
    { "lv_obj_init_draw_label_dsc", (void*)&lv_obj_init_draw_label_dsc },
    { "lv_obj_init_draw_line_dsc", (void*)&lv_obj_init_draw_line_dsc },
    { "lv_obj_init_draw_rect_dsc", (void*)&lv_obj_init_draw_rect_dsc },
    { "lv_obj_invalidate", (void*)&lv_obj_invalidate },
    { "lv_obj_invalidate_area", (void*)&lv_obj_invalidate_area },
    { "lv_obj_is_editable", (void*)&lv_obj_is_editable },
    { "lv_obj_is_group_def", (void*)&lv_obj_is_group_def },
    { "lv_obj_is_layout_positioned", (void*)&lv_obj_is_layout_positioned },
    { "lv_obj_is_radio_button", (void*)&lv_obj_is_radio_button },
    { "lv_obj_is_valid", (void*)&lv_obj_is_valid },
    { "lv_obj_is_visible", (void*)&lv_obj_is_visible },
    { "lv_obj_mark_layout_as_dirty", (void*)&lv_obj_mark_layout_as_dirty },
    { "lv_obj_move_children_by", (void*)&lv_obj_move_children_by },
    { "lv_obj_move_to", (void*)&lv_obj_move_to },
    { "lv_obj_move_to_index", (void*)&lv_obj_move_to_index },
    { "lv_obj_readjust_scroll", (void*)&lv_obj_readjust_scroll },
    { "lv_obj_redraw", (void*)&lv_obj_redraw },
    { "lv_obj_refr", (void*)&lv_obj_refr },
    { "lv_obj_refr_pos", (void*)&lv_obj_refr_pos },
    { "lv_obj_refr_size", (void*)&lv_obj_refr_size },
    { "lv_obj_refresh_ext_draw_size", (void*)&lv_obj_refresh_ext_draw_size },
    { "lv_obj_refresh_self_size", (void*)&lv_obj_refresh_self_size },
    { "lv_obj_refresh_style", (void*)&lv_obj_refresh_style },
    { "lv_obj_remove_event", (void*)&lv_obj_remove_event },
    { "lv_obj_remove_event_cb", (void*)&lv_obj_remove_event_cb },
    { "lv_obj_remove_flag", (void*)&lv_obj_remove_flag },
    { "lv_obj_remove_local_style_prop", (void*)&lv_obj_remove_local_style_prop },
    { "lv_obj_remove_state", (void*)&lv_obj_remove_state },
    { "lv_obj_remove_style", (void*)&lv_obj_remove_style },
    { "lv_obj_remove_style_all", (void*)&lv_obj_remove_style_all },
    { "lv_obj_report_style_change", (void*)&lv_obj_report_style_change },
    { "lv_obj_scroll_by", (void*)&lv_obj_scroll_by },
    { "lv_obj_scroll_by_bounded", (void*)&lv_obj_scroll_by_bounded },
    { "lv_obj_scroll_by_raw", (void*)&lv_obj_scroll_by_raw },
    { "lv_obj_scroll_to", (void*)&lv_obj_scroll_to },
    { "lv_obj_scroll_to_view", (void*)&lv_obj_scroll_to_view },
    { "lv_obj_scroll_to_view_recursive", (void*)&lv_obj_scroll_to_view_recursive },
    { "lv_obj_scroll_to_x", (void*)&lv_obj_scroll_to_x },
    { "lv_obj_scroll_to_y", (void*)&lv_obj_scroll_to_y },
    { "lv_obj_scrollbar_invalidate", (void*)&lv_obj_scrollbar_invalidate },
    { "lv_obj_send_event", (void*)&lv_obj_send_event },
    { "lv_obj_set_ext_click_area", (void*)&lv_obj_set_ext_click_area },
    { "lv_obj_set_flex_align", (void*)&lv_obj_set_flex_align },
    { "lv_obj_set_flex_flow", (void*)&lv_obj_set_flex_flow },
    { "lv_obj_set_flex_grow", (void*)&lv_obj_set_flex_grow },
    { "lv_obj_set_height", (void*)&lv_obj_set_height },
    { "lv_obj_set_layout", (void*)&lv_obj_set_layout },
    { "lv_obj_set_local_style_prop", (void*)&lv_obj_set_local_style_prop },
    { "lv_obj_set_parent", (void*)&lv_obj_set_parent },
    { "lv_obj_set_pos", (void*)&lv_obj_set_pos },
    { "lv_obj_set_scroll_dir", (void*)&lv_obj_set_scroll_dir },
    { "lv_obj_set_scroll_snap_x", (void*)&lv_obj_set_scroll_snap_x },
    { "lv_obj_set_scroll_snap_y", (void*)&lv_obj_set_scroll_snap_y },
    { "lv_obj_set_scrollbar_mode", (void*)&lv_obj_set_scrollbar_mode },
    { "lv_obj_set_size", (void*)&lv_obj_set_size },
    { "lv_obj_set_state", (void*)&lv_obj_set_state },
    { "lv_obj_set_style_align", (void*)&lv_obj_set_style_align },
    { "lv_obj_set_style_base_dir", (void*)&lv_obj_set_style_base_dir },
    { "lv_obj_set_style_bg_color", (void*)&lv_obj_set_style_bg_color },
    { "lv_obj_set_style_bg_image_src", (void*)&lv_obj_set_style_bg_image_src },
    { "lv_obj_set_style_bg_opa", (void*)&lv_obj_set_style_bg_opa },
    { "lv_obj_set_style_border_color", (void*)&lv_obj_set_style_border_color },
    { "lv_obj_set_style_border_opa", (void*)&lv_obj_set_style_border_opa },
    { "lv_obj_set_style_border_side", (void*)&lv_obj_set_style_border_side },
    { "lv_obj_set_style_border_width", (void*)&lv_obj_set_style_border_width },
    { "lv_obj_set_style_clip_corner", (void*)&lv_obj_set_style_clip_corner },
    { "lv_obj_set_style_flex_cross_place", (void*)&lv_obj_set_style_flex_cross_place },
    { "lv_obj_set_style_flex_flow", (void*)&lv_obj_set_style_flex_flow },
    { "lv_obj_set_style_flex_grow", (void*)&lv_obj_set_style_flex_grow },
    { "lv_obj_set_style_flex_main_place", (void*)&lv_obj_set_style_flex_main_place },
    { "lv_obj_set_style_flex_track_place", (void*)&lv_obj_set_style_flex_track_place },
    { "lv_obj_set_style_height", (void*)&lv_obj_set_style_height },
    { "lv_obj_set_style_layout", (void*)&lv_obj_set_style_layout },
    { "lv_obj_set_style_margin_bottom", (void*)&lv_obj_set_style_margin_bottom },
    { "lv_obj_set_style_margin_top", (void*)&lv_obj_set_style_margin_top },
    { "lv_obj_set_style_min_width", (void*)&lv_obj_set_style_min_width },
    { "lv_obj_set_style_opa", (void*)&lv_obj_set_style_opa },
    { "lv_obj_set_style_outline_color", (void*)&lv_obj_set_style_outline_color },
    { "lv_obj_set_style_outline_opa", (void*)&lv_obj_set_style_outline_opa },
    { "lv_obj_set_style_outline_pad", (void*)&lv_obj_set_style_outline_pad },
    { "lv_obj_set_style_outline_width", (void*)&lv_obj_set_style_outline_width },
    { "lv_obj_set_style_pad_bottom", (void*)&lv_obj_set_style_pad_bottom },
    { "lv_obj_set_style_pad_column", (void*)&lv_obj_set_style_pad_column },
    { "lv_obj_set_style_pad_left", (void*)&lv_obj_set_style_pad_left },
    { "lv_obj_set_style_pad_right", (void*)&lv_obj_set_style_pad_right },
    { "lv_obj_set_style_pad_row", (void*)&lv_obj_set_style_pad_row },
    { "lv_obj_set_style_pad_top", (void*)&lv_obj_set_style_pad_top },
    { "lv_obj_set_style_radius", (void*)&lv_obj_set_style_radius },
    { "lv_obj_set_style_shadow_color", (void*)&lv_obj_set_style_shadow_color },
    { "lv_obj_set_style_shadow_opa", (void*)&lv_obj_set_style_shadow_opa },
    { "lv_obj_set_style_shadow_width", (void*)&lv_obj_set_style_shadow_width },
    { "lv_obj_set_style_text_align", (void*)&lv_obj_set_style_text_align },
    { "lv_obj_set_style_text_color", (void*)&lv_obj_set_style_text_color },
    { "lv_obj_set_style_text_font", (void*)&lv_obj_set_style_text_font },
    { "lv_obj_set_style_text_line_space", (void*)&lv_obj_set_style_text_line_space },
    { "lv_obj_set_style_transform_pivot_x", (void*)&lv_obj_set_style_transform_pivot_x },
    { "lv_obj_set_style_transform_pivot_y", (void*)&lv_obj_set_style_transform_pivot_y },
    { "lv_obj_set_style_transform_rotation", (void*)&lv_obj_set_style_transform_rotation },
    { "lv_obj_set_style_transform_scale_x", (void*)&lv_obj_set_style_transform_scale_x },
    { "lv_obj_set_style_transform_scale_y", (void*)&lv_obj_set_style_transform_scale_y },
    { "lv_obj_set_style_width", (void*)&lv_obj_set_style_width },
    { "lv_obj_set_style_x", (void*)&lv_obj_set_style_x },
    { "lv_obj_set_style_y", (void*)&lv_obj_set_style_y },
    { "lv_obj_set_user_data", (void*)&lv_obj_set_user_data },
    { "lv_obj_set_width", (void*)&lv_obj_set_width },
    { "lv_obj_set_x", (void*)&lv_obj_set_x },
    { "lv_obj_set_y", (void*)&lv_obj_set_y },
    { "lv_obj_stop_scroll_anim", (void*)&lv_obj_stop_scroll_anim },
    { "lv_obj_style_apply_color_filter", (void*)&lv_obj_style_apply_color_filter },
    { "lv_obj_style_apply_recolor", (void*)&lv_obj_style_apply_recolor },
    { "lv_obj_style_create_transition", (void*)&lv_obj_style_create_transition },
    { "lv_obj_style_deinit", (void*)&lv_obj_style_deinit },
    { "lv_obj_style_init", (void*)&lv_obj_style_init },
    { "lv_obj_style_state_compare", (void*)&lv_obj_style_state_compare },
    { "lv_obj_transform_point", (void*)&lv_obj_transform_point },
    { "lv_obj_transform_point_array", (void*)&lv_obj_transform_point_array },
    { "lv_obj_tree_walk", (void*)&lv_obj_tree_walk },
    { "lv_obj_update_layer_type", (void*)&lv_obj_update_layer_type },
    { "lv_obj_update_layout", (void*)&lv_obj_update_layout },
    { "lv_os_init", (void*)&lv_os_init },
    { "lv_palette_darken", (void*)&lv_palette_darken },
    { "lv_palette_lighten", (void*)&lv_palette_lighten },
    { "lv_palette_main", (void*)&lv_palette_main },
    { "lv_pct", (void*)&lv_pct },
    { "lv_pct_to_px", (void*)&lv_pct_to_px },
    { "lv_point_array_transform", (void*)&lv_point_array_transform },
    { "lv_point_from_precise", (void*)&lv_point_from_precise },
    { "lv_point_set", (void*)&lv_point_set },
    { "lv_point_swap", (void*)&lv_point_swap },
    { "lv_point_to_precise", (void*)&lv_point_to_precise },
    { "lv_point_transform", (void*)&lv_point_transform },
    { "lv_pow", (void*)&lv_pow },
    { "lv_rand_set_seed", (void*)&lv_rand_set_seed },
    { "lv_rb_destroy", (void*)&lv_rb_destroy },
    { "lv_rb_drop_node", (void*)&lv_rb_drop_node },
    { "lv_rb_find", (void*)&lv_rb_find },
    { "lv_rb_init", (void*)&lv_rb_init },
    { "lv_rb_insert", (void*)&lv_rb_insert },
    { "lv_rb_minimum_from", (void*)&lv_rb_minimum_from },
    { "lv_rb_remove_node", (void*)&lv_rb_remove_node },
    { "lv_realloc", (void*)&lv_realloc },
    { "lv_realloc_core", (void*)&lv_realloc_core },
    { "lv_refr_deinit", (void*)&lv_refr_deinit },
    { "lv_refr_get_disp_refreshing", (void*)&lv_refr_get_disp_refreshing },
    { "lv_refr_get_top_obj", (void*)&lv_refr_get_top_obj },
    { "lv_refr_init", (void*)&lv_refr_init },
    { "lv_refr_set_disp_refreshing", (void*)&lv_refr_set_disp_refreshing },
    { "lv_roller_set_options", (void*)&lv_roller_set_options },
    { "lv_roller_set_selected", (void*)&lv_roller_set_selected },
    { "lv_scale_set_image_needle_value", (void*)&lv_scale_set_image_needle_value },
    { "lv_scale_set_line_needle_value", (void*)&lv_scale_set_line_needle_value },
    { "lv_screen_active", (void*)&lv_screen_active },
    { "lv_screen_load", (void*)&lv_screen_load },
    { "lv_screen_load_anim", (void*)&lv_screen_load_anim },
    { "lv_slider_create", (void*)&lv_slider_create },
    { "lv_slider_get_left_value", (void*)&lv_slider_get_left_value },
    { "lv_slider_get_mode", (void*)&lv_slider_get_mode },
    { "lv_slider_get_value", (void*)&lv_slider_get_value },
    { "lv_slider_is_symmetrical", (void*)&lv_slider_is_symmetrical },
    { "lv_slider_set_range", (void*)&lv_slider_set_range },
    { "lv_slider_set_start_value", (void*)&lv_slider_set_start_value },
    { "lv_slider_set_value", (void*)&lv_slider_set_value },
    { "lv_snprintf", (void*)&lv_snprintf },
    { "lv_span_stack_deinit", (void*)&lv_span_stack_deinit },
    { "lv_span_stack_init", (void*)&lv_span_stack_init },
    { "lv_spinbox_decrement", (void*)&lv_spinbox_decrement },
    { "lv_spinbox_increment", (void*)&lv_spinbox_increment },
    { "lv_spinbox_step_next", (void*)&lv_spinbox_step_next },
    { "lv_spinbox_step_prev", (void*)&lv_spinbox_step_prev },
    { "lv_spinner_set_anim_params", (void*)&lv_spinner_set_anim_params },
    { "lv_sqrt32", (void*)&lv_sqrt32 },
    { "lv_strcmp", (void*)&lv_strcmp },
    { "lv_strcpy", (void*)&lv_strcpy },
    { "lv_strdup", (void*)&lv_strdup },
    { "lv_strlen", (void*)&lv_strlen },
    { "lv_strncpy", (void*)&lv_strncpy },
    { "lv_strndup", (void*)&lv_strndup },
    { "lv_strnlen", (void*)&lv_strnlen },
    { "lv_style_get_prop", (void*)&lv_style_get_prop },
    { "lv_style_init", (void*)&lv_style_init },
    { "lv_style_is_empty", (void*)&lv_style_is_empty },
    { "lv_style_prop_get_default", (void*)&lv_style_prop_get_default },
    { "lv_style_prop_lookup_flags", (void*)&lv_style_prop_lookup_flags },
    { "lv_style_remove_prop", (void*)&lv_style_remove_prop },
    { "lv_style_reset", (void*)&lv_style_reset },
    { "lv_style_set_anim_duration", (void*)&lv_style_set_anim_duration },
    { "lv_style_set_arc_color", (void*)&lv_style_set_arc_color },
    { "lv_style_set_arc_rounded", (void*)&lv_style_set_arc_rounded },
    { "lv_style_set_arc_width", (void*)&lv_style_set_arc_width },
    { "lv_style_set_bg_color", (void*)&lv_style_set_bg_color },
    { "lv_style_set_bg_grad_color", (void*)&lv_style_set_bg_grad_color },
    { "lv_style_set_bg_image_src", (void*)&lv_style_set_bg_image_src },
    { "lv_style_set_bg_opa", (void*)&lv_style_set_bg_opa },
    { "lv_style_set_border_color", (void*)&lv_style_set_border_color },
    { "lv_style_set_border_opa", (void*)&lv_style_set_border_opa },
    { "lv_style_set_border_post", (void*)&lv_style_set_border_post },
    { "lv_style_set_border_side", (void*)&lv_style_set_border_side },
    { "lv_style_set_border_width", (void*)&lv_style_set_border_width },
    { "lv_style_set_clip_corner", (void*)&lv_style_set_clip_corner },
    { "lv_style_set_height", (void*)&lv_style_set_height },
    { "lv_style_set_length", (void*)&lv_style_set_length },
    { "lv_style_set_line_color", (void*)&lv_style_set_line_color },
    { "lv_style_set_line_width", (void*)&lv_style_set_line_width },
    { "lv_style_set_max_height", (void*)&lv_style_set_max_height },
    { "lv_style_set_outline_color", (void*)&lv_style_set_outline_color },
    { "lv_style_set_outline_opa", (void*)&lv_style_set_outline_opa },
    { "lv_style_set_outline_pad", (void*)&lv_style_set_outline_pad },
    { "lv_style_set_outline_width", (void*)&lv_style_set_outline_width },
    { "lv_style_set_pad_bottom", (void*)&lv_style_set_pad_bottom },
    { "lv_style_set_pad_column", (void*)&lv_style_set_pad_column },
    { "lv_style_set_pad_left", (void*)&lv_style_set_pad_left },
    { "lv_style_set_pad_right", (void*)&lv_style_set_pad_right },
    { "lv_style_set_pad_row", (void*)&lv_style_set_pad_row },
    { "lv_style_set_pad_top", (void*)&lv_style_set_pad_top },
    { "lv_style_set_prop", (void*)&lv_style_set_prop },
    { "lv_style_set_radius", (void*)&lv_style_set_radius },
    { "lv_style_set_recolor", (void*)&lv_style_set_recolor },
    { "lv_style_set_recolor_opa", (void*)&lv_style_set_recolor_opa },
    { "lv_style_set_rotary_sensitivity", (void*)&lv_style_set_rotary_sensitivity },
    { "lv_style_set_shadow_color", (void*)&lv_style_set_shadow_color },
    { "lv_style_set_shadow_offset_y", (void*)&lv_style_set_shadow_offset_y },
    { "lv_style_set_shadow_opa", (void*)&lv_style_set_shadow_opa },
    { "lv_style_set_shadow_spread", (void*)&lv_style_set_shadow_spread },
    { "lv_style_set_shadow_width", (void*)&lv_style_set_shadow_width },
    { "lv_style_set_text_align", (void*)&lv_style_set_text_align },
    { "lv_style_set_text_color", (void*)&lv_style_set_text_color },
    { "lv_style_set_text_font", (void*)&lv_style_set_text_font },
    { "lv_style_set_text_line_space", (void*)&lv_style_set_text_line_space },
    { "lv_style_set_transform_height", (void*)&lv_style_set_transform_height },
    { "lv_style_set_transform_width", (void*)&lv_style_set_transform_width },
    { "lv_style_set_transition", (void*)&lv_style_set_transition },
    { "lv_style_set_width", (void*)&lv_style_set_width },
    { "lv_style_transition_dsc_init", (void*)&lv_style_transition_dsc_init },
    { "lv_tabview_get_content", (void*)&lv_tabview_get_content },
    { "lv_tabview_get_tab_active", (void*)&lv_tabview_get_tab_active },
    { "lv_tabview_get_tab_bar", (void*)&lv_tabview_get_tab_bar },
    { "lv_tabview_get_tab_count", (void*)&lv_tabview_get_tab_count },
    { "lv_tabview_set_active", (void*)&lv_tabview_set_active },
    { "lv_tabview_set_tab_bar_position", (void*)&lv_tabview_set_tab_bar_position },
    { "lv_tabview_set_tab_bar_size", (void*)&lv_tabview_set_tab_bar_size },
    { "lv_text_cut", (void*)&lv_text_cut },
    { "lv_text_encoded_letter_next_2", (void*)&lv_text_encoded_letter_next_2 },
    { "lv_text_get_next_line", (void*)&lv_text_get_next_line },
    { "lv_text_get_size_attributes", (void*)&lv_text_get_size_attributes },
    { "lv_text_get_width", (void*)&lv_text_get_width },
    { "lv_text_ins", (void*)&lv_text_ins },
    { "lv_text_is_cmd", (void*)&lv_text_is_cmd },
    { "lv_text_set_text_vfmt", (void*)&lv_text_set_text_vfmt },
    { "lv_textarea_add_char", (void*)&lv_textarea_add_char },
    { "lv_textarea_add_text", (void*)&lv_textarea_add_text },
    { "lv_textarea_clear_selection", (void*)&lv_textarea_clear_selection },
    { "lv_textarea_create", (void*)&lv_textarea_create },
    { "lv_textarea_cursor_down", (void*)&lv_textarea_cursor_down },
    { "lv_textarea_cursor_left", (void*)&lv_textarea_cursor_left },
    { "lv_textarea_cursor_right", (void*)&lv_textarea_cursor_right },
    { "lv_textarea_cursor_up", (void*)&lv_textarea_cursor_up },
    { "lv_textarea_delete_char", (void*)&lv_textarea_delete_char },
    { "lv_textarea_delete_char_forward", (void*)&lv_textarea_delete_char_forward },
    { "lv_textarea_get_accepted_chars", (void*)&lv_textarea_get_accepted_chars },
    { "lv_textarea_get_cursor_pos", (void*)&lv_textarea_get_cursor_pos },
    { "lv_textarea_get_max_length", (void*)&lv_textarea_get_max_length },
    { "lv_textarea_get_one_line", (void*)&lv_textarea_get_one_line },
    { "lv_textarea_get_password_bullet", (void*)&lv_textarea_get_password_bullet },
    { "lv_textarea_get_text", (void*)&lv_textarea_get_text },
    { "lv_textarea_set_cursor_click_pos", (void*)&lv_textarea_set_cursor_click_pos },
    { "lv_textarea_set_cursor_pos", (void*)&lv_textarea_set_cursor_pos },
    { "lv_textarea_set_one_line", (void*)&lv_textarea_set_one_line },
    { "lv_textarea_set_password_mode", (void*)&lv_textarea_set_password_mode },
    { "lv_textarea_set_placeholder_text", (void*)&lv_textarea_set_placeholder_text },
    { "lv_textarea_set_text", (void*)&lv_textarea_set_text },
    { "lv_theme_apply", (void*)&lv_theme_apply },
    { "lv_theme_default_deinit", (void*)&lv_theme_default_deinit },
    { "lv_theme_default_get", (void*)&lv_theme_default_get },
    { "lv_theme_default_init", (void*)&lv_theme_default_init },
    { "lv_theme_default_is_inited", (void*)&lv_theme_default_is_inited },
    { "lv_theme_get_color_primary", (void*)&lv_theme_get_color_primary },
    { "lv_theme_get_from_obj", (void*)&lv_theme_get_from_obj },
    { "lv_theme_simple_deinit", (void*)&lv_theme_simple_deinit },
    { "lv_thread_delete", (void*)&lv_thread_delete },
    { "lv_thread_init", (void*)&lv_thread_init },
    { "lv_thread_sync_delete", (void*)&lv_thread_sync_delete },
    { "lv_thread_sync_init", (void*)&lv_thread_sync_init },
    { "lv_thread_sync_signal", (void*)&lv_thread_sync_signal },
    { "lv_thread_sync_wait", (void*)&lv_thread_sync_wait },
    { "lv_tick_diff", (void*)&lv_tick_diff },
    { "lv_tick_elaps", (void*)&lv_tick_elaps },
    { "lv_tick_get", (void*)&lv_tick_get },
    { "lv_tick_inc", (void*)&lv_tick_inc },
    { "lv_timer_core_deinit", (void*)&lv_timer_core_deinit },
    { "lv_timer_core_init", (void*)&lv_timer_core_init },
    { "lv_timer_create", (void*)&lv_timer_create },
    { "lv_timer_delete", (void*)&lv_timer_delete },
    { "lv_timer_enable", (void*)&lv_timer_enable },
    { "lv_timer_get_next", (void*)&lv_timer_get_next },
    { "lv_timer_get_paused", (void*)&lv_timer_get_paused },
    { "lv_timer_get_user_data", (void*)&lv_timer_get_user_data },
    { "lv_timer_handler", (void*)&lv_timer_handler },
    { "lv_timer_pause", (void*)&lv_timer_pause },
    { "lv_timer_ready", (void*)&lv_timer_ready },
    { "lv_timer_resume", (void*)&lv_timer_resume },
    { "lv_timer_set_repeat_count", (void*)&lv_timer_set_repeat_count },
    { "lv_trigo_cos", (void*)&lv_trigo_cos },
    { "lv_trigo_sin", (void*)&lv_trigo_sin },
    { "lv_unlock", (void*)&lv_unlock },
    { "lv_utils_bsearch", (void*)&lv_utils_bsearch },
    { "lv_vsnprintf", (void*)&lv_vsnprintf },
    { "memcmp", (void*)&memcmp },
    { "memcpy", (void*)&memcpy },
    { "memset", (void*)&memset },
    { "powf", (void*)&powf },
    { "rand", (void*)&rand },
    { "sinf", (void*)&sinf },
    { "snprintf", (void*)&snprintf },
    { "sqrtf", (void*)&sqrtf },
    { "srand", (void*)&srand },
    { "strchr", (void*)&strchr },
    { "strcmp", (void*)&strcmp },
    { "strlen", (void*)&strlen },
    { "strrchr", (void*)&strrchr },
    { "strstr", (void*)&strstr },
    { "vsnprintf", (void*)&vsnprintf },
    { NULL, NULL }
};

