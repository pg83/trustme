/* core_arch's `debug_simd_finish`: `array::from_fn::<&dyn Debug, N, _>(|i| &array[i])` -
   the closure's return is `&dyn Debug` from `F: FnMut(usize) -> &dyn Debug` before
   its body is checked, so `&array[i]` unsizes into it. */
use std::fmt::Debug;

fn debug_simd_finish<T: Debug, const N: usize>(array: &[T; N]) -> String {
    let parts = std::array::from_fn::<&dyn Debug, N, _>(|i| &array[i]);
    parts.iter().map(|d| format!("{d:?}")).collect::<Vec<_>>().join(",")
}

fn main() {
    assert_eq!(debug_simd_finish(&[1u8, 2, 3]), "1,2,3");
}
