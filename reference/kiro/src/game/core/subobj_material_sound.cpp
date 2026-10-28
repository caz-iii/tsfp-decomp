// subobj_material_sound.cpp
// Sub-object (breakable physics prop) material -> sound-bank lookup tables plus the
// per-frame rolling/impact 3D-sound driver. Given a prop/material id (its 16-bit
// class code at entity+0x94), these map to one or two SFX bank ids and an amplitude
// scalar, used to (a) preload/release the banks and (b) drive a positional sound
// whose gain tracks the contact-normal speed.
//
// Callers: entity_release_sound_banks (0x80146bf4), subobj_ambient_sound_tick
// (0x80146cb0), subobj_dirty_list_remove (0x80149360), subobj_finalize_bodies
// (0x8014e1fc), and the subobj material/mass path.
//
// Address range: 0x80252384 - 0x80253544  (batch_728)
//
// Self-contained translation unit: all raw ROM symbols and helper prototypes are
// declared file-locally with extern "C" linkage matching the ROM symbol names.

typedef unsigned char      undefined1;
typedef unsigned int       undefined4;
typedef unsigned int       uint;
typedef unsigned short     ushort;

extern "C" {
// ---- cross-module helpers (file-local, ROM signatures per call site) ----
unsigned long long FUN_803826f0(void);   // subobj sound-tick context enter (packed flag|ctx)
void FUN_8038273c(void);                 // subobj sound-tick context leave
double math_sqrt(double x);
int  subobj_get_descriptor(uint handle);
int  subobj_get_contact_normal(uint handle);
void sound_clear_bank_ref(int bankId);
void sound_stop_handle(void);
void sound_update_3d_source(double gain, double a2, double pitch, int hStateOut,
                            int posPtr, int bankId, int a7);

// ---- raw float literal pool ----
extern float FLOAT_80618d90, FLOAT_80618d94, FLOAT_80618d98, FLOAT_80618d9c, FLOAT_80618da0;
extern float FLOAT_80618da4, FLOAT_80618da8, FLOAT_80618dac, FLOAT_80618db0, FLOAT_80618db4;
extern float FLOAT_80618db8, FLOAT_80618dbc;
extern float FLOAT_80618dc0, FLOAT_80618dc4, FLOAT_80618dc8, FLOAT_80618dcc, FLOAT_80618dd0;
extern float FLOAT_80618dd4, FLOAT_80618dd8, FLOAT_80618ddc, FLOAT_80618de0, FLOAT_80618de4;
extern float FLOAT_80618de8, FLOAT_80618dec;

// ---- additional cross-module helpers for the ambient/impact sound handlers ----
unsigned long long __save_gpr_28(void);
void __restore_gpr_28(void);
uint game_rand(void);
double game_frand(void);
undefined4 *entity_get_world_position(int entity);
void sound_init_play_params(int params);
uint sound_play_request(int *params);
int  sound_is_playing(uint handle);
void sound_register_fade_out(double a1, double a2, int *hStateOut, undefined4 *pos);
void entity_register_for_update(int entity, int slot, int callback);
void entity_set_render_type(int entity, void *callback);

// ---- additional raw globals ----
extern int DAT_80612998;          // last-played impact entity net-id guard
extern int DAT_80610f18;          // last impact frame stamp
extern int DAT_80611928;          // current frame stamp
extern unsigned char DAT_804045bc; // prop-flags table base (0x40-stride records)

// ---- intra-file forward decls ----
int FUN_80253368(int matId);
undefined4 FUN_80253688(int id, undefined4 *pLo, undefined4 *pHi, undefined4 *pAmp);
undefined4 FUN_80253768(int id, undefined4 *pLo, undefined4 *pHi, undefined4 *pAmp);
void FUN_80253ae4(void);

// ============================================================================
// 0x80252384  subobj_material_table_entry
// Returns a pointer (as int) into a 0x18-stride material table for material ids
// below 0x1a; 0 otherwise.  Base = -0x7fb91e84 (0x8046e17c).
// ============================================================================
int FUN_80252384(uint matId)
{
    if ((matId & 0xff) < 0x1a) {
        return (int)((matId & 0xff) * 0x18 + -0x7fb91e84);
    }
    return 0;
}

// ============================================================================
// 0x802523ac  subobj_material_sound_pair
// Maps a prop/level material id (param_1) to a primary + secondary SFX bank id
// (out param_3 / param_4) and an amplitude scalar (out param_5). Returns 1 when a
// mapping exists, 0 otherwise. Large hand-written binary-search switch from the
// original source; the out-params are optional (NULL-checked).
// ============================================================================
undefined4 FUN_802523ac(int id, int flags, undefined4 *pBankA, undefined4 *pBankB,
                        undefined4 *pAmp)
{
    if ((flags != 0x40) && ((0x3f < flags || (flags != 0x20)))) {
        if (id == 0x3b8) {
            if (pBankA) *pBankA = 0xcec;
            if (pBankB) *pBankB = 0xced;
            if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
            return 1;
        }
        if (id < 0x3b8) {
            if (id != 0x247) {
                if (id < 0x247) {
                    if (id == 0x197) {
LAB_80253120:
                        if (pBankA) *pBankA = 0xd02;
                        if (pBankB) *pBankB = 0xd02;
                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d98;
                        return 1;
                    }
                    if (id < 0x197) {
                        if (id == 0xfc) {
                            if (pBankA) *pBankA = 0xd80;
                            if (pBankB) *pBankB = 0xd81;
                            if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                            return 1;
                        }
                        if (id < 0xfc) {
                            if (id == 0xa5) goto LAB_80252ac8;
                            if (id < 0xa5) {
                                if (id == 0xa3) {
LAB_80252b00:
                                    if (pBankA) *pBankA = 0xcdc;
                                    if (pBankB) *pBankB = 0xcdc;
                                    if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                    return 1;
                                }
                                if (0xa2 < id) goto LAB_80252f98;
                                if ((id < 0x17) && (0x13 < id)) goto LAB_80253040;
                            } else {
                                if (id == 0xe3) goto LAB_80252b70;
                                if (id < 0xe3) {
                                    if ((id < 0xe1) && (0xdd < id)) goto LAB_80252f28;
                                } else if (id < 0xe5) goto LAB_80252b00;
                            }
                        } else {
                            if (id == 0x183) {
LAB_80252b38:
                                if (pBankA) *pBankA = 0xce0;
                                if (pBankB) *pBankB = 0xce0;
                                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d94;
                                return 1;
                            }
                            if (id < 0x183) {
                                if (id < 0x111) {
                                    if (id == 0x102) {
                                        if (pBankA) *pBankA = 0xd75;
                                        if (pBankB) *pBankB = 0xd76;
                                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                        return 1;
                                    }
                                } else {
                                    if (0x17f < id) {
                                        if (pBankA) *pBankA = 0xd05;
                                        if (pBankB) *pBankB = 0xd06;
                                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                        return 1;
                                    }
                                    if (id < 0x113) {
                                        if (pBankA) *pBankA = 0xcfe;
                                        if (pBankB) *pBankB = 0xcff;
                                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d98;
                                        return 1;
                                    }
                                }
                            } else {
                                if (id == 0x189) goto LAB_80252b38;
                                if ((id < 0x189) && (id < 0x185)) goto LAB_80252b70;
                            }
                        }
                    } else {
                        if (id == 0x1bf) goto LAB_80252ef0;
                        if (id < 0x1bf) {
                            if (id == 0x1b1) goto LAB_80252b38;
                            if (id < 0x1b1) {
                                if (id == 0x1aa) {
LAB_80252c18:
                                    if (pBankA) *pBankA = 0xd07;
                                    if (pBankB) *pBankB = 0xd09;
                                    if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d98;
                                    return 1;
                                }
                                if ((id < 0x1aa) && (id == 0x1a2)) goto LAB_80253040;
                            } else {
                                if (id == 0x1b9) goto LAB_80252b70;
                                if (id < 0x1b9) {
                                    if (id == 0x1b6) goto LAB_80252b00;
                                } else if (0x1bd < id) goto LAB_80252c18;
                            }
                        } else if (id < 0x227) {
                            if (id < 0x1f0) {
                                if (0x1ed < id) goto LAB_80252e48;
                                if (id < 0x1c8) {
                                    if (pBankA) *pBankA = 0x14f;
                                    if (pBankB) *pBankB = 0x150;
                                    if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                    return 1;
                                }
                            } else if ((id < 0x20f) && (0x20c < id)) goto LAB_80252b70;
                        } else if (id < 0x237) {
                            if (0x230 < id) {
LAB_80252ac8:
                                if (pBankA) *pBankA = 0xcdb;
                                if (pBankB) *pBankB = 0xcdb;
                                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                return 1;
                            }
                            if (id < 0x22d) goto LAB_80252b38;
                        } else if (id == 0x244) goto LAB_80252ac8;
                    }
                } else {
                    if (id == 0x33f) {
LAB_80252b70:
                        if (pBankA) *pBankA = 0xcda;
                        if (pBankB) *pBankB = 0xcda;
                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d94;
                        return 1;
                    }
                    if (id < 0x33f) {
                        if (id < 0x2b6) {
                            if (id == 0x297) goto LAB_80252ac8;
                            if (id < 0x297) {
                                if (id < 0x288) {
                                    if (id == 0x256) goto LAB_80252ac8;
                                } else if (id < 0x292) goto LAB_80252f98;
                            } else {
                                if (id == 0x2af) {
LAB_80252f98:
                                    if (pBankA) *pBankA = 0xcde;
                                    if (pBankB) *pBankB = 0xcdf;
                                    if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                    return 1;
                                }
                                if (id < 0x2af) {
                                    if ((id < 0x2ac) && (0x2a8 < id)) goto LAB_80252b38;
                                } else if (0x2b3 < id) goto LAB_80252f98;
                            }
                        } else if (id < 0x321) {
                            if (id == 0x2d5) goto LAB_80252ac8;
                            if (id < 0x2d5) {
                                if (id == 0x2d3) goto LAB_80252b70;
                                if (0x2d2 < id) goto LAB_80252f98;
                            } else if (0x31e < id) goto LAB_80252f98;
                        } else {
                            if (id == 0x33d) goto LAB_80252b70;
                            if ((id < 0x33d) && (id == 0x326)) goto LAB_80252b38;
                        }
                    } else {
                        if (id == 0x382) goto LAB_80252b70;
                        if (id < 0x382) {
                            if (id < 0x353) {
                                if (id == 0x34a) goto LAB_80252ac8;
                                if (id < 0x34a) {
                                    if (id == 0x344) {
                                        if (pBankA) *pBankA = 0xcfc;
                                        if (pBankB) *pBankB = 0xcfd;
                                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d98;
                                        return 1;
                                    }
                                    if (0x343 < id) goto LAB_80253190;
                                } else if (0x34e < id) goto LAB_80253158;
                            } else {
                                if (id == 0x363) {
LAB_80253158:
                                    if (pBankA) *pBankA = 0xcfe;
                                    if (pBankB) *pBankB = 0xcff;
                                    if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d98;
                                    return 1;
                                }
                                if (id < 0x363) {
                                    if ((id < 0x361) && (0x358 < id)) {
                                        if (pBankA) *pBankA = 0xd00;
                                        if (pBankB) *pBankB = 0xd01;
                                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d98;
                                        return 1;
                                    }
                                } else if (id < 0x365) goto LAB_80253120;
                            }
                        } else {
                            if (id == 0x39c) {
                                if (pBankA) *pBankA = 0xcf0;
                                if (pBankB) *pBankB = 0xcf0;
                                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                return 1;
                            }
                            if (id < 0x39c) {
                                if (id < 0x392) {
                                    if (id == 0x385) {
LAB_80253190:
                                        if (pBankA) *pBankA = 0xd03;
                                        if (pBankB) *pBankB = 0xd04;
                                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d98;
                                        return 1;
                                    }
                                } else if (id < 0x394) {
LAB_80252c50:
                                    if (pBankA) *pBankA = 0xcea;
                                    if (pBankB) *pBankB = 0xcea;
                                    if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                    return 1;
                                }
                            } else {
                                if (id == 0x3b5) goto LAB_80252ba8;
                                if ((id < 0x3b5) && (id == 0x3a5)) goto LAB_80252c50;
                            }
                        }
                    }
                    return 0;
                }
            }
        } else if (id != 0x5af) {
            if (id < 0x5af) {
                if (id == 0x486) goto LAB_80252e80;
                if (id < 0x486) {
                    if (0x455 < id) {
                        if (id == 0x463) {
LAB_80252ef0:
                            if (pBankA) *pBankA = 0xcf1;
                            if (pBankB) *pBankB = 0xcf2;
                            if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                            return 1;
                        }
                        if (id < 0x463) {
                            if (id == 0x45b) goto LAB_80252f98;
                            if (0x45a < id) {
                                if (id != 0x460) return 0;
LAB_80252f60:
                                if (pBankA) *pBankA = 0xdc8;
                                if (pBankB) *pBankB = 0xdc9;
                                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                return 1;
                            }
                            if (id < 0x458) goto LAB_80252b38;
                        } else {
                            if (id == 0x47b) {
                                if (pBankA) *pBankA = 0xcf3;
                                if (pBankB) *pBankB = 0xcf4;
                                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                return 1;
                            }
                            if (0x47a < id) return 0;
                            if (id != 0x46b) return 0;
                        }
                        goto LAB_80252b70;
                    }
                    if (id == 0x43b) return 0;
                    if (id < 0x43b) {
                        if (id != 0x437) {
                            if (0x436 < id) {
                                if (id < 0x439) return 0;
                                if (pBankA) *pBankA = 0xcf5;
                                if (pBankB) *pBankB = 0xcf5;
                                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                return 1;
                            }
                            if (id != 0x3c1) return 0;
                            if (pBankA) *pBankA = 0xceb;
                            if (pBankB) *pBankB = 0xceb;
                            if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                            return 1;
                        }
                        goto LAB_80252f98;
                    }
                    if (id != 0x43e) {
                        if (id < 0x43e) {
                            if (0x43c < id) return 0;
                            if (pBankA) *pBankA = 0xcf6;
                            if (pBankB) *pBankB = 0xcf6;
                            if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                            return 1;
                        }
                        if (0x448 < id) return 0;
                        if (id < 0x447) return 0;
                        if (pBankA) *pBankA = 0xcf7;
                        if (pBankB) *pBankB = 0xcf8;
                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                        return 1;
                    }
                } else {
                    if (id == 0x503) {
                        if (pBankA) *pBankA = 0x36c;
                        if (pBankB) *pBankB = 0x36d;
                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d94;
                        return 1;
                    }
                    if (id < 0x503) {
                        if (id == 0x4ef) goto LAB_80252b00;
                        if (0x4ee < id) {
                            if (id == 0x4f5) {
                                if (pBankA) *pBankA = 0xce5;
                                if (pBankB) *pBankB = 0xce6;
                                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d98;
                                return 1;
                            }
                            if (0x4f4 < id) return 0;
                            if (id == 0x4f1) {
LAB_80252e48:
                                if (pBankA) *pBankA = 0xce3;
                                if (pBankB) *pBankB = 0xce3;
                                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                return 1;
                            }
                            if (0x4f0 < id) return 0;
                            goto LAB_80252ef0;
                        }
                        if (id == 0x4cb) goto LAB_80252b70;
                        if (0x4ca < id) {
                            if (id != 0x4ec) return 0;
                            if (pBankA) *pBankA = 0xce4;
                            if (pBankB) *pBankB = 0xce4;
                            if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                            return 1;
                        }
                        if (0x487 < id) return 0;
                    } else if (id != 0x549) {
                        if (0x548 < id) {
                            if (0x54c < id) {
                                if (0x54f < id) return 0;
                                if (pBankA) *pBankA = 0xcde;
                                if (pBankB) *pBankB = 0xcdf;
                                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d9c;
                                return 1;
                            }
                            if (0x54a < id) return 0;
                            goto LAB_80252f98;
                        }
                        if (id != 0x53d) {
                            if (0x53c < id) return 0;
                            if (0x50a < id) return 0;
                            if (id < 0x508) return 0;
LAB_80252f28:
                            if (pBankA) *pBankA = 0x161e;
                            if (pBankB) *pBankB = 0x161e;
                            if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                            return 1;
                        }
                    }
                }
                goto LAB_80252ac8;
            }
            if (id != 0x5e1) {
                if (id < 0x5e1) {
                    if (id != 0x5c7) {
                        if (id < 0x5c7) {
                            if (id != 0x5b8) {
                                if (id < 0x5b8) {
                                    if (id != 0x5b5) {
                                        if (0x5b4 < id) return 0;
                                        if (id != 0x5b2) return 0;
                                    }
                                } else if (id != 0x5c2) {
                                    if (0x5c1 < id) return 0;
                                    if (id != 0x5bb) return 0;
                                }
                            }
                        } else if (id != 0x5d2) {
                            if (id < 0x5d2) {
                                if (id != 0x5cf) {
                                    if (0x5ce < id) return 0;
                                    if (id != 0x5ca) return 0;
                                }
                            } else if (id != 0x5dc) {
                                if (0x5db < id) return 0;
                                if (id != 0x5d7) return 0;
                            }
                        }
                    }
                } else {
                    if (id == 0x8ab) goto LAB_80252f98;
                    if (0x8aa < id) {
                        if (id == 0x8bb) goto LAB_80252e80;
                        if (id < 0x8bb) {
                            if (id < 0x8b4) {
                                if (id != 0x8ad) {
                                    if (0x8ac < id) goto LAB_80252b70;
                                    goto LAB_80253008;
                                }
                                goto LAB_80252f98;
                            }
                            if (id < 0x8ba) goto LAB_80252f60;
                        } else if (id != 0x8be) {
                            if (id < 0x8be) {
                                if (id < 0x8bd) goto LAB_80252ba8;
                            } else if (0x8bf < id) return 0;
LAB_80252e80:
                            if (pBankA) *pBankA = 0xce7;
                            if (pBankB) *pBankB = 0xce9;
                            if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                            return 1;
                        }
LAB_80252ba8:
                        if (pBankA) *pBankA = 0xcee;
                        if (pBankB) *pBankB = 0xcef;
                        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                        return 1;
                    }
                    if (id != 0x5ed) {
                        if (0x5ec < id) {
                            if (id != 0x8a9) {
                                if (id < 0x8a9) {
                                    if (id != 0x88f) return 0;
                                    goto LAB_80253190;
                                }
LAB_80253008:
                                if (pBankA) *pBankA = 0xdcb;
                                if (pBankB) *pBankB = 0xdcc;
                                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
                                return 1;
                            }
                            goto LAB_80252f98;
                        }
                        if (id != 0x5e7) {
                            if (0x5e6 < id) return 0;
                            if (id != 0x5e4) return 0;
                        }
                    }
                }
            }
        }
LAB_80253040:
        if (pBankA) *pBankA = 0x632;
        if (pBankB) *pBankB = 0x637;
        if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618da0;
        return 1;
    }
    if (id != 0x37c) {
        if (0x37b < id) {
            if (id != 0x6de) {
                if (id < 0x6de) {
                    if (id != 0x4b4) {
                        if (id < 0x4b4) {
                            if (id != 0x430) {
                                if ((id < 0x430) && (0x42e < id)) goto LAB_802524dc;
                                goto LAB_80252514;
                            }
                        } else if (0x4b5 < id) goto LAB_80252514;
                        goto LAB_802524a4;
                    }
                } else if (id != 0x6f6) {
                    if (id < 0x6f6) {
                        if (id == 0x6e1) goto LAB_802524dc;
                    } else if (id == 0x6f9) goto LAB_802524dc;
                    goto LAB_80252514;
                }
            }
LAB_802524dc:
            if (pBankA) *pBankA = 0xcfa;
            if (pBankB) *pBankB = 0xcfa;
            if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
            return 1;
        }
        if (id != 0x151) {
            if (id < 0x151) {
                if (id != 0x143) {
                    if (id < 0x143) {
                        if (id < 0x13b) {
                            if (id < 0x12d) goto LAB_80252514;
                        } else if (id < 0x142) goto LAB_80252514;
                        goto LAB_802524a4;
                    }
                    if (id < 0x14b) {
                        if (0x144 < id) goto LAB_802524dc;
                        goto LAB_802524a4;
                    }
                }
            } else {
                if (id == 0x15c) goto LAB_802524a4;
                if (id < 0x15c) {
                    if (id == 0x153) goto LAB_802524a4;
                    if (id < 0x153) goto LAB_802524dc;
                } else if (0x37a < id) goto LAB_802524dc;
            }
LAB_80252514:
            if (pBankA) *pBankA = 0xcf9;
            if (pBankB) *pBankB = 0xcf9;
            if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
            return 1;
        }
    }
LAB_802524a4:
    if (pBankA) *pBankA = 0xcdc;
    if (pBankB) *pBankB = 0xcdc;
    if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d90;
    return 1;
}

// ============================================================================
// 0x802532b0  subobj_material_impact_pair
// Smaller impact-sound lookup: default banks 0xce1/0xce2 with amp FLOAT_80618d94,
// except for a handful of ids that fall through to the -1 "silent" pair with amp
// FLOAT_80618da4.
// ============================================================================
undefined4 FUN_802532b0(int id, undefined4 *pBankA, undefined4 *pBankB, undefined4 *pAmp)
{
    if (id != 0x1b1) {
        if (id < 0x1b1) {
            if ((id != 0x189) && ((0x188 < id || (id != 0x183)))) goto LAB_80253330;
        } else if (id < 0x456) {
            if ((0x2ab < id) || (id < 0x2a9)) {
LAB_80253330:
                if (pBankA) *pBankA = 0xffffffff;
                if (pBankB) *pBankB = 0xffffffff;
                if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618da4;
                return 1;
            }
        } else if (0x457 < id) goto LAB_80253330;
    }
    if (pBankA) *pBankA = 0xce1;
    if (pBankB) *pBankB = 0xce2;
    if (pAmp)   *pAmp   = *(undefined4 *)&FLOAT_80618d94;
    return 1;
}

// ============================================================================
// 0x80253368  subobj_material_roll_bank
// Maps a material id to the single "rolling" SFX bank id (default 0xcdd), or
// 0xffffffff (none) for the excluded ranges.
// ============================================================================
int FUN_80253368(int id)
{
    if (id != 0x45b) {
        if (0x45a < id) {
            if (0x8ad < id) {
                if (0x8b9 < id) return (int)0xffffffff;
                if (id < 0x8b4)  return (int)0xffffffff;
                return 0xdca;
            }
            if (id == 0x54a) return 0xcdd;
            if (id < 0x54a)   return (int)0xffffffff;
            if (id < 0x8a9)   return (int)0xffffffff;
            return 0xcdd;
        }
        if (id != 0x2d4) {
            if (id < 0x2d4) {
                if (id < 0x2b4) {
                    if (id != 0xa4) return (int)0xffffffff;
                } else if (0x2b5 < id) return (int)0xffffffff;
            } else {
                if (800 < id) return (int)0xffffffff;
                if (id < 799) return (int)0xffffffff;
            }
        }
    }
    return 0xcdd;
}

// ============================================================================
// 0x80253400  subobj_material_release_banks
// Releases (drops the ref on) all SFX banks associated with a material id: the
// rolling bank from subobj_material_roll_bank plus material-specific extras.
// ============================================================================
void FUN_80253400(int id)
{
    int rollBank;

    rollBank = FUN_80253368(id);
    sound_clear_bank_ref(rollBank);
    if (id == 0x487) {
LAB_80253490:
        sound_clear_bank_ref(0xcfb);
        return;
    }
    if (id < 0x487) {
        if (id == 0x34a) {
LAB_802534c0:
            sound_clear_bank_ref(0x51);
            return;
        }
        if (id < 0x34a) {
            if (id == 0xb7) {
                sound_clear_bank_ref(0x66e);
                sound_clear_bank_ref(0x66f);
                sound_clear_bank_ref(0x670);
                sound_clear_bank_ref(0x671);
                sound_clear_bank_ref(0x672);
                sound_clear_bank_ref(0x673);
                sound_clear_bank_ref(0x674);
                sound_clear_bank_ref(0x675);
                return;
            }
            if (0xb6 < id) return;
            if (id != 0xb0) return;
            sound_clear_bank_ref(0xdcd);
            sound_clear_bank_ref(0xdce);
            sound_clear_bank_ref(0xdcf);
            sound_clear_bank_ref(0xdd0);
            return;
        }
        if (id != 0x385) return;
    } else if (id != 0x88f) {
        if (id < 0x88f) {
            if (id != 0x549) {
                if (0x548 < id) return;
                if (id != 0x53d) return;
            }
            goto LAB_80253490;
        }
        if (0x8b9 < id) return;
        if (id < 0x8b4) return;
        goto LAB_802534c0;
    }
    sound_clear_bank_ref(0x3b2);
    sound_clear_bank_ref(0x3b3);
    sound_clear_bank_ref(0xd73);
    sound_clear_bank_ref(0x5ff);
}

// ============================================================================
// 0x80253544  subobj_material_roll_sound_tick
// Per-frame rolling/impact sound driver for one sub-object. FUN_803826f0 returns
// a packed (startFlag | entityCtx) pair: startFlag==0 stops the loop and resets
// the gain; otherwise the gain tracks the contact-normal speed (clamped) and is
// fed to sound_update_3d_source with a speed-scaled pitch.
// ============================================================================
void FUN_80253544(void)
{
    int    ctx;
    int    bank;
    float *pNormal;
    int    desc;
    double speed;
    double gain;
    double pitch;
    unsigned long long packed;

    packed = FUN_803826f0();
    ctx    = (int)(unsigned int)(packed >> 0x20);
    bank   = FUN_80253368((int)(uint)*(ushort *)(ctx + 0x94));
    if (bank != -1) {
        if ((int)(unsigned int)packed == 0) {
            desc = subobj_get_descriptor(*(uint *)(ctx + 0x80));
            *(float *)(desc + 0x1e0) = FLOAT_80618da4;
            sound_stop_handle();
            *(undefined4 *)(ctx + 0x22c) = 0xffffffff;
        } else {
            pNormal = (float *)subobj_get_contact_normal(*(uint *)(ctx + 0x80));
            speed = math_sqrt((double)(pNormal[2] * pNormal[2] + *pNormal * *pNormal +
                                       pNormal[1] * pNormal[1]));
            speed = (double)(float)((double)FLOAT_80618da8 * speed);
            desc  = subobj_get_descriptor(*(uint *)(ctx + 0x80));
            if ((double)FLOAT_80618d94 < speed) {
                speed = (double)FLOAT_80618d94;
            }
            gain  = (double)FLOAT_80618db4;
            pitch = (double)FLOAT_80618db0;
            *(float *)(desc + 0x1e0) = *(float *)(desc + 0x1e0) * FLOAT_80618dac;
            *(float *)(desc + 0x1e0) = (float)(pitch * speed + (double)*(float *)(desc + 0x1e0));
            if (speed <= gain) {
                gain = (double)*(float *)(desc + 0x1e0);
            }
            sound_update_3d_source(
                gain, (double)FLOAT_80618db8,
                (double)(FLOAT_80618da8 * *(float *)(desc + 0x1e0) + FLOAT_80618dbc),
                ctx + 0x22c, ctx + 100, bank, 1);
        }
    }
    FUN_8038273c();
}

// ============================================================================
// 0x80253674  subobj_ambient_sound_reset
// Clears the per-level impact-dedupe guard so the next impact sound plays fresh.
// ============================================================================
void FUN_80253674(void)
{
    DAT_80612998 = 0;
    DAT_80610f18 = -1;
}

// ============================================================================
// 0x80253688  subobj_ambient_sound_range
// Maps a prop material id to an ambient/loop SFX bank-id range [lo,hi] (out
// pLo/pHi) plus an amplitude scalar (out pAmp). Returns 1 if mapped.
// ============================================================================
undefined4 FUN_80253688(int id, undefined4 *pLo, undefined4 *pHi, undefined4 *pAmp)
{
    if (id == 0x2b9) {
LAB_802536b8:
        if (pLo)  *pLo  = 0x34a;
        if (pHi)  *pHi  = 0x34d;
        if (pAmp) *pAmp = *(undefined4 *)&FLOAT_80618dc0;
        return 1;
    }
    if (id < 0x2b9) {
        if (id == 0xb8) {
            if (pLo)  *pLo  = 0x67f;
            if (pHi)  *pHi  = 0x681;
            if (pAmp) *pAmp = *(undefined4 *)&FLOAT_80618dc8;
            return 1;
        }
        if ((id < 0xb8) && (id == 0xb5)) {
            if (pLo)  *pLo  = 0x676;
            if (pHi)  *pHi  = 0x67b;
            if (pAmp) *pAmp = *(undefined4 *)&FLOAT_80618dc4;
            return 1;
        }
    } else if (id == 0x8ca) goto LAB_802536b8;
    return 0;
}

// ============================================================================
// 0x80253768  subobj_impact_sound_range
// As subobj_ambient_sound_range, for the impact/hit SFX bank range.
// ============================================================================
undefined4 FUN_80253768(int id, undefined4 *pLo, undefined4 *pHi, undefined4 *pAmp)
{
    if (id == 0xd4) {
        if (pLo)  *pLo  = 0x684;
        if (pHi)  *pHi  = 0x684;
        if (pAmp) *pAmp = *(undefined4 *)&FLOAT_80618dc0;
        return 1;
    }
    if (id < 0xd4) {
        if (id == 0xb6) {
            if (pLo)  *pLo  = 0x67c;
            if (pHi)  *pHi  = 0x67e;
            if (pAmp) *pAmp = *(undefined4 *)&FLOAT_80618dcc;
            return 1;
        }
        if ((id < 0xb6) && (id == 0x2f)) {
            if (pLo)  *pLo  = 0x682;
            if (pHi)  *pHi  = 0x682;
            if (pAmp) *pAmp = *(undefined4 *)&FLOAT_80618dc0;
            return 1;
        }
    } else if (id == 0x1be) {
        if (pLo)  *pLo  = 0x15b;
        if (pHi)  *pHi  = 0x15c;
        if (pAmp) *pAmp = *(undefined4 *)&FLOAT_80618dd0;
        return 1;
    }
    return 0;
}

// ============================================================================
// 0x80253880  subobj_release_ambient_impact_banks
// Releases every bank id in both the ambient and impact ranges for a material id.
// ============================================================================
void FUN_80253880(int id)
{
    int rc;
    int hi;
    int lo[4];

    rc = (int)FUN_80253688(id, (undefined4 *)lo, (undefined4 *)&hi, (undefined4 *)0);
    if (rc != 0) {
        while (rc = lo[0], lo[0] <= hi) {
            lo[0] = lo[0] + 1;
            sound_clear_bank_ref(rc);
        }
    }
    rc = (int)FUN_80253768(id, (undefined4 *)lo, (undefined4 *)&hi, (undefined4 *)0);
    if (rc != 0) {
        while (rc = lo[0], lo[0] <= hi) {
            lo[0] = lo[0] + 1;
            sound_clear_bank_ref(rc);
        }
    }
}

// ============================================================================
// 0x80253924  subobj_ambient_sound_update
// Per-frame update callback (registered by subobj_material_sound_init): if the
// entity has no ambient loop playing, picks a random bank in the material's
// ambient range (avoiding an immediate repeat) and starts a positional loop.
// ============================================================================
void FUN_80253924(void)
{
    int         entity;
    undefined4 *pPos;
    uint        r;
    uint        span;
    int         bank;
    double      pitch;
    double      in_f31 = 0.0, in_ps31_1 = 0.0;
    float       local_68;
    int         local_64, local_60;
    int         local_5c[2];
    float       local_54;
    uint        local_4c = 0;
    float       local_48, local_44;
    undefined4  local_40, local_3c, local_38;
    undefined1  local_34;
    float       local_30;
    float       local_8, fStack_4;

    local_8  = (float)in_f31;
    fStack_4 = (float)in_ps31_1;
    (void)local_8; (void)fStack_4;

    entity = (int)__save_gpr_28();
    if (*(int *)(entity + 0x22c) < 0) {
        FUN_80253688((int)(uint)*(ushort *)(entity + 0x94), (undefined4 *)&local_60,
                     (undefined4 *)&local_64, (undefined4 *)&local_68);
        r = *(uint *)(entity + 0x1a0);
        if (((int)r < local_60) || (local_64 < (int)r) || (local_64 <= local_60)) {
            r    = game_rand();
            span = (local_64 + 1) - local_60;
            local_60 = local_60 + (int)(r - (r / span) * span);
        } else {
            span = game_rand();
            span = local_60 + (span - (span / (uint)(local_64 - local_60)) * (local_64 - local_60));
            local_60 = (int)span + ((int)span >> 0x1f) + ((uint)(r <= span) - ((int)r >> 0x1f));
        }
        *(int *)(entity + 0x1a0) = local_60;
        pPos  = entity_get_world_position(entity);
        pitch = game_frand();
        bank  = *(int *)(entity + 0x1a0);
        pitch = (double)(local_68 * (float)(pitch - (double)FLOAT_80618dd8) + FLOAT_80618dd4);
        sound_init_play_params((int)local_5c);
        local_34 = 1;
        local_30 = FLOAT_80618ddc;
        local_4c = local_4c | 0x2a0;
        local_54 = FLOAT_80618dc0;
        local_44 = FLOAT_80618de0;
        local_48 = (float)pitch;
        local_40 = *pPos;
        local_3c = pPos[1];
        local_38 = pPos[2];
        (void)local_54; (void)local_44; (void)local_48; (void)local_40; (void)local_3c;
        (void)local_38; (void)local_30; (void)local_34; (void)local_4c;
        local_5c[0] = bank;
        *(uint *)(entity + 0x22c) = sound_play_request(local_5c);
        *(uint *)(entity + 0x18) = *(uint *)(entity + 0x18) | 0x40;
    }
    __restore_gpr_28();
}

// ============================================================================
// 0x80253a8c  subobj_ambient_sound_poll
// Keeps the ambient loop handle alive: if it stopped, mark it free (-1); else
// re-flag the entity as sound-active for the frame.
// ============================================================================
void FUN_80253a8c(int entity)
{
    int playing;

    if (-1 < (int)*(uint *)(entity + 0x22c)) {
        playing = sound_is_playing(*(uint *)(entity + 0x22c));
        if (playing == 0) {
            *(undefined4 *)(entity + 0x22c) = 0xffffffff;
        } else {
            *(uint *)(entity + 0x18) = *(uint *)(entity + 0x18) | 0x40;
        }
    }
}

// ============================================================================
// 0x80253ae4  subobj_impact_sound_emit
// Render-callback impact-sound emitter: when the sub-object hit is significant
// (prop flag 0x80 set, contact present) and not a duplicate of the last impact
// this frame, fades the prior handle and starts a new random positional impact
// sound from the material's impact range.
// ============================================================================
void FUN_80253ae4(void)
{
    int        *ctx;
    undefined4 *pPos;
    uint        r;
    uint        span;
    double      pitch;
    double      in_f31 = 0.0, in_ps31_1 = 0.0;
    float       local_68;
    int         local_64, local_60;
    int         local_5c[4];
    uint        local_4c = 0;
    float       local_48, local_44;
    undefined4  local_40, local_3c, local_38;
    undefined1  local_34;
    float       local_30;
    float       local_8, fStack_4;

    local_8  = (float)in_f31;
    fStack_4 = (float)in_ps31_1;
    (void)local_8; (void)fStack_4;

    ctx = (int *)(unsigned int)FUN_803826f0();
    if ((ctx[6] != -1) && ((*(uint *)(&DAT_804045bc + ctx[6] * 0x40) & 0x80) != 0) &&
        (ctx[10] != 0) &&
        ((DAT_80610f18 != DAT_80611928) || (DAT_80612998 != *(int *)(*ctx + 0x2c)))) {
        FUN_80253768((int)(uint)*(ushort *)(*ctx + 0x94), (undefined4 *)&local_60,
                     (undefined4 *)&local_64, (undefined4 *)&local_68);
        pPos = entity_get_world_position(*ctx);
        sound_register_fade_out((double)FLOAT_80618de4, (double)FLOAT_80618de8,
                                (int *)(*ctx + 0x228), pPos);
        pPos  = entity_get_world_position(*ctx);
        pitch = game_frand();
        pitch = (double)(local_68 * (float)(pitch - (double)FLOAT_80618dd8) + FLOAT_80618dd4);
        r     = game_rand();
        span  = (local_64 + 1) - local_60;
        sound_init_play_params((int)local_5c);
        local_4c = local_4c | 0xa0;
        local_34 = 1;
        local_30 = FLOAT_80618dec;
        local_48 = (float)pitch;
        local_44 = FLOAT_80618de4;
        local_40 = *pPos;
        local_3c = pPos[1];
        local_38 = pPos[2];
        (void)local_44; (void)local_48; (void)local_40; (void)local_3c; (void)local_38;
        (void)local_30; (void)local_34; (void)local_4c;
        local_5c[0] = local_60 + (int)(r - (r / span) * span);
        *(uint *)(*ctx + 0x228) = sound_play_request(local_5c);
        DAT_80610f18 = DAT_80611928;
        DAT_80612998 = *(int *)(*ctx + 0x2c);
    }
    FUN_8038273c();
}

// ============================================================================
// 0x80253c6c  subobj_material_sound_init
// On sub-object spawn: if the material has an ambient loop, register the ambient
// update/poll callbacks; if it has an impact sound, install the impact render
// callback (subobj_impact_sound_emit).
// ============================================================================
void FUN_80253c6c(int entity)
{
    int rc;

    rc = (int)FUN_80253688((int)(uint)*(ushort *)(entity + 0x94), (undefined4 *)0,
                           (undefined4 *)0, (undefined4 *)0);
    if (rc != 0) {
        entity_register_for_update(entity, 0, -0x7fdac6dc);
        entity_register_for_update(entity, 3, -0x7fdac574);
        *(uint *)(entity + 0x28) = *(uint *)(entity + 0x28) | 4;
        *(undefined4 *)(entity + 0x1a0) = 0xffffffff;
    }
    rc = (int)FUN_80253768((int)(uint)*(ushort *)(entity + 0x94), (undefined4 *)0,
                           (undefined4 *)0, (undefined4 *)0);
    if (rc != 0) {
        entity_set_render_type(entity, (void *)FUN_80253ae4);
        *(uint *)(entity + 0x28) = *(uint *)(entity + 0x28) | 0x10;
    }
}

// ============================================================================
// 0x80253d24  prop_type_to_sound_class
// Maps a prop type id to its impact-sound class index (or -1 for none).
// ============================================================================
undefined4 FUN_80253d24(undefined4 propType)
{
    switch (propType) {
    case 8:    return 8;
    default:   return 0xffffffff;
    case 0x10: return 0xd;
    case 0x13: return 10;
    case 0x17: return 0xb;
    case 0x18: return 0;
    case 0x1e: return 2;
    case 0x20: return 3;
    case 0x21: return 5;
    case 0x22: return 6;
    case 0x23: return 7;
    case 0x24: return 0x12;
    case 0x25: return 9;
    case 0x28: return 0xe;
    case 0x29: return 0x10;
    case 0x2b: return 0x11;
    case 0x2c: return 1;
    case 0x2d: return 4;
    case 0x2f: return 0xf;
    case 0x39: return 0xc;
    case 0x52: return 0x13;
    }
}

// ============================================================================
// 0x80253df0  prop_type_to_sound_group
// Maps a prop type id to its sound-group index (or -1 for none).
// ============================================================================
undefined4 FUN_80253df0(undefined4 propType)
{
    switch (propType) {
    case 8:
    case 0x23:
    case 0x25: return 1;
    default:   return 0xffffffff;
    case 0x10:
    case 0x39: return 0x16;
    case 0x13:
    case 0x17: return 6;
    case 0x18:
    case 0x2c: return 2;
    case 0x1e:
    case 0x20: return 4;
    case 0x21:
    case 0x22: return 0xb;
    case 0x24: return 9;
    case 0x28: return 5;
    case 0x29:
    case 0x2f: return 0xe;
    case 0x2b:
    case 0x52: return 9;
    case 0x2d: return 0x12;
    }
}

} // extern "C"
