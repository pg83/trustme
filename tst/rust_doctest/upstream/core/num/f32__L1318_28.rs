// Extracted from library/core/src/num/f32.rs:1318
#![allow(unused)]
fn main() {
    struct GoodBoy {
        name: String,
        weight: f32,
    }

    let mut bois = vec![
        GoodBoy { name: "Pucci".to_owned(), weight: 0.1 },
        GoodBoy { name: "Woofer".to_owned(), weight: 99.0 },
        GoodBoy { name: "Yapper".to_owned(), weight: 10.0 },
        GoodBoy { name: "Chonk".to_owned(), weight: f32::INFINITY },
        GoodBoy { name: "Abs. Unit".to_owned(), weight: f32::NAN },
        GoodBoy { name: "Floaty".to_owned(), weight: -5.0 },
    ];

    bois.sort_by(|a, b| a.weight.total_cmp(&b.weight));

    // `f32::NAN` could be positive or negative, which will affect the sort order.
    if f32::NAN.is_sign_negative() {
        assert!(bois.into_iter().map(|b| b.weight)
            .zip([f32::NAN, -5.0, 0.1, 10.0, 99.0, f32::INFINITY].iter())
            .all(|(a, b)| a.to_bits() == b.to_bits()))
    } else {
        assert!(bois.into_iter().map(|b| b.weight)
            .zip([-5.0, 0.1, 10.0, 99.0, f32::INFINITY, f32::NAN].iter())
            .all(|(a, b)| a.to_bits() == b.to_bits()))
    }
}
