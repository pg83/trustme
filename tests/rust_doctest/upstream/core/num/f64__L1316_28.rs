// Extracted from library/core/src/num/f64.rs:1316
#![allow(unused)]
fn main() {
    struct GoodBoy {
        name: String,
        weight: f64,
    }
    
    let mut bois = vec![
        GoodBoy { name: "Pucci".to_owned(), weight: 0.1 },
        GoodBoy { name: "Woofer".to_owned(), weight: 99.0 },
        GoodBoy { name: "Yapper".to_owned(), weight: 10.0 },
        GoodBoy { name: "Chonk".to_owned(), weight: f64::INFINITY },
        GoodBoy { name: "Abs. Unit".to_owned(), weight: f64::NAN },
        GoodBoy { name: "Floaty".to_owned(), weight: -5.0 },
    ];
    
    bois.sort_by(|a, b| a.weight.total_cmp(&b.weight));
    
    // `f64::NAN` could be positive or negative, which will affect the sort order.
    if f64::NAN.is_sign_negative() {
        assert!(bois.into_iter().map(|b| b.weight)
            .zip([f64::NAN, -5.0, 0.1, 10.0, 99.0, f64::INFINITY].iter())
            .all(|(a, b)| a.to_bits() == b.to_bits()))
    } else {
        assert!(bois.into_iter().map(|b| b.weight)
            .zip([-5.0, 0.1, 10.0, 99.0, f64::INFINITY, f64::NAN].iter())
            .all(|(a, b)| a.to_bits() == b.to_bits()))
    }
}
