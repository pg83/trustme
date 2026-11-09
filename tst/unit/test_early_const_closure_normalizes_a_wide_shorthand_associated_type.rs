#![feature(const_closures, const_trait_impl)]

trait Storage {
    type Bytes<A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, const SIZE: usize>;
}

impl Storage for () {
    type Bytes<A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, const SIZE: usize> =
        ([u8; SIZE], core::marker::PhantomData<(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q)>);
}

std::arch::global_asm!(
    ".globl wide_associated_storage_size",
    ".type wide_associated_storage_size,@function",
    "wide_associated_storage_size:",
    "mov rax, {size}",
    "ret",
    size = const {
        let measure = &const || {
            let storage: <()>::Bytes<
                u8, u16, u32, u64, u128, i8, i16, i32, i64, i128,
                usize, isize, bool, char, (), f32, f64, 29,
            > = ([0; 29], core::marker::PhantomData);
            core::mem::size_of_val(&storage)
        };
        measure()
    },
);

extern "C" {
    fn wide_associated_storage_size() -> usize;
}

fn main() {
    assert_eq!(unsafe { wide_associated_storage_size() }, 29);
}
