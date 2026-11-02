#![feature(extern_types)]

unsafe extern "C" { type Foreign; }

#[inline(never)]
fn forward(value: &Foreign) -> *const Foreign { value }

fn shared(value: &Foreign) -> *const Foreign { forward(&*value) }

fn unique(value: &mut Foreign) -> *mut Foreign {
    let borrowed = &mut *value;
    borrowed as *mut Foreign
}

fn main() {
    std::hint::black_box(shared as fn(&Foreign) -> *const Foreign);
    std::hint::black_box(unique as fn(&mut Foreign) -> *mut Foreign);
    assert_eq!(std::mem::size_of::<&Foreign>(), std::mem::size_of::<usize>());
}
