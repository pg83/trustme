// slotmap's `get_disjoint_mut` writes `ptrs[j] = MaybeUninit::new(value)`
// with `ptrs: [MaybeUninit<*mut V>; N]`, `value: &mut V` and `j` from a
// `for` loop. rustc types the place first, so `MaybeUninit::new` sees the
// expected output `MaybeUninit<*mut V>`, takes `T = *mut V` and coerces the
// argument. Our place `ptrs[j]` is typed only once `j` is, and the value was
// checked without that expectation: the argument bound `T = &mut V`.
// The place's type now reaches the value when it becomes known.
use std::mem::MaybeUninit;

fn gather<V>(values: &mut [V; 2]) -> [&mut V; 2] {
    let mut ptrs: [MaybeUninit<*mut V>; 2] = [(); 2].map(|_| MaybeUninit::uninit());
    let base = values.as_mut_ptr();
    for j in 0..2 {
        unsafe {
            let value = &mut *base.add(j);
            ptrs[j] = MaybeUninit::new(value);
        }
    }
    ptrs.map(|p| unsafe { &mut *p.assume_init() })
}

fn main() {
    let mut v = [1u32, 2];
    let [a, b] = gather(&mut v);
    *a += 10;
    *b += 20;
    assert_eq!(v, [11, 22]);

}
