// Extracted from library/core/src/num/f16.rs:1135
#![allow(unused)]
#![feature(f16)]
fn main() {
    // FIXME(f16_f128): extendhfsf2, truncsfhf2, __gnu_h2f_ieee, __gnu_f2h_ieee missing for many platforms
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    struct GoodBoy {
        name: &'static str,
        weight: f16,
    }
    
    let mut bois = vec![
        GoodBoy { name: "Pucci", weight: 0.1 },
        GoodBoy { name: "Woofer", weight: 99.0 },
        GoodBoy { name: "Yapper", weight: 10.0 },
        GoodBoy { name: "Chonk", weight: f16::INFINITY },
        GoodBoy { name: "Abs. Unit", weight: f16::NAN },
        GoodBoy { name: "Floaty", weight: -5.0 },
    ];
    
    bois.sort_by(|a, b| a.weight.total_cmp(&b.weight));
    
    // `f16::NAN` could be positive or negative, which will affect the sort order.
    if f16::NAN.is_sign_negative() {
        bois.into_iter().map(|b| b.weight)
            .zip([f16::NAN, -5.0, 0.1, 10.0, 99.0, f16::INFINITY].iter())
            .for_each(|(a, b)| assert_eq!(a.to_bits(), b.to_bits()))
    } else {
        bois.into_iter().map(|b| b.weight)
            .zip([-5.0, 0.1, 10.0, 99.0, f16::INFINITY, f16::NAN].iter())
            .for_each(|(a, b)| assert_eq!(a.to_bits(), b.to_bits()))
    }
    }
}
