#![feature(macro_metavar_expr_concat)]
#![recursion_limit = "256"]
#![allow(non_camel_case_types, non_upper_case_globals)]

include!("wide_slot_grid.rs");

define_wide!(@collect packet [] [] [] [];
    (A, u8, 0)
    (B, u8, 16)
    (C, u8, 32)
    (D, u8, 48)
    (E, u8, 64)
    (F, u8, 80)
    (G, u8, 96)
    (H, u8, 112)
    (I, u8, 128)
    (J, u8, 144)
    (K, u8, 160)
    (L, u8, 176)
    (M, u8, 192)
    (N, u8, 208)
    (O, u8, 224)
    (P, u16, 240)
    (Q, u32, 256)
    (R, u8, 272)
    (S, u8, 288)
    (T, u8, 304)
    (U, u8, 320)
    (V, u8, 336)
    (W, u8, 352)
    (X, u8, 368)
    (Y, u8, 384)
    (Z, u8, 400)
    (AA, u8, 416)
    (AB, u8, 432)
    (AC, u8, 448)
    (AD, u8, 464)
    (AE, u8, 480)
    (AF, u64, 496)
);
