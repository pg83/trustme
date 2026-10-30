#![feature(const_type_name)]

trait Kind<const N: usize> {}

const fn name<const N: usize>() -> &'static str {
    std::any::type_name::<dyn Kind<N> + Send + Sync>()
}

const FIRST: &str = name::<3>();
const SECOND: &str = name::<5>();

fn main() {
    assert_ne!(FIRST, SECOND);
    assert_eq!(FIRST, std::any::type_name::<dyn Kind<3> + Send + Sync>());
    assert_eq!(SECOND, std::any::type_name::<dyn Kind<5> + Send + Sync>());
}
