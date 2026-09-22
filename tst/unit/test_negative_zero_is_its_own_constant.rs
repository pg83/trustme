// A MIR constant is the bit pattern it holds (upstream's ScalarInt), so
// 0.0 and -0.0 are two constants. They were compared as floats, which says
// they are equal, and a branch choosing between them folded to one arm:
// serde_json's parse_exponent_overflow returned +0.0 for "-1e-400".
#[inline(never)]
fn signed_zero(positive: bool) -> f64 {
    if positive { 0.0 } else { -0.0 }
}

#[inline(never)]
fn signed_zero_f32(positive: bool) -> f32 {
    if positive { 0.0 } else { -0.0 }
}

fn main() {
    assert!(signed_zero(false).is_sign_negative());
    assert!(signed_zero(true).is_sign_positive());
    assert!(signed_zero_f32(false).is_sign_negative());
    assert!(signed_zero_f32(true).is_sign_positive());
    let picked = match std::hint::black_box(2u8) {
        1 => 0.0_f64,
        _ => -0.0_f64,
    };
    assert!(picked.is_sign_negative());
}
