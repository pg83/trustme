// crypto-bigint passes `&mut [[Limb; LIMBS]; 2]` to
// `fn new_flattened_mut<const N: usize>(slice: &mut [[Limb; N]])`. rustc
// coerces by unsizing: `[[Limb; LIMBS]; 2]: Unsize<[[Limb; ?N]]>` equates the
// element types and so infers `N`. Our solver held any const equality to be
// ambiguous, the coercion fell back to dereferencing the array into a slice,
// and code generation then looked for `<[[Limb; 3]; 2] as DerefMut>::deref_mut`.
#[derive(Clone, Copy)]
pub struct Limb(pub u64);

pub fn flattened_mut<const N: usize>(slice: &mut [[Limb; N]]) -> usize {
    slice[1][0].0 = 7;
    slice.len() * N
}

pub fn flattened<const N: usize>(slice: &[[Limb; N]]) -> u64 {
    slice.iter().flatten().map(|limb| limb.0).sum()
}

fn square<const LIMBS: usize>() -> (usize, u64) {
    let mut lo_hi = [[Limb(0); LIMBS]; 2];
    let count = flattened_mut(&mut lo_hi);
    (count, lo_hi[1][0].0)
}

fn main() {
    assert_eq!(square::<3>(), (6, 7));
    let mut fixed = [[Limb(1); 4]; 3];
    assert_eq!(flattened_mut(&mut fixed), 12);
    assert_eq!(flattened(&fixed), 18);
}
