//@ crate-type: lib

struct Vector<T, const N: usize>([T; N]);

trait Swizzle<const N: usize> {
    fn swizzle<T, const M: usize>(value: Vector<T, M>) -> Vector<T, N>;
}

pub fn splat<T: Copy, const N: usize>(value: T) -> Vector<T, N> {
    struct Splat;

    impl<const N: usize> Swizzle<N> for Splat {
        fn swizzle<T, const M: usize>(_: Vector<T, M>) -> Vector<T, N> {
            loop {}
        }
    }

    Splat::swizzle::<T, 1>(Vector([value]))
}
