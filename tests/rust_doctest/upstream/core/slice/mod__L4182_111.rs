// Extracted from library/core/src/slice/mod.rs:4182
#![allow(unused)]
#![feature(portable_simd)]
fn main() {
    use core::simd::prelude::*;
    
    let short = &[1, 2, 3];
    let (prefix, middle, suffix) = short.as_simd::<4>();
    assert_eq!(middle, []); // Not enough elements for anything in the middle
    
    // They might be split in any possible way between prefix and suffix
    let it = prefix.iter().chain(suffix).copied();
    assert_eq!(it.collect::<Vec<_>>(), vec![1, 2, 3]);
    
    fn basic_simd_sum(x: &[f32]) -> f32 {
        use std::ops::Add;
        let (prefix, middle, suffix) = x.as_simd();
        let sums = f32x4::from_array([
            prefix.iter().copied().sum(),
            0.0,
            0.0,
            suffix.iter().copied().sum(),
        ]);
        let sums = middle.iter().copied().fold(sums, f32x4::add);
        sums.reduce_sum()
    }
    
    let numbers: Vec<f32> = (1..101).map(|x| x as _).collect();
    assert_eq!(basic_simd_sum(&numbers[1..99]), 4949.0);
}
