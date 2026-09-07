/* core's `choose_partition_impl`: `if .. { partition_a::<T, F> } else { partition_b::<T, F> }`
   returned as `fn(&mut [T], &T, &mut F) -> usize` - two fn items join into the fn
   pointer (upstream `CoerceMany`), so the first arm must not fix the branches' type. */
fn partition_a<T, F: FnMut(&T, &T) -> bool>(v: &mut [T], _pivot: &T, _is_less: &mut F) -> usize {
    v.len()
}
fn partition_b<T, F: FnMut(&T, &T) -> bool>(v: &mut [T], _pivot: &T, _is_less: &mut F) -> usize {
    v.len() / 2
}
fn choose<T, F: FnMut(&T, &T) -> bool>() -> fn(&mut [T], &T, &mut F) -> usize {
    if size_of::<T>() <= 8 {
        partition_a::<T, F>
    } else {
        partition_b::<T, F>
    }
}
fn main() {
    let mut v = [3u64, 1, 2, 4];
    let pivot = 2u64;
    let mut less = |a: &u64, b: &u64| a < b;
    let f = choose::<u64, _>();
    assert_eq!(f(&mut v, &pivot, &mut less), 4);
    let mut w = [[0u8; 16]; 3];
    let p = [0u8; 16];
    let mut less16 = |a: &[u8; 16], b: &[u8; 16]| a < b;
    let g = choose::<[u8; 16], _>();
    assert_eq!(g(&mut w, &p, &mut less16), 1);
}
