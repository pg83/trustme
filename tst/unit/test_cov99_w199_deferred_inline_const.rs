const fn deferred<const N: usize>(active: bool) -> usize {
    if active {
        const { N + 1 }
    } else {
        0
    }
}

const EARLY: usize = deferred::<7>(false);

fn main() {
    assert_eq!(EARLY, 0);
    assert_eq!(deferred::<7>(true), 8);
}
