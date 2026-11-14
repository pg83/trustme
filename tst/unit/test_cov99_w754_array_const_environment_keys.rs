//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(adt_const_params)]
#![allow(incomplete_features, dead_code)]
#![recursion_limit = "256"]

macro_rules! row {
    ($module:ident : $left:literal; $($name:ident : $right:literal),*) => {
        mod $module {
            $(
                pub fn $name<const A: [u8; $left], const B: [u8; $right]>()
                    -> ([u8; $left], [u8; $right])
                {
                    (A, B)
                }
            )*
        }
    };
}

macro_rules! rows {
    ([$($columns:tt)*];) => {};
    ([$($columns:tt)*]; $module:ident : $left:literal $(, $rest:ident : $size:literal)*) => {
        row!($module : $left; $($columns)*);
        rows!([$($columns)*]; $($rest : $size),*);
    };
}

macro_rules! matrix {
    ($($name:ident : $size:literal),* $(,)?) => {
        rows!([$($name : $size),*]; $($name : $size),*);
    };
}

matrix!(
    n00: 0, n01: 1, n02: 2, n03: 3, n04: 4, n05: 5, n06: 6, n07: 7,
    n08: 8, n09: 9, n10: 10, n11: 11, n12: 12, n13: 13, n14: 14, n15: 15,
    n16: 16, n17: 17, n18: 18, n19: 19, n20: 20, n21: 21, n22: 22, n23: 23,
    n24: 24, n25: 25, n26: 26, n27: 27, n28: 28, n29: 29, n30: 30, n31: 31,
    n32: 32, n33: 33, n34: 34, n35: 35, n36: 36, n37: 37, n38: 38, n39: 39,
    n40: 40, n41: 41, n42: 42, n43: 43, n44: 44, n45: 45, n46: 46, n47: 47,
    n48: 48, n49: 49, n50: 50, n51: 51, n52: 52, n53: 53, n54: 54, n55: 55,
    n56: 56, n57: 57, n58: 58, n59: 59, n60: 60, n61: 61, n62: 62, n63: 63,
);
