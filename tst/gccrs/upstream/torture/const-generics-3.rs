
#![feature(lang_items)]
fn simd_shuffle<const N: usize>(idx: [u32; N]) -> [u32; N] {
    idx
}

fn gccrs_main() -> i32 {
    let a = [1u32, 2, 3, 4];
    let out = simd_shuffle(a);
    let _check: [u32; 4] = out;
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
