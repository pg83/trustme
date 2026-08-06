// Extracted from library/core/src/num/f128.rs:1158
#![allow(unused)]
#![feature(f128)]
fn main() {
    
    struct GoodBoy {
        name: &'static str,
        weight: f128,
    }
    
    let mut bois = vec![
        GoodBoy { name: "Pucci", weight: 0.1 },
        GoodBoy { name: "Woofer", weight: 99.0 },
        GoodBoy { name: "Yapper", weight: 10.0 },
        GoodBoy { name: "Chonk", weight: f128::INFINITY },
        GoodBoy { name: "Abs. Unit", weight: f128::NAN },
        GoodBoy { name: "Floaty", weight: -5.0 },
    ];
    
    bois.sort_by(|a, b| a.weight.total_cmp(&b.weight));
    
    // `f128::NAN` could be positive or negative, which will affect the sort order.
    if f128::NAN.is_sign_negative() {
        bois.into_iter().map(|b| b.weight)
            .zip([f128::NAN, -5.0, 0.1, 10.0, 99.0, f128::INFINITY].iter())
            .for_each(|(a, b)| assert_eq!(a.to_bits(), b.to_bits()))
    } else {
        bois.into_iter().map(|b| b.weight)
            .zip([-5.0, 0.1, 10.0, 99.0, f128::INFINITY, f128::NAN].iter())
            .for_each(|(a, b)| assert_eq!(a.to_bits(), b.to_bits()))
    }
}
