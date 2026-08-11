pub fn math_f32(value: f32, other: f32, third: f32) -> [f32; 20] {
    [
        value.sqrt(),
        value.sin(),
        value.cos(),
        value.powf(other),
        value.powi(3),
        value.exp(),
        value.exp2(),
        value.ln(),
        value.log10(),
        value.log2(),
        value.mul_add(other, third),
        value.abs(),
        value.min(other),
        value.max(other),
        value.copysign(other),
        value.floor(),
        value.ceil(),
        value.trunc(),
        value.round(),
        value.round_ties_even(),
    ]
}

pub fn math_f64(value: f64, other: f64, third: f64) -> [f64; 20] {
    [
        value.sqrt(),
        value.sin(),
        value.cos(),
        value.powf(other),
        value.powi(3),
        value.exp(),
        value.exp2(),
        value.ln(),
        value.log10(),
        value.log2(),
        value.mul_add(other, third),
        value.abs(),
        value.min(other),
        value.max(other),
        value.copysign(other),
        value.floor(),
        value.ceil(),
        value.trunc(),
        value.round(),
        value.round_ties_even(),
    ]
}
