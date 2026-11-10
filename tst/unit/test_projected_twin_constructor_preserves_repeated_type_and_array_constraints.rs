#![feature(generic_arg_infer)]

trait Split<A, B, const N: usize, const M: usize> {
    type Both;
}

struct Router;
struct Twin<A, B, const N: usize, const M: usize>(A, B, [u8; N], [u8; M]);

impl<A, B, const N: usize, const M: usize> Split<A, B, N, M> for Router {
    type Both = Twin<A, B, N, M>;
}

impl<T, const N: usize> Twin<T, T, N, N> {
    fn pack(left: T, right: T, first: [u8; N], second: [u8; N]) -> Self {
        Twin(left, right, first, second)
    }
}

fn generic<T>(left: T, right: T) -> Twin<T, T, 2, 2> {
    <Router as Split<T, _, _, _>>::Both::pack(left, right, [17, 19], [23, 29])
}

fn main() {
    let words = generic("left", "right");
    assert_eq!((words.0, words.1), ("left", "right"));
    assert_eq!((words.2, words.3), ([17, 19], [23, 29]));
    let numbers = <Router as Split<u32, _, _, _>>::Both::pack(31, 37, [41], [43]);
    assert_eq!((numbers.0, numbers.1), (31, 37));
    assert_eq!((numbers.2, numbers.3), ([41], [43]));
}
